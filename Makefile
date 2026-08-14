CC = cc
CFLAGS = -std=c11 -Wall -Wextra -g -Isrc

BIN_DIR = bin

SRC = src/class.c src/symbol.c src/lexer.c src/parser.c src/eval.c src/primitives.c \
      src/stringobj.c src/environment.c
OBJ = $(SRC:.c=.o)

all: $(BIN_DIR)/smalltalk

$(BIN_DIR)/smalltalk: src/main.c $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/smalltalk src/main.c $(OBJ)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: tests/test_main.c $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/run_tests tests/test_main.c $(OBJ)
	./$(BIN_DIR)/run_tests

# Runs every examples/*.st file and fails the build if any of them prints
# an "error:"/"parse error:" line -- a cheap regression guard doubling as
# living documentation (see examples/README.md).
examples: $(BIN_DIR)/smalltalk
	@status=0; \
	for f in examples/*.st; do \
		echo "-- $$f --"; \
		out=$$(./$(BIN_DIR)/smalltalk "$$f" 2>&1); \
		echo "$$out" | tail -n +2; \
		if echo "$$out" | grep -qE "error:|parse error:"; then \
			echo "FAILED: $$f produced an error"; \
			status=1; \
		fi; \
	done; \
	exit $$status

clean:
	rm -rf $(BIN_DIR) src/*.o tests/*.o

.PHONY: all test examples clean
