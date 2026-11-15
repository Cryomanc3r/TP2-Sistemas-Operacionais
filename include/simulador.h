#ifndef SIMULADOR_H
#define SIMULADOR_H

#include <stddef.h>
#include "ferramentas.h"

/* Algoritmos de substituição de páginas. */
enum { ALG_RANDOM, ALG_LRU, ALG_LFU, ALG_CLOCK };

/* Tipos de tabela de páginas. */
enum { PT_DENSE, PT_2LEVEL, PT_3LEVEL, PT_INVERTED };

extern const char *algo_names[];   /* nomes canônicos dos algoritmos      */
extern const char *pt_args[];      /* nomes aceitos em pt=...             */
extern const char *pt_names[];     /* nomes exibidos no relatório         */

/* Parâmetros da execução. */
struct config {
    char    *file;
    int      algo;
    int      pt_type;
    unsigned page_kb, mem_kb;
    unsigned shift, vpn_bits, nframes;
    unsigned seed;
    int      debug;   /* 0/1/2 */
    int      stats;
};

/* Contadores da simulação. */
struct stats {
    unsigned long long accesses;
    unsigned long long faults;
    unsigned long long cold;
    unsigned long long writebacks;
    double             elapsed;
};

/* Tipos opacos: quem usa só vê ponteiros. */
struct pagetable;
struct memory;

/* ------- Tabelas de páginas (vtable embutida) --------------------- */
int  pt_lookup(struct pagetable *pt, unsigned vpn);
void pt_insert(struct pagetable *pt, unsigned vpn, int frame);
void pt_remove(struct pagetable *pt, unsigned vpn);
void pt_free  (struct pagetable *pt);

struct pagetable *densept     (unsigned vpn_bits);
struct pagetable *twolevelpt  (unsigned vpn_bits);
struct pagetable *threelevelpt(unsigned vpn_bits);
struct pagetable *invertedpt  (unsigned nframes);

/* ------- Memória física e simulação ------------------------------- */
struct memory *newmemory(int algo, int nframes);
void           free_memory(struct memory *m);

void simulate    (struct config *cfg, struct trace *tr,
                  struct memory *m, struct pagetable *pt,
                  struct stats  *st);
void print_report(struct config *cfg, struct stats *st,
                  struct pagetable *pt);

#endif