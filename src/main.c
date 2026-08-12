#include <stdio.h>
#include <string.h>

#include "class.h"
#include "eval.h"
#include "parser.h"

static void printOop(oop o) {
    if (oopIsSmallInteger(o)) {
        printf("%ld\n", smallIntegerValue(o));
        return;
    }
    if (o == nilObject) {
        printf("nil\n");
        return;
    }
    if (o == trueObject) {
        printf("true\n");
        return;
    }
    if (o == falseObject) {
        printf("false\n");
        return;
    }
    printf("a %s\n", classOf(o)->name);
}

int main(void) {
    bootstrapClasses();

    printf("Smalltalk REPL (milestone 1). Type an expression, or 'quit' to exit.\n");

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
        printOop(result);
    }

    return 0;
}
