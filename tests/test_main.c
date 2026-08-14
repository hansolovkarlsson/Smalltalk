#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/class.h"
#include "../src/eval.h"
#include "../src/gc.h"
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

static void testBlocks(void) {
    assert(smallIntegerValue(evalString("[3 + 4] value")) == 7);
    assert(smallIntegerValue(evalString("[:a :b | a + b] value: 3 value: 4")) == 7);
    assert(smallIntegerValue(evalString("[:x | x * 2] value: 5")) == 10);

    /* Falls off the end without '^': answers the LAST statement's value,
     * unlike a method (which would default to self). */
    assert(smallIntegerValue(evalString("[1. 2. 3] value")) == 3);

    printf("testBlocks passed\n");
}

static void testClosures(void) {
    evalString("Object subclass: #Adder instanceVariableNames: ''");
    evalString("Adder compile: 'makeAdder: n  ^[:x | x + n]'");

    /* Each call to makeAdder: creates its own activation, so each
     * returned block closes over its OWN n -- proof this is a real
     * closure, not just "read the most recent n" dynamic scoping (the
     * defining activation has already returned by the time these run). */
    evalString("add5 := Adder new makeAdder: 5");
    evalString("add10 := Adder new makeAdder: 10");
    assert(smallIntegerValue(evalString("add5 value: 1")) == 6);
    assert(smallIntegerValue(evalString("add10 value: 1")) == 11);
    assert(smallIntegerValue(evalString("add5 value: 100")) == 105);

    printf("testClosures passed\n");
}

static void testControlFlow(void) {
    assert(strcmp(((StringObject *)evalString("3 < 4 ifTrue: ['yes'] ifFalse: ['no']"))->bytes, "yes") == 0);
    assert(strcmp(((StringObject *)evalString("3 > 4 ifTrue: ['yes'] ifFalse: ['no']"))->bytes, "no") == 0);
    assert(evalString("3 > 4 ifTrue: ['yes']") == nilObject);

    assert(evalString("true and: [false]") == falseObject);
    assert(evalString("true or: [false]") == trueObject);
    /* Short-circuit: the block is never invoked, so a selector that would
     * DNU-error if it ran must not run. */
    assert(evalString("false and: [1 zork]") == falseObject);
    assert(evalString("true or: [1 zork]") == trueObject);

    assert(evalString("true not") == falseObject);
    assert(evalString("false not") == trueObject);

    evalString("n := 0");
    evalString("sum := 0");
    evalString("[n < 5] whileTrue: [sum := sum + n. n := n + 1]");
    assert(smallIntegerValue(evalString("sum")) == 10);

    printf("testControlFlow passed\n");
}

/* '^' inside a block always returns from the *method*, not the block --
 * and must work even when the block is invoked from several dynamic call
 * frames away from that method (here: through whileTrue:'s C-level loop
 * and another block's `value:`), which is exactly the case a naive "just
 * return normally" implementation can't handle and setjmp/longjmp exists
 * for (see eval.c's runStatementSequence()). */
static void testNonLocalReturn(void) {
    evalString("Object subclass: #Finder instanceVariableNames: ''");
    evalString(
        "Finder compile: 'firstOver: limit  "
        "| i | i := 0. "
        "[true] whileTrue: [i := i + 1. i > limit ifTrue: [^i]]'");
    assert(smallIntegerValue(evalString("Finder new firstOver: 41")) == 42);

    evalString("Finder compile: 'callBlock: aBlock  ^aBlock value'");
    evalString("Finder compile: 'earlyOut  self callBlock: [^777]. ^0'");
    assert(smallIntegerValue(evalString("Finder new earlyOut")) == 777);

    printf("testNonLocalReturn passed\n");
}

/* Recursive methods had no way to terminate before ifTrue:ifFalse:
 * existed (see docs/LANGUAGE.md's Known Limitations, pre-Milestone-4).
 * Cross-checked against SmallInteger>>factorial's iterative primitive. */
static void testRecursion(void) {
    evalString("Object subclass: #Math instanceVariableNames: ''");
    evalString("Math compile: 'fact: n  n = 0 ifTrue: [^1]. ^n * (self fact: n - 1)'");
    assert(smallIntegerValue(evalString("Math new fact: 10")) == smallIntegerValue(evalString("10 factorial")));

    evalString("Math compile: 'fib: n  n < 2 ifTrue: [^n]. ^(self fib: n - 1) + (self fib: n - 2)'");
    assert(smallIntegerValue(evalString("Math new fib: 10")) == 55);

    printf("testRecursion passed\n");
}

/* Garbage that's never bound to anything should actually go away. This is
 * inherently a little fuzzy for a *conservative* collector (see CLAUDE.md):
 * a stale copy of a pointer can linger in an not-yet-overwritten C stack
 * slot and keep something alive one collection longer than a precise GC
 * would. So this checks for a substantial drop, not that every last object
 * is gone -- allocating a few hundred throwaway strings makes "mostly
 * accidentally still alive" implausible without it being a real bug. */
static void testGCReclaimsGarbage(void) {
    gcSetThreshold((size_t)-1); /* effectively "never auto-collect"; we trigger by hand below */
    for (int i = 0; i < 500; i++) {
        evalString("'this string is garbage and nothing keeps a reference to it'");
    }
    size_t before = gcLiveCount();
    gcCollectNow();
    size_t after = gcLiveCount();
    assert(after < before / 2);

    printf("testGCReclaimsGarbage passed\n");
}

static void testGCKeepsLiveVariable(void) {
    evalString("x := 'keep me around'");
    gcCollectNow();
    oop x = evalString("x");
    assert(strcmp(((StringObject *)x)->bytes, "keep me around") == 0);

    printf("testGCKeepsLiveVariable passed\n");
}

/* A live closure must survive a real collection cycle -- both the
 * BlockObject itself (reachable from the workspace variable it's bound
 * to) and, transitively, the Activation it closed over (reachable only
 * via the block's homeActivation, not any variable directly). */
static void testGCKeepsLiveClosure(void) {
    evalString("Object subclass: #Adder2 instanceVariableNames: ''");
    evalString("Adder2 compile: 'makeAdder: n  ^[:x | x + n]'");
    evalString("add7 := Adder2 new makeAdder: 7");
    gcCollectNow();
    assert(smallIntegerValue(evalString("add7 value: 3")) == 10);
    gcCollectNow();
    assert(smallIntegerValue(evalString("add7 value: 100")) == 107);

    printf("testGCKeepsLiveClosure passed\n");
}

/* Regression test for a real bug: a message send's evaluated arguments
 * (eval.c's AST_KEYWORD_SEND) are only ever reachable via conservative
 * stack scanning, never a precise root -- so when Block>>whileTrue:
 * holds onto its body-block argument across many iterations, a collection
 * triggered mid-loop must still find it (via GC_KIND_OOP_ARRAY, see
 * gc.h). Before that fix, the body block could be swept out from under a
 * running loop, corrupting memory. A very low threshold forces many
 * collections during this loop's ~200 iterations. */
static void testGCDuringLoopWithBlockArgument(void) {
    gcSetThreshold(256); /* tiny: forces a collection on almost every iteration */
    evalString("Object subclass: #Adder3 instanceVariableNames: ''");
    evalString("Adder3 compile: 'makeAdder: n  ^[:x | x + n]'");
    evalString("keepAlive := Adder3 new makeAdder: 5");
    evalString("n := 0");
    evalString("sum := 0");
    evalString("[n < 200] whileTrue: [b := [:x | x + n]. sum := sum + (b value: 1). n := n + 1]");
    assert(smallIntegerValue(evalString("sum")) == 200 + (199 * 200) / 2);
    assert(smallIntegerValue(evalString("keepAlive value: 100")) == 105);
    gcSetThreshold(64 * 1024);

    printf("testGCDuringLoopWithBlockArgument passed\n");
}

/* Same idea as above but for recursive method calls rather than a loop:
 * a very low threshold forces several collections while many Activations
 * (one per pending recursive call, see CLAUDE.md) are simultaneously
 * "in progress" on the C stack, reachable only via the current
 * Activation's ->caller chain. If that chain weren't a correct root, a
 * mid-recursion collection would free an activation a pending call still
 * needs, corrupting the computation -- so a correct final answer is a
 * strong proxy for "nothing live was collected". */
static void testGCDuringRecursion(void) {
    gcSetThreshold(256);
    evalString("Object subclass: #Math2 instanceVariableNames: ''");
    evalString("Math2 compile: 'fact: n  n = 0 ifTrue: [^1]. ^n * (self fact: n - 1)'");
    assert(smallIntegerValue(evalString("Math2 new fact: 12")) == 479001600);
    gcSetThreshold(64 * 1024);

    printf("testGCDuringRecursion passed\n");
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
    int gcStackBottomMarker;
    gcInit(&gcStackBottomMarker);

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
    testBlocks();
    testClosures();
    testControlFlow();
    testNonLocalReturn();
    testRecursion();
    testGCReclaimsGarbage();
    testGCKeepsLiveVariable();
    testGCKeepsLiveClosure();
    testGCDuringLoopWithBlockArgument();
    testGCDuringRecursion();

    printf("All tests passed.\n");
    return 0;
}
