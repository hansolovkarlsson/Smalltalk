#include <stdio.h>
#include <string.h>

#include "class.h"
#include "eval.h"
#include "parser.h"
#include "stringobj.h"
#include "symbol.h"

/* Object's default #printString primitive guarantees every class reaches
 * one via the superclass chain, so this always gets back a real String. */
static void printResult(oop result) {
    oop str = sendMessage(result, intern("printString"), NULL, 0);
    printf("%s\n", ((StringObject *)str)->bytes);
}

int main(int argc, char **argv) {
    bootstrapClasses();

    /* With a filename argument, read from that file instead of stdin --
     * lets an example .st file (see examples/) run as `./smalltalk
     * examples/point.st`, not just `./smalltalk < examples/point.st`.
     * Otherwise identical to the interactive REPL: same one-expression-
     * per-line loop, same "st> "-prefixed transcript, so a captured run
     * of an example file reads exactly like a "Try it" doc snippet. */
    FILE *input = stdin;
    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (!input) {
            fprintf(stderr, "error: couldn't open '%s'\n", argv[1]);
            return 1;
        }
    }

    printf("Smalltalk REPL (milestone 4). Type an expression, or 'quit' to exit.\n");

    char line[1024];
    while (1) {
        printf("st> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), input)) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            break;
        }
        if (line[0] == '\0') continue;

        Parser p;
        parserInit(&p, line);
        AstNode *ast = parseExpression(&p);
        if (!ast) {
            printf("parse error: %s\n", p.errorMsg);
            continue;
        }

        oop result = eval(ast);
        printResult(result);
    }

    if (input != stdin) fclose(input);
    return 0;
}
