/*
 * Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define FONT_WIDTH 16
#define CODE_WIDTH 3
#define CODE_SIZE (CODE_WIDTH * CODE_WIDTH)
#define CELL_WIDTH 5

#define _DEBUG_

#ifdef _DEBUG_
#define ERR(fmt, ...) fprintf(stderr, "\033[31m[%-15.15s:%4u] " fmt "\033[0m\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DBG(fmt, ...) fprintf(stderr, "\033[33m[%-15.15s:%4u] " fmt "\033[0m\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG(fmt, ...) fprintf(stderr, "[%-15.15s:%4u] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define PRT(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define ERR(fmt, ...)
#define DBG(fmt, ...)
#define LOG(fmt, ...)
#define PRT(fmt, ...)
#endif

typedef struct code_cell_t {
    uint8_t code[CODE_SIZE];
    uint32_t unicode;
} code_cell_t;

typedef struct code_book_t {
    code_cell_t **book;
    int size;
} code_book_t;

void *xmalloc(size_t n);
void init_code_book(code_book_t *code_book);
void free_code_book(code_book_t *code_book);
void print_unicode_as_utf8(FILE *file, uint32_t unicode);
uint32_t read_utf8_as_unicode(const char *c, int *count);

#endif //COMMON_H
