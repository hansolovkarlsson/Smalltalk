# Smalltalk

A Smalltalk interpreter and bytecode VM, built from scratch in C — no
parser generator, no borrowed VM, grown one milestone at a time: a tagged
object model, real message dispatch, user-defined classes, closures with
non-local return, a mark-sweep garbage collector, and a bytecode compiler
with its own dispatch loop.

**[hansolovkarlsson.github.io/Smalltalk →](https://hansolovkarlsson.github.io/Smalltalk/)**

## Quick start

```
$ make
$ ./bin/smalltalk
Smalltalk REPL (milestone 6). Type an expression, or 'quit' to exit.
st> 3 + 4 factorial
```

`make test` runs the regression suite; `make examples` runs every runnable
program under `examples/`.

## Docs

- [Beginner's tutorial](https://hansolovkarlsson.github.io/Smalltalk/tutorial.html) —
  start here if you're new to this interpreter or to Smalltalk.
- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) — the full language reference.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone-by-milestone history and what's next.
- [`examples/`](examples) — fuller runnable programs (subclassing, closures, non-local return).
- [`CLAUDE.md`](CLAUDE.md) — internal architecture notes for anyone hacking on the interpreter itself.
