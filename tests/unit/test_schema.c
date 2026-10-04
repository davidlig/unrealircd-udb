#include "../../src/udb.c"
#include "runner.h"

typedef struct {
    char letter;
    const char *path;
    const char *value;
    int admitted;
} RecordVector;

#define RECORD_VECTORS(X) \
    X(n_root, 'N', "alice", NULL, 1) \
    X(n_bad_nick, 'N', "1alice::vhost", "host.test", 0) \
    X(n_vhost, 'N', "alice::vhost", "host.test", 1) \
    X(n_vhost_space, 'N', "alice::vhost", "bad host.test", 0) \
    X(n_unknown_key, 'N', "alice::unknown", "x", 0) \
    X(n_forbid, 'N', "alice::forbid", "reserved", 1) \
    X(n_forbid_numeric, 'N', "alice::forbid", "*1", 0) \
    X(n_nested_vhost, 'N', "alice::vhost::child", "host.test", 0) \
    X(n_short_hash, 'N', "alice::pass", "sha256:abcd", 0) \
    X(c_root, 'C', "#room", NULL, 1) \
    X(c_bad_root, 'C', "room", NULL, 0) \
    X(c_founder, 'C', "#room::founder", "alice", 1) \
    X(c_bad_founder, 'C', "#room::founder", "1alice", 0) \
    X(c_access, 'C', "#room::access::alice", "*100", 1) \
    X(c_bad_access, 'C', "#room::access::1alice", "*100", 0) \
    X(c_bad_access_numeric, 'C', "#room::access::alice", "*100x", 0) \
    X(c_options, 'C', "#room::options", "*3", 1) \
    X(c_options_string, 'C', "#room::options", "3", 0) \
    X(i_ipv6, 'I', "2001%3Adb8%3A%3A1::clones", "*10", 1) \
    X(i_raw_ipv6, 'I', "2001:db8::1::clones", "*10", 0) \
    X(i_bad_limit, 'I', "127.0.0.1::clones", "*2147483648", 0) \
    X(i_nolines, 'I', "127.0.0.1::nolines", "GZ", 1) \
    X(i_bad_nolines, 'I', "127.0.0.1::nolines", "?", 0) \
    X(s_clones, 'S', "clones", "*5", 1) \
    X(s_bad_clones, 'S', "clones", "*5x", 0) \
    X(s_nested_key, 'S', "clones::child", "*5", 0) \
    X(s_unknown_key, 'S', "unknown", "x", 0) \
    X(l_options, 'L', "ircd.test::options", "*3", 1) \
    X(l_unknown_key, 'L', "ircd.test::unknown", "*3", 0) \
    X(k_reason, 'K', "G::*@example.test::reason", "blocked", 1) \
    X(k_direct_value, 'K', "G::*@example.test", "blocked", 0) \
    X(k_duration_alias, 'K', "G::*@example.test::duration", "*10", 0) \
    X(k_expires, 'K', "G::*@example.test::expires", "*1787720000", 1) \
    X(k_expires_zero, 'K', "G::*@example.test::expires", "*0", 0) \
    X(k_expires_negative, 'K', "G::*@example.test::expires", "*-1", 0) \
    X(k_bad_reason, 'K', "G::*@example.test::reason", "bad\nreason", 0) \
    X(k_missing_user, 'K', "G::@example.test::reason", "blocked", 0) \
    X(k_bad_type, 'K', "X::*@example.test::reason", "blocked", 0) \
    X(k_ipv4_network, 'K', "Z::192.0.2.0/24::reason", "blocked", 1) \
    X(k_ipv4_host_bits, 'K', "Z::192.0.2.1/24::reason", "blocked", 0) \
    X(k_ipv6_network, 'K', "Z::2001%3Adb8%3A%3A/32::reason", "blocked", 1) \
    X(k_ipv6_host_bits, 'K', "Z::2001%3Adb8%3A%3A1/32::reason", "blocked", 0) \
    X(k_ipv6_noncanonical, 'K', "Z::2001%3ADB8%3A%3A/32::reason", "blocked", 0) \
    X(k_qline, 'K', "Q::Bad*::reason", "blocked", 1) \
    X(k_qline_at, 'K', "Q::Bad@*::reason", "blocked", 0) \
    X(k_spamfilter_reason, 'K', "F::b64%3AYWJj::reason", "blocked", 1) \
    X(k_spamfilter_raw, 'K', "F::abc::reason", "blocked", 0) \
    X(k_spamfilter_empty, 'K', "F::b64%3A::reason", "blocked", 0) \
    X(unknown_block, '?', "alice", NULL, 0)

#define DEFINE_VECTOR(name, letter, path, value, admitted) \
    static const RecordVector name##_vector = {letter, path, value, admitted};
RECORD_VECTORS(DEFINE_VECTOR)

static void record_schema(void **state)
{
    const RecordVector *vector = *state;
    UdbBlock block = {.letter = vector->letter};
    if (vector->letter == 'L') {
        expect_string(valid_server_name, name, "ircd.test");
        will_return_int(valid_server_name, 1);
    }
    assert_int_equal(udb_record_validate(&block, vector->path, vector->value), vector->admitted);
}

static void descriptors_cover_six_blocks(void **state)
{
    (void)state;
    assert_true(udb_block_descriptors_validate());
    const char letters[] = "NCISLK";
    for (size_t i = 0; i < 6; i++) {
        assert_int_equal(udb_block_letter_to_index(letters[i]), i);
        assert_uint_equal(udb_block_letter_to_mask(letters[i]), 1u << i);
        assert_non_null(udb_get_block_schema(letters[i]));
    }
    assert_int_equal(udb_block_letter_to_index('?'), -1);
    assert_uint_equal(udb_block_letter_to_mask('?'), 0);
    assert_null(udb_get_block_schema('?'));
    assert_null(udb_block_descriptor(6));
}

static void user_mode_registry_and_oper_prohibition(void **state)
{
    (void)state;
    Umode invisible = {.letter = 'i'}, hidden = {.letter = 'x'};
    invisible.next = &hidden;
    usermodes = &invisible;
    assert_true(udb_user_modes_record_valid("+ix"));
    assert_false(udb_user_modes_record_valid("+o"));
    assert_false(udb_user_modes_record_valid("+s"));
    assert_false(udb_user_modes_record_valid("+"));
    assert_false(udb_user_modes_record_valid("+1"));
    usermodes = NULL;
}

static void channel_modes_match_external_descriptors(void **state)
{
    (void)state;
    Cmode mode = {.type = CMODE_NORMAL, .paracount = 1};
    expect_int_value(find_channel_mode_handler, letter, 'k');
    will_return_ptr(find_channel_mode_handler, &mode);
    assert_true(udb_channel_modes_record_valid("+k secret"));
    expect_int_value(find_channel_mode_handler, letter, 'k');
    will_return_ptr(find_channel_mode_handler, &mode);
    assert_false(udb_channel_modes_record_valid("+k"));
    assert_false(udb_channel_modes_record_valid("+r"));
    assert_false(udb_channel_modes_record_valid("+P"));
    assert_false(udb_channel_modes_record_valid("+O"));
}

static void regex_compilation_is_real(void **state)
{
    (void)state;
    assert_true(udb_spamfilter_pattern_valid("b64:YWJj", "regex"));
    assert_true(udb_spamfilter_pattern_valid("b64:YWJj", "simple"));
    assert_false(udb_spamfilter_pattern_valid("b64:Ww==", "regex"));
    assert_false(udb_spamfilter_pattern_valid("b64:AA==", NULL));
    assert_false(udb_spamfilter_pattern_valid("b64:YR==", NULL));
}

static const UdbTestCase cases[] = {
#define REGISTER_VECTOR(name, letter, path, value, admitted) \
    UDB_DATA_CASE(#name, record_schema, (void *)&name##_vector),
    RECORD_VECTORS(REGISTER_VECTOR)
    UDB_CASE(descriptors_cover_six_blocks), UDB_CASE(user_mode_registry_and_oper_prohibition),
    UDB_CASE(channel_modes_match_external_descriptors), UDB_CASE(regex_compilation_is_real)
};

int main(int argc, char **argv)
{
    return udb_test_main(argc, argv, "schema", cases, sizeof(cases) / sizeof(cases[0]));
}
