/* Internal publication context must not open ordinary nickname admission. */
#include "../../src/udb.c"
#include "runner.h"

static void admission_nickname_reads_stay_closed(void **state)
{
    (void)state;
    UdbStorageProfile *profile = (UdbStorageProfile *)1;
    Client client = {0};
    udb_ready = 0;
    udb_nick_publication_client = &client;
    /* The admission path must not inspect even an active publication read. */
    udb_nick_publication_read = (UdbStorageRead *)1;
    assert_int_equal(udb_native_nick_load("bob", &profile), UDB_STORAGE_ERROR);
    assert_null(profile);
    udb_nick_publication_client = NULL;
    udb_nick_publication_read = NULL;
}

static const UdbTestCase cases[] = {UDB_CASE(admission_nickname_reads_stay_closed)};
int main(int argc, char **argv)
{
    return udb_test_main(argc, argv, "native_nick", cases, sizeof(cases) / sizeof(cases[0]));
}
