#include "../../src/udb.c"
#include "runner.h"

static UdbRecord *make_tree(void)
{
    return udb_record_create(NULL);
}

static void component_roundtrips(void **state)
{
    (void)state;
    const char *values[] = {"", "alice", "::", "%", "a b", "\t", "\177", "\303\261", "2001:db8::1"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        char encoded[128], decoded[128];
        assert_true(udb_path_encode_component(values[i], encoded, sizeof(encoded)));
        assert_true(udb_path_decode_component(encoded, decoded, sizeof(decoded)));
        assert_string_equal(decoded, values[i]);
    }
}

static void noncanonical_components_fail(void **state)
{
    (void)state;
    const char *invalid[] = {"%", "%0", "%GG", "%00", "%41", "a:b"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char output[32];
        assert_false(udb_path_decode_component(invalid[i], output, sizeof(output)));
    }
}

static void component_buffers_are_bounded(void **state)
{
    (void)state;
    char buffer[5] = "zzzz";
    assert_false(udb_path_encode_component(":", buffer, 3));
    assert_true(udb_path_encode_component(":", buffer, 4));
    assert_string_equal(buffer, "%3A");
    assert_false(udb_path_decode_component("%3A", buffer, 1));
    assert_true(udb_path_decode_component("%3A", buffer, 2));
    assert_string_equal(buffer, ":");
    assert_false(udb_path_encode_component(NULL, buffer, sizeof(buffer)));
    assert_false(udb_path_decode_component(NULL, buffer, sizeof(buffer)));
}

static void malformed_paths_fail(void **state)
{
    (void)state;
    const char *invalid[] = {NULL, "", "::a", "a::", "a::::b", "a:b", "a::%00"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert_false(udb_path_foreach(invalid[i], 8, udb_path_component_noop, NULL));
    assert_int_equal(udb_path_foreach("a::b", 2, udb_path_component_noop, NULL), 2);
    assert_false(udb_path_foreach("a::b", 1, udb_path_component_noop, NULL));
}

static void values_respect_limits(void **state)
{
    (void)state;
    char *value = malloc(UDB_RECORD_VALUE_MAX + 2);
    assert_non_null(value);
    memset(value, 'a', UDB_RECORD_VALUE_MAX + 1);
    value[UDB_RECORD_VALUE_MAX] = '\0';
    int feasible = 1 + UDB_RECORD_VALUE_MAX + UDB_S2S_OVERHEAD_MAX <= UDB_S2S_LINE_MAX;
    assert_int_equal(udb_record_fits_limits("a", value), feasible);
    value[UDB_RECORD_VALUE_MAX] = 'a';
    value[UDB_RECORD_VALUE_MAX + 1] = '\0';
    assert_false(udb_record_fits_limits("a", value));
    assert_false(udb_record_fits_limits("a", "bad\nvalue"));
    assert_false(udb_record_fits_limits("a", "bad\rvalue"));
    free(value);
}

static void secret_paths_are_contextual(void **state)
{
    (void)state;
    const char *secret[] = {"N::alice::pass", "S::encryption_key", "C::#a::modes"};
    for (size_t i = 0; i < sizeof(secret) / sizeof(secret[0]); i++)
        assert_true(udb_path_value_is_secret(secret[i]));
    const char *public[] = {NULL, "", "N::pass", "N::alice::vhost", "S::alice::encryption_key", "I::alice::pass"};
    for (size_t i = 0; i < sizeof(public) / sizeof(public[0]); i++)
        assert_false(udb_path_value_is_secret(public[i]));
}

static void hash_capacity_boundaries(void **state)
{
    (void)state;
    const size_t entries[] = {0, 1, 1536, 1537, 100000, 500000};
    const size_t expected[] = {2048, 2048, 2048, 4096, 262144, 1048576};
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        size_t count = 0;
        assert_true(udb_hash_bucket_count_for_entries(entries[i], &count));
        assert_uint_equal(count, expected[i]);
    }
    size_t count = 42;
    assert_false(udb_hash_bucket_count_for_entries(SIZE_MAX, &count));
    assert_uint_equal(count, 42);
    assert_false(udb_hash_bucket_count_for_entries(1, NULL));
}

static void prepared_hash_is_private_until_publication(void **state)
{
    (void)state;
    UdbContext context = {0};
    UdbHashIndex prepared = {0};
    UdbRecord *tree = make_tree();
    UdbRecord *profile = udb_record_insert_path(tree, "Alice::vhost", "alice.test")->parent;
    assert_true(udb_hash_init(&context));
    assert_true(udb_hash_prepare_tree(tree, &prepared));
    assert_uint_equal(prepared.entries, 1);
    assert_null(udb_hash_find(&context, 0, "alice"));
    udb_hash_publish_prepared(&context, 0, &prepared);
    assert_null(prepared.buckets);
    assert_ptr_equal(udb_hash_find(&context, 0, "ALICE"), profile);
    assert_null(udb_hash_find(&context, -1, "alice"));
    assert_null(udb_hash_find(&context, UDB_NUM_BLOCKS, "alice"));
    assert_true(udb_hash_remove_record(&context, profile, 0, "alice"));
    assert_uint_equal(context.hash[0].entries, 0);
    assert_false(udb_hash_remove_record(&context, profile, 0, "alice"));
    assert_null(udb_hash_find(&context, 0, "alice"));
    udb_hash_destroy(&context);
    udb_record_free_tree(tree);
}

static void spamfilter_keys_preserve_case(void **state)
{
    (void)state;
    UdbRecord *tree = make_tree();
    UdbRecord *upper = udb_record_insert_path(tree, "F::Pattern::reason", "upper");
    UdbRecord *lower = udb_record_insert_path(tree, "F::pattern::reason", "lower");
    assert_non_null(upper);
    assert_non_null(lower);
    assert_ptr_not_equal(upper->parent, lower->parent);
    assert_ptr_equal(udb_record_find(NULL, "Pattern", upper->parent->parent), upper->parent);
    assert_ptr_equal(udb_record_find(NULL, "pattern", upper->parent->parent), lower->parent);
    udb_record_free_tree(tree);
}

static void candidate_clone_is_independent(void **state)
{
    (void)state;
    UdbRecord *tree = make_tree();
    UdbRecord *needle = udb_record_insert_path(tree, "alice::vhost", "old.test");
    UdbRecord *needle_copy = NULL;
    UdbRecord *copy = udb_record_clone_tree(tree, needle, &needle_copy);
    assert_non_null(needle_copy);
    assert_ptr_not_equal(needle, needle_copy);
    assert_null(needle_copy->hash_next);
    assert_string_equal(needle_copy->data_str, "old.test");
    assert_non_null(udb_record_insert_path(copy, "alice::vhost", "new.test"));
    assert_string_equal(needle->data_str, "old.test");
    udb_record_free_tree(copy);
    udb_record_free_tree(tree);
}

static void logical_count_and_deletion(void **state)
{
    (void)state;
    UdbRecord *tree = make_tree();
    UdbRecord *first = udb_record_insert_path(tree, "alice::vhost", "alice.test");
    assert_non_null(first);
    assert_non_null(udb_record_insert_path(tree, "alice::options", "*0"));
    assert_uint_equal(udb_record_count_tree(tree), 3);
    assert_uint_equal(udb_record_count_logical(tree), 2);
    udb_record_delete_tree(first);
    assert_uint_equal(udb_record_count_logical(tree), 1);
    udb_record_free_tree(tree);
}

static void digest_golden_vectors(void **state)
{
    (void)state;
    char digest[65];
    UdbRecord *tree = make_tree();
    assert_true(udb_compute_tree_digest(tree, digest));
    assert_string_equal(digest, UDB_EMPTY_SHA256);
    assert_non_null(udb_record_insert_path(tree, "user1::vhost", "user1.org"));
    assert_true(udb_compute_tree_digest(tree, digest));
    assert_string_equal(digest, "5819874dff9e29994db2eeb8b949bb63cf5c213303539d35b8a6bbaa05088049");
    assert_non_null(udb_record_insert_path(tree, "user2::vhost", "user2.org"));
    assert_non_null(udb_record_insert_path(tree, "user3::vhost", "user3.org"));
    assert_true(udb_compute_tree_digest(tree, digest));
    assert_string_equal(digest, "e9f2a16960fa2fcaed0f9d72cfeabead6ccf0415ad02ce1ae9303b96fffe22ff");
    UdbRecord *reverse = make_tree();
    assert_non_null(udb_record_insert_path(reverse, "user3::vhost", "user3.org"));
    assert_non_null(udb_record_insert_path(reverse, "user2::vhost", "user2.org"));
    assert_non_null(udb_record_insert_path(reverse, "user1::vhost", "user1.org"));
    char other[65];
    assert_true(udb_compute_tree_digest(reverse, other));
    assert_string_equal(other, digest);
    assert_non_null(udb_record_insert_path(reverse, "user1::vhost", "user1.com"));
    assert_true(udb_compute_tree_digest(reverse, other));
    assert_string_not_equal(other, digest);
    udb_record_free_tree(tree);
    udb_record_free_tree(reverse);
}

static const UdbTestCase cases[] = {
    UDB_CASE(component_roundtrips), UDB_CASE(noncanonical_components_fail), UDB_CASE(component_buffers_are_bounded),
    UDB_CASE(malformed_paths_fail), UDB_CASE(values_respect_limits), UDB_CASE(secret_paths_are_contextual),
    UDB_CASE(hash_capacity_boundaries), UDB_CASE(prepared_hash_is_private_until_publication),
    UDB_CASE(spamfilter_keys_preserve_case), UDB_CASE(candidate_clone_is_independent),
    UDB_CASE(logical_count_and_deletion), UDB_CASE(digest_golden_vectors)
};

int main(int argc, char **argv)
{
    return udb_test_main(argc, argv, "store", cases, sizeof(cases) / sizeof(cases[0]));
}
