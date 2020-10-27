CC=gcc
CFLAGS= -std=c99 -W -Wall -pedantic
LDFLAGS=
EXEC=main
SRC= chunk.c compiler.c debug.c main.c memory.c scanner.c value.c vm.c
OBJ= $(SRC:.c=.o)

all: $(EXEC)

main: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: clean mrpropre

clean:
	rm -rf *.o

mrpropre: clean
	rm -rf $(EXEC)
