/* Strict external dependency adapters. Never replace a UDB helper here. */
#include "unrealircd.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

Umode *usermodes = NULL;
LoopStruct loop = {0};
Configuration iConf = {0};
Configuration tempiConf = {0};

Cmode *find_channel_mode_handler(char letter)
{
    check_expected_int(letter);
    return mock_ptr_type(Cmode *);
}

int is_valid_snomask(char c)
{
    check_expected_int(c);
    return mock_int();
}

int valid_server_name(const char *name)
{
    check_expected_ptr(name);
    return mock_int();
}

BanActionValue banact_stringtoval(const char *s)
{
    check_expected_ptr(s);
    return (BanActionValue)mock_int();
}

int banact_config_only(BanActionValue action)
{
    check_expected_int(action);
    return mock_int();
}

int spamfilter_gettargets(const char *s, Client *client)
{
    check_expected_ptr(s);
    check_expected_ptr(client);
    return mock_int();
}

char *spamfilter_target_inttostring(int v)
{
    check_expected_int(v);
    return mock_ptr_type(char *);
}

/* Mirror only the daemon allocator's zero-initialization/abort contract.
 * The allocator is external to UDB; all trees and mutations remain canonical. */
void *__wrap_safe_alloc(size_t size)
{
    if (!size)
        return NULL;
    void *result = calloc(1, size);
    if (!result)
        fail_msg("host allocation failed: %zu bytes", size);
    return result;
}

char *__wrap_our_strdup(const char *value)
{
    char *result = strdup(value);
    if (!result)
        fail_msg("host string allocation failed");
    return result;
}
