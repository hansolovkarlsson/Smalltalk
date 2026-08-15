# Beginner's Tutorial

A guided, hands-on walkthrough for someone who has never used this
interpreter (or Smalltalk) before. It builds up from arithmetic to
defining your own classes, one small step at a time. For the complete,
precise rules once you're past the basics, see `LANGUAGE.md` — this
document teaches by example rather than trying to be exhaustive.

Every transcript below was run against the built interpreter, not typed
from memory — you can follow along and get exactly this output.

## 1. Starting the REPL

```
$ make
$ ./bin/smalltalk
Smalltalk REPL (milestone 6). Type an expression, or 'quit' to exit.
st>
```

You get a `st>` prompt. Type one expression, press enter, see the result.
Type `quit` (or `exit`, or Ctrl-D) to leave.

**Important habit to build right away: one expression per line.** This
interpreter reads and evaluates a single expression from each line you
type — it does not (yet) let you chain several statements together with
`.` on one line. Get used to pressing enter after every single thing you
want evaluated.

## 2. Numbers and arithmetic

```
st> 3 + 4
7
st> 2 + 3 * 4
20
```

That second one surprises everyone coming from most other languages:
there's no `*`-before-`+` precedence in Smalltalk. Arithmetic operators
(`+ - * /`) are just messages sent to objects, all at the same
precedence, evaluated strictly left to right — `2 + 3 * 4` is `(2 + 3) *
4`, not `2 + (3 * 4)`. Use parentheses when you mean the other grouping:

```
st> 2 + (3 * 4)
14
```

`/` is integer division (it truncates, like C, not exact fraction
division):

```
st> 7 / 2
3
```

Numbers understand more than just arithmetic — `factorial`, for example:

```
st> 3 factorial
6
st> 5 factorial + 1
121
```

`factorial` here is a **unary message** — a message with no arguments,
written by just following the receiver with a word. Unary messages bind
tighter than anything else, which is why `5 factorial + 1` is `(5
factorial) + 1`, not `5 factorial(ial of) (+1)` — there's no ambiguity
once you know the rule: unary tightest, then binary (`+ - * /` etc, left
to right, one precedence level), then keyword messages (lowest — more on
those soon).

## 3. Everything is a message send

This is the one idea that unlocks the rest of Smalltalk: **there is no
special syntax for operations** — `3 + 4` is not "the `+` operator
applied to `3` and `4`," it is the message `+` sent to the object `3`
with the argument `4`. `3 factorial` is the message `factorial` sent to
`3`. Later, when you write your own classes, your own methods are invoked
exactly the same way, with exactly the same rules. There's no separate
"operator" concept to learn.

## 4. Strings

```
st> 'hello'
'hello'
st> 'hello' , ' world'
'hello world'
st> 'hello' size
5
```

`,` is the concatenation message (yes, `,` is a valid binary selector,
same precedence tier as `+`). Strings are single-quoted; to get a literal
quote inside one, double it:

```
st> 'it''s a test'
'it''s a test'
```

## 5. Variables

```
st> x := 10
10
st> y := 20
20
st> x + y
30
st> x
10
```

`:=` assigns. You don't declare a variable ahead of time — assigning to a
name you haven't used yet creates it. These variables live for the whole
REPL session (they're not scoped to anything), so `x` and `y` are still
around for the rest of this walkthrough.

## 6. `true`, `false`, and conditionals

```
st> 3 > 2
true
st> 3 > 2 ifTrue: ['bigger'] ifFalse: ['smaller']
'bigger'
```

`ifTrue:ifFalse:` is a **keyword message** — the lowest-precedence kind,
written as one or more `word:` parts each immediately followed by an
argument. Here the whole selector is `ifTrue:ifFalse:`, sent to `3 > 2`
(itself a binary send, evaluated first since keyword sends bind loosest),
with two arguments: the blocks `['bigger']` and `['smaller']`.

Which brings us to blocks.

## 7. Blocks

A block is a chunk of code you can pass around and run later — written
in square brackets:

```
st> [3 + 4] value
7
```

`[3 + 4]` builds the block (it does *not* run yet); `value` is the
message that actually runs it. A block can take parameters, declared with
a leading colon and separated from the body by `|`:

```
st> [:a :b | a * b] value: 6 value: 7
42
```

You just saw why `ifTrue:ifFalse:` needs blocks, not plain expressions,
as its arguments: only one branch should ever actually run, and wrapping
each branch in `[...]` is what lets `ifTrue:ifFalse:` decide which one to
evaluate (via `value`) instead of both being evaluated up front the way
ordinary arguments would be.

## 8. Defining your first class

Classes are created by sending a message to an existing class — usually
`Object`, the root of everything:

```
st> Object subclass: #Greeter instanceVariableNames: 'greeting'
Greeter
```

This defines a new class named `Greeter` (the name comes from the
**Symbol** literal `#Greeter` — a Symbol is just an interned, quoteless
identifier-like literal, commonly used for names) with one instance
variable, `greeting`. `Greeter` is immediately usable, the same way any
variable you assign is usable.

Now give it some behavior. There's no multi-line class-body syntax here —
each method is installed separately by sending `compile:` a string of
Smalltalk source:

```
st> Greeter compile: 'setGreeting: g  greeting := g. ^self'
true
st> Greeter compile: 'greet: name  ^greeting , '', '' , name , ''!'''
true
```

Two methods, two shapes worth noticing:

- `setGreeting: g  greeting := g. ^self` — pattern (`setGreeting: g`,
  meaning this method takes one argument, named `g`), then a
  `.`-separated body. `greeting := g` stores the argument into the
  instance variable; instance variables are just visible by name inside a
  method, no `self greeting :=` needed. `^self` returns the receiver — a
  common pattern for a "setter" method, so you can chain sends onto its
  result.
- `greet: name  ^greeting , '', '' , name , ''!'''` — builds and returns
  a new string by concatenating pieces. Note the doubled quotes
  (`''...''`) inside the source string, needed because the whole method
  body is itself written as one quoted string.

`compile:` answers `true` on success. Now use it:

```
st> g := Greeter new setGreeting: 'Hello'
a Greeter
st> g greet: 'World'
'Hello, World!'
```

`Greeter new` creates a fresh instance (all instance variables start
`nil`); sending it `setGreeting:` back-to-back is why `setGreeting:`
returning `self` mattered — it let the whole line read as one chained
expression instead of two statements.

## 9. State that changes: a Counter

A more typical stateful object — one whose instance variable actually
changes over the object's lifetime:

```
st> Object subclass: #Counter instanceVariableNames: 'count'
Counter
st> Counter compile: 'initialize  count := 0. ^self'
true
st> Counter compile: 'count  ^count'
true
st> Counter compile: 'increment  count := count + 1. ^self'
true
```

Three methods: `initialize` sets the starting state, `count` is a getter,
`increment` mutates and (again) returns `self` for chaining. One thing to
watch for — **`new` does *not* automatically call `initialize`** here
(unlike some modern Smalltalk dialects), so you must call it yourself:

```
st> c := Counter new initialize
a Counter
st> c increment
a Counter
st> c count
1
```

Because `increment` returns `self`, you can chain several in a row with a
**cascade** — `;` resends further messages to the *same receiver* as the
message before the first `;`:

```
st> c increment; increment
a Counter
st> c count
3
```

`c increment; increment` sends `increment` to `c` twice, one after
another — not `c increment` followed by sending `increment` to whatever
the first call returned (which happens to also be `c` here, but cascades
don't rely on that).

## 10. Loops

```
st> n := 1
1
st> sum := 0
0
st> [n <= 5] whileTrue: [sum := sum + n. n := n + 1]
nil
st> sum
15
```

`whileTrue:` is sent to a 0-argument block (the condition), with another
0-argument block as its argument (the body); the condition is
re-evaluated before every iteration. Notice both the condition and the
body are blocks — same mechanism as `ifTrue:ifFalse:`, just reused for
looping instead of branching. There's no dedicated `while` keyword in the
grammar; `whileTrue:` is just an ordinary message, defined on `Block`.

## 11. Where to go from here

You now have the whole shape of the language: everything is a message
send, blocks defer evaluation, and classes/methods are built up
incrementally via `subclass:instanceVariableNames:` and `compile:`. From
here:

- **`LANGUAGE.md`** — the full, precise reference: every selector
  currently implemented, exact grammar rules, all the sharp edges (no
  comments, no floats, one-expression-per-line, etc.) called out
  explicitly under "Known limitations."
- **`../examples/`** — fuller, runnable programs (`./bin/smalltalk
  examples/point.st`), including subclassing with `super`
  (`animals.st`), closures (`blocks.st`), and non-local return.
- **`ROADMAP.md`** — how the interpreter itself is built, milestone by
  milestone, plus what's planned next.

The single biggest habit to build early: when something doesn't work the
way you expect, ask "what message is this, and what is it being sent
to?" — there's no other kind of operation in this language.
