# Examples

Runnable demonstrations of what the interpreter can do, one topic per
file. Run one directly:

```
$ make && ./bin/smalltalk examples/point.st
```

or check them all at once (fails the build if any of them produces an
`error:`/`parse error:`, so these double as a lightweight regression
suite alongside `make test`):

```
$ make examples
```

There's no comment syntax yet (see `../docs/LANGUAGE.md`'s Known Limitations), so
these files are plain code with no inline narration — the explanations
live here instead. Each is just a sequence of top-level expressions, same
as typing them at the `st>` prompt one at a time; there's no multi-line
class-body syntax, so a class definition is one `subclass:
instanceVariableNames:` send followed by one `compile:` send per method.

## `point.st`

A `Point` class with two instance variables (`x`, `y`), accessors, `+`
(answers a new `Point`), and a `printString` override — showing that
overriding `printString` on a user class changes how the REPL renders it
(`p` prints as `3@4`, not the `Object` default `a Point`).

## `animals.st`

`Animal` with two subclasses, `Dog` and `Cat`, each overriding `speak` and
calling `super speak` to extend the inherited behavior rather than
replace it — the core reason `super` exists.

## `counter.st`

The simplest possible stateful object: one instance variable mutated by
its own methods (`increment`, `incrementBy:`) across several sends,
including a cascade (`c increment; increment; increment`). Also shows
that two instances of the same class don't share state: `d`'s counter
starts at `0` independently of `c`'s.

Note that `new` never auto-sends `initialize` here (unlike modern
Smalltalk dialects) — each example calls it explicitly, e.g. `Counter new
initialize`. Forgetting to would leave `count` as `nil`, and `count + 1`
would fail (see `../docs/LANGUAGE.md`'s Known Limitations: primitives don't
type-check their arguments).

## `blocks.st`

Block basics (`value`/`value:`/`value:value:`), a closure
(`Adder>>makeAdder:` returns `[:x | x + n]` — each call captures its
*own* `n`, so `add5` and `add10` stay independent even after
`makeAdder:`'s own call has returned), `ifTrue:ifFalse:`, a `whileTrue:`
loop summing 0 through 4, and a recursive `Math>>fact:` that only works
at all because `ifTrue:ifFalse:` now exists to terminate it — cross-check
its answer against the built-in `SmallInteger>>factorial`.

## `gc.st`

20,000 iterations of a `whileTrue:` loop, each creating a throwaway
closure and invoking it once — garbage collection is otherwise invisible
from the language, so what this actually demonstrates is that it *ran*
(finishes in about a second, not growing memory without bound the way it
would have before this milestone) and that `add5`, a closure created
*before* the loop and never touched by it, still answers correctly
afterward — proof the collector didn't reclaim something still reachable.
