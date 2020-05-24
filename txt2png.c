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

typedef struct aa_t {
    int width;
    int height;
    uint32_t **map;
} aa_t;

typedef struct image_t {
    int width;
    int height;
    uint8_t **map;
} image_t;

static int find_strike_index(FT_Face face);
static void read_aa_file(const char* filename, aa_t *aa);
static void read_aa_stream(FILE *file, aa_t *aa);
static void aa_to_image(aa_t *aa, image_t *img);
static void write_glyph_to_image(FT_Face face, FT_ULong unicode, image_t *img, int x, int y);
static void write_png_file(const char *filename, image_t *img);
static void write_png_stream(FILE *file, image_t *img);

int main(int argc, char **argv) {
    char *input_file = NULL;
    char *output_file = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "o:i:")) != -1) {
        switch (opt) {
            case 'i':
                input_file = optarg;
                break;
            case 'o':
                output_file = optarg;
                break;
        }
    }
    if (input_file == NULL || output_file == NULL) {
        ERR("使用方法; txt2png -i <input(png2txt result)> -o <output png file>");
        return EXIT_FAILURE;
    }
    aa_t aa;
    read_aa_file(input_file, &aa);

    image_t img;
    aa_to_image(&aa, &img);
    for (int y = 0; y < aa.height; y++) {
        free(aa.map[y]);
    }
    free(aa.map);

    write_png_file(output_file, &img);

    for (int y = 0; y < img.height; y++) {
        free(img.map[y]);
    }
    free(img.map);
    return EXIT_SUCCESS;
}

static void read_aa_file(const char* filename, aa_t *aa){
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    read_aa_stream(file, aa);
    fclose(file);
}

static void read_aa_stream(FILE *file, aa_t *aa) {
    if (fscanf(file, "%d %d\n", &aa->width, &aa->height) != 2) {
        exit(EXIT_FAILURE);
    }
    aa->map = xmalloc(sizeof(uint32_t*) * aa->height);
    for (int y = 0; y < aa->height; y++) {
        aa->map[y] = xmalloc(sizeof(uint32_t*) * aa->width);
    }
    int line_max = (aa->width + 1) * 3;
    char line[line_max];
    for (int y = 0; y < aa->height; y++) {
        if (fgets(line, line_max, file) == NULL) {
            ERR("AAファイルの読み出しに失敗しました");
            exit(EXIT_FAILURE);
        }
        int pos = 0;
        for (int x = 0; x < aa->width; x++) {
            int size;
            int unicode = read_utf8_as_unicode(&line[pos], &size);
            if (size == 0) {
                ERR("AAファイルの読み出しに失敗しました");
                exit(EXIT_FAILURE);
            }
            aa->map[y][x] = unicode;
            pos += size;
        }
    }
}

static int find_strike_index(FT_Face face) {
    for (int i = 0; i < face->num_fixed_sizes; i++) {
        if (face->available_sizes[i].height == FONT_WIDTH) {
            return i;
        }
    }
    return -1;
}

static void aa_to_image(aa_t *aa, image_t *img) {
    img->width = aa->width * FONT_WIDTH;
    img->height = aa->height * FONT_WIDTH;
    img->map = xmalloc(sizeof(uint8_t *) * img->height);
    for (int y = 0; y < img->height; y++) {
        img->map[y] = xmalloc(sizeof(uint8_t*) * img->width);
    }

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
    for (int y = 0; y < aa->height; y++) {
        for (int x = 0; x < aa->width; x++) {
            write_glyph_to_image(face, aa->map[y][x], img, x * FONT_WIDTH, y * FONT_WIDTH);
        }
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
}

static void write_glyph_to_image(FT_Face face, FT_ULong unicode, image_t *img, int x, int y) {
    FT_UInt glyph_index = FT_Get_Char_Index(face, unicode);
    if (glyph_index == 0) {
        ERR("グリフが見つかりません");
        exit(EXIT_FAILURE);
    }
    int error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
    if (error) {
        ERR("グリフの読み出しに失敗しました");
        exit(EXIT_FAILURE);
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        ERR("ビットマップグリフではありません");
        exit(EXIT_FAILURE);
    }
    FT_Bitmap *bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_MONO ||
        bitmap->width != FONT_WIDTH) {
        ERR("全角文字ではありません");
        exit(EXIT_FAILURE);
    }
    int extra_bits = bitmap->width % 8;
    int last_bits = extra_bits == 0 ? 8 : extra_bits;
    for (int fy = 0; fy < bitmap->rows; fy++) {
        for (int p = 0; p < bitmap->pitch; p++) {
            const int bits = p < bitmap->pitch - 1 ? 8 : last_bits;
            const int c = bitmap->buffer[bitmap->pitch * fy + p];
            for (int i = 0; i < bits; i++) {
                int fx = p * 8 + i;
                img->map[y + fy][x + fx] = (c & (1 << (7 - i))) == 0;
            }
        }
    }
}

static void write_png_file(const char *filename, image_t *img) {
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    write_png_stream(file, img);
    fclose(file);
}

static void write_png_stream(FILE *file, image_t *img) {
    int row_size = sizeof(png_byte) * img->width;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        ERR("png_create_write_struct に失敗しました");
        exit(EXIT_FAILURE);
    }
    png_infop info = png_create_info_struct(png);
    if (info == NULL) {
        ERR("png_create_info_struct に失敗しました");
        exit(EXIT_FAILURE);
    }
    if (setjmp(png_jmpbuf(png))) {
        ERR("PNGの書き出しに失敗しました");
        exit(EXIT_FAILURE);
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, img->width, img->height, 8,
                 PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_bytepp rows = png_malloc(png, sizeof(png_bytep) * img->height);
    if (rows == NULL) {
        ERR("メモリ確保に失敗しました");
        exit(EXIT_FAILURE);
    }
    png_set_rows(png, info, rows);
    memset(rows, 0, sizeof(png_bytep) * img->height);
    for (int y = 0; y < img->height; y++) {
        if ((rows[y] = png_malloc(png, row_size)) == NULL) {
            ERR("メモリ確保に失敗しました");
            exit(EXIT_FAILURE);
        }
    }
    png_colorp palette = png_malloc(png, sizeof(png_color) * 2);
    palette[0].red = 0;
    palette[0].green = 0;
    palette[0].blue = 0;
    palette[1].red = 255;
    palette[1].green = 255;
    palette[1].blue = 255;
    png_set_PLTE(png, info, palette, 2);
    png_free(png, palette);
    for (int y = 0; y < img->height; y++) {
        png_bytep row = rows[y];
        for (int x = 0; x < img->width; x++) {
            *row++ = img->map[y][x];
        }
    }
    png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);
    if (rows != NULL) {
        for (int y = 0; y < img->height; y++) {
            png_free(png, rows[y]);
        }
        png_free(png, rows);
    }
    png_destroy_write_struct(&png, &info);
}
