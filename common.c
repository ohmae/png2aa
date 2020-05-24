/*
 * Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 */

#include "common.h"

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (p == NULL) {
        perror("");
        exit(EXIT_FAILURE);
    }
    return p;
}

void init_code_book(code_book_t *code_book) {
    code_book->size = 0;
    code_book->book = xmalloc(sizeof(code_cell_t *) * 10000);
}

void free_code_book(code_book_t *code_book) {
    for (int i = 0; i < code_book->size; ++i) {
        free(code_book->book[i]);
    }
    free(code_book->book);
}

void print_unicode_as_utf8(FILE *file, uint32_t unicode) {
    if (unicode < 0x80) {
        char c[1];
        c[0] = unicode & 0xff;
        fwrite(c, sizeof(char), 1, file);
    } else if (unicode < 0x800) {
        char c[2];
        c[0] = 0xc0 | (unicode >> 6 & 0x1f);
        c[1] = 0x80 | (unicode & 0x3f);
        fwrite(c, sizeof(char), 2, file);
    } else if (unicode < 0x10000) {
        char c[3];
        c[0] = 0xe0 | (unicode >> 12 & 0xf);
        c[1] = 0x80 | (unicode >> 6 & 0x3f);
        c[2] = 0x80 | (unicode & 0x3f);
        fwrite(c, sizeof(char), 3, file);
    }
}

uint32_t read_utf8_as_unicode(const char *c, int *count) {
    uint32_t unicode = 0;
    int read = 0;
    if ((c[0] & 0x80) == 0) {
        unicode = c[0];
        read = 1;
    } else if ((c[0] & 0xe0) == 0xc0 &&
               (c[1] & 0xc0) == 0x80) {
        unicode = ((c[0] & 0x1f) << 6) + (c[1] & 0x3f);
        read = 2;
    } else if ((c[0] & 0xf0) == 0xe0 &&
               (c[1] & 0xc0) == 0x80 &&
               (c[2] & 0xc0) == 0x80) {
        unicode = ((c[0] & 0xf) << 12) + ((c[1] & 0x3f) << 6) + (c[2] & 0x3f);
        read = 3;
    }
    if (count != NULL) {
        *count = read;
    }
    return unicode;
}
