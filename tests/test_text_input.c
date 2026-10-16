#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "../src/config_ui/text_input.h"

static void focus_label(TextInput *t, const char *label)
{
    for (size_t r = 0; r < text_input_rows(t); r++) {
        for (size_t c = 0; c < text_input_row_length(t, r); c++) {
            if (strcmp(text_input_key_label(t, r, c), label) == 0) {
                size_t fr, fc;
                text_input_focus(t, &fr, &fc);
                text_input_move_focus(t, (int) c - (int) fc, (int) r - (int) fr);
                return;
            }
        }
    }
    CHECK_STR("label not found", label);
}

static void test_initial_state(void)
{
    TextInput *t = text_input_new("abc", LAYOUT_TEXT);
    size_t row, col;
    CHECK_STR(text_input_value(t), "abc");
    CHECK(text_input_cursor(t) == 3);
    text_input_focus(t, &row, &col);
    CHECK(row == 0 && col == 0);
    CHECK_STR(text_input_key_label(t, 0, 0), "1");
    text_input_free(t);
}

static void test_press_inserts_and_moves(void)
{
    TextInput *t = text_input_new("", LAYOUT_TEXT);
    text_input_move_focus(t, 0, 1);
    CHECK_STR(text_input_key_label(t, 1, 0), "q");
    CHECK(text_input_press(t) == TEXT_NONE);
    CHECK_STR(text_input_value(t), "q");
    text_input_move_focus(t, 3, 0);
    text_input_press(t);
    CHECK_STR(text_input_value(t), "qr");
    text_input_free(t);
}

static void test_shift_one_shot(void)
{
    TextInput *t = text_input_new("", LAYOUT_TEXT);
    text_input_move_focus(t, 0, 4);
    CHECK_STR(text_input_key_label(t, 4, 0), "Shift");
    text_input_press(t);
    CHECK_STR(text_input_key_label(t, 4, 0), "Shift");
    focus_label(t, "Q");
    text_input_press(t);
    CHECK_STR(text_input_value(t), "Q");
    text_input_press(t);
    CHECK_STR(text_input_value(t), "Qq");
    text_input_free(t);
}

static void test_done_and_cancel(void)
{
    TextInput *t = text_input_new("hi", LAYOUT_TEXT);
    focus_label(t, "Done");
    CHECK(text_input_press(t) == TEXT_DONE);
    focus_label(t, "Cancel");
    CHECK(text_input_press(t) == TEXT_CANCEL);
    CHECK_STR(text_input_value(t), "hi");
    text_input_free(t);
}

static void test_symbols_page(void)
{
    TextInput *t = text_input_new("", LAYOUT_TEXT);
    focus_label(t, "#+=");
    text_input_press(t);
    CHECK_STR(text_input_key_label(t, 1, 0), "!");
    focus_label(t, "abc");
    text_input_press(t);
    CHECK_STR(text_input_key_label(t, 1, 0), "q");
    text_input_free(t);
}

static const char *focused_label(const TextInput *t)
{
    size_t row, col;
    text_input_focus(t, &row, &col);
    return text_input_key_label(t, row, col);
}

static void test_page_switch_keeps_focus(void)
{
    TextInput *t = text_input_new("", LAYOUT_TEXT);
    focus_label(t, "Shift");
    text_input_press(t);
    CHECK_STR(focused_label(t), "Shift");
    focus_label(t, "#+=");
    text_input_press(t);
    CHECK_STR(focused_label(t), "abc");
    text_input_press(t);
    CHECK_STR(focused_label(t), "#+=");
    text_input_press(t);
    CHECK_STR(focused_label(t), "abc");
    text_input_free(t);
}

static void test_number_first_key_replaces(void)
{
    TextInput *t = text_input_new("256", LAYOUT_NUMBER);
    CHECK_STR(focused_label(t), "7");
    text_input_press(t);
    CHECK_STR(text_input_value(t), "7");
    text_input_type(t, "5");
    CHECK_STR(text_input_value(t), "75");
    text_input_free(t);

    t = text_input_new("256", LAYOUT_NUMBER);
    text_input_type(t, "12");
    CHECK_STR(text_input_value(t), "12");
    text_input_free(t);

    // Erasing or moving the cursor first edits the value instead
    t = text_input_new("256", LAYOUT_NUMBER);
    text_input_erase(t);
    text_input_type(t, "1");
    CHECK_STR(text_input_value(t), "251");
    text_input_free(t);
    t = text_input_new("256", LAYOUT_NUMBER);
    text_input_move_cursor(t, -1);
    text_input_type(t, "9");
    CHECK_STR(text_input_value(t), "2596");
    text_input_free(t);

    // Moving key focus doesn't count, and text layouts never replace
    t = text_input_new("256", LAYOUT_NUMBER);
    text_input_move_focus(t, 1, 0);
    text_input_press(t);
    CHECK_STR(text_input_value(t), "8");
    text_input_free(t);
    t = text_input_new("abc", LAYOUT_TEXT);
    text_input_type(t, "d");
    CHECK_STR(text_input_value(t), "abcd");
    text_input_free(t);
}

static void test_number_layout(void)
{
    TextInput *t = text_input_new("", LAYOUT_NUMBER);
    CHECK(text_input_rows(t) == 5);
    CHECK_STR(text_input_key_label(t, 0, 0), "7");
    CHECK_STR(text_input_key_label(t, 3, 2), ".");
    text_input_press(t);
    text_input_move_focus(t, 2, 3);
    text_input_press(t);
    text_input_move_focus(t, -1, -2);
    text_input_press(t);
    CHECK_STR(text_input_value(t), "7.5");
    text_input_free(t);
}

static void test_utf8_erase_and_cursor(void)
{
    TextInput *t = text_input_new("a\xC3\xB1" "b", LAYOUT_TEXT);
    text_input_move_cursor(t, -1);
    CHECK(text_input_cursor(t) == 3);
    text_input_erase(t);
    CHECK_STR(text_input_value(t), "ab");
    CHECK(text_input_cursor(t) == 1);
    text_input_type(t, "\xC3\xA9");
    CHECK_STR(text_input_value(t), "a\xC3\xA9" "b");
    text_input_move_cursor(t, 5);
    CHECK(text_input_cursor(t) == 4);
    text_input_move_cursor(t, -9);
    CHECK(text_input_cursor(t) == 0);
    text_input_erase(t);
    CHECK_STR(text_input_value(t), "a\xC3\xA9" "b");
    text_input_free(t);
}

static void test_max_length(void)
{
    TextInput *t = text_input_new("", LAYOUT_TEXT);
    for (int i = 0; i < 600; i++)
        text_input_type(t, "x");
    CHECK(strlen(text_input_value(t)) == 511);
    text_input_type(t, "\xC3\xA9");
    CHECK(strlen(text_input_value(t)) == 511);
    text_input_free(t);
}

static void test_focus_clamps_between_rows(void)
{
    TextInput *t = text_input_new("", LAYOUT_TEXT);
    size_t row, col;
    text_input_move_focus(t, 0, 1);
    text_input_move_focus(t, 20, 0);
    text_input_focus(t, &row, &col);
    CHECK(row == 1 && col == 9);
    CHECK_STR(text_input_key_label(t, row, col), "p");
    text_input_move_focus(t, 0, 1);
    text_input_focus(t, &row, &col);
    CHECK(row == 2 && col == 8);
    CHECK_STR(text_input_key_label(t, row, col), "l");
    text_input_move_focus(t, 0, -10);
    text_input_focus(t, &row, &col);
    CHECK(row == 0);
    text_input_free(t);
}

void suite_text_input(void)
{
    test_initial_state();
    test_press_inserts_and_moves();
    test_shift_one_shot();
    test_done_and_cancel();
    test_symbols_page();
    test_number_layout();
    test_utf8_erase_and_cursor();
    test_max_length();
    test_focus_clamps_between_rows();
    test_page_switch_keeps_focus();
    test_number_first_key_replaces();
}
