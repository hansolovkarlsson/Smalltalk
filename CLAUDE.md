# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch Smalltalk interpreter/VM, written in C, built incrementally.
Currently at **Milestone 6**: a real bytecode compiler and dispatch-loop
VM, replacing the tree-walking evaluator every earlier milestone was built
on — layered on top of Milestone 5's mark-sweep garbage collector,
Milestone 4's block literals/closures/non-local return, Milestone 3's
user-defined classes/compiled methods, Milestone 2's variables/String/
Symbol/printString/cascades, and Milestone 1's object model and message
dispatch. This is the last milestone on the original roadmap (see below)
— a "real" VM in the classic Smalltalk-80 sense.

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

As of Milestone 6, this is a genuine **compile-then-run** VM, not a
tree-walking interpreter: parsing still builds an `AstNode` tree exactly as
before, but nothing ever walks that tree directly anymore -- it's compiled
once into a flat bytecode instruction sequence (`bytecode.h`, `compiler.c`),
which then runs on an explicit dispatch loop with its own operand stack
(`eval.c`'s `vmRun()`). Execution flow for a REPL line:

```
main.c: read line
  -> lexer.c: lexerNext() tokenizes on demand
  -> parser.c: parseExpression() builds an AstNode tree (ast.h)
  -> compiler.c: compileTopLevelExpression() walks the tree ONCE,
     emitting a flat CompiledCode instruction sequence (bytecode.h)
  -> eval.c: vmRun() fetches and executes one Instr at a time against
     an explicit operand stack, calling sendMessage() at each OP_SEND
  -> class.c: sendMessage() -> classOf() + lookupMethod() walk the
     superclass chain for a matching primitive C function OR a
     CompiledMethod, whose own bytecode runs on a nested vmRun() call
  -> primitives.c: a primitive runs and returns an oop directly
  -> main.c: printResult() sends #printString and prints the resulting
     String's bytes
```

A user-defined method's or block's body is compiled exactly once (when
`Class>>compile:` runs, or when the block literal's enclosing code is
compiled) and its `CompiledCode` reused on every subsequent call -- unlike
the old tree-walking evaluator, which re-walked the same `AstNode`s from
scratch on every single invocation. See "Bytecode compiler and VM dispatch
loop" below for the whole mechanism.

**Object representation** (`object.h`): `oop` is a tagged `intptr_t`. Low
bit `1` = `SmallInteger` (value packed in the remaining bits, no heap
allocation). Low bit `0` = pointer to a heap object. As of Milestone 5,
heap objects are **garbage collected** (`gc.c`) — see the dedicated section
below.

Four heap layouts exist (`object.h`, `stringobj.h`, `symbol.h`, `block.h`):
`Object { STClass *isa; oop fields[]; }`, `StringObject { STClass *isa;
long length; char bytes[]; }`, `SymbolObject { STClass *isa; const char
*name; }`, and `BlockObject { STClass *isa; paramNames; paramCount; code;
homeActivation; }` (`code` is a `CompiledCode *`, bytecode.h — see below).
All four start with `STClass *isa` as their first field, so `classOf()`
(`class.c`) can read it uniformly by casting any heap oop to `Object*`
regardless of which concrete struct it actually points to — deliberate,
CPython-style type-punning on a common initial field, not an accident. A
primitive function knows its receiver's real layout from which class it's
registered under (e.g. String primitives cast to `StringObject*` directly)
— primitives do **not** type-check their arguments before casting.

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

**Compiled methods and activations** (`class.h`, `eval.c`): `Class>>compile:`
(`primitives.c`) parses a method source string with `parseMethod()`
(`parser.c`) into a `MethodNode` (pattern + optional `| temps |` + a
`.`-separated statement list, any statement of which may be `^expr`), then
compiles that statement list to bytecode with `compiler.c`'s
`compileMethodBody()`, wraps the result in a `CompiledMethod { argNames/
argCount; tempNames/tempCount; code; homeClass; }` (`class.h`), and
installs it via `classAddCompiledMethod()`. Dispatch (`dispatchFrom()`/
`sendMessage()` in `eval.c`) checks `MethodEntry.kind`: primitives call
straight through as before, compiled methods go through
`invokeCompiledMethod()`, which builds an `Activation { self; homeClass;
argNames/argValues; tempNames/tempValues; lexicalParent;
homeMethodActivation; caller; returnPoint; returnValue; }`, points the
file-static `currentActivation` at it, runs the method's `CompiledCode` on
`vmRun()` (see below), and restores the caller's activation afterward —
real recursion falls out of this being an ordinary (nested) C call inside
`vmRun()`'s own `OP_SEND` handler, no explicit call stack data structure
needed. An `Activation` is **heap-allocated** (`gcAlloc(GC_KIND_
ACTIVATION, ...)`, `activation.h`) rather than a stack local: a block
literal evaluated inside a method captures a pointer to the
currently-running `Activation` (`BlockObject.homeActivation`, `block.h`),
and that block can escape and be invoked long after the method that
created it has returned (stored in a variable, returned as the method's
result, ...) — a stack-local `Activation` would dangle in exactly that
case. Because of this, `invokeCompiledMethod()`/`invokeBlock()` also
**copy** their incoming `args` into the activation's own storage rather
than aliasing the caller's buffer, which is a slice of the *caller's own*
`vmRun()` operand stack — perfectly valid for the duration of the call,
but not something an `Activation` that might outlive that call should hold
a raw alias into. An `Activation` that's no longer reachable is actually
reclaimed by the garbage collector, not just leaked — see below.

**Blocks and closures** (`block.h`, `eval.c`): a block literal `[...]`
compiles (`compiler.c`'s `compileExpr()`, `AST_BLOCK_LITERAL` case) to an
`OP_PUSH_BLOCK` instruction carrying a `BlockTemplate { paramNames;
paramCount; code; }` (bytecode.h) — the block's own body, compiled exactly
once, alongside whichever method/block/top-level expression it's written
inside. Executing that instruction (`vmRun()`'s `OP_PUSH_BLOCK` case)
builds a `BlockObject` from the template plus
`homeActivation = currentActivation` at that moment — that capture *is*
the entire closure mechanism, unchanged in spirit from before this
milestone, just now built from a shared, precompiled `CompiledCode`
instead of a shared `AstNode` subtree. Invoking a block (`invokeBlock()`,
exposed via `eval.h` for `value`/`value:`/`value:value:`/`whileTrue:`/
`ifTrue:ifFalse:`/etc in `primitives.c`) builds another `Activation` whose
`lexicalParent` is the block's `homeActivation` and whose `self` is
inherited from it (so a block shares `self` with its enclosing method,
transitively through any depth of block nesting), then runs the block's
`CompiledCode` on `vmRun()` too. `activationLookup()`/`activationStore()`
(called from `vmRun()`'s `OP_PUSH_VAR`/`OP_STORE_VAR`) walk this
`lexicalParent` chain — the current activation's own args/temps, then its
parent's, and so on out to the enclosing method — before falling back to
instance variables (checked once against `currentActivation->self`, since
`self` is identical across the whole chain by construction) and then the
global environment. This is what lets a nested block see an outer block's
or the method's variables, not just its own parameters. Variable names are
still resolved *dynamically*, by interned-pointer comparison at every
`OP_PUSH_VAR`/`OP_STORE_VAR`, exactly as the old tree-walking evaluator did
at every `AST_VARIABLE_REF`/`AST_ASSIGNMENT` — this milestone deliberately
did **not** also add compile-time-resolved variable *indices* (a further,
separable optimization left for the future; see Known simplifications
below).

A method body and a block body compile to bytecode that differs only in
what happens when execution falls off the end without hitting an
`OP_RETURN`: `compiler.c`'s `compileStatements()` (shared by
`compileMethodBody()`/`compileBlockBody()`/`compileTopLevelExpression()`)
appends a trailing `OP_POP; OP_PUSH_SELF` after a method body's last
statement (so falling off the end answers `self`, real Smalltalk
semantics, *not* the last statement's value) but leaves a block/top-level
body's last statement's value on the stack untouched (so falling off the
end answers *that* — `OP_PUSH_NIL` only if the body is empty). This is a
genuinely different default, not just a formatting choice, decided once at
*compile* time now rather than by a runtime flag on every call the way the
old `runStatementSequence(..., useLastAsDefault)` worked.

**Non-local return** (`vmRun()`'s `OP_RETURN` case, `eval.c`): `^` always
means "return from the nearest *lexically* enclosing method", never
"return from this block" — found by walking `lexicalParent` up to
`homeMethodActivation` (a method activation's own `homeMethodActivation` is
always itself). Because a block can be invoked from arbitrarily far down
the C call stack from where it was defined (e.g. handed to `whileTrue:`,
which calls back into it from a C loop several frames removed from the
original method call), a plain C `return` cannot unwind that distance.
Every `^`, whether written directly in a method's own top-level statements
or nested several blocks deep, compiles to an `OP_RETURN` that does
`longjmp()` back to a `setjmp()` planted once per method activation, in
`invokeCompiledMethod()`, right before running its `CompiledCode` — even a
`^` at the method's own top level goes through this same `longjmp()` back
to its own immediately-enclosing frame, which is legal C and avoids needing
a separate "am I already at the right frame?" fast path. `^` used where
there's no enclosing method at all (a block defined and run at the REPL
top level) prints `error: '^' used outside a method` and just leaves the
value on `vmRun()`'s own operand stack instead of jumping — mirroring
`self`/`super`'s existing "used outside a method" error pattern.

**Bytecode compiler and VM dispatch loop** (`bytecode.h`, `compiler.c`,
`eval.c`'s `vmRun()`): `Instr { Opcode op; union { ... } operand; }`
(bytecode.h) is deliberately a *struct array*, not packed bytes — real
operand encoding (varints, alignment, endianness) is complexity orthogonal
to this milestone's actual architectural goal, a dispatch loop over
compiled code instead of walking an AST, so this project's usual
"deliberate simplification" pattern applies here too. `compiler.c`'s
`compileExpr()` is a straightforward recursive walk covering every
`AstNodeType` (ast.h) exactly once, each emitting a short, fixed sequence
of instructions; `compileSendArgsAndEmit()` is shared by unary/binary/
keyword sends (they differ only in argument count) and handles `super`
receivers by simply never compiling/pushing one (`OP_SEND_SUPER`'s operand
stack only ever holds `[arg1..argN]`, no receiver slot — `vmRun()` always
supplies `currentActivation->self` directly as the super-send receiver).
Cascades compile to `OP_DUP` before every message but the last (to keep a
copy of the receiver around for the next one) and `OP_POP` after every
result but the last (which is left as the cascade's own value) — no
`OP_SWAP` needed, since not `DUP`-ing before the *final* message
naturally consumes the last remaining receiver copy. String/Symbol
literals compile to `OP_PUSH_STRING_LITERAL`/`OP_PUSH_SYMBOL_LITERAL`
carrying the *raw name*, constructed fresh at `vmRun()` time via
`makeString()`/`internSymbol()`, rather than being precomputed once at
compile time the way a SmallInteger literal is (`OP_PUSH_LITERAL`, safe
because `makeSmallInteger()` never allocates) — a `gcAlloc`'d oop
embedded directly in permanently-retained, never-GC-traced `CompiledCode`
would become invisible to the collector the instant compilation finished,
so literal *heap* objects have to be (re)created at a point where they're
immediately visible on `vmRun()`'s own operand stack instead.

`vmRun(CompiledCode *code)` is a straight fetch-decode-execute loop over
`code->instrs`, with a fixed-size `oop stack[VM_STACK_MAX]` **C-local
array** as its operand stack (bounds-checked via the `VM_PUSH()` macro,
`#undef`'d at the end of the function; a would-be overflow degrades to a
graceful stderr-and-`nil` error, this codebase's usual convention, rather
than corrupting memory past the array). Being a genuine C-stack-resident
array is what makes `stack` automatically visible to `gc.c`'s conservative
stack scanning for as long as this call (and any nested `vmRun()` calls it
triggers via `OP_SEND`) remains on the C call stack — the exact same
mechanism that already covered the old tree-walking evaluator's transient
`oop receiver = eval(...)`-style locals, so garbage collection needed *no*
new design to accommodate the bytecode rewrite. This is also why
`GC_KIND_OOP_ARRAY` (Milestone 5's fix for message-send arguments living
in a *separately heap-malloc'd* buffer conservative scanning couldn't see
into) is gone as of this milestone: `OP_SEND`'s `args` now point directly
into `vmRun()`'s own stack-resident array, so the bug that fix addressed
can't occur by construction anymore — see gc.h's `GCKind` doc comment and
the Roadmap entry below for that history.

**Known simplification, left for the future**: variables are still
resolved by name at every `OP_PUSH_VAR`/`OP_STORE_VAR` (dynamic,
interned-pointer-equality lookup through `activationLookup()`/
`activationStore()`'s lexical chain walk), not by a compile-time-computed
slot index the way a "real" bytecode VM's variable access typically works.
Adding that would mean `compiler.c` tracking a full lexical scope model
(which names are this frame's own args/temps vs. an outer block's vs. an
instance variable vs. global) at compile time — a legitimate, separable
future optimization, deliberately out of scope for the milestone that
introduced the bytecode format and dispatch loop themselves.

**Variable resolution and `super`**: `self`/`super` are dedicated
`AST_SELF`/`AST_SUPER` node types (recognized in `parsePrimary()` alongside
`nil`/`true`/`false`), not ordinary variable refs — both compile to
`OP_PUSH_SELF` (`compiler.c`), since a bare `super` (not itself a send's
receiver) evaluates to `currentActivation->self` exactly like `self` does.
A message *sent to* a literal `super` receiver compiles differently:
`compileSendArgsAndEmit()` checks whether the receiver `AstNode` is
`AST_SUPER` and, if so, emits `OP_SEND_SUPER` instead of `OP_SEND` (and
never compiles/pushes a receiver value at all for it — see the bytecode
section above). `vmRun()`'s `OP_SEND_SUPER` case dispatches via
`dispatchFrom()` starting at `currentSuperclass()` —
`currentActivation->homeMethodActivation->homeClass->superclass`, i.e. the
defining class of the nearest lexically enclosing *method* (not
`currentActivation->homeClass` directly, which is only ever set on a
method activation; a block activation's own `homeClass` is unused
precisely so this indirection is the only path that matters) — instead of
going through plain `sendMessage()`. This means `super` works correctly
from inside a block written in a method body, e.g. `self do: [super foo]`.
Known gap, unchanged by this milestone: cascading directly off a bare
`super` receiver (`super foo; bar`) loses super-ness on every cascaded
send after the first, since `compileExpr()`'s `AST_CASCADE` case compiles
the receiver once via plain `OP_SEND` and dispatches every cascaded
message the same way — documented inline there rather than threading
super-dispatch through cascade compilation too, matching how the old
tree-walking evaluator's `AST_CASCADE` case handled (or rather, didn't
handle) the same case.

**Garbage collection** (`gc.h`/`gc.c`): a stop-the-world mark-sweep
collector. Every `gcAlloc(kind, size)`'d block gets a `GCHeader { next;
size; kind; marked; }` prepended to it (invisible to callers — the pointer
handed back is `header + 1`, so `(oop)` casts and `Object`-layout
assumptions elsewhere are unaffected); headers form one intrusive linked
list (`allocList`), walked in full by both mark and sweep. `GCKind` has two
values: `GC_KIND_OOP` (an `Object`/`StringObject`/`SymbolObject`/
`ClassObject`/`BlockObject`, traced via `isa` exactly like `classOf()`
does) and `GC_KIND_ACTIVATION` (an `activation.h` `Activation`, traced via
its own known field layout) — a third kind, `GC_KIND_OOP_ARRAY`, existed
through Milestone 5 and is gone as of this one; see the bytecode section
above for why the bug it fixed can't occur anymore. Every previously-
`malloc`'d oop-producing site (`class.c`'s `instantiate()`/
`registerClass()`, `stringobj.c`'s `makeStringN()`, `symbol.c`'s
`internSymbol()`, `eval.c`'s `vmRun()` `OP_PUSH_BLOCK` case, and
`invokeCompiledMethod()`/`invokeBlock()`'s `Activation` allocations) goes
through `gcAlloc()`. `STClass`, `CompiledMethod`, `MethodNode`/`AstNode`
parse trees, and (as of this milestone) `CompiledCode`/`BlockTemplate`
(bytecode.h) and the intern table's `char*` names all stay plain
`malloc`'d and permanent, on purpose: class/method/bytecode/AST metadata
is never freed even by a real Smalltalk (short of an explicit "remove this
class" feature this VM doesn't have), and none of them ever hold an `oop`
value that would need tracing directly — a `CompiledCode`'s `Instr`s
reference oops only indirectly, via `OP_PUSH_LITERAL` (a SmallInteger,
never heap-allocated) or `OP_PUSH_STRING_LITERAL`/`OP_PUSH_SYMBOL_LITERAL`
(a raw name, turned into a real oop only at `vmRun()` time — see the
bytecode section above for why that distinction is load-bearing, not
stylistic).

**Roots**: `gcCollectNow()` marks, in order: `nilObject`/`trueObject`/
`falseObject` (not bound in the environment — they're parser-level literal
pseudo-variables, never `envSet()`); `envMarkRoots()` (`environment.c`,
every workspace variable's value — this is also how a user-defined class
survives, since `registerClass()` binds its `classOop` here too);
`symbolMarkRoots()` (`symbol.c`, every interned Symbol, which is why a
Symbol is in practice never collected, same as a real Smalltalk's
SymbolTable); `gcMarkActivation(evalCurrentActivation())` (`eval.c`
exposes its file-static `currentActivation`, whose `->caller` chain
transitively covers every method/block call currently in progress on the
C stack, and whose `BlockObject.homeActivation` reachability covers
escaped closures); and finally a **conservative scan** of the C stack and
registers (below). `gcMarkOop()`/the internal `gcMarkActivation()` are the
precise, recursive tracers both this list and ordinary field-tracing
(`Object.fields[]`, `Activation.argValues`/`tempValues`/`lexicalParent`/
`caller`/`homeMethodActivation`) funnel through.

**Conservative stack/register scanning** (`scanConservativeRoots()`): the
one genuinely tricky part. It predates Milestone 6's bytecode rewrite --
originally needed because the tree-walking evaluator kept live oops in
ordinary C locals scattered across however many `eval()` frames were
currently nested (e.g. `oop receiver = eval(...)`, computed but not yet
stored anywhere a precise root walk would find it) -- and the bytecode
VM's `vmRun()` has the exact same characteristic by a different mechanism:
its `oop stack[VM_STACK_MAX]` operand stack is itself just a C local, with
values pushed onto it having no other pointer to them (see the bytecode
section above). There is no shadow stack in either design, and adding one
would be a much larger, more invasive undertaking than either milestone's
scope. `setjmp()`
spills callee-saved registers into a local `jmp_buf`, which then gets
scanned as part of the stack region alongside everything from
`__builtin_frame_address(0)` (approximately "the innermost live frame
right now") up to `stackBottom` (an address captured once, early in
`main()`/`tests/test_main.c`'s `main()`, via `gcInit()` — **every**
executable entry point must call this before anything can allocate, or
the first collection scans from a null/garbage base and reads unmapped
memory). Unlike a Boehm-style collector, which must *guess* whether a
stack word "looks like" a valid heap pointer (any properly-aligned address
landing inside a live allocation's bounds counts, false positives
accepted as the price of not walking off into unmapped memory), this
collector builds a `HeaderSet` — a hash set of every currently-live
`GCHeader*`, rebuilt fresh each collection *before* sweeping — so a
candidate stack word is checked for *exact* membership in O(1) average.
This makes the conservative scan fully precise (no false-positive
retention from a coincidental bit pattern), at the cost of an O(heap) hash
set rebuild per collection — an accepted tradeoff at this VM's scale.
`candidateHeaderOf()` deliberately computes the candidate's header address
via `uintptr_t` arithmetic rather than real pointer arithmetic: forming an
out-of-bounds *pointer* from an unvalidated bit pattern (even one that's
merely computed, never dereferenced) is undefined behavior in C, caught by
`-fsanitize=undefined` during this milestone's own testing — the
`uintptr_t` version produces the identical address on every mainstream
platform without tripping that check, the same trick the
Boehm-Demers-Weiser collector uses.

**History: the `GC_KIND_OOP_ARRAY` bug (Milestone 5, obsoleted by Milestone
6)**: the old tree-walking evaluator built a keyword/cascade send's
evaluated arguments as a separately `malloc()`'d heap array (arity wasn't
known until parse time, unlike a binary send's fixed-size on-stack
`oop args[1]`), handed to a primitive as a plain `oop *`. Most primitives
use `args` synchronously and return, but `Block>>whileTrue:`/`whileFalse:`
hold onto their body-block argument across many iterations of an internal
C `while` loop, re-reading `args[0]` fresh every iteration. None of
`gcMarkOop()`'s precise roots ever reached into that array, and
conservative scanning didn't help either: it only checks whether a stack
*word* is itself a live header's address, it never dereferences a heap
pointer looking for further oops nested inside whatever it points to. The
result, found empirically (not by inspection) while stress-testing
Milestone 5: `[cond] whileTrue: [body]` with enough iterations for a
collection to land mid-loop would have its `body` block **swept out from
under the still-running loop** — a genuine use-after-free, which (since
the freed memory usually got reused for something else entirely by the
time it mattered) manifested as memory corruption masquerading as a
runaway allocation loop, not a clean crash, making it slow to pin down.
Milestone 5's fix was a third `GCKind`, `GC_KIND_OOP_ARRAY`, so
conservative scanning could trace *into* such an array once it found the
array's own address on the stack. Milestone 6's bytecode VM removes the
need for that fix entirely, by construction: `vmRun()`'s operand stack
*is* a C-local array, so `OP_SEND`'s `args` already point directly into
genuinely stack-resident memory, the same as any other local — see the
bytecode section above. `tests/test_main.c`'s
`testGCDuringLoopWithBlockArgument()` still exists as the regression test
for this exact scenario (run with a deliberately tiny `gcSetThreshold()`
to force a collection on nearly every loop iteration), now exercising the
new mechanism instead of the old fix.

**Sweep**: unmarked headers are unlinked and `free()`'d; for
`GC_KIND_ACTIVATION` specifically, its `argValues`/`tempValues` buffers
(plain `malloc()`'d, not separately `gcAlloc()`'d — see above) are freed
first, since nothing else owns them. Surviving headers have `marked` reset
to `0`, ready for the next collection. `gcAlloc()` triggers a collection
when `bytesAllocated` crosses `gcThreshold` (starts at 64 KiB), and doubles
the threshold afterward if the live set is still more than half of it —
a simple growth heuristic that avoids thrashing on a program with a
genuinely large working set, without needing anything more elaborate at
this scale. Because the conservative scan makes a collection safe to
trigger from *anywhere* (not just between top-level REPL statements), a
long-running loop's garbage gets reclaimed as it goes, not just once the
whole statement finishes.

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

## Testing this codebase for memory bugs

`gc.c` is the highest-risk file in this codebase for exactly the kind of
bug regular tests don't reliably catch (a rare, timing-dependent
use-after-free). When changing anything under `src/`, especially
`gc.c`/`eval.c`/anything touching allocation, an ad hoc sanitizer build is
worth running before trusting `make test` alone:

```
cc -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -mllvm -asan-stack=0 \
  -Isrc -o /tmp/run_tests_asan tests/test_main.c src/*.c && /tmp/run_tests_asan
```

`-mllvm -asan-stack=0` disables ASan's stack redzones specifically — a
conservative collector *deliberately* reads raw stack bytes in the gaps
between distinct local variables looking for leftover pointer values,
which is precisely the pattern ASan's stack instrumentation exists to
flag as a bug. Leaving stack instrumentation on produces a wall of
false-positive `stack-buffer-underflow` reports pointing at
`scanConservativeRoots`/`scanRange` in `gc.c` (a well-known, documented
tension between conservative GC and ASan — the Boehm collector's own docs
describe the same workaround). Heap-related bugs (use-after-free,
double-free, heap buffer overflow) and UBSan's checks are unaffected by
this flag and still fully active. `src/gc.c` and `src/eval.c` are the
files most likely to produce a real finding here; a clean run doesn't
prove there's no bug, but it's meaningfully stronger evidence than
`make test` passing alone. (One pre-existing, unrelated UBSan finding is
expected and not a regression: `object.h`'s `makeSmallInteger()` left-shifts
a possibly-negative `intptr_t`, technically undefined behavior since
Milestone 1, though every mainstream compiler/platform this project
targets implements it as a plain arithmetic shift.)

## Roadmap

See `docs/ROADMAP.md` for the milestone plan, current progress, and
example REPL sessions to try for each completed milestone. Narrative
documentation lives under `docs/` (`docs/ROADMAP.md`, `docs/LANGUAGE.md`);
this file (`CLAUDE.md`) stays at the repo root since Claude Code only
auto-loads it from there.
