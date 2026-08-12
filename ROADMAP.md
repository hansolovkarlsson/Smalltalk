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

## Milestone 3 — Class-definition syntax + user-defined methods 🚧

Define new classes and real Smalltalk-level methods (not just C
primitives) from the REPL, with instance variables and accessors. This is
the milestone where the language stops being "a calculator with objects"
and starts being able to grow itself.

Open design questions to resolve when this starts: class-definition syntax
(a `subclass:instanceVariableNames:...` message in the classic style, vs.
something simpler for a REPL), where compiled user methods live relative
to the existing C-primitive `MethodEntry` table, and how `self`/`super`
work in a tree-walking evaluator with no call-frame representation yet.

**Try it (once built, syntax TBD):** something in the spirit of
```
st> Object subclass: #Point instanceVariableNames: 'x y'
st> Point new setX: 3 setY: 4; printString
```

## Milestone 4 — Blocks/closures, control flow ⏳

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
