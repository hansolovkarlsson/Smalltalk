# Roadmap

Status: ✅ done · 🚧 next up · ⏳ planned

Each milestone lands as its own commit and is a deliberately separate step,
so the object model and message-send semantics get proven out before more
moving parts (a compiler, a GC) are added on top. See `../CLAUDE.md` for
the internal architecture; this file tracks progress and how to try each
milestone yourself.

## How to test as you go

- `make test` — the growing assert-based regression suite
  (`tests/test_main.c`). Every milestone's new syntax/primitives get a test
  here; run this after pulling any change.
- `make && ./bin/smalltalk` — the REPL, for manual/interactive testing. Each
  milestone below lists example expressions you can paste in directly.
- `quit` or `exit` (or Ctrl-D) leaves the REPL.
- `make examples` — runs every file in `../examples/` (also runnable one
  at a time: `./bin/smalltalk examples/point.st`) and fails if any of them
  errors. Bigger, narrated demonstrations than the inline snippets below —
  see `../examples/README.md`.

## Milestone 1 — Core object model, message dispatch, REPL ✅

*Commit `92454c8`.*

Tagged `SmallInteger`s, a C-bootstrapped class hierarchy (`Object` /
`UndefinedObject` / `Boolean` / `True` / `False` / `SmallInteger`), real
method-lookup message dispatch (not hardcoded operators), a recursive-
descent parser with correct unary/binary/keyword precedence, a tree-walking
evaluator, and a REPL.

**Try it:**
```
$ make && ./bin/smalltalk
st> 3 + 4 factorial
st> 10 factorial
st> 3 < 4
st> (1 + 2) * 3
st> 3 foo            "unhandled selector -> graceful error, not a crash"
```

## Milestone 2 — Variables, String/Symbol, printString, cascades ✅

*Commit `201536a`.*

A workspace variable environment with assignment (`x := expr`, right-
associative), `String` and `Symbol` as real heap objects, a real
`#printString` message-dispatch protocol (the REPL prints by sending
`printString`, not by hand-rolled formatting), and cascades (`;`).

**Try it:**
```
st> x := 5
st> x + 1
st> a := b := 3          "chained assignment"
st> 'hello' , ' world'
st> 'it''s'               "embedded quote"
st> #foo asString
st> #foo printString
st> 3 factorial; + 1; * 2 "cascade: all three sends target 3, not the previous result"
st> 3 printString
```

## Milestone 3 — Class-definition syntax + user-defined methods ✅

Define new classes and real Smalltalk-level methods (not just C
primitives) from the REPL, with instance variables, `self`/`super`, and
`^return`. This is the milestone where the language stops being "a
calculator with objects" and starts being able to grow itself.

Design questions from the original writeup, resolved:
- **Class-definition syntax**: the classic `subclass:instanceVariableNames:`
  keyword message, sent to any existing class. No `classVariableNames:` /
  `package:` etc yet — a deliberately trimmed two-keyword version.
- **Where compiled methods live**: `MethodEntry` now holds *either* a
  primitive C function *or* a `CompiledMethod` (an AST-based body), tagged
  by a `kind` field, in the same per-class array as before.
- **Method definition syntax**: no multi-line REPL input yet, so methods
  are installed via `Class>>compile: aString` — a String of Smalltalk
  method source (pattern, optional `| temps |`, `.`-separated statements),
  parsed by a new `parseMethod()`. Not the classic chunk-format `!`
  syntax, but avoids extending the REPL's one-line-at-a-time input loop.
- **`self`/`super` with no call-frame representation**: added one now — a
  small `Activation` struct (self, args, temps, caller) that `eval.c`
  threads through compiled-method calls; real recursion comes for free
  from it being an ordinary (recursive) C call. `super` dispatches from
  the *defining* method's class, not the receiver's actual class.

Also fell out of this milestone almost for free: `anObject class` (basic
reflection — every class has a real, sendable class object now) and
method redefinition-in-place (recompiling a selector replaces it, rather
than silently shadowing the old definition behind it).

Known gaps: no accessor auto-generation (write `x  ^x` by hand), no class-
side (metaclass) methods, no real per-class metaclass (`Point class
printString` is `'Class'` for every user class, not `'Point class'`), and
cascading directly off a bare `super` receiver loses super-dispatch after
the first message — see `../CLAUDE.md` for the full list of deliberate
simplifications.

**Try it:**
```
st> Object subclass: #Point instanceVariableNames: 'x y'
st> Point compile: 'setX: ax setY: ay  x := ax. y := ay. ^self'
st> Point compile: 'x  ^x'
st> Point compile: 'y  ^y'
st> Point compile: '+ aPoint  ^Point new setX: x + aPoint x setY: y + aPoint y'
st> p := Point new setX: 3 setY: 4
st> q := Point new setX: 1 setY: 2
st> (p + q) x
st> p class printString      "'Point'"
st> 3 class printString      "'SmallInteger' -- basic reflection"

st> Object subclass: #Animal instanceVariableNames: 'name'
st> Animal compile: 'setName: n  name := n. ^self'
st> Animal compile: 'speak  ^name , '' makes a sound'''
st> Animal subclass: #Dog instanceVariableNames: ''
st> Dog compile: 'speak  ^super speak , ''! (woof)'''
st> Dog new setName: 'Rex'; speak
```

Fuller, runnable versions of both of the above (plus a third, `Counter`,
showing instance-variable mutation and cascades together) live in
`../examples/point.st` and `../examples/animals.st` — see
`../examples/README.md`.

## Milestone 4 — Blocks/closures, control flow ✅

Block literals `[ ... ]`, closures over enclosing variables, non-local
return, and `ifTrue:ifFalse:` / `whileTrue:` built *from* blocks rather
than being special-cased in the evaluator — matching how real Smalltalk
has almost no built-in control flow.

This is also the milestone where recursive methods became genuinely
useful: before this, there was no way for one to terminate itself (no
`ifTrue:ifFalse:` to stop recursing on). See `Math>>fact:`/`fib:` below.

What "real closures" required under the hood: an `Activation` (the
per-call record of `self`/args/temps that a compiled method already had
from Milestone 3) now has to **survive past the call that created it**,
since a block can capture one and be invoked long after its defining
method has returned — so activations moved from a stack-local to a
heap-allocated (`malloc`, never freed, consistent with this project's "no
GC yet" stance) record with a `lexicalParent` chain for variable capture.
Non-local `^return` — needed because a block can run arbitrarily far down
the C call stack from where it was written, e.g. invoked back into from
`whileTrue:`'s C loop — uses `setjmp`/`longjmp` to unwind straight to the
right method activation regardless of how many block calls are in
between. See `CLAUDE.md` for the full mechanism.

Also added alongside the headline features, since they were cheap once
blocks existed: `and:`/`or:` (short-circuiting, block-argument versions of
boolean combination) and `not`. Known gap: block literals have no `|
temps |` of their own in this milestone (only their parameters) — use the
enclosing method's temps instead.

**Try it:**
```
st> [3 + 4] value
st> [:a :b | a + b] value: 3 value: 4
st> 3 < 4 ifTrue: ['yes'] ifFalse: ['no']

st> Object subclass: #Adder instanceVariableNames: ''
st> Adder compile: 'makeAdder: n  ^[:x | x + n]'
st> add5 := Adder new makeAdder: 5
st> add10 := Adder new makeAdder: 10
st> add5 value: 1                      "6 -- each block closed over its own n"
st> add10 value: 1                     "11"

st> n := 0
st> sum := 0
st> [n < 5] whileTrue: [sum := sum + n. n := n + 1]
st> sum                                "10"

st> Object subclass: #Math instanceVariableNames: ''
st> Math compile: 'fact: n  n = 0 ifTrue: [^1]. ^n * (self fact: n - 1)'
st> Math new fact: 10                  "3628800, matches 10 factorial"
```

Fuller, runnable version of the above lives in `../examples/blocks.st` —
see `../examples/README.md`.

## Milestone 5 — Garbage collection ✅

A mark-sweep collector, replacing the "`malloc` and never free" policy
every earlier milestone deliberately relied on. Needed a root set (the
variable environment, the interned-symbol table, the in-progress method/
block call chain) and a real object-header story now that five oop heap
layouts exist (`Object`, `StringObject`, `SymbolObject`, `ClassObject`,
`BlockObject`) plus one more heap-allocated-but-not-a-tagged-oop thing
(`Activation`, since Milestone 4).

The hard part wasn't the mark-sweep algorithm itself — it was finding
every *root*. A tree-walking evaluator keeps live objects in ordinary C
local variables scattered across however many `eval()` frames happen to
be nested at any given moment, with no shadow stack tracking them. This
collector handles that with **conservative stack/register scanning**
(`setjmp` to spill registers, then scan from the current frame up to a
`stackBottom` recorded once at startup), made fully *precise* rather than
"probably correct" by checking each candidate stack word against a hash
set of every currently-live allocation's exact address, rather than just
guessing it "looks like" a pointer (the Boehm-Demers-Weiser collector's
approach, which accepts false positives as the price of not walking off
into unmapped memory).

That conservative scan also found (and fixed) a real, subtle bug: a
message send's evaluated arguments live in a plain heap array that
*itself* was reachable from the C stack, but whose *contents* weren't —
conservative scanning checks whether a stack word points at a tracked
allocation, it doesn't recursively dereference what that allocation
contains looking for further oops. `Block>>whileTrue:`/`whileFalse:`, which
hold onto their body-block argument across many loop iterations, could
therefore have that block collected out from under a still-running loop.
Tagging argument arrays with their own GC kind (so the collector knows to
trace *into* them) fixed it — see `CLAUDE.md` for the full story, including
why it manifested as a runaway allocation loop rather than a clean crash,
which is what made it slow to track down in the first place.

**Try it:** there's no new surface syntax this milestone — collection is
automatic and invisible in normal use. What's observable is that programs
that allocate a lot no longer just grow forever:

```
st> Object subclass: #Adder instanceVariableNames: ''
st> Adder compile: 'makeAdder: n  ^[:x | x + n]'
st> add5 := Adder new makeAdder: 5
st> n := 0
st> sum := 0
st> [n < 20000] whileTrue: [b := [:x | x + n]. sum := sum + (b value: 1). n := n + 1]
st> sum          "200010000 -- 20000 throwaway blocks + activations, collected as it goes"
st> add5 value: 100
```

Before this milestone, the loop above ran fine at small counts but
accumulated unbounded memory (and, once the argument-array bug above is
accounted for, could misbehave) at larger ones; now it runs in about a
second regardless of iteration count, memory usage stays bounded, and
`add5` — a closure that's been alive since before the loop started, never
touched by anything inside it — still answers correctly afterward,
proving the collector didn't free something still reachable.

## Milestone 6 — Bytecode compiler + VM dispatch loop ⏳

Replace the tree-walking evaluator with a bytecode compiler and a
dispatch-loop interpreter over compiled methods — this is the point where
it becomes a "real" VM in the classic Smalltalk-80 sense, per the
project's original goal.
