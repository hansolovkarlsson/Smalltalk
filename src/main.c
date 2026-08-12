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

int main(void) {
    bootstrapClasses();

    printf("Smalltalk REPL (milestone 2). Type an expression, or 'quit' to exit.\n");

    char line[1024];
    while (1) {
        printf("st> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
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

    return 0;
}
