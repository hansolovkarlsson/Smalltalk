# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch Smalltalk interpreter/VM, written in C, built incrementally.
Currently at **Milestone 2**: variables, `String`/`Symbol` as real objects,
`#printString`, and cascades layered on top of Milestone 1's object model
and message dispatch — still not a bytecode VM yet (see Roadmap below).

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
`Object` — there's no metaclass yet. Each class has a small array of
`{selector, primitive C function}` pairs; `lookupMethod()` walks
`superclass` chain doing **pointer-equality** comparison on selectors. This
only works because selectors are always interned first. Bootstrapped
classes: `Object`, `UndefinedObject`, `Boolean`/`True`/`False`,
`SmallInteger`, `String`, `Symbol` — no numeric tower (`Magnitude`/`Number`)
yet, no user-defined classes or methods. `Symbol` is **not** a subclass of
`String` (real Smalltalk makes it one) because their C layouts differ; it's
its own direct subclass of `Object`, a deliberate simplification noted in
`class.c`.

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
stderr, evaluates to `nil`), same pattern as an unhandled selector.

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

`nil`/`true`/`false` remain literal pseudo-variables recognized directly by
`parsePrimary()` (not stored in the variable environment); any other bare
identifier parses as `AST_VARIABLE_REF` and is resolved at eval time.

## Roadmap (not yet built)

Milestones are meant to land in roughly this order; each is a deliberately
separate step so the object model and message-send semantics get proven
out before more moving parts (a compiler, a GC) are added on top:

1. Class-definition syntax + user-defined Smalltalk methods (not just C
   primitives) and instance variables.
2. Block literals/closures, non-local return, control flow
   (`ifTrue:ifFalse:`, `whileTrue:`) built from blocks.
3. Garbage collection (mark-sweep).
4. Bytecode compiler + bytecode dispatch loop, replacing the tree-walker —
   this is when it becomes a "real" VM in the classic Smalltalk-80 sense.
