#include "serial.h"
#include "console.h"
#include "libc.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static size_t vga_row;
static size_t vga_column;
static uint8_t vga_color;
static uint16_t* vga_buffer;

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg)
{
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color)
{
    return (uint16_t) uc | (uint16_t) color << 8;
}

static void vga_putentryat(char c, uint8_t color, size_t x, size_t y)
{
    const size_t index = y * VGA_WIDTH + x;
    vga_buffer[index] = vga_entry(c, color);
}

static void vga_scroll(void)
{
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            size_t from_index = y * VGA_WIDTH + x;
            size_t to_index = (y - 1) * VGA_WIDTH + x;
            vga_buffer[to_index] = vga_buffer[from_index];
        }
    }

    for (size_t x = 0; x < VGA_WIDTH; x++) {
        size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(' ', vga_color);
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_put_char(char c)
{
    if (c == '\b') {
        if (vga_column > 0) {
            vga_column--;
            vga_putentryat(' ', vga_color, vga_column, vga_row);
        }
        return;
    }

    if (c == '\n') {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT)
            vga_scroll();
        return;
    }

    vga_putentryat(c, vga_color, vga_column, vga_row);
    if (++vga_column == VGA_WIDTH) {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT)
            vga_scroll();
    }
}

void init_vga(void)
{
    vga_row = 0;
    vga_column = 0;
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_buffer = (uint16_t*) 0xB8000;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_color);
        }
    }

    console_set_targets(CONSOLE_TARGET_VGA);
    printf("VGA initialized.\n");
}