#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "ini_doc.h"

typedef enum {
    LINE_BLANK,
    LINE_COMMENT,
    LINE_SECTION,
    LINE_KEY,
    LINE_COMMENTED_KEY,
    LINE_CONTINUATION,
    LINE_OTHER
} LineKind;

typedef struct {
    LineKind kind;
    char *text;     // the line as written, without its newline
    char *section;  // owning section ("" before the first header); a header's own name
    char *key;      // LINE_KEY, LINE_COMMENTED_KEY, LINE_CONTINUATION (the key it continues)
    char *value;    // the value inih reports for this line
} IniLine;

struct ini_doc {
    IniLine *lines;
    size_t count;
    size_t capacity;
    bool crlf;
    bool bom;
    bool trailing_newline;
    bool has_stat;
    long long file_size;
    long long file_mtime;
    char **dirty_sections;
    char **dirty_keys;
    size_t dirty_count;
};

static char *dup_range(const char *start, size_t length);
static char *trimmed(const char *start, const char *end);
static const char *find_chars_or_comment(const char *s, const char *chars);
static bool parse_commented_key(const char *s, char **key, char **value);
static IniLine classify_line(const char *raw, const char *section, const char *prev_key);
static void insert_line(IniDoc *doc, size_t index, IniLine line);
static void free_line(IniLine *line);
static bool line_matches(const IniLine *line, const char *section, const char *key);
static void delete_line(IniDoc *doc, size_t index);
static void make_key_line(IniLine *line, const char *key, const char *value);
static size_t section_insert_point(IniDoc *doc, const char *section);
static void mark_dirty(IniDoc *doc, const char *section, const char *key);

static char *dup_range(const char *start, size_t length)
{
    char *s = malloc(length + 1);
    memcpy(s, start, length);
    s[length] = '\0';
    return s;
}

static char *trimmed(const char *start, const char *end)
{
    while (start < end && isspace((unsigned char) *start))
        start++;
    while (end > start && isspace((unsigned char) end[-1]))
        end--;
    return dup_range(start, (size_t) (end - start));
}

// Mirrors inih's find_chars_or_comment(): stops at any char in `chars`,
// or at a ';' that follows whitespace (an inline comment)
static const char *find_chars_or_comment(const char *s, const char *chars)
{
    bool was_space = false;
    while (*s && (chars == NULL || strchr(chars, *s) == NULL) && !(was_space && *s == ';')) {
        was_space = isspace((unsigned char) *s);
        s++;
    }
    return s;
}

// A comment whose text is "key=value" with a space-free key, e.g. "#FPSLimit=60"
static bool parse_commented_key(const char *s, char **key, char **value)
{
    while (*s == ' ' || *s == '\t')
        s++;
    const char *key_start = s;
    while (*s && *s != '=' && !isspace((unsigned char) *s))
        s++;
    if (s == key_start)
        return false;
    const char *key_end = s;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s != '=')
        return false;
    *key = dup_range(key_start, (size_t) (key_end - key_start));
    *value = trimmed(s + 1, find_chars_or_comment(s + 1, NULL));
    return true;
}

// Classify one physical line (without its newline) with inih's rules
static IniLine classify_line(const char *raw, const char *section, const char *prev_key)
{
    IniLine line = { .kind = LINE_OTHER, .text = strdup(raw), .section = strdup(section) };
    const char *start = raw;
    while (*start && isspace((unsigned char) *start))
        start++;
    const char *end = raw + strlen(raw);
    while (end > start && isspace((unsigned char) end[-1]))
        end--;
    char *body = dup_range(start, (size_t) (end - start));

    if (*body == '\0')
        line.kind = LINE_BLANK;
    else if (*body == ';' || *body == '#')
        line.kind = parse_commented_key(body + 1, &line.key, &line.value) ? LINE_COMMENTED_KEY : LINE_COMMENT;
    else if (prev_key != NULL && start > raw) {
        line.kind = LINE_CONTINUATION;
        line.key = strdup(prev_key);
        line.value = trimmed(body, find_chars_or_comment(body, NULL));
    }
    else if (*body == '[') {
        const char *close = find_chars_or_comment(body + 1, "]");
        if (*close == ']') {
            line.kind = LINE_SECTION;
            free(line.section);
            line.section = dup_range(body + 1, (size_t) (close - body - 1));
        }
    }
    else {
        const char *separator = find_chars_or_comment(body, "=:");
        if (*separator == '=' || *separator == ':') {
            line.kind = LINE_KEY;
            line.key = trimmed(body, separator);
            line.value = trimmed(separator + 1, find_chars_or_comment(separator + 1, NULL));
        }
    }
    free(body);
    return line;
}

static void insert_line(IniDoc *doc, size_t index, IniLine line)
{
    if (doc->count == doc->capacity) {
        doc->capacity = doc->capacity ? doc->capacity * 2 : 64;
        doc->lines = realloc(doc->lines, doc->capacity * sizeof(IniLine));
    }
    memmove(doc->lines + index + 1, doc->lines + index, (doc->count - index) * sizeof(IniLine));
    doc->lines[index] = line;
    doc->count++;
}

static void free_line(IniLine *line)
{
    free(line->text);
    free(line->section);
    free(line->key);
    free(line->value);
}

IniDoc *ini_doc_parse(const char *text)
{
    IniDoc *doc = calloc(1, sizeof(IniDoc));
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        doc->bom = true;
        text += 3;
    }
    const char *first_newline = strchr(text, '\n');
    doc->crlf = first_newline != NULL && first_newline > text && first_newline[-1] == '\r';
    size_t length = strlen(text);
    doc->trailing_newline = length == 0 || text[length - 1] == '\n';

    char *section = strdup("");
    char *prev_key = NULL;
    const char *p = text;
    while (*p) {
        const char *newline = strchr(p, '\n');
        const char *line_end = newline ? newline : p + strlen(p);
        size_t n = (size_t) (line_end - p);
        if (n > 0 && p[n - 1] == '\r')
            n--;
        char *raw = dup_range(p, n);
        IniLine line = classify_line(raw, section, prev_key);
        free(raw);
        if (line.kind == LINE_SECTION) {
            free(section);
            section = strdup(line.section);
            free(prev_key);
            prev_key = NULL;
        }
        else if (line.kind == LINE_KEY) {
            free(prev_key);
            prev_key = strdup(line.key);
        }
        insert_line(doc, doc->count, line);
        p = newline ? newline + 1 : line_end;
    }
    free(section);
    free(prev_key);
    return doc;
}

char *ini_doc_serialize(const IniDoc *doc)
{
    const char *newline = doc->crlf ? "\r\n" : "\n";
    size_t newline_length = strlen(newline);
    size_t size = 4;
    for (size_t i = 0; i < doc->count; i++)
        size += strlen(doc->lines[i].text) + newline_length;
    char *out = malloc(size);
    char *p = out;
    if (doc->bom) {
        memcpy(p, "\xEF\xBB\xBF", 3);
        p += 3;
    }
    for (size_t i = 0; i < doc->count; i++) {
        size_t n = strlen(doc->lines[i].text);
        memcpy(p, doc->lines[i].text, n);
        p += n;
        if (i + 1 < doc->count || doc->trailing_newline) {
            memcpy(p, newline, newline_length);
            p += newline_length;
        }
    }
    *p = '\0';
    return out;
}

static bool line_matches(const IniLine *line, const char *section, const char *key)
{
    return line->key != NULL && strcmp(line->section, section) == 0 && strcmp(line->key, key) == 0;
}

// Last active value wins, like config_handler(); continuation lines count as values
const char *ini_doc_get(const IniDoc *doc, const char *section, const char *key)
{
    const char *value = NULL;
    for (size_t i = 0; i < doc->count; i++) {
        const IniLine *line = &doc->lines[i];
        if ((line->kind == LINE_KEY || line->kind == LINE_CONTINUATION) && line_matches(line, section, key))
            value = line->value;
    }
    return value;
}

static void delete_line(IniDoc *doc, size_t index)
{
    free_line(&doc->lines[index]);
    memmove(doc->lines + index, doc->lines + index + 1, (doc->count - index - 1) * sizeof(IniLine));
    doc->count--;
}

// Rewrite a line as an active, unindented "key=value"
// (an indented key line would be read by inih as a continuation)
static void make_key_line(IniLine *line, const char *key, const char *value)
{
    size_t n = strlen(key) + strlen(value) + 2;
    char *text = malloc(n);
    snprintf(text, n, "%s=%s", key, value);
    char *new_key = strdup(key);
    char *new_value = strdup(value);
    free(line->text);
    line->text = text;
    free(line->key);
    line->key = new_key;
    free(line->value);
    line->value = new_value;
    line->kind = LINE_KEY;
}

// Index just after the last setting or header of `section`; creates the section if missing
static size_t section_insert_point(IniDoc *doc, const char *section)
{
    size_t last = (size_t) -1;
    for (size_t i = 0; i < doc->count; i++) {
        const IniLine *line = &doc->lines[i];
        if (strcmp(line->section, section) == 0 &&
        (line->kind == LINE_SECTION || line->kind == LINE_KEY ||
        line->kind == LINE_COMMENTED_KEY || line->kind == LINE_CONTINUATION))
            last = i;
    }
    if (last != (size_t) -1)
        return last + 1;
    ini_doc_add_section(doc, section);
    return doc->count;
}

static void mark_dirty(IniDoc *doc, const char *section, const char *key)
{
    for (size_t i = 0; i < doc->dirty_count; i++) {
        if (strcmp(doc->dirty_sections[i], section) == 0 && strcmp(doc->dirty_keys[i], key) == 0)
            return;
    }
    doc->dirty_sections = realloc(doc->dirty_sections, (doc->dirty_count + 1) * sizeof(char*));
    doc->dirty_keys = realloc(doc->dirty_keys, (doc->dirty_count + 1) * sizeof(char*));
    doc->dirty_sections[doc->dirty_count] = strdup(section);
    doc->dirty_keys[doc->dirty_count] = strdup(key);
    doc->dirty_count++;
}

// True if inih would read `value` back unchanged from "key=value"
bool ini_doc_value_is_representable(const char *value)
{
    if (strchr(value, '\n') != NULL || strchr(value, '\r') != NULL)
        return false;
    size_t n = strlen(value);
    if (n > 0 && (isspace((unsigned char) value[0]) || isspace((unsigned char) value[n - 1])))
        return false;
    return *find_chars_or_comment(value, NULL) == '\0';
}

bool ini_doc_set(IniDoc *doc, const char *section, const char *key, const char *value)
{
    if (!ini_doc_value_is_representable(value))
        return false;

    // Replace the last active instance (and drop its continuation lines)
    for (size_t i = doc->count; i-- > 0;) {
        IniLine *line = &doc->lines[i];
        if (line->kind == LINE_KEY && line_matches(line, section, key)) {
            make_key_line(line, key, value);
            while (i + 1 < doc->count && doc->lines[i + 1].kind == LINE_CONTINUATION)
                delete_line(doc, i + 1);
            mark_dirty(doc, section, key);
            return true;
        }
    }

    // Un-comment a commented-out instance
    for (size_t i = doc->count; i-- > 0;) {
        IniLine *line = &doc->lines[i];
        if (line->kind == LINE_COMMENTED_KEY && line_matches(line, section, key)) {
            make_key_line(line, key, value);
            mark_dirty(doc, section, key);
            return true;
        }
    }

    // Append to the section
    size_t at = section_insert_point(doc, section);
    IniLine line = { .kind = LINE_KEY, .text = strdup(""), .section = strdup(section) };
    insert_line(doc, at, line);
    make_key_line(&doc->lines[at], key, value);
    mark_dirty(doc, section, key);
    return true;
}

void ini_doc_unset(IniDoc *doc, const char *section, const char *key)
{
    for (size_t i = 0; i < doc->count; i++) {
        IniLine *line = &doc->lines[i];
        if (line->kind == LINE_KEY && line_matches(line, section, key)) {
            size_t n = strlen(line->text) + 2;
            char *text = malloc(n);
            snprintf(text, n, "#%s", line->text);
            free(line->text);
            line->text = text;
            line->kind = LINE_COMMENTED_KEY;
            // Also comment out continuation lines that follow this key
            for (size_t j = i + 1; j < doc->count && doc->lines[j].kind == LINE_CONTINUATION; j++) {
                IniLine *cont = &doc->lines[j];
                size_t cont_n = strlen(cont->text) + 2;
                char *cont_text = malloc(cont_n);
                snprintf(cont_text, cont_n, "#%s", cont->text);
                free(cont->text);
                cont->text = cont_text;
                cont->kind = LINE_COMMENT;
            }
            mark_dirty(doc, section, key);
        }
    }
}

size_t ini_doc_dirty_count(const IniDoc *doc)
{
    return doc->dirty_count;
}

void ini_doc_dirty_get(const IniDoc *doc, size_t index, const char **section, const char **key)
{
    *section = doc->dirty_sections[index];
    *key = doc->dirty_keys[index];
}

bool ini_doc_has_section(const IniDoc *doc, const char *section)
{
    for (size_t i = 0; i < doc->count; i++) {
        if (doc->lines[i].kind == LINE_SECTION && strcmp(doc->lines[i].section, section) == 0)
            return true;
    }
    return false;
}

void ini_doc_add_section(IniDoc *doc, const char *section)
{
    if (ini_doc_has_section(doc, section))
        return;
    if (doc->count > 0 && doc->lines[doc->count - 1].kind != LINE_BLANK) {
        IniLine blank = {
            .kind = LINE_BLANK,
            .text = strdup(""),
            .section = strdup(doc->lines[doc->count - 1].section)
        };
        insert_line(doc, doc->count, blank);
    }
    size_t n = strlen(section) + 3;
    char *text = malloc(n);
    snprintf(text, n, "[%s]", section);
    IniLine header = { .kind = LINE_SECTION, .text = text, .section = strdup(section) };
    insert_line(doc, doc->count, header);
    doc->trailing_newline = true;
}

void ini_doc_free(IniDoc *doc)
{
    if (doc == NULL)
        return;
    for (size_t i = 0; i < doc->count; i++)
        free_line(&doc->lines[i]);
    free(doc->lines);
    for (size_t i = 0; i < doc->dirty_count; i++) {
        free(doc->dirty_sections[i]);
        free(doc->dirty_keys[i]);
    }
    free(doc->dirty_sections);
    free(doc->dirty_keys);
    free(doc);
}
