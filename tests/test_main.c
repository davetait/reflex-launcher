#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"

int test_failures = 0;
extern FILE *log_file;

void suite_ini_doc(void);
void suite_schema(void);
void suite_config_io(void);
void suite_settings_model(void);
void suite_text_input(void);
void suite_file_browser(void);

static const struct {
    const char *name;
    void (*run)(void);
} suites[] = {
    {"ini_doc", suite_ini_doc},
    {"schema", suite_schema},
    {"config_io", suite_config_io},
    {"settings_model", suite_settings_model},
    {"text_input", suite_text_input},
    {"file_browser", suite_file_browser},
};

void test_tmp_path(char *buf, size_t len, const char *name)
{
    snprintf(buf, len, "%s/%s", TEST_TMP_DIR, name);
}

void test_write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    fwrite(text, 1, strlen(text), file);
    fclose(file);
}

char *test_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc((size_t) size + 1);
    size_t read = fread(buffer, 1, (size_t) size, file);
    buffer[read] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, char *argv[])
{
    log_file = stderr;
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (argc < 2 || strcmp(argv[1], suites[i].name) == 0)
            suites[i].run();
    }
    if (test_failures) {
        fprintf(stderr, "%d check(s) failed\n", test_failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
