/* UDB service-client resolution and service-originated notices. */

static Client *udb_service_source(const char *service_key)
{
	const char *mask = udb_get_bot_mask(service_key, 0);
	Client *source = NULL;
	Client *client;

	if (!mask || !match_user)
	{
		udb_log(ULOG_WARNING, "UDB_SERVICE_SOURCE_FALLBACK", NULL,
		        "No user-mask matcher is available for $service; using the local server source",
		        log_data_string("service", service_key));
		return &me;
	}

	list_for_each_entry(client, &client_list, client_node)
	{
		if (!IsUser(client) || IsDead(client) || !client->user || !IsULine(client) ||
		    !client->name[0] || !client->user->username[0] || !client->user->realhost[0])
			continue;
		if (!match_user(mask, client, MATCH_CHECK_ALL))
			continue;
		if (source)
		{
			udb_log(ULOG_WARNING, "UDB_SERVICE_SOURCE_FALLBACK", NULL,
			        "Multiple connected ULine users match the $service mask; using the local server source",
			        log_data_string("service", service_key),
			        log_data_string("mask", mask));
			return &me;
		}
		source = client;
	}

	if (!source)
	{
		udb_log(ULOG_WARNING, "UDB_SERVICE_SOURCE_FALLBACK", NULL,
		        "No connected ULine user matches the $service mask; using the local server source",
		        log_data_string("service", service_key),
		        log_data_string("mask", mask));
		return &me;
	}

	return source;
}

static void udb_send_service_notice(Client *target, const char *service_key,
                                    FORMAT_STRING(const char *pattern), ...)
{
	Client *source;
	char message[1024];
	char *name;
	va_list vl;

	if (!target || !target->name[0])
		return;
	source = udb_service_source(service_key);
	name = *target->name ? target->name : "*";
	va_start(vl, pattern);
	ircvsnprintf(message, sizeof(message), pattern, vl);
	va_end(vl);
	if (source != &me)
	{
		sendto_prefix_one(target, source, NULL, ":%s NOTICE %s :%s",
		                  source->name, name, message);
	} else
	{
		const char *bot_nick = udb_get_bot_nick(service_key, 0);
		sendto_one(target, NULL, ":%s NOTICE %s :%s",
		           bot_nick ? bot_nick : me.name, name, message);
	}
}
