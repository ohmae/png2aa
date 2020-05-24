/*
 * Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <libpng16/png.h>
#include <setjmp.h>
#include "common.h"

typedef struct image_t {
    int width;
    int height;
    uint8_t **map;
} image_t;

static void read_code_book_file(char *filename, code_book_t *book);
static void read_code_book_stream(FILE *file, code_book_t *book);
static uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b);
static void read_png_file(char *filename, image_t *img);
static void read_png_stream(FILE *file, image_t *img);
static void free_image(image_t *img);
static void image_to_text(FILE *file, code_book_t *book, image_t *img);
static int calculate_distance(uint8_t *a, uint8_t *b);

int main(int argc, char **argv) {
    char *code_book_file = NULL;
    char *image_file = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "c:i:")) != -1) {
        switch (opt) {
            case 'c':
                code_book_file = optarg;
                break;
            case 'i':
                image_file = optarg;
                break;
        }
    }
    if (code_book_file == NULL || image_file == NULL) {
        ERR("使用用法: png2txt -c <code book> -i <image_t>");
        return EXIT_FAILURE;
    }
    code_book_t book;
    init_code_book(&book);
    read_code_book_file(code_book_file, &book);
    image_t img;
    read_png_file(image_file, &img);
    image_to_text(stdout, &book, &img);
    free_image(&img);
    free_code_book(&book);
    return EXIT_SUCCESS;
}

static void read_code_book_file(char *filename, code_book_t *book) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    read_code_book_stream(file, book);
    fclose(file);
}

static void read_code_book_stream(FILE *file, code_book_t *book) {
    char string[100];
    int code[CODE_SIZE];
    while (fscanf(file, "%x,%x,%x,%x,%x,%x,%x,%x,%x,%s",
                  &code[0], &code[1], &code[2],
                  &code[3], &code[4], &code[5],
                  &code[6], &code[7], &code[8],
                  string
    ) == 10) {
        code_cell_t *cell = xmalloc(sizeof(code_cell_t));
        for (int i = 0; i < CODE_SIZE; i++) {
            cell->code[i] = code[i];
        }
        cell->unicode = read_utf8_as_unicode(string, NULL);
        book->book[book->size++] = cell;
    }
}

static uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t) (0.299f * r + 0.587f * g + 0.114f * b + 0.5f);
}

static void read_png_file(char *filename, image_t *img) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    read_png_stream(file, img);
    fclose(file);
}

static void read_png_stream(FILE *file, image_t *img) {
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
    img->map = xmalloc(sizeof(uint8_t*) * height);
    for (int y = 0; y < height; y++) {
        img->map[y] = xmalloc(sizeof(uint8_t) * width);
    }
    img->width = width;
    img->height = height;
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
                    img->map[y][x] = p[*row++];
                }
            }
        }
            break;
        case PNG_COLOR_TYPE_GRAY:
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    img->map[y][x] = *row++;
                }
            }
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            for (y = 0; y < height; y++) {
                png_bytep row = rows[y];
                for (x = 0; x < width; x++) {
                    uint8_t g = *row++;
                    uint8_t a = *row++;
                    img->map[y][x] = g * a / 255 + 255 - a;
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
                    img->map[y][x] = rgb_to_gray(r, g, b);
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
                    img->map[y][x] = gray * a / 255 + 255 - a;
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

static void image_to_text(FILE *file, code_book_t *book, image_t *img) {
    int width = img->width / CODE_WIDTH;
    int height = img->height / CODE_WIDTH;
    fprintf(file, "%d %d\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t sample[CODE_SIZE];
            for (int cy = 0; cy < CODE_WIDTH; cy++) {
                for (int cx = 0; cx < CODE_WIDTH; cx++) {
                    sample[cy * CODE_WIDTH + cx] = img->map[y * CODE_WIDTH + cy][x * CODE_WIDTH + cx];
                }
            }
            int min = INT_MAX;
            int index = 0;
            for (int i = 0; i < book->size; i++) {
                int d = calculate_distance(sample, book->book[i]->code);
                if (min > d) {
                    min = d;
                    index = i;
                }
            }
            print_unicode_as_utf8(file, book->book[index]->unicode);
        }
        fprintf(file, "\n");
    }
}

static int calculate_distance(uint8_t *a, uint8_t *b) {
    int distance = 0;
    for (int i = 0; i < CODE_SIZE; i++) {
        distance += abs(a[i] - b[i]);
    }
    return distance;
}