#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/class.h"
#include "../src/eval.h"
#include "../src/object.h"
#include "../src/parser.h"
#include "../src/stringobj.h"
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

static void testVariablesAndAssignment(void) {
    oop r1 = evalString("x := 5");
    assert(smallIntegerValue(r1) == 5);

    oop r2 = evalString("x + 1");
    assert(smallIntegerValue(r2) == 6);

    /* right-associative: both a and b end up bound to 3 */
    oop r3 = evalString("a := b := 3");
    assert(smallIntegerValue(r3) == 3);
    assert(smallIntegerValue(evalString("a")) == 3);
    assert(smallIntegerValue(evalString("b")) == 3);

    printf("testVariablesAndAssignment passed\n");
}

static void testStringsAndSymbols(void) {
    oop s = evalString("'hello'");
    assert(strcmp(((StringObject *)s)->bytes, "hello") == 0);

    oop escaped = evalString("'it''s'");
    assert(strcmp(((StringObject *)escaped)->bytes, "it's") == 0);

    oop concatenated = evalString("'foo' , 'bar'");
    assert(strcmp(((StringObject *)concatenated)->bytes, "foobar") == 0);

    assert(evalString("'ab' = 'ab'") == trueObject);
    assert(evalString("'ab' = 'ac'") == falseObject);

    /* Symbols are interned: the same name always yields the same object. */
    assert(evalString("#foo") == evalString("#foo"));

    oop symStr = evalString("#foo asString");
    assert(strcmp(((StringObject *)symStr)->bytes, "foo") == 0);

    printf("testStringsAndSymbols passed\n");
}

static void testCascade(void) {
    /* Every cascaded message targets the ORIGINAL receiver (3), not the
     * intermediate "3 factorial" result: 3 + 1 = 4, then 3 * 2 = 6, and
     * the cascade's value is the last send's result. */
    oop r = evalString("3 factorial; + 1; * 2");
    assert(smallIntegerValue(r) == 6);

    printf("testCascade passed\n");
}

static void testPrintString(void) {
    assert(strcmp(((StringObject *)evalString("3 printString"))->bytes, "3") == 0);
    assert(strcmp(((StringObject *)evalString("nil printString"))->bytes, "nil") == 0);
    assert(strcmp(((StringObject *)evalString("true printString"))->bytes, "true") == 0);
    assert(strcmp(((StringObject *)evalString("'hi' printString"))->bytes, "'hi'") == 0);
    assert(strcmp(((StringObject *)evalString("#foo printString"))->bytes, "#foo") == 0);

    printf("testPrintString passed\n");
}

static void testUserDefinedClasses(void) {
    evalString("Object subclass: #Point instanceVariableNames: 'x y'");
    assert(evalString("Point compile: 'setX: ax setY: ay  x := ax. y := ay. ^self'") == trueObject);
    assert(evalString("Point compile: 'x  ^x'") == trueObject);
    assert(evalString("Point compile: 'y  ^y'") == trueObject);
    assert(evalString("Point compile: '+ aPoint  ^Point new setX: x + aPoint x setY: y + aPoint y'") ==
           trueObject);

    /* A fresh instance's instance variables start out nil, not garbage. */
    assert(evalString("Point new x") == nilObject);

    evalString("p := Point new setX: 3 setY: 4");
    assert(smallIntegerValue(evalString("p x")) == 3);
    assert(smallIntegerValue(evalString("p y")) == 4);

    evalString("q := Point new setX: 1 setY: 2");
    assert(smallIntegerValue(evalString("(p + q) x")) == 4);
    assert(smallIntegerValue(evalString("(p + q) y")) == 6);

    /* Two instances don't share storage. */
    evalString("q setX: 100 setY: 200");
    assert(smallIntegerValue(evalString("p x")) == 3);
    assert(smallIntegerValue(evalString("q x")) == 100);

    printf("testUserDefinedClasses passed\n");
}

static void testSuperAndInheritance(void) {
    evalString("Object subclass: #Animal instanceVariableNames: 'name'");
    evalString("Animal compile: 'setName: n  name := n. ^self'");
    evalString("Animal compile: 'speak  ^name , '' makes a sound'''");
    evalString("Animal subclass: #Dog instanceVariableNames: ''");
    evalString("Dog compile: 'speak  ^super speak , ''! (woof)'''");

    evalString("d := Dog new setName: 'Rex'");
    oop said = evalString("d speak");
    assert(strcmp(((StringObject *)said)->bytes, "Rex makes a sound! (woof)") == 0);

    printf("testSuperAndInheritance passed\n");
}

static void testMethodRedefinition(void) {
    evalString("Object subclass: #Counter instanceVariableNames: ''");
    evalString("Counter compile: 'value  ^1'");
    assert(smallIntegerValue(evalString("Counter new value")) == 1);

    /* Recompiling the same selector replaces it in place -- lookupMethod's
     * front-to-back scan must not keep finding a stale first copy. */
    evalString("Counter compile: 'value  ^2'");
    assert(smallIntegerValue(evalString("Counter new value")) == 2);

    printf("testMethodRedefinition passed\n");
}

static void testReflection(void) {
    assert(strcmp(((StringObject *)evalString("3 class printString"))->bytes, "SmallInteger") == 0);
    assert(strcmp(((StringObject *)evalString("nil class printString"))->bytes, "UndefinedObject") == 0);
    assert(strcmp(((StringObject *)evalString("Object printString"))->bytes, "Object") == 0);
    assert(strcmp(((StringObject *)evalString("Object class printString"))->bytes, "Class") == 0);

    printf("testReflection passed\n");
}

/* Regression test: the REPL reuses one fixed line buffer across inputs.
 * isBinaryChar() used to treat '\0' as a valid selector character (since
 * strchr(set, '\0') always "matches" the set's own terminator), so lexing
 * a short line after a longer one could scan straight past the new '\0'
 * into stale bytes left over from the previous, longer line. */
static void testLexerBufferReuse(void) {
    char buf[64];
    strcpy(buf, "#at:put:");
    Parser p1;
    parserInit(&p1, buf);
    AstNode *a1 = parseExpression(&p1);
    assert(a1 != NULL && !p1.hasError);

    strcpy(buf, "#+"); /* shorter line, same buffer, tail bytes untouched */
    Parser p2;
    parserInit(&p2, buf);
    AstNode *a2 = parseExpression(&p2);
    assert(a2 != NULL && !p2.hasError);
    assert(a2->type == AST_SYMBOL_LITERAL);
    assert(strcmp(a2->as.symbolName, "+") == 0);

    printf("testLexerBufferReuse passed\n");
}

int main(void) {
    testTagging();
    testParsePrecedence();
    testLexerBufferReuse();

    bootstrapClasses();
    testDispatch();
    testEndToEnd();
    testVariablesAndAssignment();
    testStringsAndSymbols();
    testCascade();
    testPrintString();
    testUserDefinedClasses();
    testSuperAndInheritance();
    testMethodRedefinition();
    testReflection();

    printf("All tests passed.\n");
    return 0;
}
