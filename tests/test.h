#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>

extern int test_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        test_failures++; \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    const char *_a = (actual), *_e = (expected); \
    if ((_a == NULL) != (_e == NULL) || (_a != NULL && strcmp(_a, _e) != 0)) { \
        fprintf(stderr, "%s:%d: CHECK_STR failed:\n  got:      \"%s\"\n  expected: \"%s\"\n", \
            __FILE__, __LINE__, _a ? _a : "(null)", _e ? _e : "(null)"); \
        test_failures++; \
    } \
} while (0)

void test_tmp_path(char *buf, size_t len, const char *name);
void test_write_file(const char *path, const char *text);
char *test_read_file(const char *path);

#endif
