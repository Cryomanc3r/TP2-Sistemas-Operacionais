#ifndef FERRAMENTAS_H
#define FERRAMENTAS_H

#include <stddef.h>

#define ADDR_BITS    32   /* Endereços dos logs têm 32 bits           */
#define DEFAULT_SEED 42   /* Semente padrão do random() (reproduzível) */

/* Um acesso lido do log. */
struct memref {
    unsigned addr;
    char     rw;         /* 'R' ou 'W' */
};

struct trace {
    struct memref *ref;
    size_t         n;
};

/* Aloca memória zerada; aborta se faltar memória. */
void *xcalloc(size_t n, size_t size);

/* Converte inteiro positivo; aborta se inválido. */
unsigned parse_uint(const char *s, const char *what);

/* Lê o log inteiro em memória (endereço hex + R/W por linha). */
void read_trace(const char *path, struct trace *tr);

/* Libera o vetor interno de um trace. */
void free_trace(struct trace *tr);

#endif