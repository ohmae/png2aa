/*
 * Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <libpng16/png.h>
#include <setjmp.h>
#include <pthread.h>
#include "common.h"

#define DEFAULT_THREAD_NUM 4

typedef struct work_t {
    pthread_t thread_id;
    int start;
    int end;
    code_book_t *code_book;
    image_t *image;
    aa_t *aa;
} work_t;

static void read_code_book_file(char *filename, code_book_t *code_book);
static void read_code_book_stream(FILE *file, code_book_t *code_book);
static uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b);
static void read_png_file(char *filename, image_t *image);
static void read_png_stream(FILE *file, image_t *image);
static void free_image(image_t *img);
static void image_to_text(FILE *file, code_book_t *code_book, image_t *image, int thread_num);
static int calculate_distance(uint8_t *a, uint8_t *b);
static void *work_fragment(void *argument);
static void adjust_luminance(code_book_t *code_book, image_t *image);

int main(int argc, char **argv) {
    char *code_book_file = NULL;
    char *image_file = NULL;
    int thread_num = DEFAULT_THREAD_NUM;
    int opt;
    while ((opt = getopt(argc, argv, "c:i:j:")) != -1) {
        switch (opt) {
            case 'c':
                code_book_file = optarg;
                break;
            case 'i':
                image_file = optarg;
                break;
            case 'j':
                thread_num = atoi(optarg);
                break;
        }
    }
    if (thread_num < 1) {
        thread_num = DEFAULT_THREAD_NUM;
    }
    if (code_book_file == NULL || image_file == NULL) {
        ERR("使用用法: png2txt -c <code book> -i <image> -j <jobs>");
        return EXIT_FAILURE;
    }
    code_book_t book;
    init_code_book(&book);
    read_code_book_file(code_book_file, &book);
    image_t image;
    read_png_file(image_file, &image);
    adjust_luminance(&book, &image);
    image_to_text(stdout, &book, &image, thread_num);
    free_image(&image);
    free_code_book(&book);
    return EXIT_SUCCESS;
}

static void read_code_book_file(char *filename, code_book_t *code_book) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    read_code_book_stream(file, code_book);
    fclose(file);
}

static void read_code_book_stream(FILE *file, code_book_t *code_book) {
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
        add_code_book(code_book, cell);
    }
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

static void *work_fragment(void *argument) {
    work_t *work = (work_t *)argument;

    for (int y = work->start; y < work->end; y++) {
        for (int x = 0; x < work->aa->width; x++) {
            uint8_t sample[CODE_SIZE];
            for (int cy = 0; cy < CODE_WIDTH; cy++) {
                for (int cx = 0; cx < CODE_WIDTH; cx++) {
                    sample[cy * CODE_WIDTH + cx] =  work->image->map[y * CODE_WIDTH + cy][x * CODE_WIDTH + cx];
                }
            }
            int min = INT_MAX;
            int index = 0;
            for (int i = 0; i <  work->code_book->size; i++) {
                int d = calculate_distance(sample,  work->code_book->code[i]->code);
                if (min > d) {
                    min = d;
                    index = i;
                }
            }
            work->aa->map[y][x] = work->code_book->code[index]->unicode;
        }
    }
}

static void image_to_text(FILE *file, code_book_t *code_book, image_t *image, int thread_num) {
    int width = image->width / CODE_WIDTH;
    int height = image->height / CODE_WIDTH;
    aa_t aa;
    aa.width = width;
    aa.height = height;
    aa.map = xmalloc(sizeof(uint32_t*) * height);
    for (int i = 0; i < height; i++) {
        aa.map[i] = xmalloc(sizeof(uint32_t) * width);
    }
    int step = 0;
    work_t *works = xmalloc(sizeof(work_t)* thread_num);
    if (thread_num > height) {
        thread_num = height;
    }
    for (int i = 0; i < thread_num; i++) {
        works[i].code_book = code_book;
        works[i].image = image;
        works[i].aa = &aa;
        works[i].start = step;
        step += height / thread_num + (i < height % thread_num);
        works[i].end = step;
        pthread_create(&works[i].thread_id, NULL, work_fragment, &works[i]);
    }
    for (int i = 0; i < thread_num; i++) {
        pthread_join(works[i].thread_id, NULL);
    }
    free(works);
    fprintf(file, "%d %d\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            print_unicode_as_utf8(file, aa.map[y][x]);
        }
        fprintf(file, "\n");
    }
    for (int i = 0; i < height; i++) {
        free(aa.map[i]);
    }
    free(aa.map);
}

static int calculate_distance(uint8_t *a, uint8_t *b) {
    int distance = 0;
    for (int i = 0; i < CODE_SIZE; i++) {
        distance += abs(a[i] - b[i]);
    }
    return distance;
}

static void adjust_luminance(code_book_t *code_book, image_t *image) {
    int min = 255;
    for (int i = 0; i < code_book->size; i++) {
        for (int j = 0; j < CODE_SIZE; ++j) {
            int p = code_book->code[i]->code[j];
            if (min > p) {
                min = p;
            }
        }
    }
    for (int y = 0; y < image->height; ++y) {
        for (int x = 0; x < image->width; ++x) {
            image->map[y][x] = (image->map[y][x] * (255 - min)) / 255 + min;
        }
    }
}
