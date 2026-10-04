/* Each executable publishes a registry and executes exactly one literal case.
 * pytest, not cmocka signal recovery, is responsible for process isolation. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

typedef struct {
    const char *name;
    CMUnitTestFunction function;
    void *data;
} UdbTestCase;

#define UDB_CASE(name) {#name, name, NULL}
#define UDB_DATA_CASE(name, function, data) {name, function, data}

static int udb_test_main(int argc, char **argv, const char *suite,
                         const UdbTestCase *cases, size_t count)
{
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        putchar('[');
        for (size_t i = 0; i < count; i++)
            printf("%s{\"suite\":\"%s\",\"name\":\"%s\"}", i ? "," : "", suite, cases[i].name);
        puts("]");
        return count ? 0 : 2;
    }
    if (argc == 3 && !strcmp(argv[1], "--case")) {
        for (size_t i = 0; i < count; i++) {
            if (strcmp(argv[2], cases[i].name))
                continue;
            const struct CMUnitTest selected[] = {{cases[i].name, cases[i].function, NULL, NULL, cases[i].data}};
            return cmocka_run_group_tests_name(suite, selected, NULL, NULL);
        }
    }
    fprintf(stderr, "unknown case or invalid invocation\n");
    return 2;
}
