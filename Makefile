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

# Runs every examples/*.st file and fails the build if any of them prints
# an "error:"/"parse error:" line -- a cheap regression guard doubling as
# living documentation (see examples/README.md).
examples: smalltalk
	@status=0; \
	for f in examples/*.st; do \
		echo "-- $$f --"; \
		out=$$(./smalltalk "$$f" 2>&1); \
		echo "$$out" | tail -n +2; \
		if echo "$$out" | grep -qE "error:|parse error:"; then \
			echo "FAILED: $$f produced an error"; \
			status=1; \
		fi; \
	done; \
	exit $$status

clean:
	rm -rf smalltalk run_tests src/*.o tests/*.o smalltalk.dSYM run_tests.dSYM

.PHONY: all test examples clean
