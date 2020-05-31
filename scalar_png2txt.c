/*
 * Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <libpng16/png.h>
#include <setjmp.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "common.h"

#define FONT_SIZE 15
#define FONT_PIXELS (FONT_SIZE * FONT_SIZE)

typedef struct scalar_cell_t {
    uint8_t scalar;
    uint32_t unicode;
} scalar_cell_t;

typedef struct scalar_book_t {
    scalar_cell_t **scalar;
    int size;
    int capacity;
} scalar_book_t;

static int find_strike_index(FT_Face face);
static void init_scalar_book(scalar_book_t *scalar_book);
static void free_scalar_book(scalar_book_t *scalar_book);
static void add_scalar_book(scalar_book_t *scalar_book, scalar_cell_t *scalar_cell);
static void make_scalar_book(scalar_book_t *scalar_book);
static scalar_cell_t *make_scalar_cell(FT_Face face, FT_ULong unicode);
static int compare_scalar(const void *a, const void *b);
static uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b);
static void read_png_file(char *filename, image_t *image);
static void read_png_stream(FILE *file, image_t *image);
static void free_image(image_t *img);
static void image_to_text(FILE *file, scalar_book_t *scalar_book, image_t *image);
static void adjust_luminance(image_t *image);

int main(int argc, char **argv) {
    char *image_file = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "c:i:j:")) != -1) {
        switch (opt) {
            case 'i':
                image_file = optarg;
                break;
        }
    }
    if (image_file == NULL) {
        return EXIT_FAILURE;
    }
    scalar_book_t scalar_book;
    make_scalar_book(&scalar_book);
    image_t image;
    read_png_file(image_file, &image);
    adjust_luminance(&image);
    image_to_text(stdout, &scalar_book, &image);
    free_image(&image);
    free_scalar_book(&scalar_book);
}

static int find_strike_index(FT_Face face) {
    for (int i = 0; i < face->num_fixed_sizes; i++) {
        if (face->available_sizes[i].height == FONT_WIDTH) {
            return i;
        }
    }
    return -1;
}

static void init_scalar_book(scalar_book_t *scalar_book) {
    scalar_book->size = 0;
    scalar_book->capacity = 8;
    scalar_book->scalar = xmalloc(sizeof(scalar_cell_t *) * 8);
}

static void free_scalar_book(scalar_book_t *scalar_book) {
    for (int i = 0; i < scalar_book->size; ++i) {
        free(scalar_book->scalar[i]);
    }
    free(scalar_book->scalar);
}

static void add_scalar_book(scalar_book_t *scalar_book, scalar_cell_t *scalar_cell) {
    if (scalar_book->size == scalar_book->capacity) {
        scalar_book->capacity *= 2;
        scalar_book->scalar = xrealloc(scalar_book->scalar, sizeof(scalar_cell_t *) * scalar_book->capacity);
    }
    scalar_book->scalar[scalar_book->size++] = scalar_cell;
}

static void make_scalar_book(scalar_book_t *scalar_book) {
    FT_Face face;
    FT_Library library;
    FT_Init_FreeType(&library);
    if (FT_New_Face(library, "msgothic.ttc", 0, &face) != 0) {
        ERR("フォントが読み込めません。msgothic.ttc を同じディレクトリに置いてください");
        exit(EXIT_FAILURE);
    }
    int strike_index = find_strike_index(face);
    if (strike_index < 0) {
        ERR("対象サイズが見つかりません");
        exit(EXIT_FAILURE);
    }
    FT_Select_Size(face, strike_index);
    init_scalar_book(scalar_book);
    for (int i = 0x80; i <= 0xffff; i++) {
        if (i == 0x25A0 || i == 0x25CF || i == 0x25C6 || i == 0x25BC || i == 0x25B2 || i == 0x2605 || i == 0x3013) {
            continue;
        }
        scalar_cell_t *scalar = make_scalar_cell(face, i);
        if (scalar != NULL) {
            add_scalar_book(scalar_book, scalar);
        }
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    qsort(scalar_book->scalar, scalar_book->size, sizeof(scalar_book_t *), compare_scalar);
}

static scalar_cell_t *make_scalar_cell(FT_Face face, FT_ULong unicode) {
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
    int scalar = 0;
    int extra_bits = bitmap->width % 8;
    int last_bits = extra_bits == 0 ? 8 : extra_bits;
    for (int y = 0; y < bitmap->rows; y++) {
        if (y >= FONT_SIZE) {
            break;
        }
        for (int p = 0; p < bitmap->pitch; p++) {
            const int bits = p < bitmap->pitch - 1 ? 8 : last_bits;
            const int c = bitmap->buffer[bitmap->pitch * y + p];
            for (int i = 0; i < bits; i++) {
                int x = p * 8 + i;
                if (x >= FONT_SIZE) {
                    break;
                }
                scalar += (c & (1 << (7 - i))) == 0;
            }
        }
    }
    scalar_cell_t *result = xmalloc(sizeof(scalar_cell_t));
    result->unicode = unicode;
    double temp = scalar * 255. / FONT_PIXELS;
    result->scalar = temp < 0 ? 0 : temp > 255 ? 255 : (int) temp;
    return result;
}

static int compare_scalar(const void *a, const void *b) {
    scalar_cell_t *ac = *(scalar_cell_t **) a;
    scalar_cell_t *bc = *(scalar_cell_t **) b;
    return ac->scalar - bc->scalar;
}


static uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t) (0.299f * r + 0.587f * g + 0.114f * b + 0.5f);
}

static void read_png_file(char *filename, image_t *image) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    read_png_stream(file, image);
    fclose(file);
}

static void read_png_stream(FILE *file, image_t *image) {
    int i, x, y;
    int width, height;
    int num;
    png_byte sig_bytes[8];
    if (fread(sig_bytes, sizeof(sig_bytes), 1, file) != 1) {
        ERR("シグネチャが読み出せません");
        exit(EXIT_FAILURE);
    }
    if (png_sig_cmp(sig_bytes, 0, sizeof(sig_bytes))) {
        ERR("シグネチャが一致しません");
        exit(EXIT_FAILURE);
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        ERR("png_create_read_struct が失敗しました");
        exit(EXIT_FAILURE);
    }
    png_infop info = png_create_info_struct(png);
    if (info == NULL) {
        ERR("png_create_info_struct が失敗しました");
        exit(EXIT_FAILURE);
    }
    if (setjmp(png_jmpbuf(png))) {
        ERR("PNGの読み出しに失敗しました");
        exit(EXIT_FAILURE);
    }
    png_init_io(png, file);
    png_set_sig_bytes(png, sizeof(sig_bytes));
    png_read_png(png, info, PNG_TRANSFORM_PACKING | PNG_TRANSFORM_STRIP_16, NULL);
    width = png_get_image_width(png, info);
    height = png_get_image_height(png, info);
    image->map = xmalloc(sizeof(uint8_t*) * height);
    for (int y = 0; y < height; y++) {
        image->map[y] = xmalloc(sizeof(uint8_t) * width);
    }
    image->width = width;
    image->height = height;
    png_bytepp rows = png_get_rows(png, info);
    switch (png_get_color_type(png, info)) {
        case PNG_COLOR_TYPE_PALETTE:
        {
            png_colorp palette;
            png_get_PLTE(png, info, &palette, &num);
            uint8_t p[num];
            for (i = 0; i < num; i++) {
                p[i] = rgb_to_gray(palette[i].red, palette[i].green, palette[i].blue);
            }
            png_bytep trans = NULL;
            int num_trans = 0;
            if (png_get_tRNS(png, info, &trans, &num_trans, NULL) == PNG_INFO_tRNS && trans != NULL && num_trans > 0) {
                for (i = 0; i < num_trans; i++) {
                    p[i] = p[i] * trans[i] / 255 + 255 - trans[i];
                }
            }
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    image->map[y][x] = p[*row++];
                }
            }
        }
            break;
        case PNG_COLOR_TYPE_GRAY:
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    image->map[y][x] = *row++;
                }
            }
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    uint8_t g = *row++;
                    uint8_t a = *row++;
                    image->map[y][x] = g * a / 255 + 255 - a;
                }
            }
            break;
        case PNG_COLOR_TYPE_RGB:  // RGB
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    uint8_t r = *row++;
                    uint8_t g = *row++;
                    uint8_t b = *row++;
                    image->map[y][x] = rgb_to_gray(r, g, b);
                }
            }
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    uint8_t r = *row++;
                    uint8_t g = *row++;
                    uint8_t b = *row++;
                    uint8_t a = *row++;
                    uint8_t gray = rgb_to_gray(r, g, b);
                    image->map[y][x] = gray * a / 255 + 255 - a;
                }
            }
            break;
    }
}

static void free_image(image_t *img) {
    for (int y = 0; y < img->height; y++) {
        free(img->map[y]);
    }
    free(img->map);
}

static void adjust_luminance(image_t *image) {
    int min = 71;
    for (int y = 0; y < image->height; ++y) {
        for (int x = 0; x < image->width; ++x) {
            image->map[y][x] = (image->map[y][x] * (255 - min)) / 255 + min;
        }
    }
}

static void image_to_text(FILE *file, scalar_book_t *scalar_book, image_t *image) {
    printf("%d %d\n", image->width, image->height);
    for (int y = 0; y < image->height; y++) {
        for (int x = 0; x < image->width; x++) {
            int c = image->map[y][x];
            int min = 255;
            int index = 0;
            for (int i = 0; i < scalar_book->size; i++) {
                int diff = abs(c - scalar_book->scalar[i]->scalar);
                index = i;
                if (min < diff) {
                    break;
                }
                min = diff;
            }
            print_unicode_as_utf8(file, scalar_book->scalar[index]->unicode);
        }
        printf("\n");
    }
}
