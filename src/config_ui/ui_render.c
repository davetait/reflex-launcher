#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <SDL.h>
#include "ui_render.h"

#define VALUE_BUF 512
#define TEXT_BUF 1024
#define PATH_SHOW_CHARS 40
#define MAX_WRAP_LINES 12
#define ARC_SEGMENTS 8

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Align;

// Everything one frame needs, with sizes already scaled to the screen
typedef struct {
    SDL_Renderer *r;
    UiTextCache *c;
    const UiState *s;
    int w, h;
    int m;          // outer margin
    int body;       // body "line" unit (font size)
    int small;
    int header;
    int top;        // top of the sidebar and rows
    int footer_y;   // top of the footer
    int footer_line;
    int bar;        // accent bar / outline thickness
} Frame;

static const SDL_Color COLOR_PANEL = {14, 17, 23, 235};
static const SDL_Color COLOR_ACCENT = {61, 139, 253, 255};
static const SDL_Color COLOR_LABEL = {230, 233, 238, 255};
static const SDL_Color COLOR_DIM = {140, 146, 160, 255};
static const SDL_Color COLOR_ERROR = {255, 99, 99, 255};
static const SDL_Color COLOR_FOCUS_ROW = {255, 255, 255, 28};
static const SDL_Color COLOR_OFF = {90, 96, 110, 255};
static const SDL_Color COLOR_CARD = {28, 33, 43, 250};
static const SDL_Color COLOR_WHITE = {255, 255, 255, 255};
static const SDL_Color COLOR_SCRIM = {0, 0, 0, 120};
static const SDL_Color COLOR_KEY = {255, 255, 255, 24};
static const SDL_Color COLOR_TRACK = {255, 255, 255, 40};
static const SDL_Color COLOR_FIELD = {0, 0, 0, 110};
static const SDL_Color COLOR_RULE = {255, 255, 255, 30};

static const char *const HINTS = "Select: change   \xE2\x86\x90/\xE2\x86\x92: adjust   Y/Del: reset   "
                                 "Start/Ctrl+S: save   B/Esc: back";
static const char *const CLOSE_HINTS = "Select or B/Esc: close";
static const char *const EXIT_MESSAGE = "You have unsaved changes. Save them before leaving?";
static const char *const ELLIPSIS = "\xE2\x80\xA6";

// Primitives
static void set_color(SDL_Renderer *r, SDL_Color color);
static void fill_rect(const Frame *f, int x, int y, int w, int h, SDL_Color color);
static void fill_round(const Frame *f, SDL_Rect rect, int radius, SDL_Color color);
static void fill_round_spans(SDL_Renderer *r, SDL_Rect rect, int radius);
static int corner_inset(int radius, int row);
static void outline_round(const Frame *f, SDL_Rect rect, int radius, int thickness, SDL_Color color);
static int draw_text(const Frame *f, UiFont font, const char *text, SDL_Color color, int x, int cy, Align align);
static size_t utf8_chars(const char *s);
static const char *utf8_skip(const char *s, size_t chars);
static void fit_text(const Frame *f, UiFont font, const char *text, int max_w, bool keep_end, char *out, size_t len);
static size_t wrap_text(const Frame *f, UiFont font, const char *text, int max_w, char lines[][VALUE_BUF], size_t max_lines);
static size_t first_visible(size_t focus, size_t visible);
static void draw_scrollbar(const Frame *f, int x, int y, int h, size_t first, size_t visible, size_t count);
static bool parse_color(const char *text, SDL_Color *out);

// Screen parts
static void draw_header(const Frame *f);
static void draw_sidebar(const Frame *f);
static void draw_rows(const Frame *f);
static void draw_control(const Frame *f, const SettingDef *def, int right, int cy, int max_w, bool focused);
static void draw_stepper(const Frame *f, const char *value, SDL_Color color, int right, int cy, int max_w, bool focused);
static void draw_footer(const Frame *f);

// Overlays
static SDL_Rect card_rect(const Frame *f, int content_h);
static int draw_card(const Frame *f, int content_h, const char *title, SDL_Rect *inner);
static void draw_list_item(const Frame *f, SDL_Rect item, const char *text, SDL_Color color, bool focused);
static void draw_choice_list(const Frame *f);
static void draw_text_entry(const Frame *f);
static void draw_files(const Frame *f);
static void draw_color(const Frame *f);
static void draw_dialog(const Frame *f);

void ui_render(SDL_Renderer *r, UiTextCache *c, const UiState *s, int w, int h)
{
    Frame f;
    f.r = r;
    f.c = c;
    f.s = s;
    f.w = w;
    f.h = h;
    f.m = h / 20;
    f.body = ui_text_size(c, UI_FONT_BODY);
    f.small = ui_text_size(c, UI_FONT_SMALL);
    f.header = ui_text_size(c, UI_FONT_HEADER);
    f.top = f.m + h / 10;
    f.footer_line = f.small * 3 / 2;
    f.footer_y = h - f.m - 2 * f.footer_line;
    f.bar = h >= 1000 ? 3 : 2;

    SDL_BlendMode old_mode;
    SDL_GetRenderDrawBlendMode(r, &old_mode);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    fill_rect(&f, 0, 0, w, h, COLOR_PANEL);
    draw_header(&f);
    if (s->model != NULL) {
        draw_sidebar(&f);
        draw_rows(&f);
    }

    switch (s->mode) {
        case MODE_CHOICE_LIST:
        case MODE_TEXT:
        case MODE_FILES:
        case MODE_COLOR:
        case MODE_CONFIRM_EXIT:
        case MODE_CONFIRM_OVERWRITE:
        case MODE_MESSAGE:
            fill_rect(&f, 0, 0, w, h, COLOR_SCRIM);
            break;
        default:
            break;
    }
    switch (s->mode) {
        case MODE_CHOICE_LIST: draw_choice_list(&f); break;
        case MODE_TEXT: draw_text_entry(&f); break;
        case MODE_FILES: draw_files(&f); break;
        case MODE_COLOR: draw_color(&f); break;
        case MODE_CONFIRM_EXIT:
        case MODE_CONFIRM_OVERWRITE:
        case MODE_MESSAGE: draw_dialog(&f); break;
        default: break;
    }
    draw_footer(&f);

    SDL_SetRenderDrawBlendMode(r, old_mode);
}

// ---------------------------------------------------------------- primitives

static void set_color(SDL_Renderer *r, SDL_Color color)
{
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
}

static void fill_rect(const Frame *f, int x, int y, int w, int h, SDL_Color color)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_Rect rect = {x, y, w, h};
    set_color(f->r, color);
    SDL_RenderFillRect(f->r, &rect);
}

// How far a rounded corner cuts into a scanline `row` pixels from the corner's edge
static int corner_inset(int radius, int row)
{
    if (row >= radius)
        return 0;
    double dy = (double) radius - (double) row - 0.5;
    double dx = sqrt((double) radius * radius - dy * dy);
    return (int) ((double) radius - dx + 0.5);
}

// Rounded rect as one span per corner scanline plus a middle block (works on any SDL 2)
static void fill_round_spans(SDL_Renderer *r, SDL_Rect rect, int radius)
{
    for (int row = 0; row < radius; row++) {
        int inset = corner_inset(radius, row);
        SDL_Rect top = {rect.x + inset, rect.y + row, rect.w - 2 * inset, 1};
        SDL_Rect bottom = {rect.x + inset, rect.y + rect.h - 1 - row, rect.w - 2 * inset, 1};
        SDL_RenderFillRect(r, &top);
        SDL_RenderFillRect(r, &bottom);
    }
    SDL_Rect middle = {rect.x, rect.y + radius, rect.w, rect.h - 2 * radius};
    if (middle.h > 0)
        SDL_RenderFillRect(r, &middle);
}

static void fill_round(const Frame *f, SDL_Rect rect, int radius, SDL_Color color)
{
    if (rect.w <= 0 || rect.h <= 0)
        return;
    if (radius > rect.w / 2)
        radius = rect.w / 2;
    if (radius > rect.h / 2)
        radius = rect.h / 2;
    if (radius < 0)
        radius = 0;
    set_color(f->r, color);
    if (radius == 0) {
        SDL_RenderFillRect(f->r, &rect);
        return;
    }
    // UI_NO_GEOMETRY forces the span fallback, to check it on newer SDL
#if SDL_VERSION_ATLEAST(2, 0, 18) && !defined(UI_NO_GEOMETRY)
    // A triangle fan around the centre: corner arcs, clockwise from the top-left
    SDL_Vertex vertices[1 + 4 * (ARC_SEGMENTS + 1)];
    int indices[3 * 4 * (ARC_SEGMENTS + 1)];
    const float cx[4] = {(float) (rect.x + radius), (float) (rect.x + rect.w - radius),
                         (float) (rect.x + rect.w - radius), (float) (rect.x + radius)};
    const float cy[4] = {(float) (rect.y + radius), (float) (rect.y + radius),
                         (float) (rect.y + rect.h - radius), (float) (rect.y + rect.h - radius)};
    const double pi = 3.14159265358979323846;
    int n = 0;
    memset(vertices, 0, sizeof(vertices));
    vertices[n].position.x = (float) rect.x + (float) rect.w / 2.0f;
    vertices[n].position.y = (float) rect.y + (float) rect.h / 2.0f;
    vertices[n++].color = color;
    for (int corner = 0; corner < 4; corner++) {
        for (int i = 0; i <= ARC_SEGMENTS; i++) {
            double angle = pi + (double) corner * pi / 2.0 + (double) i * (pi / 2.0) / ARC_SEGMENTS;
            vertices[n].position.x = cx[corner] + (float) (cos(angle) * radius);
            vertices[n].position.y = cy[corner] + (float) (sin(angle) * radius);
            vertices[n++].color = color;
        }
    }
    int perimeter = n - 1, count = 0;
    for (int i = 0; i < perimeter; i++) {
        indices[count++] = 0;
        indices[count++] = 1 + i;
        indices[count++] = 1 + (i + 1) % perimeter;
    }
    if (SDL_RenderGeometry(f->r, NULL, vertices, n, indices, count) == 0)
        return;
#endif
    fill_round_spans(f->r, rect, radius);
}

// A rounded ring drawn as non-overlapping spans, so translucent colors blend once
static void outline_round(const Frame *f, SDL_Rect rect, int radius, int thickness, SDL_Color color)
{
    if (rect.w <= 2 * thickness || rect.h <= 2 * thickness)
        return;
    if (radius > rect.h / 2)
        radius = rect.h / 2;
    if (radius > rect.w / 2)
        radius = rect.w / 2;
    int inner_radius = radius > thickness ? radius - thickness : 0;
    set_color(f->r, color);
    for (int row = 0; row < rect.h; row++) {
        int from_edge = row < rect.h - 1 - row ? row : rect.h - 1 - row;
        int outer = corner_inset(radius, from_edge);
        int y = rect.y + row;
        if (from_edge < thickness) {
            SDL_Rect span = {rect.x + outer, y, rect.w - 2 * outer, 1};
            SDL_RenderFillRect(f->r, &span);
            continue;
        }
        int inner = thickness + corner_inset(inner_radius, from_edge - thickness);
        SDL_Rect left = {rect.x + outer, y, inner - outer, 1};
        SDL_Rect right = {rect.x + rect.w - inner, y, inner - outer, 1};
        if (left.w > 0) {
            SDL_RenderFillRect(f->r, &left);
            SDL_RenderFillRect(f->r, &right);
        }
    }
}

// Draws text vertically centred on cy; returns its width
static int draw_text(const Frame *f, UiFont font, const char *text, SDL_Color color, int x, int cy, Align align)
{
    int tw, th;
    SDL_Texture *texture = ui_text(f->c, font, text, color, &tw, &th);
    if (texture == NULL)
        return 0;
    SDL_Rect dst = {x, cy - th / 2, tw, th};
    if (align == ALIGN_RIGHT)
        dst.x = x - tw;
    else if (align == ALIGN_CENTER)
        dst.x = x - tw / 2;
    SDL_RenderCopy(f->r, texture, NULL, &dst);
    return tw;
}

static size_t utf8_chars(const char *s)
{
    size_t count = 0;
    for (; *s != '\0'; s++) {
        if (((unsigned char) *s & 0xC0) != 0x80)
            count++;
    }
    return count;
}

// Pointer to the character `chars` characters into s (or the end)
static const char *utf8_skip(const char *s, size_t chars)
{
    while (*s != '\0') {
        if (((unsigned char) *s & 0xC0) != 0x80) {
            if (chars == 0)
                return s;
            chars--;
        }
        s++;
    }
    return s;
}

// Shortens text with an ellipsis until it fits max_w, keeping its start or its end
static void fit_text(const Frame *f, UiFont font, const char *text, int max_w, bool keep_end, char *out, size_t len)
{
    snprintf(out, len, "%s", text);
    if (ui_text_width(f->c, font, out) <= max_w)
        return;
    size_t total = utf8_chars(text);
    char candidate[TEXT_BUF];
    // Binary search for the most characters that still fit
    size_t lo = 0, hi = total;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (keep_end)
            snprintf(candidate, sizeof(candidate), "%s%s", ELLIPSIS, utf8_skip(text, total - mid));
        else
            snprintf(candidate, sizeof(candidate), "%.*s%s", (int) (utf8_skip(text, mid) - text), text, ELLIPSIS);
        if (ui_text_width(f->c, font, candidate) <= max_w)
            lo = mid;
        else
            hi = mid - 1;
    }
    if (keep_end)
        snprintf(out, len, "%s%s", ELLIPSIS, utf8_skip(text, total - lo));
    else
        snprintf(out, len, "%.*s%s", (int) (utf8_skip(text, lo) - text), text, ELLIPSIS);
}

// Word-wraps text into lines no wider than max_w; overlong words are shortened
static size_t wrap_text(const Frame *f, UiFont font, const char *text, int max_w, char lines[][VALUE_BUF], size_t max_lines)
{
    size_t count = 0;
    char line[VALUE_BUF] = "";
    const char *p = text;
    while (*p != '\0' && count < max_lines) {
        while (*p == ' ')
            p++;
        const char *end = p;
        while (*end != '\0' && *end != ' ' && *end != '\n')
            end++;
        char word[VALUE_BUF];
        snprintf(word, sizeof(word), "%.*s", (int) (end - p), p);
        char joined[2 * VALUE_BUF];
        snprintf(joined, sizeof(joined), "%s%s%s", line, line[0] != '\0' ? " " : "", word);
        if (line[0] != '\0' && ui_text_width(f->c, font, joined) > max_w) {
            snprintf(lines[count++], VALUE_BUF, "%s", line);
            snprintf(joined, sizeof(joined), "%s", word);
        }
        if (ui_text_width(f->c, font, joined) > max_w)
            fit_text(f, font, joined, max_w, false, line, sizeof(line));
        else
            snprintf(line, sizeof(line), "%s", joined);
        p = end;
        if (*p == '\n') {
            p++;
            if (count < max_lines)
                snprintf(lines[count++], VALUE_BUF, "%s", line);
            line[0] = '\0';
        }
    }
    if (line[0] != '\0' && count < max_lines)
        snprintf(lines[count++], VALUE_BUF, "%s", line);
    return count;
}

static size_t first_visible(size_t focus, size_t visible)
{
    if (visible == 0)
        return focus;
    return focus < visible ? 0 : focus - visible + 1;
}

static void draw_scrollbar(const Frame *f, int x, int y, int h, size_t first, size_t visible, size_t count)
{
    if (count <= visible || count == 0)
        return;
    int width = f->bar + 1;
    SDL_Rect track = {x, y, width, h};
    fill_round(f, track, width / 2, COLOR_TRACK);
    int thumb_h = (int) ((double) h * (double) visible / (double) count);
    int thumb_y = y + (int) ((double) h * (double) first / (double) count);
    SDL_Rect thumb = {x, thumb_y, width, thumb_h};
    fill_round(f, thumb, width / 2, COLOR_DIM);
}

// ---------------------------------------------------------------- screen parts

static void draw_header(const Frame *f)
{
    int tw, th;
    ui_text(f->c, UI_FONT_HEADER, "Settings", COLOR_LABEL, &tw, &th);
    int cy = f->m + th / 2;
    draw_text(f, UI_FONT_HEADER, "Settings", COLOR_LABEL, f->m, cy, ALIGN_LEFT);
    if (f->s->model != NULL && settings_model_dirty(f->s->model))
        draw_text(f, UI_FONT_SMALL, "Unsaved changes", COLOR_ACCENT, f->w - f->m, cy, ALIGN_RIGHT);
}

static void draw_sidebar(const Frame *f)
{
    const UiState *s = f->s;
    int x = f->m, width = f->w * 24 / 100;
    int item_h = f->body * 19 / 10;
    int pad = f->body * 6 / 10;
    for (size_t i = 0; i < settings_model_section_count(); i++) {
        SDL_Rect item = {x, f->top + (int) i * item_h, width, item_h - f->body / 8};
        bool focused = i == s->section;
        if (focused && s->mode == MODE_SECTIONS)
            fill_round(f, item, f->body / 4, COLOR_ACCENT);
        else if (focused)
            outline_round(f, item, f->body / 4, f->bar, COLOR_ACCENT);
        SDL_Color color = focused || s->mode == MODE_SECTIONS ? COLOR_LABEL : COLOR_DIM;
        if (focused && s->mode == MODE_SECTIONS)
            color = COLOR_WHITE;
        char name[VALUE_BUF];
        fit_text(f, UI_FONT_BODY, settings_model_section_name(i), width - 2 * pad, false, name, sizeof(name));
        draw_text(f, UI_FONT_BODY, name, color, x + pad, item.y + item.h / 2, ALIGN_LEFT);
    }
}

static void draw_rows(const Frame *f)
{
    const UiState *s = f->s;
    int x0 = f->m + f->w * 27 / 100, x1 = f->w - f->m;
    int row_h = f->body * 2;
    int pad = f->body * 6 / 10;
    int bottom = f->footer_y - f->body / 2;
    size_t visible = (size_t) ((bottom - f->top) / row_h);
    size_t count = settings_model_row_count(s->section);
    bool show_focus = s->mode != MODE_SECTIONS;
    size_t first = show_focus ? first_visible(s->row, visible) : 0;

    for (size_t i = first; i < count && i < first + visible; i++) {
        const SettingDef *def = settings_model_row(s->section, i);
        if (def == NULL)
            continue;
        int y = f->top + (int) (i - first) * row_h;
        int cy = y + row_h / 2;
        bool focused = show_focus && i == s->row;
        if (focused) {
            SDL_Rect fill = {x0, y, x1 - x0, row_h};
            fill_round(f, fill, f->body / 5, COLOR_FOCUS_ROW);
            fill_rect(f, x0, y, f->bar, row_h, COLOR_ACCENT);
        }
        char label[VALUE_BUF];
        fit_text(f, UI_FONT_BODY, def->label, (x1 - x0) * 45 / 100, false, label, sizeof(label));
        int label_w = draw_text(f, UI_FONT_BODY, label, COLOR_LABEL, x0 + pad, cy, ALIGN_LEFT);
        int control_max = (x1 - x0) - 2 * pad - label_w - f->body;
        draw_control(f, def, x1 - pad, cy, control_max, focused);
    }
    if (show_focus || count > visible)
        draw_scrollbar(f, x1 + f->m / 3, f->top, (int) visible * row_h, first, visible, count);
}

static void draw_stepper(const Frame *f, const char *value, SDL_Color color, int right, int cy, int max_w, bool focused)
{
    SDL_Color arrow = focused ? COLOR_LABEL : COLOR_DIM;
    int arrow_w = ui_text_width(f->c, UI_FONT_BODY, "\xE2\x80\xBA");
    int gap = f->body / 2;
    char shown[VALUE_BUF];
    fit_text(f, UI_FONT_BODY, value, max_w - 2 * (arrow_w + gap), false, shown, sizeof(shown));
    int x = right;
    x -= draw_text(f, UI_FONT_BODY, "\xE2\x80\xBA", arrow, x, cy, ALIGN_RIGHT) + gap;
    x -= draw_text(f, UI_FONT_BODY, shown, color, x, cy, ALIGN_RIGHT) + gap;
    draw_text(f, UI_FONT_BODY, "\xE2\x80\xB9", arrow, x, cy, ALIGN_RIGHT);
}

static bool parse_color(const char *text, SDL_Color *out)
{
    if (text[0] != '#' || strlen(text) != 7)
        return false;
    unsigned int value = 0;
    for (int i = 1; i < 7; i++) {
        char ch = text[i];
        unsigned int digit;
        if (ch >= '0' && ch <= '9')
            digit = (unsigned int) (ch - '0');
        else if (ch >= 'a' && ch <= 'f')
            digit = (unsigned int) (ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F')
            digit = (unsigned int) (ch - 'A' + 10);
        else
            return false;
        value = value * 16 + digit;
    }
    out->r = (Uint8) (value >> 16);
    out->g = (Uint8) (value >> 8);
    out->b = (Uint8) value;
    out->a = 255;
    return true;
}

static void draw_control(const Frame *f, const SettingDef *def, int right, int cy, int max_w, bool focused)
{
    char value[VALUE_BUF];
    ValueState state = settings_model_display(f->s->model, def, value, sizeof(value));
    char shown[TEXT_BUF];

    if (state == VALUE_INVALID) {
        char text[TEXT_BUF];
        snprintf(text, sizeof(text), "%s  (invalid)", value);
        fit_text(f, UI_FONT_BODY, text, max_w, false, shown, sizeof(shown));
        draw_text(f, UI_FONT_BODY, shown, COLOR_ERROR, right, cy, ALIGN_RIGHT);
        return;
    }
    if (state == VALUE_NOT_SET) {
        draw_text(f, UI_FONT_BODY, "Not set", COLOR_DIM, right, cy, ALIGN_RIGHT);
        return;
    }
    bool is_default = state == VALUE_DEFAULT;
    SDL_Color text_color = is_default ? COLOR_DIM : COLOR_LABEL;

    switch (def->type) {
        case SET_BOOL: {
            bool on = SDL_strcasecmp(value, "true") == 0;
            int pill_w = f->body * 24 / 10, pill_h = f->body * 12 / 10;
            SDL_Rect pill = {right - pill_w, cy - pill_h / 2, pill_w, pill_h};
            SDL_Color fill = on ? COLOR_ACCENT : COLOR_OFF;
            if (is_default)
                fill.a = 140;
            fill_round(f, pill, pill_h / 2, fill);
            int inset = pill_h / 8;
            int knob = pill_h - 2 * inset;
            SDL_Rect knob_rect = {on ? pill.x + pill_w - inset - knob : pill.x + inset, pill.y + inset, knob, knob};
            fill_round(f, knob_rect, knob / 2, COLOR_WHITE);
            if (is_default)
                draw_text(f, UI_FONT_SMALL, "Default", COLOR_DIM, pill.x - f->body / 2, cy, ALIGN_RIGHT);
            return;
        }
        case SET_CHOICE:
        case SET_MENU_REF:
        case SET_INT:
        case SET_DECIMAL:
        case SET_LENGTH: {
            char text[TEXT_BUF];
            const char *unit = "";
            if (def->type == SET_LENGTH && strchr(value, '%') == NULL)
                unit = " px";
            snprintf(text, sizeof(text), "%s%s%s", is_default ? "Default: " : "", value, unit);
            draw_stepper(f, text, text_color, right, cy, max_w, focused);
            return;
        }
        case SET_PERCENT: {
            int percent = atoi(value);
            if (percent < 0)
                percent = 0;
            if (percent > 100)
                percent = 100;
            char text[TEXT_BUF];
            snprintf(text, sizeof(text), "%s%d%%", is_default ? "Default: " : "", atoi(value));
            // Fixed-width number column so the bars line up
            int number_w = ui_text_width(f->c, UI_FONT_BODY, text);
            int column = ui_text_width(f->c, UI_FONT_BODY, "100%");
            if (number_w < column)
                number_w = column;
            draw_text(f, UI_FONT_BODY, text, text_color, right, cy, ALIGN_RIGHT);
            int bar_w = f->w * 18 / 100, bar_h = f->body / 4;
            if (bar_w > max_w - number_w - f->body / 2)
                bar_w = max_w - number_w - f->body / 2;
            if (bar_w <= 0)
                return;
            SDL_Rect track = {right - number_w - f->body / 2 - bar_w, cy - bar_h / 2, bar_w, bar_h};
            fill_round(f, track, bar_h / 2, COLOR_TRACK);
            SDL_Rect level = track;
            level.w = bar_w * percent / 100;
            SDL_Color fill = is_default ? COLOR_DIM : COLOR_ACCENT;
            if (level.w > 0)
                fill_round(f, level, bar_h / 2, fill);
            return;
        }
        case SET_COLOR: {
            char text[TEXT_BUF];
            snprintf(text, sizeof(text), "%s%s", is_default ? "Default: " : "", value);
            int text_w = draw_text(f, UI_FONT_BODY, text, text_color, right, cy, ALIGN_RIGHT);
            int size = f->body * 12 / 10;
            SDL_Rect swatch = {right - text_w - f->body / 2 - size, cy - size / 2, size, size};
            SDL_Color color;
            if (parse_color(value, &color))
                fill_rect(f, swatch.x, swatch.y, size, size, color);
            set_color(f->r, COLOR_WHITE);
            SDL_RenderDrawRect(f->r, &swatch);
            return;
        }
        default: {
            char text[TEXT_BUF];
            const char *body = value;
            char shortened[TEXT_BUF];
            bool is_path = def->type == SET_FILE || def->type == SET_DIR || def->type == SET_FONT;
            if (is_path && utf8_chars(value) > PATH_SHOW_CHARS) {
                snprintf(shortened, sizeof(shortened), "%s%s", ELLIPSIS,
                         utf8_skip(value, utf8_chars(value) - (PATH_SHOW_CHARS - 1)));
                body = shortened;
            }
            snprintf(text, sizeof(text), "%s%s", is_default ? "Default: " : "", body);
            fit_text(f, UI_FONT_BODY, text, max_w, is_path, shown, sizeof(shown));
            draw_text(f, UI_FONT_BODY, shown, text_color, right, cy, ALIGN_RIGHT);
            return;
        }
    }
}

static void draw_footer(const Frame *f)
{
    const UiState *s = f->s;
    int x0 = f->m, x1 = f->w - f->m;
    fill_rect(f, x0, f->footer_y - f->body / 4, x1 - x0, 1, COLOR_RULE);
    int cy1 = f->footer_y + f->footer_line / 2;
    int cy2 = cy1 + f->footer_line;
    char line[TEXT_BUF];
    if (s->footer[0] != '\0') {
        fit_text(f, UI_FONT_SMALL, s->footer, x1 - x0, false, line, sizeof(line));
        draw_text(f, UI_FONT_SMALL, line, COLOR_ERROR, x0, cy1, ALIGN_LEFT);
    }
    else if (s->model != NULL && s->mode != MODE_SECTIONS && s->mode != MODE_MESSAGE &&
             s->mode != MODE_CONFIRM_EXIT && s->mode != MODE_CONFIRM_OVERWRITE) {
        const SettingDef *def = s->editing != NULL ? s->editing : settings_model_row(s->section, s->row);
        if (def != NULL) {
            fit_text(f, UI_FONT_SMALL, def->help, x1 - x0, false, line, sizeof(line));
            draw_text(f, UI_FONT_SMALL, line, COLOR_LABEL, x0, cy1, ALIGN_LEFT);
        }
    }
    fit_text(f, UI_FONT_SMALL, s->model != NULL ? HINTS : CLOSE_HINTS, x1 - x0, false, line, sizeof(line));
    draw_text(f, UI_FONT_SMALL, line, COLOR_DIM, x0, cy2, ALIGN_LEFT);
}

// ---------------------------------------------------------------- overlays

static SDL_Rect card_rect(const Frame *f, int content_h)
{
    int pad = f->body;
    SDL_Rect card;
    card.w = f->w / 2;
    card.h = content_h + 2 * pad;
    card.x = (f->w - card.w) / 2;
    // Centred between the header and the footer
    int area_top = f->top - f->body / 2, area_bottom = f->footer_y - f->body / 2;
    card.y = area_top + (area_bottom - area_top - card.h) / 2;
    if (card.y < f->m)
        card.y = f->m;
    return card;
}

// Draws the card and its title; returns the y below the title. inner gets the content box.
static int draw_card(const Frame *f, int content_h, const char *title, SDL_Rect *inner)
{
    int title_h = title != NULL ? f->body * 16 / 10 : 0;
    SDL_Rect card = card_rect(f, content_h + title_h);
    fill_round(f, card, f->h / 90, COLOR_CARD);
    int pad = f->body;
    inner->x = card.x + pad;
    inner->y = card.y + pad;
    inner->w = card.w - 2 * pad;
    inner->h = card.h - 2 * pad;
    if (title != NULL) {
        char shown[VALUE_BUF];
        fit_text(f, UI_FONT_BODY, title, inner->w, false, shown, sizeof(shown));
        draw_text(f, UI_FONT_BODY, shown, COLOR_LABEL, inner->x, inner->y + f->body / 2, ALIGN_LEFT);
    }
    return inner->y + title_h;
}

static void draw_list_item(const Frame *f, SDL_Rect item, const char *text, SDL_Color color, bool focused)
{
    if (focused)
        fill_round(f, item, f->body / 5, COLOR_ACCENT);
    int pad = f->body / 2;
    char shown[TEXT_BUF];
    fit_text(f, UI_FONT_BODY, text, item.w - 2 * pad, false, shown, sizeof(shown));
    draw_text(f, UI_FONT_BODY, shown, focused ? COLOR_WHITE : color, item.x + pad, item.y + item.h / 2, ALIGN_LEFT);
}

static void draw_choice_list(const Frame *f)
{
    const UiState *s = f->s;
    int item_h = f->body * 16 / 10;
    size_t visible = s->choice_count < 8 ? s->choice_count : 8;
    SDL_Rect inner;
    int y = draw_card(f, (int) visible * item_h, s->editing != NULL ? s->editing->label : NULL, &inner);
    size_t first = first_visible(s->list_index, visible);
    for (size_t i = first; i < s->choice_count && i < first + visible; i++) {
        SDL_Rect item = {inner.x, y + (int) (i - first) * item_h, inner.w - f->body / 2, item_h};
        draw_list_item(f, item, s->choices[i], COLOR_LABEL, i == s->list_index);
    }
    draw_scrollbar(f, inner.x + inner.w - f->bar, y, (int) visible * item_h, first, visible, s->choice_count);
}

static void draw_text_entry(const Frame *f)
{
    const UiState *s = f->s;
    const TextInput *t = s->text;
    if (t == NULL)
        return;
    size_t rows = text_input_rows(t);
    size_t max_len = 1;
    for (size_t row = 0; row < rows; row++) {
        if (text_input_row_length(t, row) > max_len)
            max_len = text_input_row_length(t, row);
    }
    int field_h = f->body * 16 / 10;
    int key_h = f->body * 15 / 10;
    int gap = f->body / 5;
    int content_h = field_h + f->body * 6 / 10 + (int) rows * (key_h + gap) - gap;
    SDL_Rect inner;
    int y = draw_card(f, content_h, s->editing != NULL ? s->editing->label : NULL, &inner);

    // Field, scrolled so the cursor stays visible
    SDL_Rect field = {inner.x, y, inner.w, field_h};
    fill_round(f, field, f->body / 5, COLOR_FIELD);
    outline_round(f, field, f->body / 5, 2, COLOR_ACCENT);
    const char *value = text_input_value(t);
    size_t cursor = text_input_cursor(t);
    char before[TEXT_BUF];
    snprintf(before, sizeof(before), "%.*s", (int) cursor, value);
    int pad = f->body / 2;
    int text_room = field.w - 2 * pad;
    int cursor_x = ui_text_width(f->c, UI_FONT_BODY, before);
    int offset = cursor_x > text_room - f->bar ? cursor_x - (text_room - f->bar) : 0;
    SDL_Rect clip = {field.x + pad, field.y, text_room, field.h};
    SDL_RenderSetClipRect(f->r, &clip);
    draw_text(f, UI_FONT_BODY, value, COLOR_WHITE, field.x + pad - offset, field.y + field_h / 2, ALIGN_LEFT);
    SDL_RenderSetClipRect(f->r, NULL);
    fill_rect(f, field.x + pad + cursor_x - offset, field.y + (field_h - f->body * 11 / 10) / 2, f->bar,
              f->body * 11 / 10, COLOR_WHITE);
    y += field_h + f->body * 6 / 10;

    // Key grid: full rows span the width; shorter rows keep the key width and centre
    size_t focus_row, focus_col;
    text_input_focus(t, &focus_row, &focus_col);
    int unit = (inner.w + gap) / (int) max_len;
    for (size_t row = 0; row < rows; row++) {
        size_t length = text_input_row_length(t, row);
        if (length == 0)
            continue;
        bool last = row + 1 == rows;
        int key_w = last ? (inner.w + gap) / (int) length - gap : unit - gap;
        int row_w = (int) length * (key_w + gap) - gap;
        int x = inner.x + (inner.w - row_w) / 2;
        for (size_t col = 0; col < length; col++) {
            SDL_Rect key = {x + (int) col * (key_w + gap), y, key_w, key_h};
            bool focused = row == focus_row && col == focus_col;
            fill_round(f, key, f->body / 6, focused ? COLOR_ACCENT : COLOR_KEY);
            const char *label = text_input_key_label(t, row, col);
            // Single characters in the body font; words (and wide ASCII stand-ins) in the small one
            UiFont font = UI_FONT_BODY;
            if (utf8_chars(label) > 1 || ui_text_width(f->c, UI_FONT_BODY, label) > key_w / 2)
                font = UI_FONT_SMALL;
            draw_text(f, font, label, focused ? COLOR_WHITE : COLOR_LABEL, key.x + key_w / 2, key.y + key_h / 2,
                      ALIGN_CENTER);
        }
        y += key_h + gap;
    }
}

static void draw_files(const Frame *f)
{
    const UiState *s = f->s;
    const FileBrowser *fb = s->files;
    if (fb == NULL)
        return;
    int item_h = f->body * 14 / 10;
    int path_h = f->small * 2;
    size_t count = file_browser_count(fb);
    size_t visible = count < 9 ? count : 9;
    SDL_Rect inner;
    int y = draw_card(f, path_h + (int) visible * item_h, s->editing != NULL ? s->editing->label : NULL, &inner);
    char shown[TEXT_BUF];
    fit_text(f, UI_FONT_SMALL, file_browser_dir(fb), inner.w, true, shown, sizeof(shown));
    draw_text(f, UI_FONT_SMALL, shown, COLOR_DIM, inner.x, y + path_h / 2 - f->small / 4, ALIGN_LEFT);
    y += path_h;
    size_t first = first_visible(s->list_index, visible);
    for (size_t i = first; i < count && i < first + visible; i++) {
        FileEntry entry = file_browser_entry(fb, i);
        char name[TEXT_BUF];
        bool folder = entry.kind == ENTRY_DIR || entry.kind == ENTRY_PARENT;
        snprintf(name, sizeof(name), "%s%s", entry.name, folder ? "/" : "");
        SDL_Rect item = {inner.x, y + (int) (i - first) * item_h, inner.w - f->body / 2, item_h};
        SDL_Color color = entry.kind == ENTRY_USE_FOLDER ? COLOR_ACCENT : COLOR_LABEL;
        draw_list_item(f, item, name, color, i == s->list_index);
    }
    draw_scrollbar(f, inner.x + inner.w - f->bar, y, (int) visible * item_h, first, visible, count);
}

static void draw_color(const Frame *f)
{
    const UiState *s = f->s;
    int cell = f->body * 2;
    int gap = f->body / 2;
    int grid_w = 4 * cell + 3 * gap;
    int button_h = f->body * 14 / 10;
    int content_h = 4 * (cell + gap) + gap + button_h;
    SDL_Rect inner;
    int y = draw_card(f, content_h, s->editing != NULL ? s->editing->label : NULL, &inner);
    int x = inner.x + (inner.w - grid_w) / 2;
    int ring = f->bar + 2;
    for (size_t i = 0; i < UI_PALETTE_SIZE; i++) {
        int col = (int) (i % 4), row = (int) (i / 4);
        SDL_Rect swatch = {x + col * (cell + gap), y + row * (cell + gap), cell, cell};
        SDL_Color color;
        if (parse_color(ui_palette[i], &color))
            fill_round(f, swatch, f->body / 6, color);
        outline_round(f, swatch, f->body / 6, 1, COLOR_TRACK);
        if (i == s->list_index) {
            SDL_Rect outer = {swatch.x - ring - 1, swatch.y - ring - 1, cell + 2 * (ring + 1), cell + 2 * (ring + 1)};
            outline_round(f, outer, f->body / 6 + ring, f->bar, COLOR_WHITE);
        }
    }
    SDL_Rect button = {x, y + 4 * (cell + gap) + gap, grid_w, button_h};
    bool custom = s->list_index >= UI_PALETTE_SIZE;
    fill_round(f, button, f->body / 5, COLOR_KEY);
    if (custom)
        outline_round(f, button, f->body / 5, f->bar, COLOR_WHITE);
    char label[64];
    snprintf(label, sizeof(label), "Custom hex%s", ELLIPSIS);
    draw_text(f, UI_FONT_BODY, label, COLOR_LABEL, button.x + button.w / 2, button.y + button_h / 2, ALIGN_CENTER);
}

static void draw_dialog(const Frame *f)
{
    const UiState *s = f->s;
    size_t button_count = 0;
    const char *const *buttons = ui_dialog_buttons(s, &button_count);
    int line_h = f->body * 14 / 10;
    int button_h = f->body * 16 / 10;
    char lines[MAX_WRAP_LINES][VALUE_BUF];
    SDL_Rect probe = card_rect(f, 0);
    const char *message = s->message;
    if (message[0] == '\0' && s->mode == MODE_CONFIRM_EXIT)
        message = EXIT_MESSAGE;
    size_t line_count = wrap_text(f, UI_FONT_BODY, message, probe.w - 2 * f->body, lines, MAX_WRAP_LINES);
    int content_h = (int) line_count * line_h + f->body + button_h;
    SDL_Rect inner;
    int y = draw_card(f, content_h, NULL, &inner);
    for (size_t i = 0; i < line_count; i++)
        draw_text(f, UI_FONT_BODY, lines[i], COLOR_LABEL, inner.x, y + (int) i * line_h + line_h / 2, ALIGN_LEFT);
    y += (int) line_count * line_h + f->body;

    int gap = f->body / 2;
    int widths[8];
    int total = 0;
    if (button_count > 8)
        button_count = 8;
    for (size_t i = 0; i < button_count; i++) {
        widths[i] = ui_text_width(f->c, UI_FONT_BODY, buttons[i]) + 2 * f->body;
        if (widths[i] < 4 * f->body)
            widths[i] = 4 * f->body;
        total += widths[i] + (i > 0 ? gap : 0);
    }
    int x = inner.x + (inner.w - total) / 2;
    for (size_t i = 0; i < button_count; i++) {
        SDL_Rect button = {x, y, widths[i], button_h};
        bool focused = i == s->list_index;
        fill_round(f, button, f->body / 5, focused ? COLOR_ACCENT : COLOR_KEY);
        draw_text(f, UI_FONT_BODY, buttons[i], focused ? COLOR_WHITE : COLOR_LABEL, button.x + widths[i] / 2,
                  y + button_h / 2, ALIGN_CENTER);
        x += widths[i] + gap;
    }
}
