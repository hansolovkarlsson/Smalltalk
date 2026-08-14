# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch Smalltalk interpreter/VM, written in C, built incrementally.
Currently at **Milestone 3**: user-defined classes and compiled
Smalltalk-level methods (instance variables, `self`/`super`, `^return`),
layered on top of Milestone 2's variables/String/Symbol/printString/cascades
and Milestone 1's object model and message dispatch — still not a bytecode
VM yet (see Roadmap below).

## Commands

- `make` — build the `smalltalk` REPL binary.
- `make test` — build and run `tests/test_main.c` (assert-based, no external
  framework). Always run this after touching `src/`.
- `make clean` — remove build artifacts (including macOS `.dSYM` bundles).
- `./smalltalk` — run the REPL. Type an expression (e.g. `3 + 4 factorial`,
  `x := 'hello' , ' world'`) and press enter; `quit` or `exit` to leave.

There is no single-test filter — `test_main.c` is a handful of sequential
`static void testX(void)` functions called from `main()`; comment out calls
in `main()` to isolate one during debugging.

## Architecture

The evaluator is a **tree-walking interpreter**, not a bytecode VM — this is
a deliberate staging choice (see Roadmap). Execution flow for a REPL line:

```
main.c: read line
  -> lexer.c: lexerNext() tokenizes on demand
  -> parser.c: parseExpression() builds an AstNode tree (ast.h)
  -> eval.c: eval() walks the tree, calling sendMessage() at each send
  -> class.c: sendMessage() -> classOf() + lookupMethod() walk the
     superclass chain for a matching primitive C function
  -> primitives.c: the primitive runs and returns an oop
  -> main.c: printResult() sends #printString and prints the resulting
     String's bytes
```

**Object representation** (`object.h`): `oop` is a tagged `intptr_t`. Low
bit `1` = `SmallInteger` (value packed in the remaining bits, no heap
allocation). Low bit `0` = pointer to a heap object. There is **no garbage
collection** — everything is `malloc`'d and never freed; this is
intentional for now, not an oversight.

Three heap layouts exist (`object.h`, `stringobj.h`, `symbol.h`):
`Object { STClass *isa; oop fields[]; }`, `StringObject { STClass *isa;
long length; char bytes[]; }`, and `SymbolObject { STClass *isa; const char
*name; }`. All three start with `STClass *isa` as their first field, so
`classOf()` (`class.c`) can read it uniformly by casting any heap oop to
`Object*` regardless of which concrete struct it actually points to —
deliberate, CPython-style type-punning on a common initial field, not an
accident. A primitive function knows its receiver's real layout from which
class it's registered under (e.g. String primitives cast to `StringObject*`
directly) — primitives do **not** type-check their arguments before
casting.

**Classes** (`class.h`/`class.c`): `STClass` (named to avoid colliding with
the Objective-C runtime's `Class`/`class` on macOS — headers here get
parsed as Objective-C++ by some tooling) is a plain C struct, *not* a heap
`Object` — there's still no real metaclass hierarchy. Each class has a
growable array of `MethodEntry { selector; kind; fn | compiled; }` — a
method is either a primitive C function or a `CompiledMethod` (an AST-based
body installed by `Class>>compile:`, see below); `lookupMethod()` walks the
`superclass` chain doing **pointer-equality** comparison on selectors, so
selectors reaching it must be interned first. `findOrAddSlot()` in
`class.c` makes `compile:` (and `classAddPrimitive`) replace an existing
selector's entry in place rather than appending a shadowed duplicate —
matters because redefining a method while iterating at the REPL is the
common case, and `lookupMethod()`'s front-to-back scan would otherwise keep
finding the stale first copy forever.

A class still needs to be a valid message receiver (`Point new`, `Object
subclass: #Point instanceVariableNames: 'x y'`), so every `STClass` owns
one `ClassObject { isa; thisClass; }` wrapper (`cls->classOop`) that *is* a
real heap oop — `isa` is always `ClassClass`, a bootstrapped class carrying
the `new`/`subclass:instanceVariableNames:`/`compile:`/`printString`
primitives (`primitives.c`). `registerClass()` creates this wrapper and
also binds the class's name as a global variable (`environment.c`), which
is *the entire mechanism* by which a bare identifier like `Point` resolves
to something sendable — no separate "system dictionary" exists. `classOf()`
on a `ClassObject` returns `ClassClass`, so `Point class printString` is
`'Class'` for every user class alike — there's no per-class metaclass, a
deliberate simplification (real Smalltalk would answer `'Point class'`).

Bootstrapped classes: `Object`, `UndefinedObject`, `Boolean`/`True`/`False`,
`SmallInteger`, `String`, `Symbol`, `Class` — no numeric tower
(`Magnitude`/`Number`) yet. `Symbol` is **not** a subclass of `String` (real
Smalltalk makes it one) because their C layouts differ; it's its own direct
subclass of `Object`, a deliberate simplification noted in `class.c`. User
classes defined via `subclass:instanceVariableNames:` (`defineSubclass()`)
should descend from `Object`, not from `String`/`Symbol`/`SmallInteger`/etc
— `new`/`instantiate()` always allocates the plain `Object { isa;
fields[]; }` layout regardless of superclass, so subclassing a
special-layout class wouldn't preserve that class's real representation.
Each `STClass` carries its own *cumulative* `instanceVarNames`/
`instanceVarCount` (superclass's names first, then its own appended) so a
field index computed against any class in a chain stays valid for every
subclass.

**Selector interning** (`symbol.h`/`symbol.c`): a simple linear-scan intern
table (`intern()`), plus a parallel table of lazily-created `SymbolObject`s
(`internSymbol()`) so that `#foo == #foo` by pointer identity. Every
selector reaching `sendMessage()` — from the parser or from any other
caller — **must** be interned via `intern()` first, or lookup silently
fails. When editing `internIndex()` (the shared growth helper both
`intern()` and `internSymbol()` call), keep the "grow, *then* index" as two
separate statements — `table[internIndex(name)]` in one expression was a
real bug: `internIndex()` can `realloc()` (and move) `table`, and C does
not guarantee `table` is read after the call that moves it, so it could
index through a stale pointer.

**Variables** (`environment.h`/`environment.c`): a flat global table of
`{interned name -> oop}` bindings for the REPL's workspace. `x := expr`
auto-declares `x` if it isn't already bound (see `AST_ASSIGNMENT` in
`eval.c`); referencing an unbound name is a non-fatal error (prints to
stderr, evaluates to `nil`), same pattern as an unhandled selector. This is
also where user-defined classes live: `registerClass()` calls `envSet()` on
the class's name, so `Point` resolves through the exact same lookup path as
any workspace variable — no separate global/system namespace.

**Compiled methods and activations** (`eval.c`): `Class>>compile:`
(`primitives.c`) parses a method source string with `parseMethod()`
(`parser.c`) into a `MethodNode` (pattern + optional `| temps |` + a
`.`-separated statement list, any statement of which may be `^expr`), then
wraps it in a `CompiledMethod` (`class.h`) and installs it via
`classAddCompiledMethod()`. Dispatch (`dispatchFrom()`/`sendMessage()` in
`eval.c`) checks `MethodEntry.kind`: primitives call straight through as
before, compiled methods go through `invokeCompiledMethod()`, which builds
a stack-allocated `Activation { self; homeClass; argNames/argValues;
tempNames/tempValues; caller; }`, points the file-static `currentActivation`
at it, runs the statement list, and restores the caller's activation
afterward — real recursion falls out of this being an ordinary C call
inside `eval()`, no explicit call stack needed. A statement list with no
`^` returns `self` by default (real Smalltalk semantics, *not* the last
statement's value); `^` only ever appears as a top-level statement (the
method grammar has no way to nest one inside another expression), so
"returning" is just "stop the statement loop here" — no non-local-return
machinery (blocks don't exist yet, so there's nothing to return non-locally
*out of* besides the method itself).

**Variable resolution inside a method body** (`activationLookup()`/
`activationStore()` in `eval.c`): for `AST_VARIABLE_REF`/`AST_ASSIGNMENT`,
check the current activation's args, then temps, then `self`'s class's
instance variables (`classOf(currentActivation->self)`, so subclass
overrides still see the right indices — layouts are cumulative, see
above), falling back to the global environment only if none of those match.
`self`/`super` are dedicated `AST_SELF`/`AST_SUPER` node types (recognized
in `parsePrimary()` alongside `nil`/`true`/`false`), not ordinary variable
refs — both evaluate to `currentActivation->self`, but a message *sent to*
a literal `super` receiver dispatches differently: `AST_UNARY_SEND`/
`AST_BINARY_SEND`/`AST_KEYWORD_SEND` in `eval.c` each check whether their
receiver sub-node is `AST_SUPER` and, if so, call `dispatchFrom()` starting
at `currentActivation->homeClass->superclass` (the class the *running*
method was compiled into, not `classOf(self)`) instead of going through
plain `sendMessage()`. Known gap: cascading directly off a bare `super`
receiver (`super foo; bar`) loses super-ness on every cascaded send after
the first, since `decomposeSend()`/`AST_CASCADE` evaluate the receiver once
via plain `eval()` and dispatch every cascaded message with ordinary
`sendMessage()` — documented inline at the `AST_CASCADE` case rather than
threading super-dispatch through cascade rewriting too.

**Grammar/precedence** (`parser.c`): standard Smalltalk precedence — unary
sends bind tightest and chain left-to-right, then binary sends (all equal
precedence, left-to-right), then keyword sends (lowest, selector parts
concatenated into one, e.g. `at:put:`), with assignment (`:=`, right-
associative) as a lookahead wrapping the whole thing and cascades (`;`)
wrapping the message-send grammar. `parseExpression()`'s assignment
lookahead snapshots and restores the whole `Parser` struct (safe: no owned
pointers, `lexer.src` is borrowed) when the identifier turns out not to be
followed by `:=`. Cascades are parsed by first parsing a normal send, then
`decomposeSend()` splits it back into `(receiver, first message)` so
`;`-separated message patterns that follow can all be resent to that same
receiver — see the doc comment there before changing cascade parsing.

The lexer (`lexer.c`) disambiguates a leading `-` as part of a negative
integer literal vs. a binary selector character using an `expectOperand`
flag that tracks whether the previous token completed an expression.
`isBinaryChar()` explicitly excludes `'\0'`: `strchr(set, '\0')` always
"matches" per the C standard (the terminator is considered part of the
search string), so without the explicit exclusion a binary-selector or
symbol-literal scan would run straight past the end of a short string. This
was a real, hard-to-reproduce bug: the REPL reuses one fixed line buffer
across inputs, so a short line parsed right after a longer one could scan
into stale bytes left over past its own `'\0'` — reproduced only through
the *reused-buffer* REPL loop, not through fresh string literals, which is
why `tests/test_main.c`'s regression test for it explicitly reuses one
buffer across two `parserInit()` calls rather than using two separate C
string literals.

`nil`/`true`/`false`/`self`/`super` remain literal pseudo-variables
recognized directly by `parsePrimary()` (not stored in the variable
environment); any other bare identifier parses as `AST_VARIABLE_REF` and is
resolved at eval time. `parseMethod()` (`parser.c`) is a second, separate
top-level entry point alongside `parseExpression()` — it doesn't parse a
normal expression at all, but a method's pattern/temps/statement-list
grammar, reusing `parseExpression()` only for each individual statement (or
each `^`-prefixed one, `TOK_CARET` being the one token `parseExpression()`
itself never consumes).

## Roadmap

See `ROADMAP.md` for the milestone plan, current progress, and example
REPL sessions to try for each completed milestone.
