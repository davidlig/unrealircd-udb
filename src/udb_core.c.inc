/*
 * UDB Core Engine for UnrealIRCd 6
 * Implements the database structures, hash, file I/O, and record manipulation.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Forward declare prototypes from other inc files to prevent compiler warnings */
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec, const char *subkey, int is_new);
static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey);
static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new);
static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey);
static void udb_line_apply_record(UdbRecord *line_rec, int is_new);
static void udb_line_remove_record(UdbRecord *line_rec);

/* ========================================================================
 * Block Index Helpers
 * ======================================================================== */
static int udb_block_letter_to_index(char letter) {
    switch (letter) {
        case 'N': return 0;
        case 'C': return 1;
        case 'I': return 2;
        case 'S': return 3;
        case 'L': return 4;
        case 'K': return 5;
        default: return 0;
    }
}

/* ========================================================================
 * Hash Operations
 * ======================================================================== */
static void udb_hash_init(void) {
    for (int i = 0; i < UDB_NUM_BLOCKS; i++) {
        udb_ctx->hash_table[i] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
    }
}

static void udb_hash_destroy(void) {
    for (int i = 0; i < UDB_NUM_BLOCKS; i++) {
        if (udb_ctx->hash_table[i]) {
            safe_free(udb_ctx->hash_table[i]);
            udb_ctx->hash_table[i] = NULL;
        }
    }
}

static unsigned int udb_hash_str(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        hash = ((hash << 5) + hash) + c; // djb2
    }
    return hash & UDB_HASH_MASK;
}

static void udb_hash_insert_record(UdbRecord *rec, int block_idx, const char *key) {
    unsigned int h = udb_hash_str(key);
    rec->hash_next = udb_ctx->hash_table[block_idx][h];
    udb_ctx->hash_table[block_idx][h] = rec;
}

static int udb_hash_remove_record(UdbRecord *rec, int block_idx, const char *key) {
    unsigned int h = udb_hash_str(key);
    UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
    UdbRecord *prev = NULL;
    
    while (curr) {
        if (curr == rec) {
            if (prev) prev->hash_next = curr->hash_next;
            else udb_ctx->hash_table[block_idx][h] = curr->hash_next;
            return 1;
        }
        prev = curr;
        curr = curr->hash_next;
    }
    return 0;
}

static UdbRecord *udb_hash_find(int block_idx, const char *key) {
    if (!key) return NULL;
    unsigned int h = udb_hash_str(key);
    UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
    while (curr) {
        if (!strcasecmp(curr->key, key)) return curr;
        curr = curr->hash_next;
    }
    return NULL;
}

/* ========================================================================
 * Record Operations
 * ======================================================================== */
static const char *udb_get_shared_subkey(const char *key) {
    static const char *known_keys[] = {
        "pass", "vhost", "oper", "swhois", "snomasks", "modes", "access",
        "forbid", "suspended", "challenge", "founder", "topic", "options",
        "clones", "nolines", "host", "encryption_key", "suffix", "nickserv",
        "chanserv", "ipserv", "quit_ips", "quit_clones", "flood", "prefixes",
        "type", "action", "duration", "reason", NULL
    };
    for (int i = 0; known_keys[i]; i++) {
        if (!strcasecmp(known_keys[i], key)) return known_keys[i];
    }
    return NULL;
}

static UdbRecord *udb_record_create(UdbRecord *parent) {
    UdbRecord *rec = safe_alloc(sizeof(UdbRecord));
    rec->parent = parent;
    
    if (parent) {
        rec->block_idx = parent->block_idx;
        rec->sibling = parent->child;
        parent->child = rec;
    }
    return rec;
}

static UdbRecord *udb_record_find(const char *key, UdbRecord *parent) {
    if (!parent) return NULL;
    
    if (parent->parent == NULL && udb_ctx) {
        return udb_hash_find(parent->block_idx, key);
    }
    
    UdbRecord *child = parent->child;
    while (child) {
        if (!strcasecmp(child->key, key)) {
            return child;
        }
        child = child->sibling;
    }
    return NULL;
}

static UdbRecord *udb_record_find_path(UdbBlock *block, const char *path) {
    if (!block || !block->tree || !path) return NULL;
    char pathbuf[512];
    strlcpy(pathbuf, path, sizeof(pathbuf));
    char *cur = pathbuf;
    char *ds;
    UdbRecord *rec = block->tree;
    while ((ds = strstr(cur, "::"))) {
        *ds = '\0';
        rec = udb_record_find(cur, rec);
        if (!rec) return NULL;
        cur = ds + 2;
    }
    return udb_record_find(cur, rec);
}

static UdbRecord *udb_record_insert(UdbBlock *block, UdbRecord *parent, const char *key, const char *data_str, unsigned long data_num, int persist) {
    if (!parent) parent = block->tree;
    UdbRecord *rec = udb_record_find(key, parent);
    if (!rec) {
        rec = udb_record_create(parent);
        if (key) {
            if (parent == block->tree) {
                safe_strdup(rec->key, key);
                rec->is_dynamic_key = 1;
            } else {
                const char *shared = udb_get_shared_subkey(key);
                if (shared) {
                    rec->key = (char *)shared;
                    rec->is_dynamic_key = 0;
                } else {
                    safe_strdup(rec->key, key);
                    rec->is_dynamic_key = 1;
                }
            }
        }
        if (parent == block->tree) {
            udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), key);
        }
        block->record_count++;
        udb_ctx->total_records++;
    }
    
    if (rec->data_str) {
        safe_free(rec->data_str);
    }
    
    // Auto-detect numeric data if it starts with *
    if (data_str && *data_str == '*') {
        rec->data_num = atoi(data_str + 1);
        rec->data_str = NULL;
    } else if (data_str) {
        safe_strdup(rec->data_str, data_str);
        rec->data_num = 0;
    } else {
        rec->data_str = NULL;
        rec->data_num = data_num;
    }
    
    if (persist) {
        udb_file_save_block(block);
    }
    
    udb_apply_special_record(block, rec, 1);
    
    return rec;
}

static void udb_record_free_tree(UdbRecord *rec) {
    if (!rec) return;
    
    UdbRecord *child = rec->child;
    while (child) {
        UdbRecord *next = child->sibling;
        udb_record_free_tree(child);
        child = next;
    }
    
    if (rec->key && rec->is_dynamic_key) safe_free(rec->key);
    if (rec->data_str) safe_free(rec->data_str);
    safe_free(rec);
}

static UdbRecord *udb_record_delete(UdbBlock *block, UdbRecord *rec, int persist) {
    if (!rec) return NULL;
    
    udb_remove_special_record(block, rec);
    
    if (rec->parent) {
        if (rec->parent->parent == NULL) {
            udb_hash_remove_record(rec, udb_block_letter_to_index(block->letter), rec->key);
        }
        
        UdbRecord *curr = rec->parent->child;
        UdbRecord *prev = NULL;
        while (curr) {
            if (curr == rec) {
                if (prev) prev->sibling = curr->sibling;
                else rec->parent->child = curr->sibling;
                break;
            }
            prev = curr;
            curr = curr->sibling;
        }
    }
    
    if (block->record_count > 0) block->record_count--;
    if (udb_ctx->total_records > 0) udb_ctx->total_records--;
    
    udb_record_free_tree(rec);
    
    if (persist) {
        udb_file_save_block(block);
    }
    return NULL;
}

/* ========================================================================
 * Checksum Operations
 * ======================================================================== */
static unsigned long udb_crc32_step(unsigned long crc, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= (unsigned char)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static unsigned long udb_crc32(const char *data, size_t len) {
    return udb_crc32_step(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}

static unsigned long udb_compute_block_checksum(UdbBlock *block) {
    if (!block || !block->filepath) return 0;
    FILE *fp = fopen(block->filepath, "rb");
    if (!fp) return 0;
    
    unsigned long crc = 0xFFFFFFFFUL;
    char buf[4096];
    size_t len;
    
    while ((len = fread(buf, 1, sizeof(buf), fp)) > 0) {
        crc = udb_crc32_step(crc, buf, len);
    }
    fclose(fp);
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static UdbRecord *udb_file_parse_line(UdbBlock *block, char *line) {
    if (!line || !*line || *line == ';') return NULL;
    
    char *value = strchr(line, ' ');
    char *data_str = NULL;
    unsigned long data_num = 0;
    
    if (value) {
        *value++ = '\0';
        if (*value == '*') {
            data_num = strtoul(value + 1, NULL, 10);
        } else {
            data_str = value;
        }
    }
    
    char *p = line;
    UdbRecord *parent = block->tree;
    UdbRecord *leaf_rec = NULL;
    while (p && *p) {
        char *next = strstr(p, "::");
        if (next) {
            *next = '\0';
            next += 2;
        }
        
        UdbRecord *rec = udb_record_find(p, parent);
        if (!rec) {
            rec = udb_record_create(parent);
            if (p && *p) {
                if (parent == block->tree) {
                    safe_strdup(rec->key, p);
                    rec->is_dynamic_key = 1;
                } else {
                    const char *shared = udb_get_shared_subkey(p);
                    if (shared) {
                        rec->key = (char *)shared;
                        rec->is_dynamic_key = 0;
                    } else {
                        safe_strdup(rec->key, p);
                        rec->is_dynamic_key = 1;
                    }
                }
            }
            if (parent == block->tree) {
                udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), p);
            }
            block->record_count++;
            udb_ctx->total_records++;
        }
        
        if (!next) {
            if (data_str && *data_str == '*') {
                rec->data_num = atoi(data_str + 1);
                if (rec->data_str) {
                    safe_free(rec->data_str);
                    rec->data_str = NULL;
                }
            } else if (data_str) {
                safe_strdup(rec->data_str, data_str);
                rec->data_num = 0;
            } else {
                if (rec->data_str) {
                    safe_free(rec->data_str);
                    rec->data_str = NULL;
                }
                rec->data_num = data_num;
            }
            leaf_rec = rec;
        }
        
        parent = rec;
        p = next;
    }
    return leaf_rec;
}

static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, int pathlen) {
    if (!rec) return;
    
    size_t old_len = strlen(pathbuf);
    
    if (depth > 0) {
        strlcat(pathbuf, "::", pathlen);
    }
    strlcat(pathbuf, rec->key, pathlen);
    
    if (rec->data_str || rec->data_num > 0 || !rec->child) {
        if (rec->data_str) {
            fprintf(fp, "%s %s\n", pathbuf, rec->data_str);
        } else if (rec->data_num > 0 || !rec->child) {
            fprintf(fp, "%s *%lu\n", pathbuf, rec->data_num);
        }
    }
    
    if (rec->child) {
        UdbRecord *child = rec->child;
        while (child) {
            udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen);
            child = child->sibling;
        }
    }
    
    // Restore pathbuf
    pathbuf[old_len] = '\0';
}

static int udb_file_save_block(UdbBlock *block) {
    if (!block || !block->filepath) return 0;
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) return 0;
    
    fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
    fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
    fprintf(fp, "; Records: %u\n", block->record_count);
    
    char pathbuf[4096];
    pathbuf[0] = '\0';
    
    if (block->tree) {
        UdbRecord *rec = block->tree->child;
        while (rec) {
            udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
            rec = rec->sibling;
        }
    }
    
    fclose(fp);
    
    rename(tmp_path, block->filepath);
    block->checksum = udb_compute_block_checksum(block);
    block->modified_at = time(NULL);
    
    struct stat st;
    if (stat(block->filepath, &st) == 0) {
        block->filesize = st.st_size;
    }
    
    return 1;
}

static int udb_file_load_block(UdbBlock *block) {
    if (!block || !block->filepath) return 0;
    FILE *fp = fopen(block->filepath, "r");
    if (!fp) return 0;
    
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        char *p = strchr(line, '\n');
        if (p) *p = '\0';
        p = strchr(line, '\r');
        if (p) *p = '\0';
        
        if (line[0] == ';' || line[0] == '\0') continue;
        
        udb_file_parse_line(block, line);
    }
    fclose(fp);
    
    UdbRecord *curr = block->tree->child;
    while (curr) {
        UdbRecord *sub = curr->child;
        while (sub) {
            udb_apply_special_record(block, sub, 1);
            sub = sub->sibling;
        }
        udb_apply_special_record(block, curr, 1);
        curr = curr->sibling;
    }
    
    block->checksum = udb_compute_block_checksum(block);
    
    struct stat st;
    if (stat(block->filepath, &st) == 0) {
        block->filesize = st.st_size;
        block->modified_at = st.st_mtime;
    }
    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "[UDB] Loaded block %c from %s (%u records)", block->letter, block->filepath, block->record_count);
    unreal_log(ULOG_INFO, "udb", "UDB_FILE_LOADED", NULL, "$msg", log_data_string("msg", logbuf));
    
    return 1;
}

/* ========================================================================
 * Block Management
 * ======================================================================== */
static UdbBlock *udb_block_create(char letter, const char *name) {
    UdbBlock *b = safe_alloc(sizeof(UdbBlock));
    b->letter = letter;
    b->version = 1;
    b->tree = udb_record_create(NULL);
    b->tree->block_idx = (unsigned char)udb_block_letter_to_index(letter);
    safe_strdup(b->tree->key, name);
    b->tree->data_num = 1;
    
    char path[512];
    snprintf(path, sizeof(path), "udb_%c.db", letter);
    safe_strdup(b->filepath, path);
    convert_to_absolute_path(&b->filepath, PERMDATADIR);
    
    udb_ctx->blocks[(unsigned char)letter] = b;
    b->next = udb_ctx->block_list;
    udb_ctx->block_list = b;
    
    udb_ctx->block_count++;
    return b;
}

static int udb_block_load(UdbBlock *block) {
    return udb_file_load_block(block);
}

static void udb_block_unload(UdbBlock *block) {
    if (block->tree) {
        udb_record_free_tree(block->tree);
        block->tree = NULL;
    }
}

static void udb_blocks_load_all(void) {
    UdbBlock *b = udb_ctx->block_list;
    while (b) {
        udb_block_load(b);
        b = b->next;
    }
}

static void udb_blocks_save_all(void) {
    UdbBlock *b = udb_ctx->block_list;
    while (b) {
        udb_file_save_block(b);
        b = b->next;
    }
}

static UdbBlock *udb_block_by_letter(char letter) {
    return udb_ctx ? udb_ctx->blocks[(unsigned char)letter] : NULL;
}

/* ========================================================================
 * Initialization and Shutdown
 * ======================================================================== */
static int udb_engine_init(void) {
    udb_ctx = safe_alloc(sizeof(UdbContext));
    udb_hash_init();
    
    struct stat st = {0};
    const char *dir = udb_cfg && udb_cfg->db_directory ? udb_cfg->db_directory : "data/udb";
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0700);
    }
    
    udb_block_create('N', "Nicks");
    udb_block_create('C', "Channels");
    udb_block_create('I', "IPs");
    udb_block_create('S', "Settings");
    udb_block_create('L', "Links");
    udb_block_create('K', "Lines");
    
    udb_ctx->nicks = udb_ctx->blocks['N']->tree;
    udb_ctx->channels = udb_ctx->blocks['C']->tree;
    udb_ctx->ips = udb_ctx->blocks['I']->tree;
    udb_ctx->settings = udb_ctx->blocks['S']->tree;
    udb_ctx->links = udb_ctx->blocks['L']->tree;
    udb_ctx->lines = udb_ctx->blocks['K']->tree;
    
    udb_blocks_load_all();
    return 1;
}

static void udb_engine_shutdown(void) {
    if (!udb_ctx) return;
    
    udb_blocks_save_all();
    
    UdbBlock *b = udb_ctx->block_list;
    while (b) {
        UdbBlock *next = b->next;
        udb_block_unload(b);
        safe_free(b->filepath);
        safe_free(b);
        b = next;
    }
    
    udb_hash_destroy();
    safe_free(udb_ctx);
    udb_ctx = NULL;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */
static const char *udb_get_bot_nick(const char *service_key, int force_default) {
    if (!force_default && udb_ctx && udb_ctx->settings) {
        UdbRecord *rec = udb_record_find(service_key, udb_ctx->settings);
        if (rec && rec->data_str) {
            static char buf[64];
            strlcpy(buf, rec->data_str, sizeof(buf));
            char *p = strchr(buf, '!');
            if (p) *p = '\0';
            return buf;
        }
    }
    if (!strcasecmp(service_key, SKEY_NICKSERV)) return "NickServ";
    if (!strcasecmp(service_key, SKEY_CHANSERV)) return "ChanServ";
    if (!strcasecmp(service_key, SKEY_IPSERV)) return "IpServ";
    return "UDB";
}

static const char *udb_get_bot_mask(const char *service_key, int force_default) {
    if (!force_default && udb_ctx && udb_ctx->settings) {
        UdbRecord *rec = udb_record_find(service_key, udb_ctx->settings);
        if (rec && rec->data_str) {
            return rec->data_str;
        }
    }
    if (!strcasecmp(service_key, SKEY_NICKSERV)) return "NickServ!*@*";
    if (!strcasecmp(service_key, SKEY_CHANSERV)) return "ChanServ!*@*";
    if (!strcasecmp(service_key, SKEY_IPSERV)) return "IpServ!*@*";
    return "UDB!*@*";
}

static int udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new) {
    if (!rec) return 0;
    if (block->letter == 'N') {
        UdbRecord *nick_rec = rec->parent == block->tree ? rec : rec->parent;
        Client *client = find_user(nick_rec->key, NULL);
        if (client && IsUser(client)) {
            udb_nick_apply(client, nick_rec, is_new);
        }
    } else if (block->letter == 'C') {
        UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
        Channel *channel = find_channel(chan_rec->key);
        if (channel) {
            udb_channel_apply_record(channel, chan_rec, rec->key, is_new);
        }
    } else if (block->letter == 'I') {
        UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
        udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
    } else if (block->letter == 'K') {
        udb_line_apply_record(rec, is_new);
    }
    return 1;
}

static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec) {
    if (!rec) return;
    if (block->letter == 'N') {
        if (rec->parent != block->tree) {
            UdbRecord *nick_rec = rec->parent;
            Client *client = find_user(nick_rec->key, NULL);
            if (client && IsUser(client)) {
                if (!strcmp(rec->key, NKEY_VHOST)) {
                    udb_nick_remove_vhost(client);
                } else if (!strcmp(rec->key, NKEY_OPER)) {
                    if (IsOper(client)) {
                        if (MyUser(client) && !list_empty(&client->special_node)) {
                            list_del(&client->special_node);
                            INIT_LIST_HEAD(&client->special_node);
                        }
                        if (irccounts.operators > 0)
                            irccounts.operators--;
                        remove_oper_privileges(client, 1);
                    }
                } else if (!strcmp(rec->key, NKEY_SWHOIS)) {
                    swhois_delete(client, "udb", "*", &me, NULL);
                } else if (!strcmp(rec->key, NKEY_SUSPENDED)) {
                    long old_umodes = client->umodes & ALL_UMODES;
                    client->umodes &= ~set_usermode("S");
                    send_umode_out(client, 1, old_umodes);
                } else if (!strcmp(rec->key, NKEY_PASS)) {
                    udb_nick_strip(client, nick_rec);
                }
            }
        } else {
            Client *client = find_user(rec->key, NULL);
            if (client && IsUser(client)) {
                udb_nick_strip(client, rec);
            }
        }
    } else if (block->letter == 'C') {
        UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
        Channel *channel = find_channel(chan_rec->key);
        if (channel) {
            udb_channel_remove_record(channel, chan_rec, rec->key);
        }
    } else if (block->letter == 'I') {
        UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
        udb_ip_remove_record(ip_rec->key, ip_rec, rec->key);
    } else if (block->letter == 'K') {
        udb_line_remove_record(rec);
    }
}

static void udb_send_to_debugs(Client *source, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    Client *client;
    list_for_each_entry(client, &client_list, client_node) {
        if (IsServer(client) && client != source) {
            if (udb_ctx && udb_ctx->links) {
                UdbRecord *srv_rec = udb_record_find(client->name, udb_ctx->links);
                if (srv_rec) {
                    UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, srv_rec);
                    if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG)) {
                        sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
                    }
                }
            }
        }
    }
    
    // Also send to local opers if our own server has the debug option
    if (udb_ctx && udb_ctx->links) {
        UdbRecord *me_rec = udb_record_find(me.name, udb_ctx->links);
        if (me_rec) {
            UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, me_rec);
            if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG)) {
                unreal_log(ULOG_INFO, "udb", "UDB_DEBUG_OPER", source, 
                           "[UDB Debug] $msg", log_data_string("msg", buf));
            }
        }
    }
    
    unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
}
