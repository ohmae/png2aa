/*
 * Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include "common.h"

static int find_strike_index(FT_Face face);

static code_cell_t *make_code_cell(FT_Face face, FT_ULong unicode);

static int compare_code(const void *a, const void *b);

static void print_code_book(FILE *file, code_book_t *code_book);

int main(int argc, char **argv) {
    FT_Face face;
    FT_Library library;
    FT_Init_FreeType(&library);
    if (FT_New_Face(library, "msgothic.ttc", 0, &face) != 0) {
        ERR("フォントが読み込めません。msgothic.ttc を同じディレクトリに置いてください");
        return EXIT_FAILURE;
    }
    int strike_index = find_strike_index(face);
    if (strike_index < 0) {
        ERR("対象サイズが見つかりません");
        return EXIT_FAILURE;
    }
    FT_Select_Size(face, strike_index);
    m(face, 0x3042);
    code_book_t code_book;
    init_code_book(&code_book);
    for (int i = 0x80; i <= 0xffff; i++) {
        code_cell_t *code = make_code_cell(face, i);
        if (code != NULL) {
            code_book.book[code_book.size++] = code;
        }
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    print_code_book(stdout, &code_book);
    free_code_book(&code_book);
    return EXIT_SUCCESS;
}

static int find_strike_index(FT_Face face) {
    for (int i = 0; i < face->num_fixed_sizes; i++) {
        if (face->available_sizes[i].height == FONT_WIDTH) {
            return i;
        }
    }
    return -1;
}

static code_cell_t *make_code_cell(FT_Face face, FT_ULong unicode) {
    FT_UInt glyph_index = FT_Get_Char_Index(face, unicode);
    if (glyph_index == 0) {
        return NULL;
    }
    int error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
    if (error) {
        return NULL;
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        return NULL;
    }
    FT_Bitmap *bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_MONO ||
        bitmap->width != FONT_WIDTH) {
        return NULL;
    }
    int code[CODE_SIZE];
    memset(code, 0, sizeof(code));
    int extra_bits = bitmap->width % 8;
    int last_bits = extra_bits == 0 ? 8 : extra_bits;
    for (int y = 0; y < bitmap->rows; y++) {
        if (y / CELL_WIDTH >= CODE_WIDTH) {
            break;
        }
        for (int p = 0; p < bitmap->pitch; p++) {
            const int bits = p < bitmap->pitch - 1 ? 8 : last_bits;
            const int c = bitmap->buffer[bitmap->pitch * y + p];
            for (int i = 0; i < bits; i++) {
                int x = p * 8 + i;
                if (x / CELL_WIDTH >= CODE_WIDTH) {
                    break;
                }
                code[(y / CELL_WIDTH) * CODE_WIDTH + (x / CELL_WIDTH)] += (c & (1 << (7 - i))) == 0;
            }
        }
    }
    code_cell_t *result = xmalloc(sizeof(code_cell_t));
    result->unicode = unicode;
    for (int i = 0; i < CODE_SIZE; i++) {
        double temp = code[i] * 255. / (CELL_WIDTH * CELL_WIDTH);
        result->code[i] = temp < 0 ? 0 : temp > 255 ? 255 : (int) temp;
    }
    return result;
}

static int compare_code(const void *a, const void *b) {
    code_cell_t *ac = *(code_cell_t **) a;
    code_cell_t *bc = *(code_cell_t **) b;
    int total = 0;
    for (int i = 0; i < CODE_SIZE; i++) {
        total += ac->code[i] - bc->code[i];
    }
    if (total != 0) {
        return total;
    }
    for (int i = 0; i < CODE_SIZE; i++) {
        int diff = ac->code[i] - bc->code[i];
        if (diff != 0) {
            return diff;
        }
    }
    return 0;
}

static void print_code_book(FILE *file, code_book_t *code_book) {
    qsort(code_book->book, code_book->size, sizeof(code_cell_t *), compare_code);
    code_cell_t *last_cell = NULL;
    for (int i = 0; i < code_book->size; i++) {
        code_cell_t *code_cell = code_book->book[i];
        if (last_cell == NULL || compare_code(&last_cell, &code_cell) != 0) {
            if (last_cell != NULL) {
                fprintf(file, "\n");
            }
            for (int j = 0; j < CODE_SIZE; j++) {
                fprintf(file, "%02x,", code_cell->code[j]);
            }
        }
        last_cell = code_cell;
        print_unicode_as_utf8(file, code_cell->unicode);
        // fprintf(file, " %04x ", code_cell->unicode);
    }
    fprintf(file, "\n");
}
