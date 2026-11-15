CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude
BIN     = bin/simulador
SRC     = src/main.c src/simulador.c src/ferramentas.c
OBJ     = $(SRC:src/%.c=obj/%.o)

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj:
	@mkdir -p obj

clean:
	rm -rf obj bin

.PHONY: all clean
