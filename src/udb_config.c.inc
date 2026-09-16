/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Configuration Parsing, Block S Settings & Link Options
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static UdbConfig *udb_cfg = NULL;
static int udb_config_seen_propagator = 0;

static int udb_flood_valid(const char *value, int *attempts, int *period);

typedef enum UdbConfigKind
{
	UDB_CFG_SERVER_NAME,
	UDB_CFG_FLOOD,
	UDB_CFG_UINT,
	UDB_CFG_INT,
	UDB_CFG_SIZE
} UdbConfigKind;

typedef struct UdbConfigDirective
{
	const char *name;
	UdbConfigKind kind;
	const char *unit;
	size_t offset;
	unsigned int min;
	unsigned int max;
	size_t size_min;
	size_t size_max;
	unsigned int fallback;
} UdbConfigDirective;

static const UdbConfigDirective udb_config_directives[] = {
	{"propagator", UDB_CFG_SERVER_NAME, "", 0, 0, 0, 0, 0, 0},
	{"password-flood", UDB_CFG_FLOOD, "", 0, 0, 0, 0, 0, 0},
	{"max-staged-records", UDB_CFG_UINT, "records", offsetof(UdbConfig, max_staged_records), UDB_MIN_MAX_STAGED_RECORDS,
	 UDB_MAX_MAX_STAGED_RECORDS, 0, 0, UDB_DEFAULT_MAX_STAGED_RECORDS},
	{"max-staged-bytes", UDB_CFG_SIZE, "bytes", offsetof(UdbConfig, max_staged_bytes), 0, 0, UDB_MIN_MAX_STAGED_BYTES,
	 (size_t)UDB_MAX_MAX_STAGED_BYTES, UDB_DEFAULT_MAX_STAGED_BYTES},
	{"sync-inactivity-timeout", UDB_CFG_INT, "seconds", offsetof(UdbConfig, sync_inactivity_timeout), 1, 86400, 0, 0,
	 UDB_SYNC_INACTIVITY_TIMEOUT},
	{"sync-absolute-timeout", UDB_CFG_INT, "seconds", offsetof(UdbConfig, sync_absolute_timeout), 1, 86400, 0, 0,
	 UDB_SYNC_ABSOLUTE_TIMEOUT},
	{"stale-timeout", UDB_CFG_INT, "seconds", offsetof(UdbConfig, stale_timeout), 1, 604800, 0, 0, 300},
	{"anti-entropy-interval", UDB_CFG_INT, "seconds", offsetof(UdbConfig, anti_entropy_interval),
	 UDB_MIN_ANTI_ENTROPY_INTERVAL, UDB_MAX_ANTI_ENTROPY_INTERVAL, 0, 0, UDB_DEFAULT_ANTI_ENTROPY_INTERVAL},
};

static const UdbConfigDirective *udb_config_directive_find(const char *name)
{
	for (size_t i = 0; i < sizeof(udb_config_directives) / sizeof(udb_config_directives[0]); i++)
		if (!strcmp(udb_config_directives[i].name, name))
			return &udb_config_directives[i];
	return NULL;
}

static int udb_config_apply_entry(const UdbConfigDirective *directive, ConfigEntry *cep, int apply)
{
	switch (directive->kind)
	{
	case UDB_CFG_SERVER_NAME:
		if (!cep->value || !udb_server_name_valid(cep->value))
			return 0;
		if (apply)
		{
			udb_config_seen_propagator = 1;
			safe_strdup(udb_cfg->propagator, cep->value);
		}
		return 1;
	case UDB_CFG_FLOOD:
		if (!cep->value || !udb_flood_valid(cep->value, apply ? &udb_cfg->flood_attempts : NULL,
											apply ? &udb_cfg->flood_period : NULL))
			return 0;
		return 1;
	case UDB_CFG_UINT:
	{
		unsigned int value;

		if (!cep->value || !udb_parse_uint_strict(cep->value, &value, directive->min, directive->max))
			return 0;
		if (apply)
			*(unsigned int *)((char *)udb_cfg + directive->offset) = value;
		return 1;
	}
	case UDB_CFG_INT:
	{
		unsigned int value;

		if (!cep->value || !udb_parse_uint_strict(cep->value, &value, directive->min, directive->max))
			return 0;
		if (apply)
			*(int *)((char *)udb_cfg + directive->offset) = (int)value;
		return 1;
	}
	case UDB_CFG_SIZE:
	{
		size_t value;

		if (!cep->value || !udb_parse_size_strict(cep->value, &value, directive->size_min, directive->size_max))
			return 0;
		if (apply)
			*(size_t *)((char *)udb_cfg + directive->offset) = value;
		return 1;
	}
	}
	return 0;
}

static void udb_config_error_entry(const UdbConfigDirective *directive, ConfigEntry *cep)
{
	switch (directive->kind)
	{
	case UDB_CFG_SERVER_NAME:
		config_error("%s:%i: udb::%s requires a valid server name", cep->file->filename, cep->line_number,
					 directive->name);
		break;
	case UDB_CFG_FLOOD:
		config_error("%s:%i: udb::%s requires format attempts:seconds with positive integers (e.g. 5:30)",
					 cep->file->filename, cep->line_number, directive->name);
		break;
	case UDB_CFG_UINT:
	case UDB_CFG_INT:
		if (directive->fallback)
			config_error("%s:%i: udb::%s must be between %u and %u %s (default %u)", cep->file->filename,
						 cep->line_number, directive->name, directive->min, directive->max, directive->unit,
						 directive->fallback);
		else
			config_error("%s:%i: udb::%s must be between %u and %u %s", cep->file->filename, cep->line_number,
						 directive->name, directive->min, directive->max, directive->unit);
		break;
	case UDB_CFG_SIZE:
		config_error("%s:%i: udb::%s must be between %llu and %llu %s", cep->file->filename, cep->line_number,
					 directive->name, (unsigned long long)directive->size_min, (unsigned long long)directive->size_max,
					 directive->unit);
		break;
	}
}

static void udb_config_defaults(void)
{
	if (!udb_cfg)
		return;
	for (size_t i = 0; i < sizeof(udb_config_directives) / sizeof(udb_config_directives[0]); i++)
	{
		const UdbConfigDirective *directive = &udb_config_directives[i];

		if (!directive->fallback || directive->kind == UDB_CFG_SERVER_NAME || directive->kind == UDB_CFG_FLOOD)
			continue;
		if (directive->kind == UDB_CFG_UINT)
		{
			unsigned int *slot = (unsigned int *)((char *)udb_cfg + directive->offset);
			if (*slot == 0)
				*slot = directive->fallback;
		}
		else if (directive->kind == UDB_CFG_INT)
		{
			int *slot = (int *)((char *)udb_cfg + directive->offset);
			if (*slot == 0)
				*slot = (int)directive->fallback;
		}
		else if (directive->kind == UDB_CFG_SIZE)
		{
			size_t *slot = (size_t *)((char *)udb_cfg + directive->offset);
			if (*slot == 0)
				*slot = (size_t)directive->fallback;
		}
	}
	if (udb_cfg->flood_attempts == 0)
		udb_cfg->flood_attempts = 5;
	if (udb_cfg->flood_period == 0)
		udb_cfg->flood_period = 60;
	udb_cfg->config_flood_attempts = udb_cfg->flood_attempts;
	udb_cfg->config_flood_period = udb_cfg->flood_period;
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
		const UdbConfigDirective *directive = udb_config_directive_find(cep->name);

		if (!directive)
		{
			config_error("%s:%i: unknown directive udb::%s", cep->file->filename, cep->line_number, cep->name);
			errors++;
			continue;
		}
		if (!udb_config_apply_entry(directive, cep, 0))
		{
			udb_config_error_entry(directive, cep);
			errors++;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

/* CONFIGRUN only visits directives that still exist. Track the new generation
 * without mutating the active value until a successful postconf, so a failed
 * rehash cannot erase the last known-good propagator override. */
static int udb_config_rehash(void)
{
	udb_config_seen_propagator = 0;
	return 0;
}

static int udb_config_postconf(void)
{
	if (udb_cfg && !udb_config_seen_propagator)
		safe_free(udb_cfg->propagator);
	if (udb_ctx)
		udb_propagator_policy_changed(udb_ctx);
	/* HOOKTYPE_REHASH_COMPLETE: the new configuration is active, so rebuild
	 * the local operclass inventory and publish any changes. */
	udb_ocl_local_rebuild();
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
		const UdbConfigDirective *directive = udb_config_directive_find(cep->name);

		if (directive)
			(void)udb_config_apply_entry(directive, cep, 1);
	}

	/* Persistent files always live in UnrealIRCd PERMDATADIR. */
	udb_config_defaults();

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
		safe_free(ctx->propagator_setting);
	}
	if (udb_cfg)
	{
		safe_free(udb_cfg->propagator);
		safe_free(udb_cfg);
		udb_cfg = NULL;
	}
}

static int udb_setting_string_valid(const char *value)
{
	return value && *value && strlen(value) <= UDB_RECORD_VALUE_MAX && !strpbrk(value, "\r\n");
}

static int udb_suffix_valid(const char *value)
{
	const unsigned char *p;
	const char *label;

	/* The derived label is 32 hexadecimal characters. */
	if (!value || !*value || strlen(value) > HOSTLEN - 32 || value[0] != '.')
		return 0;
	label = value + 1;
	p = (const unsigned char *)label;
	for (; *p; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
			  *p == '-'))
			return 0;
		if (*p == '.')
		{
			if (p == (const unsigned char *)label || p[-1] == '-')
				return 0;
			label = (const char *)p + 1;
		}
		else if (*p == '-' && p == (const unsigned char *)label)
		{
			return 0;
		}
	}
	return value[1] && *label && value[strlen(value) - 1] != '-' && value[strlen(value) - 1] != '.';
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
	const char *colon;
	unsigned int parsed_attempts, parsed_period;

	if (!value || !*value)
		return 0;
	colon = strchr(value, ':');
	if (!colon || colon == value || !colon[1])
		return 0;
	char attempts_buf[32];
	size_t attempts_len = colon - value;
	if (attempts_len >= sizeof(attempts_buf))
		return 0;
	memcpy(attempts_buf, value, attempts_len);
	attempts_buf[attempts_len] = '\0';
	if (!udb_parse_uint_strict(attempts_buf, &parsed_attempts, 1, INT_MAX) ||
		!udb_parse_uint_strict(colon + 1, &parsed_period, 1, INT_MAX))
		return 0;
	if (attempts)
		*attempts = (int)parsed_attempts;
	if (period)
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

typedef struct UdbSettingOps
{
	const char *key;
	UdbValFunc validator;
	size_t value_offset;
	unsigned int flags;
} UdbSettingOps;

#define UDB_SETTING_NUMERIC_ONLY 0x1
#define UDB_SETTING_FLOOD 0x2
#define UDB_SETTING_REFRESH_HOSTS 0x4
#define UDB_SETTING_POLICY_CHANGED 0x8

static const UdbSettingOps udb_setting_ops[] = {
	{SKEY_QUIT_IPS, udb_setting_string_valid, offsetof(UdbContext, quit_ips), 0},
	{SKEY_QUIT_CLONES, udb_setting_string_valid, offsetof(UdbContext, quit_clones), 0},
	{SKEY_CLONES, NULL, 0, UDB_SETTING_NUMERIC_ONLY},
	{SKEY_FLOOD, NULL, 0, UDB_SETTING_FLOOD},
	{SKEY_SUFFIX, udb_suffix_valid, offsetof(UdbContext, suffix), UDB_SETTING_REFRESH_HOSTS},
	{SKEY_CRYPT_KEY, udb_encryption_key_valid, offsetof(UdbContext, encryption_key), UDB_SETTING_REFRESH_HOSTS},
	{SKEY_NICKSERV, udb_service_mask_valid, offsetof(UdbContext, nickserv_mask), 0},
	{SKEY_CHANSERV, udb_service_mask_valid, offsetof(UdbContext, chanserv_mask), 0},
	{SKEY_IPSERV, udb_service_mask_valid, offsetof(UdbContext, ipserv_mask), 0},
	{SKEY_PROPAGATOR, udb_propagator_list_valid, offsetof(UdbContext, propagator_setting), UDB_SETTING_POLICY_CHANGED},
};

static const UdbSettingOps *udb_setting_ops_find(const char *key)
{
	for (size_t i = 0; i < sizeof(udb_setting_ops) / sizeof(udb_setting_ops[0]); i++)
		if (!strcmp(udb_setting_ops[i].key, key))
			return &udb_setting_ops[i];
	return NULL;
}

static char **udb_setting_slot(UdbContext *ctx, const UdbSettingOps *ops)
{
	return (char **)((char *)ctx + ops->value_offset);
}

static int udb_settings_apply_record(UdbContext *ctx, UdbRecord *rec)
{
	const UdbSettingOps *ops;
	int attempts;
	int period;

	if (!ctx || !rec || !rec->key)
		return 0;
	ops = udb_setting_ops_find(rec->key);
	if (!ops)
		return 0;
	if (ops->flags & UDB_SETTING_NUMERIC_ONLY)
		return !rec->data_str && rec->data_num != 0;
	if (ops->flags & UDB_SETTING_FLOOD)
	{
		if (!udb_flood_valid(rec->data_str, &attempts, &period))
			return 0;
		if (udb_cfg)
		{
			udb_cfg->flood_attempts = attempts;
			udb_cfg->flood_period = period;
		}
		return 1;
	}
	if (ops->validator && !ops->validator(rec->data_str))
		return 0;
	udb_settings_replace(udb_setting_slot(ctx, ops), rec->data_str);
	if (ops->flags & UDB_SETTING_REFRESH_HOSTS)
		udb_ip_refresh_derived_hosts();
	if (ops->flags & UDB_SETTING_POLICY_CHANGED)
		udb_propagator_policy_changed(ctx);
	return 1;
}

static void udb_settings_remove_record(UdbContext *ctx, UdbRecord *rec)
{
	const UdbSettingOps *ops;

	if (!ctx || !rec || !rec->key)
		return;
	ops = udb_setting_ops_find(rec->key);
	if (!ops || (ops->flags & UDB_SETTING_NUMERIC_ONLY))
		return;
	if (ops->flags & UDB_SETTING_FLOOD)
	{
		udb_settings_restore_flood();
		return;
	}
	udb_settings_replace(udb_setting_slot(ctx, ops), NULL);
	if (ops->flags & UDB_SETTING_REFRESH_HOSTS)
		udb_ip_refresh_derived_hosts();
	if (ops->flags & UDB_SETTING_POLICY_CHANGED)
		udb_propagator_policy_changed(ctx);
}

static void udb_link_apply_record(UdbContext *ctx, UdbRecord *rec)
{
	if (!rec || !rec->parent || !rec->key || strcmp(rec->key, LKEY_OPTIONS))
		return;
	if (rec->data_str || (rec->data_num & ~UDB_LNKOPT_DEBUG))
		udb_log(ULOG_WARNING, "UDB_LINK_OPTIONS", NULL, "Ignoring invalid L::options for $server",
				log_data_string("server", rec->parent->key));
}

static void udb_link_remove_record(UdbContext *ctx, UdbRecord *rec)
{
	(void)ctx;
	(void)rec;
}

static void udb_config_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (block->letter == UDB_BLOCK_SETTINGS)
	{
		if (!udb_settings_apply_record(ctx, rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL, "Ignoring invalid or unsupported S::$setting",
					log_data_string("setting", rec->key));
	}
	else if (block->letter == UDB_BLOCK_LINKS)
	{
		udb_link_apply_record(ctx, rec);
	}
}

static void udb_config_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (block->letter == UDB_BLOCK_SETTINGS)
		udb_settings_remove_record(ctx, rec);
	else if (block->letter == UDB_BLOCK_LINKS)
		udb_link_remove_record(ctx, rec);
}
