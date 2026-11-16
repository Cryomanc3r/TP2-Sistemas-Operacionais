#!/usr/bin/env python3
"""
Gera traces sintéticos de acesso à memória no mesmo formato dos logs
fornecidos (endereço hexadecimal de 32 bits + R/W por linha).

Cada padrão foi pensado para isolar um comportamento dos algoritmos:

  sequencial.log  varredura linear repetida de uma região maior que a
                  memória (laço cíclico) -> pior caso clássico do LRU.
  aleatorio.log   endereços uniformes num espaço grande -> sem
                  localidade; todos os algoritmos devem empatar.
  localidade.log  80% dos acessos num conjunto "quente" pequeno e 20%
                  espalhados (regra 80/20) -> favorece LRU/LFU/CLOCK.
  fases.log       o conjunto quente muda de região a cada fase ->
                  penaliza o LFU, que guarda frequências antigas.

Uso: python3 testes/gerar_traces.py [diretorio_saida] [n_acessos]
A semente é fixa para que os arquivos sejam reproduzíveis.
"""
import os
import random
import sys

KB = 1024
PAGE = 4 * KB


def rw(p_write):
    return "W" if random.random() < p_write else "R"


def sequencial(n, regiao=640 * KB, base=0x10000000):
    # Percorre a região de 64 em 64 bytes, recomeçando ao chegar no fim.
    passo = 64
    for i in range(n):
        yield base + (i * passo) % regiao, rw(0.3)


def aleatorio(n, espaco=64 * 1024 * KB, base=0x20000000):
    for _ in range(n):
        yield base + random.randrange(0, espaco, 4), rw(0.3)


def localidade(n, quente=96 * KB, frio=32 * 1024 * KB, base=0x30000000):
    for _ in range(n):
        if random.random() < 0.8:
            yield base + random.randrange(0, quente, 4), rw(0.4)
        else:
            yield base + quente + random.randrange(0, frio, 4), rw(0.2)


def fases(n, n_fases=10, quente=96 * KB, base=0x40000000):
    # Cada fase usa um conjunto quente diferente, distante do anterior.
    por_fase = n // n_fases
    for i in range(n):
        f = min(i // por_fase, n_fases - 1)
        inicio = base + f * 4 * 1024 * KB
        if random.random() < 0.9:
            yield inicio + random.randrange(0, quente, 4), rw(0.4)
        else:
            yield base + random.randrange(0, 64 * 1024 * KB, 4), rw(0.2)


def main():
    saida = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 1_000_000
    os.makedirs(saida, exist_ok=True)

    for nome, gen in [("sequencial", sequencial), ("aleatorio", aleatorio),
                      ("localidade", localidade), ("fases", fases)]:
        random.seed(2025)
        caminho = os.path.join(saida, nome + ".log")
        with open(caminho, "w") as f:
            for addr, op in gen(n):
                f.write(f"{addr & 0xffffffff:08x} {op}\n")
        print("gerado", caminho)


if __name__ == "__main__":
    main()
