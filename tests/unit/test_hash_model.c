/* A flat reference table checks canonical root-index operations after every event. */
#include "../../src/udb.c"
#include "runner.h"

#define MODEL_KEYS 97

static uint32_t next_event(uint32_t *seed)
{
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

static void seeded_events_match_flat_reference(void **state)
{
    (void)state;
    const uint32_t seeds[] = {1, 17, UINT32_C(0xdeadbeef)};
    for (size_t run = 0; run < sizeof(seeds) / sizeof(seeds[0]); run++) {
        UdbContext context = {0};
        UdbRecord *root = udb_record_create(NULL);
        UdbRecord *reference[MODEL_KEYS] = {0};
        uint32_t seed = seeds[run];
        unsigned created = 0, updated = 0, removed = 0;
        assert_true(udb_hash_init(&context));
        for (unsigned event = 0; event < 1000; event++) {
            unsigned slot = next_event(&seed) % MODEL_KEYS;
            unsigned operation = next_event(&seed) % 4;
            char key[32], path[48], value[32];
            snprintf(key, sizeof(key), "profile%u", slot);
            snprintf(path, sizeof(path), "%s::vhost", key);
            snprintf(value, sizeof(value), "event%u.test", event);
            if (operation == 0 && reference[slot]) {
                assert_true(udb_hash_remove_record(&context, reference[slot], 0, key));
                assert_null(reference[slot]->hash_next);
                udb_record_delete_tree(reference[slot]);
                reference[slot] = NULL;
                removed++;
            } else if (operation == 1 || operation == 2) {
                UdbRecord *leaf = udb_record_insert_path(root, path, value);
                assert_non_null(leaf);
                if (!reference[slot]) {
                    reference[slot] = leaf->parent;
                    udb_hash_insert_record(&context, leaf->parent, 0, key);
                    created++;
                } else {
                    updated++;
                }
                assert_string_equal(leaf->data_str, value);
            }
            size_t present = 0;
            for (unsigned i = 0; i < MODEL_KEYS; i++) {
                snprintf(key, sizeof(key), "PROFILE%u", i);
                assert_ptr_equal(udb_hash_find(&context, 0, key), reference[i]);
                if (reference[i]) present++;
            }
            assert_uint_equal(context.hash[0].entries, present);
        }
        assert_true(created > 10 && updated > 10 && removed > 10);
        udb_hash_destroy(&context);
        udb_record_free_tree(root);
    }
}

static void growth_reindexes_all_profiles_and_excludes_nested_keys(void **state)
{
    (void)state;
    UdbContext context = {0};
    UdbRecord *root = udb_record_create(NULL);
    UdbRecord *reference[3200];
    assert_true(udb_hash_init(&context));
    for (unsigned i = 0; i < 3200; i++) {
        char key[32], path[64];
        snprintf(key, sizeof(key), "user%u", i);
        snprintf(path, sizeof(path), "%s::access::nested", key);
        reference[i] = udb_record_insert_path(root, path, "allowed")->parent->parent;
        udb_hash_insert_record(&context, reference[i], 0, key);
        assert_uint_equal(context.hash[0].entries, i + 1);
        if (i == 1535) assert_uint_equal(context.hash[0].bucket_count, 2048);
        if (i == 1536) assert_uint_equal(context.hash[0].bucket_count, 4096);
        if (i == 3072) assert_uint_equal(context.hash[0].bucket_count, 8192);
    }
    for (unsigned i = 0; i < 3200; i++) {
        char key[32];
        snprintf(key, sizeof(key), "USER%u", i);
        assert_ptr_equal(udb_hash_find(&context, 0, key), reference[i]);
    }
    assert_null(udb_hash_find(&context, 0, "nested"));
    assert_null(udb_hash_find(&context, 0, "access"));
    assert_null(udb_hash_find(&context, 1, "user1"));
    udb_hash_destroy(&context);
    udb_record_free_tree(root);
}

static void candidate_publication_replaces_only_selected_block_and_can_be_discarded(void **state)
{
    (void)state;
    UdbContext context = {0};
    UdbHashIndex candidate = {0};
    UdbRecord *old = udb_record_create(NULL), *other = udb_record_create(NULL);
    UdbRecord *replacement = udb_record_create(NULL);
    UdbRecord *a = udb_record_insert_path(old, "alice::vhost", "old.test")->parent;
    UdbRecord *b = udb_record_insert_path(other, "bob::vhost", "other.test")->parent;
    UdbRecord *c = udb_record_insert_path(replacement, "carol::vhost", "new.test")->parent;
    assert_true(udb_hash_init(&context));
    udb_hash_insert_record(&context, a, 0, "alice");
    udb_hash_insert_record(&context, b, 1, "bob");
    assert_true(udb_hash_prepare_tree(replacement, &candidate));
    udb_hash_dispose_prepared(&candidate);
    assert_null(candidate.buckets);
    assert_ptr_equal(udb_hash_find(&context, 0, "alice"), a);
    assert_null(udb_hash_find(&context, 0, "carol"));
    assert_true(udb_hash_prepare_tree(replacement, &candidate));
    udb_hash_publish_prepared(&context, 0, &candidate);
    assert_null(candidate.buckets);
    assert_null(udb_hash_find(&context, 0, "alice"));
    assert_ptr_equal(udb_hash_find(&context, 0, "carol"), c);
    assert_ptr_equal(udb_hash_find(&context, 1, "bob"), b);
    udb_hash_destroy(&context);
    udb_record_free_tree(old);
    udb_record_free_tree(other);
    udb_record_free_tree(replacement);
}

static const UdbTestCase cases[] = {
    UDB_CASE(seeded_events_match_flat_reference),
    UDB_CASE(growth_reindexes_all_profiles_and_excludes_nested_keys),
    UDB_CASE(candidate_publication_replaces_only_selected_block_and_can_be_discarded),
};

int main(int argc, char **argv)
{
    return udb_test_main(argc, argv, "hash_model", cases, sizeof(cases) / sizeof(cases[0]));
}
