/* Compile the unmodified canonical translation unit with real daemon headers. */
#include "../../src/udb.c"
#include "runner.h"

static void uint64_maximum(void **state)
{
    (void)state;
    uint64_t result = 0;
    assert_true(udb_parse_uint64_strict("18446744073709551615", &result));
    assert_uint_equal(result, UINT64_MAX);
}

static void uint64_overflow(void **state)
{
    (void)state;
    uint64_t result = 42;
    assert_false(udb_parse_uint64_strict("18446744073709551616", &result));
    assert_uint_equal(result, 42);
}

static void uint64_rejects_nondecimal(void **state)
{
    (void)state;
    const char *invalid[] = {NULL, "", "+1", "-1", " 1", "1 ", "1x", "0x10", "1\n", "1.0"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        uint64_t result = 42;
        assert_false(udb_parse_uint64_strict(invalid[i], &result));
        assert_uint_equal(result, 42);
    }
}

static void unsigned_bounds(void **state)
{
    (void)state;
    unsigned int value = 99;
    assert_true(udb_parse_uint_strict("10", &value, 10, 20));
    assert_uint_equal(value, 10);
    assert_true(udb_parse_uint_strict("20", &value, 10, 20));
    assert_uint_equal(value, 20);
    assert_false(udb_parse_uint_strict("9", &value, 10, 20));
    assert_false(udb_parse_uint_strict("21", &value, 10, 20));
    assert_uint_equal(value, 20);
    assert_true(udb_parse_uint_strict("15", NULL, 10, 20));
}

static void size_and_ulong_bounds(void **state)
{
    (void)state;
    size_t size = 99;
    unsigned long value = 99;
    assert_true(udb_parse_size_strict("1024", &size, 1, 1024));
    assert_uint_equal(size, 1024);
    assert_false(udb_parse_size_strict("1025", &size, 1, 1024));
    assert_uint_equal(size, 1024);
    assert_true(udb_parse_ulong_strict("10", &value, 10, 20));
    assert_false(udb_parse_ulong_strict("21", &value, 10, 20));
    assert_uint_equal(value, 10);
}

static void timestamp_limits(void **state)
{
    (void)state;
    char maximum[32], overflow[32];
    time_t value = 7;
    unsigned long long max = udb_time_t_max_val();
    snprintf(maximum, sizeof(maximum), "%llu", max);
    assert_true(udb_parse_time_t(maximum, &value));
    assert_uint_equal((uintmax_t)value, max);
    if (max < ULLONG_MAX) {
        snprintf(overflow, sizeof(overflow), "%llu", max + 1);
        assert_false(udb_parse_time_t(overflow, &value));
        assert_uint_equal((uintmax_t)value, max);
    }
    assert_false(udb_parse_time_t("-1", &value));
}

static void timestamp_addition(void **state)
{
    (void)state;
    time_t value = 7;
    time_t maximum = (time_t)udb_time_t_max_val();
    assert_true(udb_time_add(10, 20, &value));
    assert_uint_equal(value, 30);
    assert_true(udb_time_add(maximum, 0, &value));
    assert_false(udb_time_add(maximum, 1, &value));
    assert_uint_equal(value, maximum);
    if ((time_t)-1 < 0)
        assert_false(udb_time_add(-1, 0, &value));
}

static void canonical_digest_format(void **state)
{
    (void)state;
    char output[65];
    const char *valid = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    assert_true(udb_digest_parse(valid, output));
    assert_string_equal(output, valid);
    assert_true(udb_digest_parse(valid, NULL));
    assert_false(udb_digest_parse(NULL, output));
    assert_false(udb_digest_parse("", output));
    assert_false(udb_digest_parse("E3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", output));
    assert_false(udb_digest_parse("g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", output));
}

static void propagator_list_iterates_without_truncation(void **state)
{
    (void)state;
    const char *cursor = "alpha.example, beta.example";
    char name[HOSTLEN + 1];

    expect_string(valid_server_name, name, "alpha.example");
    will_return_int(valid_server_name, 1);
    assert_int_equal(udb_propagator_list_next(&cursor, name), 1);
    assert_string_equal(name, "alpha.example");
    assert_string_equal(cursor, " beta.example");

    expect_string(valid_server_name, name, "beta.example");
    will_return_int(valid_server_name, 1);
    assert_int_equal(udb_propagator_list_next(&cursor, name), 1);
    assert_string_equal(name, "beta.example");
    assert_string_equal(cursor, "");
    assert_int_equal(udb_propagator_list_next(&cursor, name), 0);
}

static void propagator_lists_fail_closed_on_bad_separators(void **state)
{
    (void)state;
    char name[HOSTLEN + 1];
    const char *leading = ",alpha.example";
    const char *trailing = "alpha.example,";
    const char *empty = "   ";

    assert_int_equal(udb_propagator_list_next(&leading, name), -1);
    assert_false(udb_propagator_list_valid(",alpha.example"));

    expect_string(valid_server_name, name, "alpha.example");
    will_return_int(valid_server_name, 1);
    assert_int_equal(udb_propagator_list_next(&trailing, name), -1);

    expect_string(valid_server_name, name, "alpha.example");
    will_return_int(valid_server_name, 1);
    assert_false(udb_propagator_list_valid("alpha.example,,beta.example"));
    assert_false(udb_propagator_list_valid("alpha.example,\tbeta.example"));
    assert_false(udb_propagator_list_valid(empty));
}

static const UdbTestCase cases[] = {
    UDB_CASE(uint64_maximum), UDB_CASE(uint64_overflow), UDB_CASE(uint64_rejects_nondecimal),
    UDB_CASE(unsigned_bounds), UDB_CASE(size_and_ulong_bounds), UDB_CASE(timestamp_limits),
    UDB_CASE(timestamp_addition), UDB_CASE(canonical_digest_format),
    UDB_CASE(propagator_list_iterates_without_truncation),
    UDB_CASE(propagator_lists_fail_closed_on_bad_separators)
};

int main(int argc, char **argv)
{
    return udb_test_main(argc, argv, "numeric", cases, sizeof(cases) / sizeof(cases[0]));
}
