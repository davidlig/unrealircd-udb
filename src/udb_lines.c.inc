/* udb_lines.inc.c
 * Implements K-line, Z-line, Shun, Q-line, and Spamfilter support for UDB.
 */

static void udb_line_apply_record(UdbRecord *line_rec, int is_new)
{
	char type;
	UdbRecord *raz;

	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;

	type = line_rec->parent->key[0];
	
	const char *reason = NULL;
	raz = udb_record_find(KKEY_REASON, line_rec);
	if (raz && raz->data_str) {
		reason = raz->data_str;
	} else if (line_rec->data_str) {
		reason = line_rec->data_str;
	}
	
	if (!reason)
		return;

	if (type == 'F') {
		UdbRecord *tip = udb_record_find(KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(KKEY_ACTION, line_rec);
		UdbRecord *dur = udb_record_find(KKEY_DURATION, line_rec);
		
		if (tip && acc && tip->data_str && acc->data_str) {
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			BanAction *action = banact_value_to_struct(act_val);
			long duration = dur ? dur->data_num : 0;
			
			const char *err = NULL;
			Match *match = unreal_create_match(MATCH_PCRE_REGEX, line_rec->key, &err);
			if (match) {
				tkl_add_spamfilter(TKL_SPAMF|TKL_GLOBAL, line_rec->key, target, action, match,
				                   line_rec->key, NULL, "UDB", 0, TStime(),
				                   duration, reason, 0, 0, 0);
			} else {
				udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to compile spamfilter regex: $regex ($err)",
				        log_data_string("regex", line_rec->key),
				        log_data_string("err", err ? err : "unknown error"), NULL);
			}
		}
	} else {
		char user[128];
		char host[128];
		const char *p = strchr(line_rec->key, '@');
		if (p) {
			strlcpy(user, line_rec->key, p - line_rec->key + 1);
			strlcpy(host, p + 1, sizeof(host));
		} else {
			strlcpy(user, "*", sizeof(user));
			strlcpy(host, line_rec->key, sizeof(host));
		}

		if (type == 'G') {
			tkl_add_serverban(TKL_KILL|TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", 0, TStime(), 0, 0);
		} else if (type == 'Z') {
			tkl_add_serverban(TKL_ZAP|TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", 0, TStime(), 0, 0);
		} else if (type == 'S') {
			tkl_add_serverban(TKL_SHUN|TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", 0, TStime(), 0, 0);
		} else if (type == 'Q') {
			tkl_add_nameban(TKL_NAME|TKL_GLOBAL, line_rec->key, 0, reason, "UDB",
			                0, TStime(), 0);
		}
	}
}

static void udb_line_remove_record(UdbRecord *line_rec)
{
	char type;
	TKL *tkl = NULL;

	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;

	type = line_rec->parent->key[0];

	if (type == 'F') {
		UdbRecord *tip = udb_record_find(KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(KKEY_ACTION, line_rec);
		if (tip && acc && tip->data_str && acc->data_str) {
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			tkl = find_tkl_spamfilter(TKL_SPAMF|TKL_GLOBAL, line_rec->key, act_val, target);
		}
	} else {
		char user[128];
		char host[128];
		const char *p = strchr(line_rec->key, '@');
		if (p) {
			strlcpy(user, line_rec->key, p - line_rec->key + 1);
			strlcpy(host, p + 1, sizeof(host));
		} else {
			strlcpy(user, "*", sizeof(user));
			strlcpy(host, line_rec->key, sizeof(host));
		}

		if (type == 'G') {
			tkl = find_tkl_serverban(TKL_KILL|TKL_GLOBAL, user, host, 0);
		} else if (type == 'Z') {
			tkl = find_tkl_serverban(TKL_ZAP|TKL_GLOBAL, user, host, 0);
		} else if (type == 'S') {
			tkl = find_tkl_serverban(TKL_SHUN|TKL_GLOBAL, user, host, 0);
		} else if (type == 'Q') {
			tkl = find_tkl_nameban(TKL_NAME|TKL_GLOBAL, line_rec->key, 0);
		}
	}

	if (tkl) {
		if (type == 'S') {
			tkl_check_local_remove_shun(tkl);
		}
		tkl_del_line(tkl);
	}
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	/* TKL system handles network bans automatically, no hooks required here */
}
