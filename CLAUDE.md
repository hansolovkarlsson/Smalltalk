# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch Smalltalk interpreter/VM, written in C, built incrementally.
Currently at **Milestone 1**: a minimal object model, real message-send
dispatch, and a REPL — not a bytecode VM yet (see Roadmap below).

## Commands

- `make` — build the `smalltalk` REPL binary.
- `make test` — build and run `tests/test_main.c` (assert-based, no external
  framework). Always run this after touching `src/`.
- `make clean` — remove build artifacts (including macOS `.dSYM` bundles).
- `./smalltalk` — run the REPL. Type an expression (e.g. `3 + 4 factorial`)
  and press enter; `quit` or `exit` to leave.

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
  -> main.c: printOop() renders the result
```

**Object representation** (`object.h`): `oop` is a tagged `intptr_t`. Low
bit `1` = `SmallInteger` (value packed in the remaining bits, no heap
allocation). Low bit `0` = pointer to a heap `Object { STClass *isa;
oop fields[]; }`. There is **no garbage collection** — everything is
`malloc`'d and never freed; this is intentional for now, not an oversight.

**Classes** (`class.h`/`class.c`): `STClass` (named to avoid colliding with
the Objective-C runtime's `Class`/`class` on macOS — headers here get
parsed as Objective-C++ by some tooling) is a plain C struct, *not* a heap
`Object` — there's no metaclass yet. Each class has a small array of
`{selector, primitive C function}` pairs; `lookupMethod()` walks
`superclass` chain doing **pointer-equality** comparison on selectors. This
only works because selectors are always interned first.

**Selector interning** (`symbol.h`/`symbol.c`): a simple linear-scan intern
table. Every selector reaching `sendMessage()` — from the parser or from
any other caller — **must** be interned via `intern()` first, or lookup
silently fails (pointer comparison against a non-interned duplicate string
never matches). This bit a test once during development; if you call
`sendMessage()` directly rather than going through `eval()`, remember to
intern the selector.

**Grammar/precedence** (`parser.c`): standard Smalltalk precedence — unary
sends bind tightest and chain left-to-right, then binary sends (all equal
precedence, left-to-right), then keyword sends (lowest, selector parts
concatenated into one, e.g. `at:put:`). The lexer (`lexer.c`) disambiguates
a leading `-` as part of a negative integer literal vs. a binary selector
character using an `expectOperand` flag that tracks whether the previous
token completed an expression.

Bootstrapped classes are just `Object`, `UndefinedObject`, `Boolean`,
`True`, `False`, `SmallInteger` — no numeric tower (`Magnitude`/`Number`)
yet, no `Symbol`/`String` as real objects (selectors are interned C
strings, not Smalltalk objects), no user-defined classes or methods.
`nil`/`true`/`false` are parsed as literal pseudo-variables directly by the
parser (`parsePrimary`), not looked up in any variable environment — there
is no variable environment yet.

## Roadmap (not yet built)

Milestones are meant to land in roughly this order; each is a deliberately
separate step so the object model and message-send semantics get proven
out before more moving parts (a compiler, a GC) are added on top:

1. Variables/assignment, `Symbol`/`String` as real objects, `#printString`,
   cascades.
2. Class-definition syntax + user-defined Smalltalk methods (not just C
   primitives) and instance variables.
3. Block literals/closures, non-local return, control flow
   (`ifTrue:ifFalse:`, `whileTrue:`) built from blocks.
4. Garbage collection (mark-sweep).
5. Bytecode compiler + bytecode dispatch loop, replacing the tree-walker —
   this is when it becomes a "real" VM in the classic Smalltalk-80 sense.
