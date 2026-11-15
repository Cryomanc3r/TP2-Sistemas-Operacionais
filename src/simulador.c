/*
 * Núcleo da simulação: tabelas de páginas, memória física,
 * políticas de reposição e o loop de simulação.
 *
 * Padrão de projeto:
 *   - struct pagetable tem vtable (lookup/insert/del/destroy).
 *     Cada construtor registra as funções do seu tipo, então os
 *     despachos "pt_*" são uma linha só — nada de switch gigante.
 *   - struct memory é opaca para o main.c; só newmemory/free_memory
 *     são exportados.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferramentas.h"
#include "simulador.h"

const char *algo_names[] = {"random", "lru", "lfu", "clock"};
const char *pt_args[]    = {"dense", "2level", "3level", "inverted"};
const char *pt_names[]   = {"densa", "2 níveis", "3 níveis", "invertida"};

/* ================================================================
 * 1. Tabelas de páginas
 *
 * struct pagetable é a "classe base" (com vtable). Cada tabela
 * concreta a estende; seus membros ficam encapsulados neste .c.
 * ================================================================ */

struct pagetable {
    int  type;
    size_t bytes;              /* memória alocada pela estrutura         */
    unsigned long long tables; /* tabelas alocadas (dir + folhas)        */
    unsigned long long lookups;
    unsigned long long refs;

    int  (*lookup)(struct pagetable *, unsigned vpn);
    void (*insert)(struct pagetable *, unsigned vpn, int frame);
    void (*del)   (struct pagetable *, unsigned vpn);
    void (*destroy)(struct pagetable *);
};

/* --- Densa: um vetor por página virtual -------------------------- */
struct densept {
    struct pagetable base;
    int *entry;
};

static int dense_lookup(struct pagetable *pt, unsigned vpn) {
    struct densept *t = (struct densept *)pt;
    pt->refs++;
    return t->entry[vpn];
}
static void dense_insert(struct pagetable *pt, unsigned vpn, int f) {
    ((struct densept *)pt)->entry[vpn] = f;
}
static void dense_del(struct pagetable *pt, unsigned vpn) {
    ((struct densept *)pt)->entry[vpn] = -1;
}
static void dense_destroy(struct pagetable *pt) {
    struct densept *t = (struct densept *)pt;
    free(t->entry);
    free(t);
}

struct pagetable *densept(unsigned vpn_bits) {
    struct densept *t = xcalloc(1, sizeof(*t));
    size_t n = (size_t)1 << vpn_bits;

    t->base.type   = PT_DENSE;
    t->entry       = xcalloc(n, sizeof(int));
    memset(t->entry, 0xff, n * sizeof(int));
    t->base.bytes  = n * sizeof(int);
    t->base.tables = 1;
    t->base.lookup = dense_lookup;
    t->base.insert = dense_insert;
    t->base.del    = dense_del;
    t->base.destroy = dense_destroy;
    return &t->base;
}

/* --- Comum a tabelas hierárquicas: aloca folha zerada ------------ */
static int *newleaf(struct pagetable *base, unsigned bits) {
    size_t n = (size_t)1 << bits;
    int   *leaf = xcalloc(n, sizeof(int));

    memset(leaf, 0xff, n * sizeof(int));
    base->bytes += n * sizeof(int);
    base->tables++;
    return leaf;
}

/* --- 2 níveis: diretório + folhas sob demanda -------------------- */
struct twolevelpt {
    struct pagetable base;
    unsigned bits1, bits2;
    int **dir;
};

static int twolevel_lookup(struct pagetable *pt, unsigned vpn) {
    struct twolevelpt *t = (struct twolevelpt *)pt;
    int *leaf;

    pt->refs++;
    leaf = t->dir[vpn >> t->bits2];
    if (!leaf) return -1;
    pt->refs++;
    return leaf[vpn & (((unsigned)1 << t->bits2) - 1)];
}
static void twolevel_insert(struct pagetable *pt, unsigned vpn, int f) {
    struct twolevelpt *t = (struct twolevelpt *)pt;
    unsigned i1 = vpn >> t->bits2;
    if (t->dir[i1] == 0)
        t->dir[i1] = newleaf(pt, t->bits2);
    t->dir[i1][vpn & (((unsigned)1 << t->bits2) - 1)] = f;
}
static void twolevel_del(struct pagetable *pt, unsigned vpn) {
    struct twolevelpt *t = (struct twolevelpt *)pt;
    int *leaf = t->dir[vpn >> t->bits2];
    if (leaf)
        leaf[vpn & (((unsigned)1 << t->bits2) - 1)] = -1;
}
static void twolevel_destroy(struct pagetable *pt) {
    struct twolevelpt *t = (struct twolevelpt *)pt;
    size_t i, n = (size_t)1 << t->bits1;
    for (i = 0; i < n; i++) free(t->dir[i]);
    free(t->dir);
    free(t);
}

struct pagetable *twolevelpt(unsigned vpn_bits) {
    struct twolevelpt *t = xcalloc(1, sizeof(*t));
    t->base.type = PT_2LEVEL;
    t->bits1 = vpn_bits / 2;
    t->bits2 = vpn_bits - t->bits1;
    t->dir   = xcalloc((size_t)1 << t->bits1, sizeof(int *));
    t->base.bytes  = ((size_t)1 << t->bits1) * sizeof(int *);
    t->base.tables = 1;
    t->base.lookup = twolevel_lookup;
    t->base.insert = twolevel_insert;
    t->base.del    = twolevel_del;
    t->base.destroy = twolevel_destroy;
    return &t->base;
}

/* --- 3 níveis ---------------------------------------------------- */
struct threelevelpt {
    struct pagetable base;
    unsigned bits1, bits2, bits3;
    int ***dir;
};

static int threelevel_lookup(struct pagetable *pt, unsigned vpn) {
    struct threelevelpt *t = (struct threelevelpt *)pt;
    int **mid, *leaf;

    pt->refs++;
    mid = t->dir[vpn >> (t->bits2 + t->bits3)];
    if (!mid) return -1;
    pt->refs++;
    leaf = mid[(vpn >> t->bits3) & (((unsigned)1 << t->bits2) - 1)];
    if (!leaf) return -1;
    pt->refs++;
    return leaf[vpn & (((unsigned)1 << t->bits3) - 1)];
}
static void threelevel_insert(struct pagetable *pt, unsigned vpn, int f) {
    struct threelevelpt *t = (struct threelevelpt *)pt;
    unsigned i1 = vpn >> (t->bits2 + t->bits3);
    unsigned i2 = (vpn >> t->bits3) & (((unsigned)1 << t->bits2) - 1);

    if (t->dir[i1] == 0) {
        t->dir[i1] = xcalloc((size_t)1 << t->bits2, sizeof(int *));
        pt->bytes += ((size_t)1 << t->bits2) * sizeof(int *);
        pt->tables++;
    }
    if (t->dir[i1][i2] == 0)
        t->dir[i1][i2] = newleaf(pt, t->bits3);
    t->dir[i1][i2][vpn & (((unsigned)1 << t->bits3) - 1)] = f;
}
static void threelevel_del(struct pagetable *pt, unsigned vpn) {
    struct threelevelpt *t = (struct threelevelpt *)pt;
    unsigned i1 = vpn >> (t->bits2 + t->bits3);
    unsigned i2 = (vpn >> t->bits3) & (((unsigned)1 << t->bits2) - 1);
    if (t->dir[i1] == 0) return;
    if (t->dir[i1][i2])
        t->dir[i1][i2][vpn & (((unsigned)1 << t->bits3) - 1)] = -1;
}
static void threelevel_destroy(struct pagetable *pt) {
    struct threelevelpt *t = (struct threelevelpt *)pt;
    size_t i, j;
    for (i = 0; i < ((size_t)1 << t->bits1); i++) {
        if (!t->dir[i]) continue;
        for (j = 0; j < ((size_t)1 << t->bits2); j++) free(t->dir[i][j]);
        free(t->dir[i]);
    }
    free(t->dir);
    free(t);
}

struct pagetable *threelevelpt(unsigned vpn_bits) {
    struct threelevelpt *t = xcalloc(1, sizeof(*t));
    t->base.type = PT_3LEVEL;
    t->bits1 = vpn_bits / 3;
    t->bits2 = (vpn_bits - t->bits1) / 2;
    t->bits3 = vpn_bits - t->bits1 - t->bits2;
    t->dir   = xcalloc((size_t)1 << t->bits1, sizeof(int **));
    t->base.bytes  = ((size_t)1 << t->bits1) * sizeof(int **);
    t->base.tables = 1;
    t->base.lookup = threelevel_lookup;
    t->base.insert = threelevel_insert;
    t->base.del    = threelevel_del;
    t->base.destroy = threelevel_destroy;
    return &t->base;
}

/* --- Invertida: uma entrada por quadro físico + hash ------------- */
struct invertedpt {
    struct pagetable base;
    unsigned hbits;
    unsigned *vpn;
    int      *next;
    int      *bucket;
};

static unsigned hash_vpn(struct invertedpt *t, unsigned vpn) {
    return (vpn * 2654435769u) >> (32 - t->hbits);
}
static int inverted_lookup(struct pagetable *pt, unsigned vpn) {
    struct invertedpt *t = (struct invertedpt *)pt;
    int f;

    pt->refs++;
    for (f = t->bucket[hash_vpn(t, vpn)]; f >= 0; f = t->next[f]) {
        pt->refs++;
        if (t->vpn[f] == vpn) return f;
    }
    return -1;
}
static void inverted_insert(struct pagetable *pt, unsigned vpn, int f) {
    struct invertedpt *t = (struct invertedpt *)pt;
    unsigned h = hash_vpn(t, vpn);
    t->vpn[f]     = vpn;
    t->next[f]    = t->bucket[h];
    t->bucket[h]  = f;
}
static void inverted_del(struct pagetable *pt, unsigned vpn) {
    struct invertedpt *t = (struct invertedpt *)pt;
    unsigned h = hash_vpn(t, vpn);
    int f, prev = -1;

    for (f = t->bucket[h]; f >= 0; prev = f, f = t->next[f]) {
        if (t->vpn[f] != vpn) continue;
        if (prev < 0) t->bucket[h] = t->next[f];
        else          t->next[prev] = t->next[f];
        break;
    }
}
static void inverted_destroy(struct pagetable *pt) {
    struct invertedpt *t = (struct invertedpt *)pt;
    free(t->vpn); free(t->next); free(t->bucket); free(t);
}

struct pagetable *invertedpt(unsigned nframes) {
    struct invertedpt *t = xcalloc(1, sizeof(*t));
    size_t nb;

    t->base.type = PT_INVERTED;
    t->hbits = 1;
    while (((size_t)1 << t->hbits) < 2 * (size_t)nframes)
        t->hbits++;
    nb = (size_t)1 << t->hbits;
    t->vpn    = xcalloc(nframes,  sizeof(unsigned));
    t->next   = xcalloc(nframes,  sizeof(int));
    t->bucket = xcalloc(nb,       sizeof(int));
    memset(t->bucket, 0xff, nb * sizeof(int));
    t->base.bytes  = nframes * (sizeof(unsigned) + sizeof(int))
                   + nb * sizeof(int);
    t->base.tables = 1;
    t->base.lookup = inverted_lookup;
    t->base.insert = inverted_insert;
    t->base.del    = inverted_del;
    t->base.destroy = inverted_destroy;
    return &t->base;
}

/* --- Despachantes (agora triviais) ------------------------------ */
int  pt_lookup(struct pagetable *pt, unsigned vpn) {
    pt->lookups++;
    return pt->lookup(pt, vpn);
}
void pt_insert(struct pagetable *pt, unsigned vpn, int f) { pt->insert(pt, vpn, f); }
void pt_remove(struct pagetable *pt, unsigned vpn)        { pt->del(pt, vpn); }
void pt_free  (struct pagetable *pt)                      { if (pt) pt->destroy(pt); }

/* ================================================================
 * 2. Memória física e algoritmos de reposição
 * ================================================================ */

struct frame {
    unsigned           page;
    int                dirty, referenced;
    unsigned long long last_access, freq;
    int                prev, next;    /* LRU: elos                          */
    int                heap_pos;      /* LFU: índice no heap                */
};

struct memory {
    int             algo, nframes, used;
    struct frame   *frames;
    int             lru_head, lru_tail;
    int            *heap, heap_size;
    int             hand;
};

/* --- LRU -------------------------------------------------------- */
static void lru_unlink(struct memory *m, int f) {
    struct frame *fr = &m->frames[f];
    if (fr->prev >= 0) m->frames[fr->prev].next = fr->next;
    else               m->lru_head = fr->next;
    if (fr->next >= 0) m->frames[fr->next].prev = fr->prev;
    else               m->lru_tail = fr->prev;
    fr->prev = fr->next = -1;
}
static void lru_push_front(struct memory *m, int f) {
    struct frame *fr = &m->frames[f];
    fr->prev = -1;
    fr->next = m->lru_head;
    if (m->lru_head >= 0) m->frames[m->lru_head].prev = f;
    m->lru_head = f;
    if (m->lru_tail < 0) m->lru_tail = f;
}

/* --- LFU (heap mínimo por (freq, last_access)) ------------------ */
static int lfu_less(struct memory *m, int a, int b) {
    struct frame *fa = &m->frames[a], *fb = &m->frames[b];
    if (fa->freq != fb->freq) return fa->freq < fb->freq;
    return fa->last_access < fb->last_access;
}
static void heap_swap(struct memory *m, int i, int j) {
    int fi = m->heap[i], fj = m->heap[j];
    m->heap[i] = fj; m->heap[j] = fi;
    m->frames[fj].heap_pos = i;
    m->frames[fi].heap_pos = j;
}
static void heap_up(struct memory *m, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!lfu_less(m, m->heap[i], m->heap[p])) break;
        heap_swap(m, i, p);
        i = p;
    }
}
static void heap_down(struct memory *m, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, s = i;
        if (l < m->heap_size && lfu_less(m, m->heap[l], m->heap[s])) s = l;
        if (r < m->heap_size && lfu_less(m, m->heap[r], m->heap[s])) s = r;
        if (s == i) break;
        heap_swap(m, i, s);
        i = s;
    }
}

/* --- CLOCK ------------------------------------------------------ */
static int clock_victim(struct memory *m) {
    for (;;) {
        int f = m->hand;
        m->hand = (m->hand + 1) % m->nframes;
        if (!m->frames[f].referenced) return f;
        m->frames[f].referenced = 0;
    }
}

struct memory *newmemory(int algo, int nframes) {
    struct memory *m = xcalloc(1, sizeof(*m));
    m->algo    = algo;
    m->nframes = nframes;
    m->frames  = xcalloc(nframes, sizeof(struct frame));
    m->lru_head = m->lru_tail = -1;
    if (algo == ALG_LFU)
        m->heap = xcalloc(nframes, sizeof(int));
    return m;
}

void free_memory(struct memory *m) {
    if (!m) return;
    free(m->heap);
    free(m->frames);
    free(m);
}

static int select_victim(struct memory *m) {
    switch (m->algo) {
        case ALG_RANDOM: return (int)(random() % m->nframes);
        case ALG_LRU:    return m->lru_tail;
        case ALG_LFU:    return m->heap[0];
        case ALG_CLOCK:  return clock_victim(m);
    }
    return 0;
}

static void touch_frame(struct memory *m, int f, char rw,
                        unsigned long long now) {
    struct frame *fr = &m->frames[f];

    fr->referenced  = 1;
    fr->last_access = now;
    fr->freq++;
    if (rw == 'W') fr->dirty = 1;

    switch (m->algo) {
        case ALG_LRU:
            if (m->lru_head != f) {
                lru_unlink(m, f);
                lru_push_front(m, f);
            }
            break;
        case ALG_LFU:
            heap_down(m, fr->heap_pos);  /* chave só cresce */
            break;
    }
}

static void load_page(struct memory *m, int f, unsigned vpn, char rw,
                      unsigned long long now, int replaced) {
    struct frame *fr = &m->frames[f];

    fr->page        = vpn;
    fr->dirty       = (rw == 'W');
    fr->referenced  = 1;
    fr->last_access = now;
    fr->freq        = 1;

    switch (m->algo) {
        case ALG_LRU:
            if (replaced) lru_unlink(m, f);
            lru_push_front(m, f);
            break;
        case ALG_LFU:
            if (replaced) heap_down(m, fr->heap_pos);  /* vítima era a raiz */
            else {
                fr->heap_pos = m->heap_size;
                m->heap[m->heap_size++] = f;
                heap_up(m, fr->heap_pos);
            }
            break;
    }
}

/* ================================================================
 * 3. Simulação
 * ================================================================ */

static void dump_frames(struct memory *m) {
    int i;
    for (i = 0; i < m->used; i++) {
        struct frame *fr = &m->frames[i];
        printf("    quadro %d: pag %x%s%s  ultimo=%llu freq=%llu\n",
               i, fr->page,
               fr->dirty ? " [suja]" : "", fr->referenced ? " [R]" : "",
               fr->last_access, fr->freq);
    }
}

static void handle_access(struct config *cfg, struct memory *m,
                          struct pagetable *pt, struct stats *st,
                          unsigned char *seen, unsigned addr, char rw) {
    unsigned vpn = addr >> cfg->shift;
    unsigned long long now = ++st->accesses;
    int f, replaced = 0;
    unsigned victim = 0;
    int victim_dirty = 0;

    f = pt_lookup(pt, vpn);
    if (f >= 0) {
        touch_frame(m, f, rw, now);
        if (cfg->debug)
            printf("#%llu %08x %c pag %x: ACERTO quadro %d\n",
                   now, addr, rw, vpn, f);
    } else {
        st->faults++;
        if (!(seen[vpn >> 3] & (1 << (vpn & 7)))) {
            seen[vpn >> 3] |= 1 << (vpn & 7);
            st->cold++;
        }

        if (m->used < m->nframes) {
            f = m->used++;
        } else {
            f = select_victim(m);
            replaced      = 1;
            victim        = m->frames[f].page;
            victim_dirty  = m->frames[f].dirty;
            pt_remove(pt, victim);
            if (victim_dirty) st->writebacks++;
        }
        load_page(m, f, vpn, rw, now, replaced);
        pt_insert(pt, vpn, f);

        if (cfg->debug) {
            printf("#%llu %08x %c pag %x: FALTA quadro %d",
                   now, addr, rw, vpn, f);
            if (replaced)
                printf(" (substitui pag %x%s)", victim,
                       victim_dirty ? ", suja: escrita no disco" : "");
            printf("\n");
        }
    }
    if (cfg->debug > 1) dump_frames(m);
}

void simulate(struct config *cfg, struct trace *tr, struct memory *m,
              struct pagetable *pt, struct stats *st) {
    unsigned char *seen;
    struct timespec t0, t1;
    size_t i;

    seen = xcalloc(((size_t)1 << cfg->vpn_bits) / 8 + 1, 1);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < tr->n; i++)
        handle_access(cfg, m, pt, st, seen, tr->ref[i].addr, tr->ref[i].rw);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    st->elapsed = (t1.tv_sec - t0.tv_sec)
                + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    free(seen);
}

void print_report(struct config *cfg, struct stats *st, struct pagetable *pt) {
    printf("Arquivo de entrada: %s\n", cfg->file);
    printf("Tamanho da memória: %u KB\n", cfg->mem_kb);
    printf("Tamanho das páginas: %u KB\n", cfg->page_kb);
    printf("Técnica de reposição: %s\n", algo_names[cfg->algo]);
    printf("Total de acessos à memória: %llu\n", st->accesses);
    printf("Páginas lidas: %llu\n",  st->faults);
    printf("Páginas escritas: %llu\n", st->writebacks);

    if (cfg->stats) {
        printf("--- Estatísticas adicionais ---\n");
        printf("Quadros de memória: %u\n", cfg->nframes);
        printf("Acertos: %llu\n", st->accesses - st->faults);
        printf("Faltas compulsórias: %llu\n", st->cold);
        printf("Faltas por substituição: %llu\n", st->faults - st->cold);
        printf("Tabela de páginas: %s\n", pt_names[pt->type]);
        printf("Consultas à tabela: %llu\n", pt->lookups);
        printf("Entradas lidas por consulta: %.3f\n",
               pt->lookups ? (double)pt->refs / pt->lookups : 0.0);
        printf("Tabelas alocadas: %llu\n", pt->tables);
        printf("Memória da tabela de páginas: %.1f KB\n", pt->bytes / 1024.0);
        printf("Tempo de simulação: %.3f s\n", st->elapsed);
    }
}