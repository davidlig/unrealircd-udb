/* Lookups must preserve the digest-authenticated received inventory order. */
#include "../../src/udb.c"
#include "runner.h"

static void every_wire_permutation_preserves_lookup_and_digest(void **state)
{
    (void)state;
    const unsigned int orders[][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    const char *names[] = {"admin", "locop", "netadmin"};
    for (size_t permutation = 0; permutation < 6; permutation++) {
        UdbOclEntry entries[3] = {0};
        for (size_t i = 0; i < 3; i++) {
            strcpy(entries[i].name, names[orders[permutation][i]]);
            memset(entries[i].digest, 'a' + orders[permutation][i], 64);
        }
        UdbOclEntry original[3];
        memcpy(original, entries, sizeof(entries));
        UdbOclInventory inventory = {.count = 3, .entries = entries};
        unsigned char before[32], after[32];
        assert_true(udb_ocl_inventory_digest(&inventory, before));
        for (size_t i = 0; i < 3; i++) {
            UdbOclEntry *found = udb_ocl_find_entry(&inventory, names[i]);
            assert_non_null(found);
            assert_string_equal(found->name, names[i]);
            assert_int_equal(found->digest[0], 'a' + i);
        }
        assert_null(udb_ocl_find_entry(&inventory, "missing"));
        assert_memory_equal(entries, original, sizeof(entries));
        assert_true(udb_ocl_inventory_digest(&inventory, after));
        assert_memory_equal(before, after, sizeof(before));
    }
}

static void empty_inventory_and_null_inventory_fail_closed(void **state)
{
    (void)state;
    UdbOclInventory empty = {0};
    assert_null(udb_ocl_find_entry(NULL, "admin"));
    assert_null(udb_ocl_find_entry(&empty, "admin"));
}

static const UdbTestCase cases[] = {
    UDB_CASE(every_wire_permutation_preserves_lookup_and_digest),
    UDB_CASE(empty_inventory_and_null_inventory_fail_closed),
};

int main(int argc, char **argv)
{
    return udb_test_main(argc, argv, "ocl_lookup", cases, sizeof(cases) / sizeof(cases[0]));
}
