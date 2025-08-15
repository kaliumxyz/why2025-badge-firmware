
// #include "driver/gpio.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>

#include <badgevms/application.h>
#include <badgevms/compositor.h>
#include <badgevms/device.h>
#include <badgevms/event.h>
#include <badgevms/framebuffer.h>
#include <badgevms/keyboard.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCREEN_WIDTH  720
#define SCREEN_HEIGHT 720

typedef struct {
    uint16_t bg;
    uint16_t fg;
    uint16_t panel;
    uint16_t border_light;
    uint16_t border_dark;
    uint16_t text;
    uint16_t selected_bg;
    uint16_t button;
    uint16_t title_bg;
    uint16_t text_selected;
    uint16_t text_inactive;
    uint16_t popup;
} Theme;

typedef struct {
    bool  enabled;
    int   count;
    int   max;
    char *buffer;

} Capture;

typedef struct {
    window_handle_t window;
    framebuffer_t  *framebuffer;
    uint16_t       *pixels;
    application_t **applications;
    int             scroll_offset;
    int             selected_item;
    int             total_items;
    int             items_per_page;
    int             themeCursor;
    bool            show_settings;
    Theme           theme[4];
    int             themeActive;
    bool            show_theme;
    bool            quit;
    bool            buzz;
    int             bl;
    Capture         capture;
} Context;

static void draw_rect(Context *ctx, int x, int y, int w, int h, uint16_t color) {
    int x2 = x + w;
    int y2 = y + h;

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x2 > SCREEN_WIDTH)
        x2 = SCREEN_WIDTH;
    if (y2 > SCREEN_HEIGHT)
        y2 = SCREEN_HEIGHT;

    for (int py = y; py < y2; py++) {
        uint16_t *row   = &ctx->pixels[py * SCREEN_WIDTH + x];
        int       width = x2 - x;
        for (int i = 0; i < width; i++) {
            row[i] = color;
        }
    }
}

static void draw_char(Context *ctx, int x, int y, char c, uint16_t color) {
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR)
        return;

    int             char_index = c - FONT_FIRST_CHAR;
    uint16_t const *char_data  = pixel_font[char_index];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint16_t row_data = char_data[row];
        int      py       = y + row;

        if (py < 0 || py >= SCREEN_HEIGHT)
            continue;

        for (int col = 0; col < FONT_WIDTH; col++) {
            if (row_data & (0x800 >> col)) {
                int px = x + col;
                if (px >= 0 && px < SCREEN_WIDTH) {
                    ctx->pixels[py * SCREEN_WIDTH + px] = color;
                }
            }
        }
    }
}

static void draw_text(Context *ctx, int x, int y, char const *text, uint16_t color) {
    int current_x = x;
    while (*text) {
        draw_char(ctx, current_x, y, *text, color);
        current_x += FONT_WIDTH;
        text++;
    }
}

static void draw_text_bold(Context *ctx, int x, int y, char const *text, uint16_t color) {
    draw_text(ctx, x, y, text, color);
    draw_text(ctx, x + 1, y, text, color);
}

static void draw_text_centered(Context *ctx, int x, int y, int width, char const *text, uint16_t color) {
    int text_w = strlen(text) * FONT_WIDTH;
    int text_x = x + (width - text_w) / 2;
    draw_text(ctx, text_x, y, text, color);
}

static void draw_3d_border(Context *ctx, int x, int y, int w, int h, int inset, int themeActive) {
    uint16_t light_color = inset ? ctx->theme[themeActive].border_dark : ctx->theme[themeActive].border_light;
    uint16_t dark_color  = inset ? ctx->theme[themeActive].border_light : ctx->theme[themeActive].border_dark;

    draw_rect(ctx, x, y, w, 2, light_color);
    draw_rect(ctx, x, y, 2, h, light_color);

    draw_rect(ctx, x, y + h - 2, w, 2, dark_color);
    draw_rect(ctx, x + w - 2, y, 2, h, dark_color);
}

static void draw_settings_dialog(Context *ctx) {
    int dialog_w = 450;
    int dialog_h = 350;
    int dialog_x = (SCREEN_WIDTH - dialog_w) / 2;
    int dialog_y = (SCREEN_HEIGHT - dialog_h) / 2;

    draw_rect(ctx, dialog_x + 5, dialog_y + 5, dialog_w, dialog_h, ctx->theme[ctx->themeActive].popup);

    draw_rect(ctx, dialog_x, dialog_y, dialog_w, dialog_h, ctx->theme[ctx->themeActive].panel);
    draw_3d_border(ctx, dialog_x, dialog_y, dialog_w, dialog_h, 0, ctx->themeActive);

    int title_h = 30;
    draw_rect(ctx, dialog_x + 2, dialog_y + 2, dialog_w - 4, title_h, ctx->theme[ctx->themeActive].title_bg);
    draw_text_bold(ctx, dialog_x + 10, dialog_y + 8, "Themes", ctx->theme[ctx->themeActive].text_selected);

    int content_y = dialog_y + title_h + 30;

    draw_text_centered(ctx, dialog_x, content_y, dialog_w, "Pick a theme", ctx->theme[ctx->themeActive].text);

    int swats_y = content_y + 80;

    int swat_offset   = 50;
    int swat_distance = 90;

    // color swats, one swat per theme. one for custom?
    // or custom maybe using a button below for more explicit.
    // allow saving to swats?
    // indicator
    draw_rect(ctx, dialog_x + swat_offset + swat_distance * ctx->themeActive, swats_y + 75, 75, 5, 0xffc0);
    draw_3d_border(
        ctx,
        dialog_x + swat_offset + swat_distance * ctx->themeActive,
        swats_y,
        75,
        80,
        1,
        ctx->themeActive
    );

    draw_rect(ctx, dialog_x + swat_offset, swats_y, 75, 75, ctx->theme[0].bg);
    draw_3d_border(ctx, dialog_x + swat_offset, swats_y, 75, 75, 1, 0);

    draw_rect(ctx, dialog_x + swat_offset + swat_distance, swats_y, 75, 75, ctx->theme[1].bg);
    draw_3d_border(ctx, dialog_x + swat_offset + swat_distance, swats_y, 75, 75, 1, 1);

    draw_rect(ctx, dialog_x + swat_offset + swat_distance * 2, swats_y, 75, 75, ctx->theme[2].bg);
    draw_3d_border(ctx, dialog_x + swat_offset + swat_distance * 2, swats_y, 75, 75, 1, 2);

    draw_rect(ctx, dialog_x + swat_offset + swat_distance * 3, swats_y, 75, 75, ctx->theme[3].bg);
    draw_3d_border(ctx, dialog_x + swat_offset + swat_distance * 3, swats_y, 75, 75, 1, 3);

    // swats for curent theme
    draw_rect(ctx, dialog_x + swat_offset - 5, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].bg);
    draw_rect(ctx, dialog_x + swat_offset - 5 + 30, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].fg);
    draw_rect(ctx, dialog_x + swat_offset - 5 + 30 * 2, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].panel);
    draw_rect(
        ctx,
        dialog_x + swat_offset - 5 + 30 * 3,
        swats_y + 100,
        30,
        75,
        ctx->theme[ctx->themeActive].border_light
    );
    draw_rect(
        ctx,
        dialog_x + swat_offset - 5 + 30 * 4,
        swats_y + 100,
        30,
        75,
        ctx->theme[ctx->themeActive].border_dark
    );
    draw_rect(ctx, dialog_x + swat_offset - 5 + 30 * 5, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].button);
    draw_rect(
        ctx,
        dialog_x + swat_offset - 5 + 30 * 6,
        swats_y + 100,
        30,
        75,
        ctx->theme[ctx->themeActive].selected_bg
    );
    draw_rect(ctx, dialog_x + swat_offset - 5 + 30 * 7, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].title_bg);
    draw_rect(ctx, dialog_x + swat_offset - 5 + 30 * 8, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].text);
    draw_rect(
        ctx,
        dialog_x + swat_offset - 5 + 30 * 9,
        swats_y + 100,
        30,
        75,
        ctx->theme[ctx->themeActive].text_selected
    );
    draw_rect(
        ctx,
        dialog_x + swat_offset - 5 + 30 * 10,
        swats_y + 100,
        30,
        75,
        ctx->theme[ctx->themeActive].text_inactive
    );
    draw_rect(ctx, dialog_x + swat_offset - 5 + 30 * 11, swats_y + 100, 30, 75, ctx->theme[ctx->themeActive].popup);

    // cursor
    draw_rect(ctx, dialog_x + swat_offset + 30 * ctx->themeCursor, swats_y + 100 + 60, 15, 8, 0xffc0);

    draw_3d_border(ctx, dialog_x + swat_offset - 5, swats_y + 100, 30 * 12, 75, 1, ctx->themeActive);



    // draw_rect(ctx, dialog_x + swat_offset + swat_distance * 8, swats_y, 20, 20,
    // ctx->theme[ctx->themeActive].border_dark); draw_3d_border(ctx, dialog_x + swat_offset + swat_distance * 8,
    // swats_y, 20, 20, 1);

    // draw_rect(ctx, dialog_x + swat_offset + swat_distance * 9, swats_y, 20, 20,
    // ctx->theme[ctx->themeActive].border_light); draw_3d_border(ctx, dialog_x + swat_offset + swat_distance * 9,
    // swats_y, 20, 20, 1);

    // draw_rect(ctx, dialog_x + swat_offset + swat_distance * 10, swats_y, 20, 20,
    // ctx->theme[ctx->themeActive].title_bg); draw_3d_border(ctx, dialog_x + swat_offset + swat_distance * 10, swats_y,
    // 20, 20, 1);

    draw_text_centered(
        ctx,
        dialog_x,
        swats_y - 40,
        dialog_w,
        "Press ENTER or ESC to save or close",
        ctx->theme[ctx->themeActive].text_inactive
    );
}

static void draw_launcher_window(Context *ctx) {
    int window_x = 30;
    int window_y = 30;
    int window_w = SCREEN_WIDTH - 60;
    int window_h = SCREEN_HEIGHT - 60;

    draw_rect(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ctx->theme[ctx->themeActive].bg);

    draw_rect(ctx, window_x, window_y, window_w, window_h, ctx->theme[ctx->themeActive].panel);
    draw_3d_border(ctx, window_x, window_y, window_w, window_h, 0, ctx->themeActive);

    int title_h = 45;
    draw_rect(ctx, window_x + 3, window_y + 3, window_w - 6, title_h, ctx->theme[ctx->themeActive].title_bg);
    draw_text_bold(
        ctx,
        window_x + 15,
        window_y + 11,
        "WHY Application Launcher",
        ctx->theme[ctx->themeActive].text_selected
    );

    char count_text[64];
    if (ctx->total_items == 1) {
        snprintf(count_text, sizeof(count_text), "1 Application Available");
    } else {
        snprintf(count_text, sizeof(count_text), "%d Applications Available", ctx->total_items);
    }
    draw_text(ctx, window_x + 15, window_y + title_h + 20, count_text, ctx->theme[ctx->themeActive].text);

    int list_y      = window_y + title_h + 55;
    int list_h      = window_h - title_h - 110;
    int item_height = 80;

    draw_rect(ctx, window_x + 15, list_y, window_w - 30, list_h, ctx->theme[ctx->themeActive].fg);
    draw_3d_border(ctx, window_x + 15, list_y, window_w - 30, list_h, 1, ctx->themeActive);

    ctx->items_per_page = (list_h - 6) / item_height;
    int visible_start   = ctx->scroll_offset;
    int visible_end     = visible_start + ctx->items_per_page;
    if (visible_end > ctx->total_items)
        visible_end = ctx->total_items;

    for (int i = visible_start; i < visible_end; i++) {
        int item_y = list_y + 3 + (i - visible_start) * item_height;
        int item_x = window_x + 18;
        int item_w = window_w - 36;

        if (i == ctx->selected_item) {
            draw_rect(ctx, item_x, item_y, item_w, item_height - 2, ctx->theme[ctx->themeActive].selected_bg);
        }

        uint16_t text_color =
            (i == ctx->selected_item) ? ctx->theme[ctx->themeActive].text_selected : ctx->theme[ctx->themeActive].text;

        int icon_size = 48;
        int icon_x    = item_x + 10;
        int icon_y    = item_y + (item_height - icon_size) / 2;

        uint16_t icon_color = (i == ctx->selected_item) ? ctx->theme[ctx->themeActive].text_selected
                                                        : ctx->theme[ctx->themeActive].button;
        draw_rect(ctx, icon_x, icon_y, icon_size, icon_size, icon_color);
        draw_3d_border(ctx, icon_x, icon_y, icon_size, icon_size, 1, ctx->themeActive);

        int text_x = icon_x + icon_size + 15;
        draw_text_bold(ctx, text_x, item_y + 10, ctx->applications[i]->name, text_color);

        if (ctx->applications[i]->version) {
            char version_text[64];
            snprintf(version_text, sizeof(version_text), "v%s", ctx->applications[i]->version);
            draw_text(ctx, text_x, item_y + 35, version_text, text_color);
        }

#if 0
        if (ctx->applications[i].description) {
            char desc[60] = {0};
            int max_desc_chars = ((item_w - text_x + item_x - 16) / FONT_WIDTH);
            if (max_desc_chars > 59) max_desc_chars = 59;
            
            strncpy(desc, ctx->applications[i].description, max_desc_chars);
            desc[max_desc_chars] = '\0';
            
            if (strlen(ctx->applications[i].description) > max_desc_chars) {
                desc[max_desc_chars - 3] = '.';
                desc[max_desc_chars - 2] = '.';
                desc[max_desc_chars - 1] = '.';
            }
            draw_text(ctx, text_x, item_y + 58, desc, text_color);
        }
#endif

        if (i < visible_end - 1) {
            draw_rect(ctx, item_x, item_y + item_height - 2, item_w, 1, ctx->theme[ctx->themeActive].border_dark);
        }
    }

    if (ctx->total_items > ctx->items_per_page) {
        int scrollbar_x = window_x + window_w - 35;
        int scrollbar_y = list_y + 3;
        int scrollbar_h = list_h - 6;

        draw_rect(ctx, scrollbar_x, scrollbar_y, 20, scrollbar_h, ctx->theme[ctx->themeActive].button);
        draw_3d_border(ctx, scrollbar_x, scrollbar_y, 20, scrollbar_h, 1, ctx->themeActive);

        int thumb_h = (scrollbar_h * ctx->items_per_page) / ctx->total_items;
        if (thumb_h < 30)
            thumb_h = 30;

        int thumb_y = scrollbar_y;
        if (ctx->total_items > ctx->items_per_page) {
            thumb_y += ((scrollbar_h - thumb_h) * ctx->scroll_offset) / (ctx->total_items - ctx->items_per_page);
        }

        draw_rect(ctx, scrollbar_x + 3, thumb_y, 14, thumb_h, ctx->theme[ctx->themeActive].panel);
        draw_3d_border(ctx, scrollbar_x + 3, thumb_y, 14, thumb_h, 0, ctx->themeActive);
    }

    draw_rect(ctx, window_x + 3, window_y + window_h - 42, window_w - 6, 39, ctx->theme[ctx->themeActive].button);
    draw_3d_border(ctx, window_x + 3, window_y + window_h - 42, window_w - 6, 39, 1, ctx->themeActive);

    draw_text(
        ctx,
        window_x + 15,
        window_y + window_h - 35,
        "UP/DOWN: Navigate  ENTER: Launch  S: Settings  ESC: Exit",
        ctx->theme[ctx->themeActive].text
    );
}

#define SAVE_FILE "APPS:[badgevms_launcher]theme.txt"

static bool load(Context *ctx) {
    FILE *file = fopen(SAVE_FILE, "r");
    if (!file) {
        printf("No saved state found at %s\n", SAVE_FILE);
        return false;
    }


    int offset = 12;
    int size   = offset * 4 + 2;

    uint16_t buffer[size];

    if (fread(buffer, sizeof(buffer), size, file)) {
        int index = (int)buffer;
        if (index >= 0 && index < 4) {
            ctx->themeActive = index;
        }
        printf("%X", index);
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 12; j++) {
            printf("%X", buffer[i * offset + j]);
        }
        ctx->theme[i].bg            = buffer[i * offset + 1];
        ctx->theme[i].fg            = buffer[i * offset + 2];
        ctx->theme[i].panel         = buffer[i * offset + 3];
        ctx->theme[i].border_light  = buffer[i * offset + 4];
        ctx->theme[i].border_dark   = buffer[i * offset + 5];
        ctx->theme[i].text          = buffer[i * offset + 6];
        ctx->theme[i].selected_bg   = buffer[i * offset + 7];
        ctx->theme[i].text_selected = buffer[i * offset + 8];
        ctx->theme[i].button        = buffer[i * offset + 9];
        ctx->theme[i].title_bg      = buffer[i * offset + 10];
        ctx->theme[i].text_inactive = buffer[i * offset + 11];
        ctx->theme[i].popup         = buffer[i * offset + 12];
    }
    fclose(file);
    printf("State loaded from %s\n", SAVE_FILE);
    return true;
}
static bool save(Context *ctx) {
    // Work around the truncation bug by removing the file first
    remove(SAVE_FILE);

    FILE *file = fopen(SAVE_FILE, "w");

    if (!file) {
        printf("Failed to save state to %s\n", SAVE_FILE);
        return false;
    }

    int offset = 12;
    int size   = offset * 4 + 2;

    uint16_t buff[size];

    buff[0] = (uint16_t)ctx->themeActive;


    for (int i = 0; i < 3; i++) {
        buff[i * offset + 1]  = ctx->theme[i].bg;
        buff[i * offset + 2]  = ctx->theme[i].fg;
        buff[i * offset + 3]  = ctx->theme[i].panel;
        buff[i * offset + 4]  = ctx->theme[i].border_light;
        buff[i * offset + 5]  = ctx->theme[i].border_dark;
        buff[i * offset + 6]  = ctx->theme[i].text;
        buff[i * offset + 7]  = ctx->theme[i].selected_bg;
        buff[i * offset + 8]  = ctx->theme[i].text_selected;
        buff[i * offset + 9]  = ctx->theme[i].button;
        buff[i * offset + 10] = ctx->theme[i].title_bg;
        buff[i * offset + 11] = ctx->theme[i].text_inactive;
        buff[i * offset + 12] = ctx->theme[i].popup;
    }
    fwrite(buff, sizeof buff[0], size, file);

    fclose(file);
    printf("State saved to %s\n", SAVE_FILE);

    return true;
}

/**
 * hex2int
 * take a hex string and convert it to a 32bit number (max 8 hex digits)
 */
uint16_t hex2int(char *hex) {
    uint16_t val = 0;
    while (*hex) {
        // get current character then increment
        uint8_t byte = *hex++;
        // transform hex character to the 4bit equivalent number, using the ascii table indexes
        if (byte >= '0' && byte <= '9')
            byte = byte - '0';
        else if (byte >= 'a' && byte <= 'f')
            byte = byte - 'a' + 10;
        else if (byte >= 'A' && byte <= 'F')
            byte = byte - 'A' + 10;
        // shift 4 to make space for new digit, and add the 4 bits of the new digit
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}

static void handle_keyboard_capture(Context *ctx, keyboard_scancode_t key_code) {
    if (ctx->capture.count > 3) {
        switch (ctx->themeCursor) {
            case 0: ctx->theme[ctx->themeActive].bg = hex2int(ctx->capture.buffer); break;
            case 1: ctx->theme[ctx->themeActive].fg = hex2int(ctx->capture.buffer); break;
            case 2: ctx->theme[ctx->themeActive].panel = hex2int(ctx->capture.buffer); break;
            case 3: ctx->theme[ctx->themeActive].border_light = hex2int(ctx->capture.buffer); break;
            case 4: ctx->theme[ctx->themeActive].border_dark = hex2int(ctx->capture.buffer); break;
            case 5: ctx->theme[ctx->themeActive].text = hex2int(ctx->capture.buffer); break;
            case 6: ctx->theme[ctx->themeActive].selected_bg = hex2int(ctx->capture.buffer); break;
            case 7: ctx->theme[ctx->themeActive].text_selected = hex2int(ctx->capture.buffer); break;
            case 8: ctx->theme[ctx->themeActive].button = hex2int(ctx->capture.buffer); break;
            case 9: ctx->theme[ctx->themeActive].title_bg = hex2int(ctx->capture.buffer); break;
            case 10: ctx->theme[ctx->themeActive].text_inactive = hex2int(ctx->capture.buffer); break;
            case 11: ctx->theme[ctx->themeActive].popup = hex2int(ctx->capture.buffer); break;
        }
        ctx->capture.buffer  = "";
        ctx->capture.count   = 0;
        ctx->capture.enabled = false;
        return;
    }
    switch (key_code) {
        case KEY_SCANCODE_0: ctx->capture.buffer += '0'; break;
        case KEY_SCANCODE_1: ctx->capture.buffer += '1'; break;
        case KEY_SCANCODE_2: ctx->capture.buffer += '2'; break;
        case KEY_SCANCODE_3: ctx->capture.buffer += '3'; break;
        case KEY_SCANCODE_4: ctx->capture.buffer += '4'; break;
        case KEY_SCANCODE_5: ctx->capture.buffer += '5'; break;
        case KEY_SCANCODE_6: ctx->capture.buffer += '6'; break;
        case KEY_SCANCODE_7: ctx->capture.buffer += '7'; break;
        case KEY_SCANCODE_8: ctx->capture.buffer += '8'; break;
        case KEY_SCANCODE_9: ctx->capture.buffer += '9'; break;
        case KEY_SCANCODE_A: ctx->capture.buffer += 'a'; break;
        case KEY_SCANCODE_B: ctx->capture.buffer += 'b'; break;
        case KEY_SCANCODE_C: ctx->capture.buffer += 'c'; break;
        case KEY_SCANCODE_D: ctx->capture.buffer += 'd'; break;
        case KEY_SCANCODE_E: ctx->capture.buffer += 'e'; break;
        case KEY_SCANCODE_F: ctx->capture.buffer += 'f'; break;
        case KEY_SCANCODE_ESCAPE: ctx->capture.enabled = false; break;
    }
    ctx->capture.count++;
}

static void handle_keyboard_settings(Context *ctx, keyboard_scancode_t key_code) {
    switch (key_code) {
        case KEY_SCANCODE_SQUARE: ctx->themeActive = 0; break;

        case KEY_SCANCODE_TRIANGLE: ctx->themeActive = 1; break;

        case KEY_SCANCODE_CROSS: ctx->themeActive = 2; break;

        case KEY_SCANCODE_CIRCLE: ctx->themeActive = 3; break;

        case KEY_SCANCODE_CLOUD: ctx->buzz = true; break;

        case KEY_SCANCODE_DIAMOND: ctx->buzz = false; break;

        case KEY_SCANCODE_L: load(ctx); break;

        case KEY_SCANCODE_K: save(ctx); break;

        // case KEY_SCANCODE_LEFT:
        //     if (ctx->themeActive > 0) {
        //         ctx->themeActive -= 1;
        //     } else {
        //         ctx->themeActive = 3;
        //     }
        //     break;

        // case KEY_SCANCODE_RIGHT:
        //     if (ctx->themeActive < 3) {
        //         ctx->themeActive += 1;
        //     } else {
        //         ctx->themeActive = 0;
        //     }
        //     break;
        case KEY_SCANCODE_LEFT:
            if (ctx->themeCursor > 0) {
                ctx->themeCursor -= 1;
            } else {
                ctx->themeCursor = 11;
            }
            break;

        case KEY_SCANCODE_RIGHT:
            if (ctx->themeCursor < 11) {
                ctx->themeCursor += 1;
            } else {
                ctx->themeCursor = 0;
            }
            break;

        case KEY_SCANCODE_SPACE:
            ctx->capture.enabled = true;
            ctx->capture.buffer  = "";
            break;

        case KEY_SCANCODE_UP:
            switch (ctx->themeCursor) {
                case 0: ctx->theme[ctx->themeActive].bg++; break;
                case 1: ctx->theme[ctx->themeActive].fg++; break;
                case 2: ctx->theme[ctx->themeActive].panel++; break;
                case 3: ctx->theme[ctx->themeActive].border_light++; break;
                case 4: ctx->theme[ctx->themeActive].border_dark++; break;
                case 5: ctx->theme[ctx->themeActive].text++; break;
                case 6: ctx->theme[ctx->themeActive].selected_bg++; break;
                case 7: ctx->theme[ctx->themeActive].text_selected++; break;
                case 8: ctx->theme[ctx->themeActive].button++; break;
                case 9: ctx->theme[ctx->themeActive].title_bg++; break;
                case 10: ctx->theme[ctx->themeActive].text_inactive++; break;
                case 11: ctx->theme[ctx->themeActive].popup++; break;
            }
            break;

        case KEY_SCANCODE_DOWN:
            switch (ctx->themeCursor) {
                case 0: ctx->theme[ctx->themeActive].bg--; break;
                case 1: ctx->theme[ctx->themeActive].fg--; break;
                case 2: ctx->theme[ctx->themeActive].panel--; break;
                case 3: ctx->theme[ctx->themeActive].border_light--; break;
                case 4: ctx->theme[ctx->themeActive].border_dark--; break;
                case 5: ctx->theme[ctx->themeActive].text--; break;
                case 6: ctx->theme[ctx->themeActive].selected_bg--; break;
                case 7: ctx->theme[ctx->themeActive].text_selected--; break;
                case 8: ctx->theme[ctx->themeActive].button--; break;
                case 9: ctx->theme[ctx->themeActive].title_bg--; break;
                case 10: ctx->theme[ctx->themeActive].text_inactive--; break;
                case 11: ctx->theme[ctx->themeActive].popup--; break;
            }
            break;
        case KEY_SCANCODE_ESCAPE:
            ctx->show_settings = false;
            load(ctx);
            break;

        case KEY_SCANCODE_RETURN:
            ctx->show_settings = false;
            save(ctx);
            break;
    }
}

static void handle_keyboard(Context *ctx, keyboard_scancode_t key_code) {
    if (ctx->capture.enabled && ctx->capture.count < ctx->capture.max) {
        return handle_keyboard_capture(ctx, key_code);
    } else {
        ctx->capture.enabled = false;
    }

    if (ctx->show_settings) {
        return handle_keyboard_settings(ctx, key_code);
    }

    switch (key_code) {
        case KEY_SCANCODE_UP:
            if (ctx->selected_item > 0) {
                ctx->selected_item--;
                if (ctx->selected_item < ctx->scroll_offset) {
                    ctx->scroll_offset = ctx->selected_item;
                }
            }
            break;

        case KEY_SCANCODE_DOWN:
            if (ctx->selected_item < ctx->total_items - 1) {
                ctx->selected_item++;
                if (ctx->selected_item >= ctx->scroll_offset + ctx->items_per_page) {
                    ctx->scroll_offset = ctx->selected_item - ctx->items_per_page + 1;
                }
            }
            break;

        case KEY_SCANCODE_RETURN:
        case KEY_SCANCODE_SPACE:
            printf("Launching: %s\n", ctx->applications[ctx->selected_item]->name);
            application_launch(ctx->applications[ctx->selected_item]->unique_identifier);
            break;

        case KEY_SCANCODE_S:
            ctx->show_settings = true;
            break;
            //      case KEY_SCANCODE_T: switchTheme(ctx); break;

        case KEY_SCANCODE_V:
            ctx->buzz = !ctx->buzz;
            /* gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT); */
            /* gpio_set_level(GPIO_NUM_8, ctx->buzz); */
            break;

        case KEY_SCANCODE_D:
            // ctx->bl = !ctx->bl;
            break;

        case KEY_SCANCODE_ESCAPE: ctx->quit = true; break;
    }
}

static bool run_launcher(application_t **applications, size_t num) {
    printf("Starting application launcher with %zu applications\n", num);

    Context ctx = {0};

    ctx.theme[0].bg            = 0x9d13;
    ctx.theme[0].fg            = 0xFFFF;
    ctx.theme[0].panel         = 0xad96;
    ctx.theme[0].border_light  = 0xffff;
    ctx.theme[0].border_dark   = 0x630C;
    ctx.theme[0].text          = 0x0000;
    ctx.theme[0].selected_bg   = 0x03DA;
    ctx.theme[0].text_selected = 0xFFFF;
    ctx.theme[0].button        = 0xd678;
    ctx.theme[0].title_bg      = 0x8410;
    ctx.theme[0].text_inactive = 0x8410;
    ctx.theme[0].popup         = 0x0500;

    ctx.theme[1].bg            = 0x0000;
    ctx.theme[1].fg            = 0xbe19;
    ctx.theme[1].panel         = 0x4a8b;
    ctx.theme[1].border_light  = 0x41e8;
    ctx.theme[1].border_dark   = 0x3186;
    ctx.theme[1].text          = 0xbf2c;
    ctx.theme[1].selected_bg   = 0x2121;
    ctx.theme[1].text_selected = 0xffff;
    ctx.theme[1].button        = 0x2121;
    ctx.theme[1].title_bg      = 0x1111;
    ctx.theme[1].text_inactive = 0xbbbb;
    ctx.theme[1].popup         = 0x0000;

    ctx.theme[2].bg            = 0x2966;
    ctx.theme[2].fg            = 0xbe19;
    ctx.theme[2].panel         = 0x4a8b;
    ctx.theme[2].border_light  = 0x41e8;
    ctx.theme[2].border_dark   = 0x3186;
    ctx.theme[2].selected_bg   = 0x2125;
    ctx.theme[2].text          = 0xad1b;
    ctx.theme[2].text_selected = 0xffff;
    ctx.theme[2].button        = 0xd678;
    ctx.theme[2].title_bg      = 0xbe19;
    ctx.theme[2].text_inactive = 0xbe19;
    ctx.theme[2].popup         = 0x528a;

    ctx.theme[3].bg            = 0x2966;
    ctx.theme[3].fg            = 0xbe19;
    ctx.theme[3].panel         = 0x4a8b;
    ctx.theme[3].border_light  = 0x41e8;
    ctx.theme[3].border_dark   = 0x3186;
    ctx.theme[3].text          = 0xbf2c;
    ctx.theme[3].selected_bg   = 0x2125;
    ctx.theme[3].text_selected = 0xffff;
    ctx.theme[3].button        = 0xd678;
    ctx.theme[3].title_bg      = 0xbe19;
    ctx.theme[3].text_inactive = 0xbe19;
    ctx.theme[3].popup         = 0x528a;

    ctx.theme[4].text = 0xFFFF;

    ctx.themeActive = 0;

    if (!load(&ctx)) {
        printf("failed to load prior state");
    }


    char keystroke_buffer[64];

    Capture cap = {enabled : false, buffer : keystroke_buffer};

    ctx.capture = cap;

    ctx.applications  = applications;
    ctx.total_items   = num;
    ctx.selected_item = 0;
    ctx.scroll_offset = 0;
    ctx.show_settings = false;
    ctx.quit          = false;

    ctx.window = window_create(
        "Application Launcher",
        (window_size_t){SCREEN_WIDTH, SCREEN_HEIGHT},
        WINDOW_FLAG_DOUBLE_BUFFERED | WINDOW_FLAG_FULLSCREEN | WINDOW_FLAG_LOW_PRIORITY
    );

    if (ctx.window == NULL) {
        printf("Window could not be created\n");
        return false;
    }


    ctx.framebuffer = window_framebuffer_create(
        ctx.window,
        (window_size_t){SCREEN_WIDTH, SCREEN_HEIGHT},
        BADGEVMS_PIXELFORMAT_RGB565
    );

    if (ctx.framebuffer == NULL) {
        printf("Framebuffer texture could not be created\n");
        return false;
    }

    ctx.pixels = ctx.framebuffer->pixels;
    event_t e;

    while (!ctx.quit) {
        memset(ctx.pixels, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));

        draw_launcher_window(&ctx);

        if (ctx.show_settings) {
            draw_settings_dialog(&ctx);
        }

        window_present(ctx.window, true, NULL, 0);

        e = window_event_poll(ctx.window, true, 0);
        if (e.type == EVENT_QUIT) {
            ctx.quit = true;
        } else if (e.type == EVENT_KEY_DOWN) {
            handle_keyboard(&ctx, e.keyboard.scancode);
        }
    }

    return true;
}

int main(int argc, char *argv[]) {
    size_t num_apps = 0;
    application_t *this;
    application_list_handle app_list = application_list(&this);
    application_t         **apps     = NULL;


    printf("Currently installed applications: \n");
    while (this) {
        printf("Name: %s\n", this->name);
        printf("  UID: %s\n", this->unique_identifier);
        printf("  Version: %s\n", this->version);
        printf("  Binary : %s\n", this->binary_path);
        if (this->binary_path && strlen(this->binary_path) && this->unique_identifier &&
            (strcmp(this->unique_identifier, "badgevms_launcher") != 0) &&
            (strcmp(this->unique_identifier, "doom_launcher") != 0) &&
            (strcmp(this->unique_identifier, "why2025_firmware_ota_c6") != 0)) {
            ++num_apps;
            apps               = realloc(apps, sizeof(application_t *) * num_apps);
            apps[num_apps - 1] = this;
        }
        this = application_list_get_next(app_list);
    }

    run_launcher(apps, num_apps);

    return 0;
}