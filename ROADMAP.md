# Roadmap

Status: ✅ done · 🚧 next up · ⏳ planned

Each milestone lands as its own commit and is a deliberately separate step,
so the object model and message-send semantics get proven out before more
moving parts (a compiler, a GC) are added on top. See `CLAUDE.md` for the
internal architecture; this file tracks progress and how to try each
milestone yourself.

## How to test as you go

- `make test` — the growing assert-based regression suite
  (`tests/test_main.c`). Every milestone's new syntax/primitives get a test
  here; run this after pulling any change.
- `make && ./smalltalk` — the REPL, for manual/interactive testing. Each
  milestone below lists example expressions you can paste in directly.
- `quit` or `exit` (or Ctrl-D) leaves the REPL.

## Milestone 1 — Core object model, message dispatch, REPL ✅

*Commit `92454c8`.*

Tagged `SmallInteger`s, a C-bootstrapped class hierarchy (`Object` /
`UndefinedObject` / `Boolean` / `True` / `False` / `SmallInteger`), real
method-lookup message dispatch (not hardcoded operators), a recursive-
descent parser with correct unary/binary/keyword precedence, a tree-walking
evaluator, and a REPL.

**Try it:**
```
$ make && ./smalltalk
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
the first message — see `CLAUDE.md` for the full list of deliberate
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

## Milestone 4 — Blocks/closures, control flow 🚧

Block literals `[ ... ]`, closures over enclosing variables, non-local
return, and `ifTrue:ifFalse:` / `whileTrue:` built *from* blocks rather
than being special-cased in the evaluator — matching how real Smalltalk
has almost no built-in control flow.

## Milestone 5 — Garbage collection ⏳

A mark-sweep collector, replacing the current "`malloc` and never free."
Needs a root set (the variable environment, the REPL's in-flight AST/eval
stack) and a real object-header story now that three heap layouts exist
(`Object`, `StringObject`, `SymbolObject`).

## Milestone 6 — Bytecode compiler + VM dispatch loop ⏳

Replace the tree-walking evaluator with a bytecode compiler and a
dispatch-loop interpreter over compiled methods — this is the point where
it becomes a "real" VM in the classic Smalltalk-80 sense, per the
project's original goal.
