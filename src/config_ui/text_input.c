#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "text_input.h"

// Labels are UTF-8; written as escapes so the file is encoding-independent.
#define KEY_BACKSPACE "\xE2\x8C\xAB"    // U+232B
#define KEY_LEFT "\xE2\x86\x90"         // U+2190
#define KEY_RIGHT "\xE2\x86\x92"        // U+2192

typedef struct {
    const char *const *keys;
    size_t length;
} KeyRow;

typedef struct {
    const KeyRow *rows;
    size_t row_count;
} Page;

typedef enum { PAGE_LOWER, PAGE_UPPER, PAGE_SYMBOLS, PAGE_NUMBER } PageId;

struct text_input {
    char value[TEXT_INPUT_MAX_BYTES + 1];
    size_t length;
    size_t cursor;
    PageId page;
    size_t row;
    size_t col;
    bool replace;   // the next typed character replaces the prefilled value (number layout)
};

#define ROW(array) {array, sizeof(array) / sizeof((array)[0])}

static const char *const lower_1[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
static const char *const lower_2[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
static const char *const lower_3[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l"};
static const char *const lower_4[] = {"z", "x", "c", "v", "b", "n", "m"};
static const char *const upper_2[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
static const char *const upper_3[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L"};
static const char *const upper_4[] = {"Z", "X", "C", "V", "B", "N", "M"};
static const char *const text_special[] = {"Shift", "#+=", "Space", KEY_BACKSPACE, KEY_LEFT, KEY_RIGHT, "Done", "Cancel"};

static const char *const sym_2[] = {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"};
static const char *const sym_3[] = {"-", "_", "=", "+", "[", "]", "{", "}", "\\", "|"};
static const char *const sym_4[] = {";", ":", "'", "\"", ",", ".", "<", ">", "/", "?"};
static const char *const sym_5[] = {"`", "~"};
static const char *const sym_special[] = {"abc", "Space", KEY_BACKSPACE, KEY_LEFT, KEY_RIGHT, "Done", "Cancel"};

static const char *const num_1[] = {"7", "8", "9"};
static const char *const num_2[] = {"4", "5", "6"};
static const char *const num_3[] = {"1", "2", "3"};
static const char *const num_4[] = {"-", "0", "."};
static const char *const num_special[] = {"%", KEY_BACKSPACE, KEY_LEFT, KEY_RIGHT, "Done", "Cancel"};

static const KeyRow lower_rows[] = {ROW(lower_1), ROW(lower_2), ROW(lower_3), ROW(lower_4), ROW(text_special)};
static const KeyRow upper_rows[] = {ROW(lower_1), ROW(upper_2), ROW(upper_3), ROW(upper_4), ROW(text_special)};
static const KeyRow symbol_rows[] = {ROW(lower_1), ROW(sym_2), ROW(sym_3), ROW(sym_4), ROW(sym_5), ROW(sym_special)};
static const KeyRow number_rows[] = {ROW(num_1), ROW(num_2), ROW(num_3), ROW(num_4), ROW(num_special)};

#define PAGE(rows) {rows, sizeof(rows) / sizeof((rows)[0])}
static const Page pages[] = {PAGE(lower_rows), PAGE(upper_rows), PAGE(symbol_rows), PAGE(number_rows)};

static const Page *current_page(const TextInput *t);
static size_t char_length(unsigned char lead);
static size_t prev_char_start(const char *s, size_t pos);
static void insert_text(TextInput *t, const char *utf8);
static void type_text(TextInput *t, const char *utf8);
static void switch_page(TextInput *t, PageId page, const char *focus_label);

TextInput *text_input_new(const char *initial, TextLayout layout)
{
    TextInput *t = calloc(1, sizeof(*t));
    if (t == NULL)
        return NULL;
    t->page = layout == LAYOUT_NUMBER ? PAGE_NUMBER : PAGE_LOWER;
    if (initial != NULL)
        insert_text(t, initial);
    t->replace = layout == LAYOUT_NUMBER && t->length > 0;
    return t;
}

void text_input_free(TextInput *t)
{
    free(t);
}

const char *text_input_value(const TextInput *t)
{
    return t->value;
}

size_t text_input_cursor(const TextInput *t)
{
    return t->cursor;
}

void text_input_move_focus(TextInput *t, int dx, int dy)
{
    const Page *page = current_page(t);
    long row = (long) t->row + dy;
    if (row < 0)
        row = 0;
    if (row > (long) page->row_count - 1)
        row = (long) page->row_count - 1;
    t->row = (size_t) row;

    long col = (long) t->col + dx;
    long last = (long) page->rows[t->row].length - 1;
    if (col > last)
        col = last;
    if (col < 0)
        col = 0;
    t->col = (size_t) col;
}

TextResult text_input_press(TextInput *t)
{
    const char *label = text_input_key_label(t, t->row, t->col);
    if (strcmp(label, "Done") == 0)
        return TEXT_DONE;
    if (strcmp(label, "Cancel") == 0)
        return TEXT_CANCEL;
    if (strcmp(label, KEY_BACKSPACE) == 0)
        text_input_erase(t);
    else if (strcmp(label, KEY_LEFT) == 0)
        text_input_move_cursor(t, -1);
    else if (strcmp(label, KEY_RIGHT) == 0)
        text_input_move_cursor(t, 1);
    else if (strcmp(label, "Shift") == 0)
        switch_page(t, PAGE_UPPER, "Shift");
    else if (strcmp(label, "#+=") == 0)
        switch_page(t, PAGE_SYMBOLS, "abc");
    else if (strcmp(label, "abc") == 0)
        switch_page(t, PAGE_LOWER, "#+=");
    else {
        type_text(t, strcmp(label, "Space") == 0 ? " " : label);
        if (t->page == PAGE_UPPER)
            t->page = PAGE_LOWER;   // Shift applies to one character; focus stays put
    }
    return TEXT_NONE;
}

void text_input_type(TextInput *t, const char *utf8)
{
    if (utf8 != NULL)
        type_text(t, utf8);
}

void text_input_erase(TextInput *t)
{
    t->replace = false;
    if (t->cursor == 0)
        return;
    size_t start = prev_char_start(t->value, t->cursor);
    memmove(t->value + start, t->value + t->cursor, t->length - t->cursor + 1);
    t->length -= t->cursor - start;
    t->cursor = start;
}

void text_input_move_cursor(TextInput *t, int delta)
{
    t->replace = false;
    while (delta < 0 && t->cursor > 0) {
        t->cursor = prev_char_start(t->value, t->cursor);
        delta++;
    }
    while (delta > 0 && t->cursor < t->length) {
        t->cursor += char_length((unsigned char) t->value[t->cursor]);
        if (t->cursor > t->length)
            t->cursor = t->length;
        delta--;
    }
}

size_t text_input_rows(const TextInput *t)
{
    return current_page(t)->row_count;
}

size_t text_input_row_length(const TextInput *t, size_t row)
{
    const Page *page = current_page(t);
    return row < page->row_count ? page->rows[row].length : 0;
}

const char *text_input_key_label(const TextInput *t, size_t row, size_t col)
{
    const Page *page = current_page(t);
    if (row >= page->row_count || col >= page->rows[row].length)
        return "";
    return page->rows[row].keys[col];
}

void text_input_focus(const TextInput *t, size_t *row, size_t *col)
{
    *row = t->row;
    *col = t->col;
}

static const Page *current_page(const TextInput *t)
{
    return &pages[t->page];
}

// Length of the UTF-8 sequence introduced by a lead byte (1 for invalid bytes)
static size_t char_length(unsigned char lead)
{
    if (lead >= 0xF0)
        return 4;
    if (lead >= 0xE0)
        return 3;
    if (lead >= 0xC0)
        return 2;
    return 1;
}

static size_t prev_char_start(const char *s, size_t pos)
{
    do {
        pos--;
    } while (pos > 0 && ((unsigned char) s[pos] & 0xC0) == 0x80);
    return pos;
}

// Inserts whole characters at the cursor until the byte cap would be exceeded
static void insert_text(TextInput *t, const char *utf8)
{
    while (*utf8 != '\0') {
        size_t n = char_length((unsigned char) *utf8);
        size_t avail = strlen(utf8);
        if (n > avail)
            n = avail;
        if (t->length + n > TEXT_INPUT_MAX_BYTES)
            return;
        memmove(t->value + t->cursor + n, t->value + t->cursor, t->length - t->cursor + 1);
        memcpy(t->value + t->cursor, utf8, n);
        t->cursor += n;
        t->length += n;
        utf8 += n;
    }
}

// Typing or pressing a character key; the first one replaces a prefilled number
static void type_text(TextInput *t, const char *utf8)
{
    if (t->replace && *utf8 != '\0') {
        t->value[0] = '\0';
        t->length = 0;
        t->cursor = 0;
    }
    t->replace = false;
    insert_text(t, utf8);
}

// Shows another page with focus on its bottom (special) row: on `focus_label` if the row
// has it (so the key that switches back is under the cursor), else the same column, clamped
static void switch_page(TextInput *t, PageId page, const char *focus_label)
{
    t->page = page;
    const Page *p = current_page(t);
    t->row = p->row_count - 1;
    const KeyRow *special = &p->rows[t->row];
    for (size_t i = 0; i < special->length; i++) {
        if (strcmp(special->keys[i], focus_label) == 0) {
            t->col = i;
            return;
        }
    }
    if (t->col >= special->length)
        t->col = special->length - 1;
}
