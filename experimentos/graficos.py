#!/usr/bin/env python3
"""
Gera os gráficos do relatório a partir dos CSVs de experimentos/resultados.
Uso: python3 experimentos/graficos.py
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

RAIZ = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(RAIZ, "resultados")
FIG = os.path.join(os.path.dirname(RAIZ), "relatorio", "figuras")

ALGOS = ["random", "lru", "lfu", "clock"]
COR = {"random": "#2a78d6", "lru": "#eb6834", "lfu": "#1baf7a",
       "clock": "#eda100"}
MARCA = {"random": "o", "lru": "s", "lfu": "^", "clock": "D"}
PTS = ["dense", "2level", "3level", "inverted"]
PT_NOME = {"dense": "densa", "2level": "2 níveis", "3level": "3 níveis",
           "inverted": "invertida"}
PT_COR = {"dense": "#2a78d6", "2level": "#eb6834", "3level": "#1baf7a",
          "inverted": "#6250d6"}
ORDEM = ["compilador", "simulador", "matriz", "compressor",
         "localidade", "fases", "sequencial", "aleatorio"]

plt.rcParams.update({
    "font.size": 9, "axes.titlesize": 10, "axes.spines.top": False,
    "axes.spines.right": False, "axes.edgecolor": "#8a8984",
    "axes.labelcolor": "#52514e", "xtick.color": "#52514e",
    "ytick.color": "#52514e", "grid.color": "#e4e3df", "grid.linewidth": 0.6,
    "legend.frameon": False, "savefig.dpi": 200,
})


def le(nome):
    with open(os.path.join(RES, nome)) as f:
        return list(csv.DictReader(f))


def traces(rows):
    presentes = {r["trace"] for r in rows}
    return [t for t in ORDEM if t in presentes] + \
           sorted(presentes - set(ORDEM))


def kb(v, _=None):
    v = int(v)
    return f"{v // 1024} MB" if v >= 1024 else f"{v} KB"


def multiplos(rows, eixo_x, campo, titulo_y, nome, logy=True, so=None):
    ts = so or traces(rows)
    ncol = 3 if len(ts) > 4 else 2
    nlin = (len(ts) + ncol - 1) // ncol
    fig, axs = plt.subplots(nlin, ncol, figsize=(3.0 * ncol, 2.4 * nlin),
                            squeeze=False)
    for ax, t in zip(axs.flat, ts):
        ys = []
        for a in ALGOS:
            pts = sorted((int(r[eixo_x]), int(r[campo])) for r in rows
                         if r["trace"] == t and r["algo"] == a)
            # Zero não existe em escala log: o ponto é omitido.
            pts = [p for p in pts if p[1] > 0 or not logy]
            ax.plot([p[0] for p in pts], [p[1] for p in pts],
                    color=COR[a], marker=MARCA[a], markersize=4,
                    linewidth=1.6, label=a.upper())
            ys += [p[1] for p in pts]
        ax.set_xscale("log", base=2)
        if logy:
            ax.set_yscale("log")
            # Curvas quase planas: abre o eixo para no mínimo um fator 4,
            # senão o ruído de 0,1% parece variação real.
            lo, hi = min(ys), max(ys)
            if hi / lo < 4:
                meio = (lo * hi) ** 0.5
                ax.set_ylim(meio / 2, meio * 2)
        ax.xaxis.set_major_formatter(FuncFormatter(kb))
        ax.tick_params(axis="x", labelsize=7, rotation=30)
        ax.grid(True, axis="y")
        ax.set_title(t + ".log")
    for ax in list(axs.flat)[len(ts):]:
        ax.axis("off")
    for ax in axs[:, 0]:
        ax.set_ylabel(titulo_y)
    h, l = axs.flat[0].get_legend_handles_labels()
    fig.legend(h, l, loc="upper center", ncol=4,
               bbox_to_anchor=(0.5, 1.02))
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(os.path.join(FIG, nome), bbox_inches="tight")
    plt.close(fig)
    print("figura", nome)


def tabela_memoria(rows):
    ts = [t for t in traces(rows) if t in ("compilador", "simulador")] \
        or traces(rows)[:2]
    fig, axs = plt.subplots(1, len(ts), figsize=(3.2 * len(ts), 2.6),
                            squeeze=False)
    for ax, t in zip(axs.flat, ts):
        for pt in PTS:
            pts = sorted((int(r["pagina"]), float(r["pt_kb"])) for r in rows
                         if r["trace"] == t and r["pt"] == pt)
            ax.plot([p[0] for p in pts], [p[1] for p in pts],
                    color=PT_COR[pt], marker="o", markersize=4,
                    linewidth=1.6, label=PT_NOME[pt])
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.xaxis.set_major_formatter(FuncFormatter(kb))
        ax.set_xlabel("tamanho da página (memória física 1 MB)")
        ax.grid(True, axis="y")
        ax.set_title(t + ".log")
    axs[0, 0].set_ylabel("memória da tabela (KB)")
    h, l = axs[0, 0].get_legend_handles_labels()
    fig.legend(h, l, loc="upper center", ncol=4, bbox_to_anchor=(0.5, 1.04))
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    fig.savefig(os.path.join(FIG, "tabela_memoria.png"), bbox_inches="tight")
    plt.close(fig)
    print("figura tabela_memoria.png")


def main():
    os.makedirs(FIG, exist_ok=True)
    mem = le("memoria.csv")
    pag = le("pagina.csv")

    multiplos(mem, "memoria", "lidas", "páginas lidas (faltas)",
              "faltas_memoria.png")
    multiplos(pag, "pagina", "lidas", "páginas lidas (faltas)",
              "faltas_pagina.png")
    reais = [t for t in traces(mem)
             if t in ("compilador", "simulador", "matriz", "compressor",
                      "localidade", "fases")]
    multiplos(mem, "memoria", "escritas", "páginas escritas",
              "escritas_memoria.png", so=reais)
    tabela_memoria(le("tabela.csv"))


if __name__ == "__main__":
    main()
