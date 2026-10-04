/* Exercise staged snapshot parsing on the canonical UDB candidate tree. */
#include "../../src/udb.c"
#include "runner.h"

static int staging_tree(void **state)
{
    UdbSyncSession *session = calloc(1, sizeof(*session));
    if (!session)
        return -1;
    *state = session;
    session->tree = udb_record_create(NULL);
    return 0;
}

static int discard_staging_tree(void **state)
{
    UdbSyncSession *session = *state;
    udb_record_free_tree(session->tree);
    free(session);
    *state = NULL;
    return 0;
}

static void parsed_candidate_is_private(void **state)
{
    UdbSyncSession *session = *state;
    UdbRecord *active = udb_record_create(NULL);
    assert_non_null(udb_record_insert_path(active, "alice::vhost", "old.test"));

    UdbBlock block = {.letter = 'N'};
    assert_true(udb_stage_parse_line(&block, session, "alice::vhost new.test"));
    UdbRecord *candidate = udb_record_find(NULL, "alice", session->tree);
    assert_non_null(candidate);
    assert_string_equal(udb_record_find(NULL, "vhost", candidate)->data_str, "new.test");
    assert_string_equal(udb_record_find(NULL, "vhost", udb_record_find(NULL, "alice", active))->data_str,
                        "old.test");
    assert_uint_equal(session->record_count, 2);
    udb_record_free_tree(active);
}

static void rejected_records_do_not_mutate_candidate(void **state)
{
    UdbSyncSession *session = *state;
    UdbBlock block = {.letter = 'N'};
    const char *invalid[] = {
        NULL,
        "",
        "alice::vhost",
        "1alice::vhost bad.test",
        "alice::vhost bad\nvalue",
        "alice::vhost::child nested.test",
        "alice::unknown value",
        "alice::vhost::bad%00key value",
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert_false(udb_stage_parse_line(&block, session, invalid[i]));
    assert_null(session->tree->child);
    assert_uint_equal(session->record_count, 0);
}

static void numeric_replacement_keeps_candidate_count_stable(void **state)
{
    UdbSyncSession *session = *state;
    UdbBlock block = {.letter = 'S'};

    assert_true(udb_stage_parse_line(&block, session, "clones *5"));
    assert_uint_equal(session->record_count, 1);
    assert_true(udb_stage_parse_line(&block, session, "clones *9"));
    assert_uint_equal(session->record_count, 1);

    UdbRecord *clones = udb_record_find(NULL, "clones", session->tree);
    assert_non_null(clones);
    assert_null(clones->data_str);
    assert_uint_equal(clones->data_num, 9);
    assert_false(udb_stage_parse_line(&block, session, "clones *999999999999999999999999999"));
    assert_uint_equal(clones->data_num, 9);
}

static const UdbTestCase cases[] = {
    UDB_CASE(parsed_candidate_is_private),
    UDB_CASE(rejected_records_do_not_mutate_candidate),
    UDB_CASE(numeric_replacement_keeps_candidate_count_stable),
};

int main(int argc, char **argv)
{
    const struct CMUnitTest tests[] = {
        {cases[0].name, cases[0].function, staging_tree, discard_staging_tree, NULL},
        {cases[1].name, cases[1].function, staging_tree, discard_staging_tree, NULL},
        {cases[2].name, cases[2].function, staging_tree, discard_staging_tree, NULL},
    };
    if (argc != 3 || strcmp(argv[1], "--case"))
        return udb_test_main(argc, argv, "staging", cases, sizeof(cases) / sizeof(cases[0]));
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
        if (!strcmp(argv[2], tests[i].name))
            return cmocka_run_group_tests_name("staging", &tests[i], staging_tree, discard_staging_tree);
    return 2;
}
