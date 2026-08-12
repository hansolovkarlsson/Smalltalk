CC = cc
CFLAGS = -std=c11 -Wall -Wextra -g -Isrc

SRC = src/class.c src/symbol.c src/lexer.c src/parser.c src/eval.c src/primitives.c \
      src/stringobj.c src/environment.c
OBJ = $(SRC:.c=.o)

all: smalltalk

smalltalk: src/main.c $(OBJ)
	$(CC) $(CFLAGS) -o smalltalk src/main.c $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: tests/test_main.c $(OBJ)
	$(CC) $(CFLAGS) -o run_tests tests/test_main.c $(OBJ)
	./run_tests

clean:
	rm -rf smalltalk run_tests src/*.o tests/*.o smalltalk.dSYM run_tests.dSYM

.PHONY: all test clean
