#!/usr/bin/env python3
"""
Roda a bateria de experimentos do relatório e grava os resultados em CSV.

  memoria.csv  páginas de 4 KB, memória de 128 KB a 16 MB
  pagina.csv   memória de 1 MB, páginas de 2 KB a 64 KB
  tabela.csv   custo de memória e de consulta das 4 tabelas de páginas
  seeds.csv    variação do Random com 10 sementes (dispersão)

Uso (a partir da raiz do projeto, depois de `make`):
  python3 experimentos/rodar.py
Todo arquivo *.log na raiz e em testes/ entra nos experimentos.
"""
import csv
import glob
import os
import re
import subprocess

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(RAIZ, "bin", "simulador")
OUT = os.path.join(RAIZ, "experimentos", "resultados")

ALGOS = ["random", "lru", "lfu", "clock"]
PTS = ["dense", "2level", "3level", "inverted"]
MEMS = [128, 256, 512, 1024, 2048, 4096, 8192, 16384]
PAGES = [2, 4, 8, 16, 32, 64]
MEM_FIXA = 1024

CAMPOS = {
    "acessos": r"Total de acessos à memória: (\d+)",
    "lidas": r"Páginas lidas: (\d+)",
    "escritas": r"Páginas escritas: (\d+)",
    "compulsorias": r"Faltas compulsórias: (\d+)",
    "refs_consulta": r"Entradas lidas por consulta: ([\d.]+)",
    "tabelas": r"Tabelas alocadas: (\d+)",
    "pt_kb": r"Memória da tabela de páginas: ([\d.]+) KB",
    "tempo": r"Tempo de simulação: ([\d.]+) s",
}


def traces():
    logs = sorted(glob.glob(os.path.join(RAIZ, "*.log")))
    logs += sorted(glob.glob(os.path.join(RAIZ, "testes", "*.log")))
    return [l for l in logs if not l.endswith("pequeno.log")]


def roda(algo, log, page, mem, *extra):
    cmd = [BIN, algo, log, str(page), str(mem), "stats", *extra]
    txt = subprocess.run(cmd, capture_output=True, text=True,
                         check=True).stdout
    r = {k: re.search(p, txt).group(1) for k, p in CAMPOS.items()}
    r.update(trace=os.path.basename(log)[:-4], algo=algo, pagina=page,
             memoria=mem)
    return r


def grava(nome, linhas):
    with open(os.path.join(OUT, nome), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0].keys()))
        w.writeheader()
        w.writerows(linhas)
    print("gravado", nome, len(linhas), "linhas")


def main():
    os.makedirs(OUT, exist_ok=True)
    logs = traces()

    grava("memoria.csv", [roda(a, l, 4, m)
                          for l in logs for a in ALGOS for m in MEMS])
    grava("pagina.csv", [roda(a, l, p, MEM_FIXA)
                         for l in logs for a in ALGOS for p in PAGES])

    # Tabelas: o algoritmo não muda o custo da tabela; usa LRU.
    # Repete 5 vezes e guarda a menor medida de tempo (menos ruído).
    linhas = []
    for l in logs:
        for p in PAGES:
            for pt in PTS:
                runs = [roda("lru", l, p, MEM_FIXA, "pt=" + pt)
                        for _ in range(5)]
                r = min(runs, key=lambda x: float(x["tempo"]))
                r["pt"] = pt
                linhas.append(r)
    grava("tabela.csv", linhas)

    grava("seeds.csv", [dict(roda("random", l, 4, m, f"seed={s}"), seed=s)
                        for l in logs for m in MEMS for s in range(1, 11)])


if __name__ == "__main__":
    main()
