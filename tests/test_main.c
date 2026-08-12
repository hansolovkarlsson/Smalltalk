#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/class.h"
#include "../src/eval.h"
#include "../src/object.h"
#include "../src/parser.h"
#include "../src/symbol.h"

static oop evalString(const char *src) {
    Parser p;
    parserInit(&p, src);
    AstNode *ast = parseExpression(&p);
    assert(ast != NULL);
    return eval(ast);
}

static void testTagging(void) {
    oop a = makeSmallInteger(42);
    assert(oopIsSmallInteger(a));
    assert(smallIntegerValue(a) == 42);

    oop b = makeSmallInteger(-17);
    assert(oopIsSmallInteger(b));
    assert(smallIntegerValue(b) == -17);

    printf("testTagging passed\n");
}

static void testParsePrecedence(void) {
    Parser p;
    parserInit(&p, "3 + 4 factorial");
    AstNode *ast = parseExpression(&p);
    assert(ast != NULL);
    assert(ast->type == AST_BINARY_SEND);
    assert(strcmp(ast->as.binarySend.selector, "+") == 0);
    assert(ast->as.binarySend.arg->type == AST_UNARY_SEND);
    assert(strcmp(ast->as.binarySend.arg->as.unarySend.selector, "factorial") == 0);

    printf("testParsePrecedence passed\n");
}

static void testDispatch(void) {
    oop three = makeSmallInteger(3);
    oop four = makeSmallInteger(4);
    oop args[1];
    args[0] = four;
    oop result = sendMessage(three, intern("+"), args, 1);
    assert(oopIsSmallInteger(result));
    assert(smallIntegerValue(result) == 7);

    printf("testDispatch passed\n");
}

static void testEndToEnd(void) {
    oop r1 = evalString("3 + 4 factorial");
    assert(oopIsSmallInteger(r1));
    assert(smallIntegerValue(r1) == 27); /* 4 factorial = 24, 3 + 24 = 27 */

    oop r2 = evalString("3 < 4");
    assert(r2 == trueObject);

    oop r3 = evalString("3 = 3");
    assert(r3 == trueObject);

    oop r4 = evalString("nil");
    assert(r4 == nilObject);

    oop r5 = evalString("(1 + 2) * 3");
    assert(smallIntegerValue(r5) == 9);

    printf("testEndToEnd passed\n");
}

int main(void) {
    testTagging();
    testParsePrecedence();

    bootstrapClasses();
    testDispatch();
    testEndToEnd();

    printf("All tests passed.\n");
    return 0;
}
