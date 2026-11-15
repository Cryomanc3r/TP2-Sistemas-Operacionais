#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "ferramentas.h"
#include "simulador.h"

static void usage(char *prog) {
    fprintf(stderr,
        "Uso: %s <algoritmo> <arquivo.log> <pagina KB> <memoria KB> [opcoes]\n"
        "  algoritmo: random | lru | lfu | clock\n"
        "  pagina:    2 a 64 (potência de 2);  memoria: 128 a 16384\n"
        "opcoes:\n"
        "  debug | debug=N   descreve cada acesso (N=2: mostra quadros)\n"
        "  stats             estatísticas adicionais\n"
        "  pt=TIPO           dense (padrão) | 2level | 3level | inverted\n"
        "  seed=N            semente do random() (padrão %d)\n",
        prog, DEFAULT_SEED);
    exit(-1);
}

static void parse_args(int argc, char **argv, struct config *cfg) {
    int i, j, ok;
    unsigned tmp;

    if (argc < 5) usage(argv[0]);
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed    = DEFAULT_SEED;
    cfg->pt_type = PT_DENSE;

    ok = 0;
    for (i = 0; i < 4; i++)
        if (strcasecmp(argv[1], algo_names[i]) == 0) {
            cfg->algo = i; ok = 1;
        }
    if (!ok) {
        fprintf(stderr, "Algoritmo desconhecido: %s\n", argv[1]);
        exit(-1);
    }
    cfg->file    = argv[2];
    cfg->page_kb = parse_uint(argv[3], "Tamanho da página");
    cfg->mem_kb  = parse_uint(argv[4], "Tamanho da memória");

    if (cfg->page_kb & (cfg->page_kb - 1)) {
        fprintf(stderr, "O tamanho da página deve ser potência de 2\n");
        exit(-1);
    }
    cfg->nframes = cfg->mem_kb / cfg->page_kb;
    if (cfg->nframes == 0) {
        fprintf(stderr, "A memória deve comportar ao menos uma página\n");
        exit(-1);
    }

    /* s = log2(page_kb * 1024) */
    tmp = cfg->page_kb;
    cfg->shift = 10;
    while (tmp > 1) { tmp >>= 1; cfg->shift++; }
    cfg->vpn_bits = ADDR_BITS - cfg->shift;

    for (i = 5; i < argc; i++) {
        if      (strcmp (argv[i], "debug")     == 0) cfg->debug = 1;
        else if (strncmp(argv[i], "debug=", 6) == 0) cfg->debug = atoi(argv[i] + 6);
        else if (strcmp (argv[i], "stats")     == 0) cfg->stats = 1;
        else if (strncmp(argv[i], "seed=", 5)  == 0)
            cfg->seed = (unsigned)strtoul(argv[i] + 5, 0, 10);
        else if (strncmp(argv[i], "pt=", 3)    == 0) {
            ok = 0;
            for (j = 0; j < 4; j++)
                if (strcmp(argv[i] + 3, pt_args[j]) == 0) {
                    cfg->pt_type = j; ok = 1;
                }
            if (!ok) {
                fprintf(stderr, "Tabela desconhecida: %s\n", argv[i] + 3);
                exit(-1);
            }
        } else usage(argv[0]);
    }
}

int main(int argc, char **argv) {
    struct config    cfg;
    struct trace     tr;
    struct stats     st;
    struct memory   *m;
    struct pagetable *pt;

    parse_args(argc, argv, &cfg);
    printf("Executando o simulador...\n");

    read_trace(cfg.file, &tr);
    memset(&st, 0, sizeof(st));
    m = newmemory(cfg.algo, cfg.nframes);

    switch (cfg.pt_type) {
        case PT_DENSE:   pt = densept     (cfg.vpn_bits); break;
        case PT_2LEVEL:  pt = twolevelpt  (cfg.vpn_bits); break;
        case PT_3LEVEL:  pt = threelevelpt(cfg.vpn_bits); break;
        default:         pt = invertedpt  (cfg.nframes);  break;
    }
    srandom(cfg.seed);

    simulate(&cfg, &tr, m, pt, &st);
    print_report(&cfg, &st, pt);

    pt_free(pt);
    free_memory(m);
    free_trace(&tr);
    return 0;
}