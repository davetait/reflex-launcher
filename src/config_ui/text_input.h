#ifndef TEXT_INPUT_H
#define TEXT_INPUT_H
#include <stddef.h>

// Display-free model of the on-screen keyboard used by the Settings screen.

typedef enum { LAYOUT_TEXT, LAYOUT_NUMBER } TextLayout;
typedef enum { TEXT_NONE, TEXT_DONE, TEXT_CANCEL } TextResult;
typedef struct text_input TextInput;

#define TEXT_INPUT_MAX_BYTES 511

// Cursor starts at the end, focus on the first key. Returns NULL on allocation failure.
TextInput *text_input_new(const char *initial, TextLayout layout);
void text_input_free(TextInput *t);
const char *text_input_value(const TextInput *t);
size_t text_input_cursor(const TextInput *t);               // byte offset
void text_input_move_focus(TextInput *t, int dx, int dy);   // clamps; changing rows clamps the column
TextResult text_input_press(TextInput *t);                  // activates the focused key
void text_input_type(TextInput *t, const char *utf8);       // physical typing at the cursor
void text_input_erase(TextInput *t);                        // deletes one UTF-8 character before the cursor
void text_input_move_cursor(TextInput *t, int delta);       // by characters, clamped
size_t text_input_rows(const TextInput *t);
size_t text_input_row_length(const TextInput *t, size_t row);
const char *text_input_key_label(const TextInput *t, size_t row, size_t col);
void text_input_focus(const TextInput *t, size_t *row, size_t *col);

#endif
