#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferramentas.h"

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (!p) {
        fprintf(stderr, "Sem memória\n");
        exit(-1);
    }
    return p;
}

unsigned parse_uint(const char *s, const char *what) {
    char         *end;
    unsigned long v = strtoul(s, &end, 10);

    if (*s == 0 || *end != 0 || v == 0) {
        fprintf(stderr, "%s inválido: %s\n", what, s);
        exit(-1);
    }
    return (unsigned)v;
}

void read_trace(const char *path, struct trace *tr) {
    FILE    *file;
    size_t   cap = 1 << 16;
    unsigned addr;
    char     rw;
    int      r;

    file = fopen(path, "r");
    if (!file) {
        perror(path);
        exit(-1);
    }
    tr->n   = 0;
    tr->ref = xcalloc(cap, sizeof(*tr->ref));

    while ((r = fscanf(file, "%x %c", &addr, &rw)) == 2) {
        rw = toupper((unsigned char)rw);
        if (rw != 'R' && rw != 'W') {
            fprintf(stderr, "%s: linha %zu: operação '%c' inválida\n",
                    path, tr->n + 1, rw);
            exit(-1);
        }
        if (tr->n == cap) {
            cap *= 2;
            tr->ref = realloc(tr->ref, cap * sizeof(*tr->ref));
            if (!tr->ref) {
                fprintf(stderr, "Sem memória\n");
                exit(-1);
            }
        }
        tr->ref[tr->n].addr = addr;
        tr->ref[tr->n].rw   = rw;
        tr->n++;
    }
    if (r != EOF) {
        fprintf(stderr, "%s: formato inválido perto da linha %zu\n",
                path, tr->n + 1);
        exit(-1);
    }
    fclose(file);
}

void free_trace(struct trace *tr) {
    free(tr->ref);
    tr->ref = NULL;
    tr->n   = 0;
}