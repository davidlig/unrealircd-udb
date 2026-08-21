/* UDB configuration and settings state. */

static UdbConfig *udb_cfg = NULL;

static int udb_database_directory_valid(const char *value)
{
	char *path = NULL;
	int valid;

	if (!value || !*value || strstr(value, "://") || strpbrk(value, "\r\n"))
		return 0;
	safe_strdup(path, value);
	convert_to_absolute_path(&path, PERMDATADIR);
	valid = strlen(path) + sizeof("/udb_N.db") <= UDB_BLOCK_PATH_MAX;
	safe_free(path);
	return valid;
}

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;

	/* We only handle CONFIG_MAIN blocks named "udb" */
	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "database-directory"))
		{
			if (!udb_database_directory_valid(cep->value))
			{
				config_error("%s:%i: udb::database-directory must be a local path that leaves room for UDB block files",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "propagator"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::propagator requires a server name",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "max-global-clones"))
		{
			if (!cep->value || atoi(cep->value) < 0)
			{
				config_error("%s:%i: udb::max-global-clones requires a non-negative integer",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "password-flood"))
		{
			if (!cep->value || !strchr(cep->value, ':'))
			{
				config_error("%s:%i: udb::password-flood requires format attempts:seconds (e.g. 5:30)",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else
		{
			config_error("%s:%i: unknown directive udb::%s",
			             cep->file->filename, cep->line_number, cep->name);
			errors++;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

static int udb_config_posttest(int *errs)
{
	/* Could validate that propagator is a known link, etc. */
	return 0;
}

static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	/* Allocate config if needed */
	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "database-directory"))
		{
			safe_strdup(udb_cfg->db_directory, cep->value);
		} else if (!strcmp(cep->name, "propagator"))
		{
			safe_strdup(udb_cfg->propagator, cep->value);
		} else if (!strcmp(cep->name, "max-global-clones"))
		{
			udb_cfg->max_global_clones = atoi(cep->value);
		} else if (!strcmp(cep->name, "password-flood"))
		{
			const char *colon = strchr(cep->value, ':');
			if (colon)
			{
				udb_cfg->flood_attempts = atoi(cep->value);
				udb_cfg->flood_period = atoi(colon + 1);
			}
		}
	}

	/* Set defaults if not configured */
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DEFAULT_DB_DIRECTORY);
	convert_to_absolute_path(&udb_cfg->db_directory, PERMDATADIR);
	if (udb_cfg->flood_attempts == 0)
		udb_cfg->flood_attempts = 5;
	if (udb_cfg->flood_period == 0)
		udb_cfg->flood_period = 60;
	udb_cfg->config_flood_attempts = udb_cfg->flood_attempts;
	udb_cfg->config_flood_period = udb_cfg->flood_period;

	return 1;
}

static void udb_config_free(UdbContext *ctx)
{
	if (ctx)
	{
		safe_free(ctx->quit_ips);
		safe_free(ctx->quit_clones);
		safe_free(ctx->encryption_key);
		safe_free(ctx->suffix);
		safe_free(ctx->nickserv_mask);
		safe_free(ctx->chanserv_mask);
		safe_free(ctx->ipserv_mask);
	}
	if (udb_cfg)
	{
		safe_free(udb_cfg->db_directory);
		safe_free(udb_cfg->propagator);
		safe_free(udb_cfg);
		udb_cfg = NULL;
	}
}

static int udb_setting_string_valid(const char *value)
{
	return value && *value && !strpbrk(value, "\r\n");
}

static int udb_suffix_valid(const char *value)
{
	const unsigned char *p = (const unsigned char *)value;
	const char *label;

	/* The derived label is 32 hexadecimal characters. */
	if (!value || !*value || strlen(value) > HOSTLEN - 32 || value[0] != '.')
		return 0;
	label = value + 1;
	for (; *p; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '.' || *p == '-'))
			return 0;
		if (*p == '.')
		{
			if (p == (const unsigned char *)label || p[-1] == '-')
				return 0;
			label = (const char *)p + 1;
		} else if (*p == '-' && p == (const unsigned char *)label)
		{
			return 0;
		}
	}
	return value[1] && *label && value[strlen(value) - 1] != '-' &&
	       value[strlen(value) - 1] != '.';
}

static int udb_encryption_key_valid(const char *value)
{
	size_t i;

	/* A fixed-size hexadecimal key keeps the setting portable and unambiguous. */
	if (!value || strlen(value) != 64)
		return 0;
	for (i = 0; value[i]; i++)
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	return 1;
}

static int udb_service_mask_valid(const char *value)
{
	const char *bang;
	const char *at;

	if (!udb_setting_string_valid(value) || strpbrk(value, " \t"))
		return 0;
	bang = strchr(value, '!');
	at = bang ? strchr(bang + 1, '@') : NULL;
	return bang && bang != value && at && at != bang + 1 && at[1];
}

static int udb_flood_valid(const char *value, int *attempts, int *period)
{
	char *end;
	long parsed_attempts;
	long parsed_period;

	if (!value || !*value)
		return 0;
	parsed_attempts = strtol(value, &end, 10);
	if (end == value || *end++ != ':')
		return 0;
	parsed_period = strtol(end, &end, 10);
	if (*end || parsed_attempts <= 0 || parsed_period <= 0 ||
	    parsed_attempts > INT_MAX || parsed_period > INT_MAX)
		return 0;
	*attempts = (int)parsed_attempts;
	*period = (int)parsed_period;
	return 1;
}

static void udb_settings_replace(char **destination, const char *value)
{
	safe_free(*destination);
	if (value)
		safe_strdup(*destination, value);
}

static void udb_settings_restore_flood(void)
{
	if (!udb_cfg)
		return;
	udb_cfg->flood_attempts = udb_cfg->config_flood_attempts;
	udb_cfg->flood_period = udb_cfg->config_flood_period;
}

static int udb_settings_apply_record(UdbContext *ctx, UdbRecord *rec)
{
	int attempts;
	int period;

	if (!rec || !rec->key)
		return 0;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->quit_ips, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->quit_clones, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_FLOOD))
	{
		if (!udb_flood_valid(rec->data_str, &attempts, &period))
			return 0;
		if (udb_cfg)
		{
			udb_cfg->flood_attempts = attempts;
			udb_cfg->flood_period = period;
		}
	} else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		if (!udb_suffix_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->suffix, rec->data_str);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		if (!udb_encryption_key_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->encryption_key, rec->data_str);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_NICKSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->nickserv_mask, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_CHANSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->chanserv_mask, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_IPSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->ipserv_mask, rec->data_str);
	} else
	{
		return 0;
	}
	return 1;
}

static void udb_settings_remove_record(UdbContext *ctx, UdbRecord *rec)
{
	if (!rec || !rec->key)
		return;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
		udb_settings_replace(&ctx->quit_ips, NULL);
	else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
		udb_settings_replace(&ctx->quit_clones, NULL);
	else if (!strcmp(rec->key, SKEY_FLOOD))
		udb_settings_restore_flood();
	else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		udb_settings_replace(&ctx->suffix, NULL);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		udb_settings_replace(&ctx->encryption_key, NULL);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_NICKSERV))
		udb_settings_replace(&ctx->nickserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_CHANSERV))
		udb_settings_replace(&ctx->chanserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_IPSERV))
		udb_settings_replace(&ctx->ipserv_mask, NULL);
}

static void udb_link_apply_record(UdbContext *ctx, UdbRecord *rec)
{
	if (!rec || !rec->parent || !rec->key || strcmp(rec->key, LKEY_OPTIONS))
		return;
	ctx->propagator = NULL;
	if (rec->data_str || (rec->data_num & ~(UDB_LNKOPT_DEBUG | UDB_LNKOPT_PROPAGATOR)))
		udb_log(ULOG_WARNING, "UDB_LINK_OPTIONS", NULL,
		        "Ignoring invalid L::options for $server", log_data_string("server", rec->parent->key));
}

static void udb_link_remove_record(UdbContext *ctx, UdbRecord *rec)
{
	if (rec && rec->key && !strcmp(rec->key, LKEY_OPTIONS))
		ctx->propagator = NULL;
}

static void udb_config_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (block->letter == 'S')
	{
		if (!udb_settings_apply_record(ctx, rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL,
			        "Ignoring invalid or unsupported S::$setting", log_data_string("setting", rec->key));
	} else if (block->letter == 'L')
	{
		udb_link_apply_record(ctx, rec);
	}
}

static void udb_config_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (block->letter == 'S')
		udb_settings_remove_record(ctx, rec);
	else if (block->letter == 'L')
		udb_link_remove_record(ctx, rec);
}
