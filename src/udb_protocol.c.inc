/* UDB - Unreal Database System for UnrealIRCd 6
 * Protocol implementation (S2S DB command and sync)
 */

static ModDataInfo *udb_server_md = NULL;

static const char *udb_md_serialize(ModData *m) {
    return m->i ? "1" : NULL;
}

static void udb_md_unserialize(const char *str, ModData *m) {
    m->i = (str && *str == '1') ? 1 : 0;
}

static int udb_is_udb_server(Client *server) {
    if (!server || !IsServer(server)) return 0;
    return 1;
}

static int udb_is_propagator(Client *server) {
    if (!server || !IsServer(server)) return 0;
    if (udb_cfg && udb_cfg->propagator && !strcasecmp(server->name, udb_cfg->propagator))
        return 1;
    return 0;
}

static void udb_sync_to_server(Client *server) {
    UdbBlock *block = udb_ctx->block_list;
    while (block) {
        sendto_one(server, NULL, ":%s DB %s INF %c %lX %lu",
                   me.id, server->id, block->letter, block->checksum, (unsigned long)block->modified_at);
        block = block->next;
    }
}

static int udb_hook_server_sync(Client *client) {
    if (udb_is_udb_server(client)) {
        udb_sync_to_server(client);
    }
    return 0;
}

CMD_FUNC(cmd_db) {
    /* Process DB protocol messages sent via server-to-server connection */

    if (parc < 4) {
        sendto_one(client, NULL, ":%s DB %s ERR 0 %i 0", me.id, client->id, UDB_ERR_PARAMS);
        return;
    }

    const char *target = parv[1];
    const char *subcmd = parv[2];
    int offset = 0;

    if (!strcasecmp(parv[1], "DB") && parc > 3) {
        target = parv[2];
        subcmd = parv[3];
        offset = 1;
    }
    
    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s offset=%d", parc, target, subcmd, offset);
    unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));

    int is_broadcast = (strchr(target, '*') || strchr(target, '?')) ? 1 : 0;
    int is_for_me = is_broadcast || !match_simple(target, me.id) || !match_simple(target, me.name);

    switch (toupper((unsigned char)subcmd[0])) {
        case 'I':
            if (!strcasecmp(subcmd, "INF")) {
                if (parc < 6) return;
                char letter = *parv[3];
                UdbBlock *block = udb_block_by_letter(letter);
                
                if (is_for_me) {
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR INF %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    unsigned long crc32 = strtoul(parv[4], NULL, 16);
                    time_t remote_ts = atol(parv[5]);
                    
                    if (crc32 != block->checksum) {
                        if (remote_ts > block->modified_at) {
                            udb_block_unload(block);
                            sendto_server(client, 0, 0, NULL, ":%s DB * DRP %c", me.id, letter);
                            sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
                            block->syncing_from = client;
                            block->modified_at = remote_ts;
                        } else if (remote_ts == block->modified_at) {
                            sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
                            block->syncing_from = client;
                        }
                    }
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s INF %c %s %s", client->id, target, letter, parv[4], parv[5]);
            }
            else if (!strcasecmp(subcmd, "INS")) {
                if (parc < 5 + offset) return;
                const char *path = parv[3 + offset];
                const char *data = parv[4 + offset];
                char letter = path[0];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    
                    if (block->syncing_from) {
                        if (block->syncing_from != client) {
                            sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                            return;
                        }
                    }

                    int len = strlen(path) + strlen(data) + 2;
                    char *line = safe_alloc(len);
                    snprintf(line, len, "%s %s", path + 3, data);
                    UdbRecord *rec = udb_file_parse_line(block, line);
                    safe_free(line);
                    
                    if (rec) {
                        udb_apply_special_record(block, rec, 1);
                    }
                    udb_file_save_block(block);
                    
                    char logbuf[512];
                    snprintf(logbuf, sizeof(logbuf), "[UDB] Inserted record via S2S: %s -> %s", path, data);
                    unreal_log(ULOG_INFO, "udb", "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
                    
                    if (udb_ctx->propagator && block->syncing_from == client) {
                        sendto_server(client, 0, 0, NULL, ":%s DB %s INS %s %s", udb_ctx->propagator->id, target, path, data);
                        return;
                    }
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s INS %s %s", client->id, target, path, data);
            }
            break;

        case 'R':
            if (!strcasecmp(subcmd, "RES")) {
                if (parc < 4) return;
                char letter = *parv[3];
                UdbBlock *block = udb_block_by_letter(letter);
                
                if (is_for_me) {
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (block->syncing_from && block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                        return;
                    }
                    
                    FILE *fp = fopen(block->filepath, "r");
                    if (fp) {
                        char line[1024];
                        while (fgets(line, sizeof(line), fp)) {
                            size_t len = strlen(line);
                            while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) {
                                line[--len] = '\0';
                            }
                            if (len > 0) {
                                if (strchr(line, ' '))
                                    sendto_one(client, NULL, ":%s DB * INS %c::%s", me.id, letter, line);
                                else
                                    sendto_one(client, NULL, ":%s DB * DEL %c::%s", me.id, letter, line);
                            }
                        }
                        fclose(fp);
                    }
                    sendto_one(client, NULL, ":%s DB %s FDR %c", me.id, client->id, letter);
                    block->syncing_from = NULL;
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s RES %c", client->id, target, letter);
            }
            break;

        case 'D':
            if (!strcasecmp(subcmd, "DEL")) {
                if (parc < 4) return;
                const char *path = parv[3];
                char letter = path[0];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    
                    if (block->syncing_from) {
                        if (block->syncing_from != client) {
                            sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                            return;
                        }
                    } else {
                        Client *prop = udb_ctx->propagator;
                        if (prop && prop != client) {
                            sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
                            return;
                        }
                    }
                    
                    UdbRecord *rec = udb_record_find_path(block, path + 3);
                    if (rec) {
                        udb_remove_special_record(block, rec);
                        udb_record_delete(block, rec, 1);
                    }
                    
                    if (udb_ctx->propagator && block->syncing_from == client) {
                        sendto_server(client, 0, 0, NULL, ":%s DB %s DEL %s", udb_ctx->propagator->id, target, path);
                        return;
                    }
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s DEL %s", client->id, target, path);
            }
            else if (!strcasecmp(subcmd, "DRP")) {
                if (parc < 4) return;
                char letter = *parv[3];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (block->syncing_from && block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                        return;
                    }
                    
                    udb_block_unload(block);
                    FILE *fp = fopen(block->filepath, "w");
                    if (fp) fclose(fp);
                    block->checksum = 0;
                    block->filesize = 0;
                    block->record_count = 0;
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s DRP %c", client->id, target, *parv[3]);
            }
            break;

        case 'F':
            if (!strcasecmp(subcmd, "FDR")) {
                if (parc < 4) return;
                char letter = *parv[3];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_SYNC, letter);
                        return;
                    }
                    
                    udb_block_unload(block);
                    udb_block_load(block);
                    block->syncing_from = NULL;
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s FDR %c", client->id, target, *parv[3]);
            }
            break;

        case 'O':
            if (!strcasecmp(subcmd, "OPT")) {
                if (parc < 4) return;
                char letter = *parv[3];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (parc >= 5) {
                        block->modified_at = atol(parv[4]);
                    }
                    udb_file_save_block(block);
                    
                    if (!is_broadcast) return;
                }
                if (parc >= 5)
                    sendto_server(client, 0, 0, NULL, ":%s DB %s OPT %c %s", client->id, target, *parv[3], parv[4]);
                else
                    sendto_server(client, 0, 0, NULL, ":%s DB %s OPT %c", client->id, target, *parv[3]);
            }
            break;

        case 'E':
            if (!strcasecmp(subcmd, "ERR")) {
                if (parc < 5) return;
                if (is_for_me) {
                    int errcode = atoi(parv[4]);
                    udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
                               log_data_client("client", client), log_data_string("cmd", parv[3]), log_data_integer("errcode", errcode));
                    if (!is_broadcast) return;
                }
                if (parc >= 6) {
                    sendto_server(client, 0, 0, NULL, ":%s DB %s ERR %s %s %s", client->id, target, parv[3], parv[4], parv[5]);
                } else {
                    sendto_server(client, 0, 0, NULL, ":%s DB %s ERR %s %s", client->id, target, parv[3], parv[4]);
                }
            }
            break;
    }
}

static int udb_protocol_init(ModuleInfo *modinfo) {
    ModDataInfo mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.name = "udb_server";
    mreq.type = MODDATATYPE_CLIENT;
    mreq.serialize = udb_md_serialize;
    mreq.unserialize = udb_md_unserialize;
    mreq.sync = 1;
    udb_server_md = ModDataAdd(modinfo->handle, mreq);

    if (!udb_server_md) {
        return -1; // Failed to register ModData
    }

    // Mark ourselves as a UDB server
    moddata_client((&me), udb_server_md).i = 1;

    CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER);
    HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);

    return 0;
}
