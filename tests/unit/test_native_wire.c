/* Only the new explicit value/path contract, with canonical production code. */
#include "../../src/udb.c"
#include "runner.h"

static void typed_zero_none_and_strings(void **state)
{
    (void)state;
    UdbNativeKey key;
    unsigned char value[UDB_STORAGE_VALUE_MAX+3]; char wire[6000];
    assert_int_equal(udb_native_key_parse("N::alice::created",&key),UDB_STORAGE_OK);
    assert_int_equal(udb_native_value_decode("!u0",&key.row,value,sizeof(value)),UDB_STORAGE_OK);
    assert_int_equal(key.row.kind,UDB_STORAGE_VALUE_NUMBER); assert_uint_equal(key.row.number,0);
    assert_int_equal(udb_native_value_encode(&key.row,wire,sizeof(wire)),UDB_STORAGE_OK); assert_string_equal(wire,"!u0");
    assert_int_equal(udb_native_value_decode("!n",&key.row,value,sizeof(value)),UDB_STORAGE_OK);
    assert_int_equal(key.row.kind,UDB_STORAGE_VALUE_NONE);
    assert_int_equal(udb_native_value_decode("!sKnN0cmluZw==",&key.row,value,sizeof(value)),UDB_STORAGE_OK);
    assert_int_equal(key.row.kind,UDB_STORAGE_VALUE_STRING); assert_memory_equal(key.row.value.data,"*string",7);
    assert_int_equal(udb_native_value_encode(&key.row,wire,sizeof(wire)),UDB_STORAGE_OK);
    assert_string_equal(wire,"!sKnN0cmluZw==");
}
static void malformed_values_fail_closed(void **state)
{
    (void)state;
    UdbNativeKey key; unsigned char value[UDB_STORAGE_VALUE_MAX+3];
    assert_int_equal(udb_native_key_parse("N::alice",&key),UDB_STORAGE_OK);
    const char *invalid[]={"*","plain","!u01","!u18446744073709551616","!nextra","!sYR==","!sYQ=","!sYQ=!","!sAA=="};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        assert_int_equal(udb_native_value_decode(invalid[i],&key.row,value,sizeof(value)),UDB_STORAGE_REJECTED);
}
static void canonical_paths_fail_closed(void **state)
{
    (void)state; UdbNativeKey key;
    assert_int_equal(udb_native_key_parse("I::2001%3Adb8%3A%3A1::clones",&key),UDB_STORAGE_OK);
    assert_int_equal(key.row.components[0].length,11);
    const char *invalid[]={"I::2001:db8::clones","N::%61lice","N::::pass","N::a::","N::%00","N::a b"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        assert_int_equal(udb_native_key_parse(invalid[i],&key),UDB_STORAGE_REJECTED);
}
static const UdbTestCase cases[]={UDB_CASE(typed_zero_none_and_strings),UDB_CASE(malformed_values_fail_closed),UDB_CASE(canonical_paths_fail_closed)};
int main(int argc,char **argv) { return udb_test_main(argc,argv,"native_wire",cases,sizeof(cases)/sizeof(cases[0])); }
