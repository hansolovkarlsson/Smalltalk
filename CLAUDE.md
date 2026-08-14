# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch Smalltalk interpreter/VM, written in C, built incrementally.
Currently at **Milestone 4**: block literals with real closures, non-local
`^return` through arbitrary call depth, and `ifTrue:ifFalse:`/`whileTrue:`
built *from* blocks rather than special-cased in the evaluator — layered on
top of Milestone 3's user-defined classes/compiled methods, Milestone 2's
variables/String/Symbol/printString/cascades, and Milestone 1's object
model and message dispatch. Still not a bytecode VM yet (see Roadmap
below).

## Commands

- `make` — build `bin/smalltalk`, the REPL binary.
- `make test` — build and run `tests/test_main.c` (assert-based, no external
  framework) as `bin/run_tests`. Always run this after touching `src/`.
- `make clean` — remove build artifacts (`bin/`, including macOS `.dSYM`
  bundles nested inside it, plus `src/*.o`/`tests/*.o`).
- `make examples` — runs every `examples/*.st` file and fails if any of
  them errors; also runnable individually
  (`./bin/smalltalk examples/point.st`) since `main()` accepts an optional
  filename argument, reading from that file instead of stdin.
- `./bin/smalltalk` — run the REPL. Type an expression (e.g.
  `3 + 4 factorial`, `x := 'hello' , ' world'`) and press enter; `quit` or
  `exit` to leave.

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

Four heap layouts exist (`object.h`, `stringobj.h`, `symbol.h`, `block.h`):
`Object { STClass *isa; oop fields[]; }`, `StringObject { STClass *isa;
long length; char bytes[]; }`, `SymbolObject { STClass *isa; const char
*name; }`, and `BlockObject { STClass *isa; paramNames; paramCount;
statements; statementCount; homeActivation; }`. All four start with
`STClass *isa` as their first field, so `classOf()` (`class.c`) can read it
uniformly by casting any heap oop to `Object*` regardless of which concrete
struct it actually points to — deliberate, CPython-style type-punning on a
common initial field, not an accident. A primitive function knows its
receiver's real layout from which class it's registered under (e.g. String
primitives cast to `StringObject*` directly) — primitives do **not**
type-check their arguments before casting.

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
an `Activation { self; homeClass; argNames/argValues; tempNames/tempValues;
lexicalParent; homeMethodActivation; caller; returnPoint; returnValue; }`,
points the file-static `currentActivation` at it, runs the statement list
(via the shared `runStatementSequence()`, see below), and restores the
caller's activation afterward — real recursion falls out of this being an
ordinary C call inside `eval()`, no explicit call stack needed. Unlike
Milestone 3, an `Activation` is now **heap-allocated and never freed**
(`malloc`, matching the project's established "allocate and don't free"
stance) rather than a stack local: a block literal evaluated inside a
method captures a pointer to the currently-running `Activation`
(`BlockObject.homeActivation`, `block.h`), and that block can escape and be
invoked long after the method that created it has returned (stored in a
variable, returned as the method's result, ...) — a stack-local
`Activation` would dangle in exactly that case. Because of this,
`invokeCompiledMethod()`/`invokeBlock()` also **copy** their incoming
`args` into the activation's own storage rather than aliasing the caller's
buffer, which is typically a short-lived local (e.g. `AST_KEYWORD_SEND`'s
`args` in `eval()`) freed right after the call returns.

**Blocks and closures** (`block.h`, `eval.c`): a block literal `[...]`
evaluates (`AST_BLOCK_LITERAL` in `eval()`) to a `BlockObject` — its own
AST (shared across every invocation, never copied) plus
`homeActivation = currentActivation` at the moment the literal was
evaluated. That capture *is* the entire closure mechanism. Invoking a block
(`invokeBlock()`, exposed via `eval.h` for `value`/`value:`/`value:value:`/
`whileTrue:`/`ifTrue:ifFalse:`/etc in `primitives.c`) builds another
`Activation` whose `lexicalParent` is the block's `homeActivation` and
whose `self` is inherited from it (so a block shares `self` with its
enclosing method, transitively through any depth of block nesting).
`activationLookup()`/`activationStore()` (`AST_VARIABLE_REF`/
`AST_ASSIGNMENT` in `eval()`) walk this `lexicalParent` chain — the current
activation's own args/temps, then its parent's, and so on out to the
enclosing method — before falling back to instance variables (checked once
against `currentActivation->self`, since `self` is identical across the
whole chain by construction) and then the global environment. This is what
lets a nested block see an outer block's or the method's variables, not
just its own parameters.

A method's statement list and a block's statement list are both run by the
same `runStatementSequence()`, which differs only in what "fell off the
end without a `^`" answers: a method answers `self` (real Smalltalk
semantics, *not* its last statement's value); a block answers its last
statement's value (or `nil` if empty) — genuinely different defaults, not
just a formatting choice, so the two callers pass different flags rather
than sharing one hardcoded default.

**Non-local return** (`runStatementSequence()`/`invokeCompiledMethod()` in
`eval.c`): `^` always means "return from the nearest *lexically* enclosing
method", never "return from this block" — found by walking `lexicalParent`
up to `homeMethodActivation` (a method activation's own
`homeMethodActivation` is always itself). Because a block can be invoked
from arbitrarily far down the C call stack from where it was defined (e.g.
handed to `whileTrue:`, which calls back into it from a C loop several
frames removed from the original method call), a plain C `return` cannot
unwind that distance. Every `^`, whether written directly in a method's own
top-level statements or nested several blocks deep, therefore does
`longjmp()` back to a `setjmp()` planted once per method activation, in
`invokeCompiledMethod()`, right before running its statement list — even a
`^` at the method's own top level goes through this same `longjmp()` back
to its own immediately-enclosing frame, which is legal C and avoids needing
a separate "am I already at the right frame?" fast path. `^` used where
there's no enclosing method at all (a block defined and run at the REPL
top level) prints `error: '^' used outside a method` and just uses the
value locally instead of jumping — mirroring `self`/`super`'s existing
"used outside a method" error pattern.

**Variable resolution and `super`**: `self`/`super` are dedicated
`AST_SELF`/`AST_SUPER` node types (recognized in `parsePrimary()` alongside
`nil`/`true`/`false`), not ordinary variable refs — both evaluate to
`currentActivation->self`, but a message *sent to* a literal `super`
receiver dispatches differently: `AST_UNARY_SEND`/`AST_BINARY_SEND`/
`AST_KEYWORD_SEND` in `eval.c` each check whether their receiver sub-node
is `AST_SUPER` and, if so, call `dispatchFrom()` starting at
`currentSuperclass()` — `currentActivation->homeMethodActivation->homeClass
->superclass`, i.e. the defining class of the nearest lexically enclosing
*method* (not `currentActivation->homeClass` directly, which is only ever
set on a method activation; a block activation's own `homeClass` is unused
precisely so this indirection is the only path that matters) — instead of
going through plain `sendMessage()`. This means `super` now works correctly
from inside a block written in a method body, e.g. `self do: [super foo]`.
Known gap: cascading directly off a bare `super` receiver (`super foo;
bar`) loses super-ness on every cascaded send after the first, since
`decomposeSend()`/`AST_CASCADE` evaluate the receiver once via plain
`eval()` and dispatch every cascaded message with ordinary `sendMessage()`
— documented inline at the `AST_CASCADE` case rather than threading
super-dispatch through cascade rewriting too.

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
grammar. A block literal `[ (:param)* '|'? statements ]` is parsed as part
of `parsePrimary()` (so it can appear anywhere a literal can, e.g. as a
keyword-send argument to `ifTrue:ifFalse:`); it shares a `parseStatements()`
helper with `parseMethod()`'s own statement-list tail rather than
duplicating that loop, since both are "`.`-separated statements, any of
which may be `^expr`, until some terminator token" — they differ only in
what that terminator is (`TOK_EOF` for a method, `TOK_RBRACKET` for a
block). Block parameters are their own token, `TOK_BLOCK_PARAM` (`:name`,
lexed as a unit in `lexer.c`) — deliberately distinct from a keyword part's
*trailing* colon (`name:`, `TOK_KEYWORD`) so the lexer doesn't need any
parser-side context to tell `[:a | ...]`'s parameter from an ordinary
identifier. A block with no parameters has no leading `:name`s and no `|`
at all (`[ statements ]`); one with parameters requires the `|` separator
(`[:a :b | ...]`) — unlike `parseMethod()`'s temp declarations, block
literals in this milestone have no `| temps |` of their own (a deliberate
scope cut; use the enclosing method's temps instead).

## Roadmap

See `docs/ROADMAP.md` for the milestone plan, current progress, and
example REPL sessions to try for each completed milestone. Narrative
documentation lives under `docs/` (`docs/ROADMAP.md`, `docs/LANGUAGE.md`);
this file (`CLAUDE.md`) stays at the repo root since Claude Code only
auto-loads it from there.
