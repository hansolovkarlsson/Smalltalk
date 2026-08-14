# Language Reference

What the interpreter actually accepts and does right now — Milestone 6.
This documents observable REPL behavior, not internals; see `../CLAUDE.md`
for how it's implemented and `ROADMAP.md` for what's coming next. Every
example below was run against the built interpreter, not written from
memory.

Milestone 6 replaced the evaluator internals (a bytecode compiler and VM
dispatch loop now run compiled methods/blocks, instead of walking the
parsed syntax tree directly) without changing the language itself at all
— every example in this document, and every behavior it describes, is
unaffected. See `../CLAUDE.md` for that rewrite; nothing below changed
because of it.

## Running it

```
$ make && ./bin/smalltalk
st> 3 + 4 factorial
27
st> quit
```

`quit`, `exit`, or Ctrl-D leaves the REPL. **Each line is one, and only
one, expression** — see [One expression per line](#one-expression-per-line)
below for what happens if you put more than one on a line.

## Lexical syntax

### Integers

Digits only, optionally negative: `0`, `42`, `-17`. No floats, no scaled
decimals, no radix literals (`16rFF` etc.) yet — all arithmetic is native
C `long`, with no overflow checking.

A leading `-` is read as part of a negative literal only where an operand
is expected (start of input, after `(`, after a binary/keyword selector, or
after `:=`/`;`). Elsewhere it's the binary selector `-`. So `3 - 4` is
`3 - 4` (a send), while `3 + -4` is `3 + (-4)` (a literal).

### Strings

Single-quoted: `'hello'`. Write an embedded quote by doubling it:
`'it''s'` reads as the 4-character string `it's`. No backslash escapes, no
multi-line handling beyond whatever's literally between the quotes.

### Symbols

`#` followed by an identifier (`#foo`), a keyword-style name (`#at:put:`,
built by concatenating one or more `word:` segments), or a run of binary-
selector characters (`#+`, `#<=`). Symbols are interned: two occurrences of
`#foo` anywhere are the *same* object (`==`-identity), though there's no
`==` primitive yet to observe that directly from the REPL — see
[Known limitations](#known-limitations).

### Pseudo-variables

`nil`, `true`, `false`, `self`, and `super` are reserved literal tokens
recognized by the parser itself, not ordinary variables — you can't
reassign any of them (`nil := 3` doesn't parse as an assignment; `nil`
wins and `:= 3` is leftover, unconsumed input, silently ignored — see
below).

`self` and `super` are only meaningful inside a compiled method body (see
[Defining classes and methods](#defining-classes-and-methods)); using
either at the top level of the REPL prints `error: 'self' used outside a
method` (or `'super'`) and evaluates to `nil`, the same non-fatal pattern
as every other error case. Both evaluate to the same object — the method's
receiver — but a message *sent to* `super` looks up starting from the
superclass of the class the running method was defined in, not from the
receiver's own class; see below.

### Identifiers and selectors

An identifier is a letter or `_` followed by letters/digits/`_`
(`x`, `myVar3`, `_foo`). A **keyword part** is an identifier immediately
followed by `:` with no space (`at:`); several concatenate into one
keyword selector (`at:put:`). A **binary selector** is one or more
characters from `+ - * / ~ < > = & | @ % , ? ! \`.

### Comments

**Not implemented.** Real Smalltalk uses `"double-quoted text"` for
comments; the lexer here doesn't recognize `"` at all, so it becomes an
unhandled character and the expression fails to parse.

## Expression syntax

Standard Smalltalk precedence, tightest to loosest:

1. **Unary sends**, left-to-right: `3 factorial printString` sends
   `factorial` to `3`, then `printString` to that result.
2. **Binary sends**, left-to-right, *all operators at one precedence
   level* — there is no `*`-before-`+`: `2 + 3 * 4` is `(2 + 3) * 4 = 20`,
   not 14.
3. **Keyword sends**, lowest precedence, one level, right-most keyword
   send at the top: `foo at: 1 + 1 put: 2 factorial` sends
   `at:put:` to `foo` with arguments `1 + 1` and `2 factorial` (each
   argument parsed as a full binary expression).

`( ... )` groups a sub-expression and can appear anywhere a primary
(literal or variable) can.

### Cascades

`;` resends further messages to the receiver of the *last* message in the
preceding expression, not to that message's result:

```
st> 3 factorial; + 1; * 2
6
```

Here every part (`factorial`, `+ 1`, `* 2`) is sent to `3`. The result of
`+ 1` (`4`) is computed and discarded; the cascade's value is the last
send's result: `3 * 2 = 6`.

### Assignment

`variable := expression`. Assigning to a name that's never been used
before declares it — there's no separate declaration syntax (no `| x |`
temp-var pragma yet). Assignment is right-associative, so `a := b := 3`
binds both `a` and `b` to `3`. `nil`, `true`, `false` can't be assignment
targets (see [Pseudo-variables](#pseudo-variables)).

Variables live in one global workspace table for the life of the REPL
process — there's no lexical scoping, no per-method locals, nothing
private. Referencing a name that was never assigned prints an error to
stderr and evaluates to `nil`; it does not stop the REPL.

### One expression per line

The lexer tokenizes `.` as a statement separator, but the parser only ever
consumes one expression per `parseExpression()` call, and the REPL never
checks for leftover tokens afterward. So:

```
st> 3 + 4. 5 + 6
7
```

prints `7` and silently drops `5 + 6` — it is *not* evaluated, and you get
no error telling you it was ignored. Don't rely on `.` to sequence
statements yet; one expression per line, full stop.

## Defining classes and methods

### Subclassing

```
st> Object subclass: #Point instanceVariableNames: 'x y'
Point
```

Sent to any existing class, `subclass:instanceVariableNames:` creates a
new class and binds its name (from the Symbol argument) as a global
variable, exactly like `x := ...` binds `x` — so `Point` is immediately
usable as an identifier. The second argument is a String of
space-separated instance variable names (`''` for none). Instance
variables are inherited: subclassing a class that already has some adds
the new ones after them, and a fresh instance's variables all start out
`nil`.

Only `Object` and its (transitive) subclasses make sense as a superclass
here. `new` always allocates the plain `{ instance variables }` layout
regardless of which class you subclass, so subclassing `String`, `Symbol`,
or `SmallInteger` wouldn't preserve *their* special internal
representation — don't do that.

### Methods

There's no multi-line class-body syntax yet (the REPL still reads one line
at a time) — methods are installed by sending `compile:` a String of
Smalltalk method source:

```
st> Point compile: 'setX: ax setY: ay  x := ax. y := ay. ^self'
true
st> Point compile: 'x  ^x'
true
```

A method source string is: a **pattern** (unary `foo`, binary `+ arg`, or
keyword `at: a put: b`, exactly like a message send's shape), an optional
`| temp1 temp2 |` declaration, then a `.`-separated sequence of
statements. Any statement may be `^expr` to return that value immediately
and skip the rest; a method that never hits a `^` returns `self` (**not**
its last statement's value — this differs from some Smalltalk dialects
that also default to `self` but easy to forget if you're expecting the
last expression's value back). `compile:` answers `true` on success, or
`false` (with a `parse error`-style message on stderr) if the source
doesn't parse. Recompiling an existing selector **replaces** it in place —
there's no warning, and no way to keep both versions.

Inside a method body: `self` is the receiver; instance variable names
resolve directly (no `self x`/getter needed, just `x`); `super foo` sends
`foo` starting lookup at the superclass of the class the method was
*defined* in (so an overriding method can still reach the inherited
behavior it's overriding), rather than the receiver's actual class.
Arguments and temps are ordinary assignable local names, scoped to that
one method activation — real recursion works (each call gets its own
activation, same way C's own call stack works). Combined with
`ifTrue:ifFalse:` (see [Blocks and control flow](#blocks-and-control-flow)
below) for a base case, this makes genuinely useful recursive methods
possible:

```
st> Object subclass: #Math instanceVariableNames: ''
Math
st> Math compile: 'fact: n  n = 0 ifTrue: [^1]. ^n * (self fact: n - 1)'
true
st> Math new fact: 10
3628800
```

```
st> Point compile: '+ aPoint  ^Point new setX: x + aPoint x setY: y + aPoint y'
true
st> p := Point new setX: 3 setY: 4
a Point
st> q := Point new setX: 1 setY: 2
a Point
st> (p + q) x
4
```

```
st> Object subclass: #Animal instanceVariableNames: 'name'
Animal
st> Animal compile: 'setName: n  name := n. ^self'
true
st> Animal compile: 'speak  ^name , '' makes a sound'''
true
st> Animal subclass: #Dog instanceVariableNames: ''
Dog
st> Dog compile: 'speak  ^super speak , ''! (woof)'''
true
st> Dog new setName: 'Rex'; speak
'Rex makes a sound! (woof)'
```

## Blocks and control flow

### Block literals

`[ ... ]`, optionally with parameters: `[:a :b | a + b]`. Parameters are
written with a *leading* colon (`:a`), unlike a keyword message part's
*trailing* one (`at:`) — don't confuse the two. A block with parameters
needs the `|` separator before its statements; one with none doesn't:

```
st> [3 + 4] value
7
st> [:a :b | a + b] value: 3 value: 4
7
```

A block's body is the same statement grammar as a method's — a
`.`-separated sequence, any statement of which may be `^expr` (see
[Non-local return](#non-local-return) below) — **except** for what
happens when execution falls off the end without hitting a `^`: a block
answers its **last statement's value** (`nil` if it has none), whereas a
method answers `self` regardless of its last statement. This is a real,
easy-to-forget difference, not just documentation noise:

```
st> [1. 2. 3] value
3
st> Object subclass: #Foo instanceVariableNames: ''
Foo
st> Foo compile: 'bar  1. 2. 3'
true
st> Foo new bar
a Foo
```

There's no `| temp1 temp2 |` declaration for a block's *own* locals in
this milestone (only its parameters) — use a temp on the enclosing
method instead.

Invoke a block with `value` (0 params), `value:` (1), or `value:value:`
(2); there's no `value:value:value:` or a variadic
`valueWithArguments:` yet (no `Array` to pass one as). Sending the wrong
one for a block's actual parameter count doesn't crash, it prints
`error: wrong number of block arguments (expected N, got M)` and answers
`nil`, the usual non-fatal pattern.

### Closures

A block captures its enclosing method's (and, for a block written inside
another block, that block's) arguments, temps, and `self` **by
reference**, live, for as long as the block itself exists — including
after the method that created it has already returned:

```
st> Object subclass: #Adder instanceVariableNames: ''
Adder
st> Adder compile: 'makeAdder: n  ^[:x | x + n]'
true
st> add5 := Adder new makeAdder: 5
a Block
st> add10 := Adder new makeAdder: 10
a Block
st> add5 value: 1
6
st> add10 value: 1
11
st> add5 value: 100
105
```

`add5` and `add10` each closed over their *own* `n` from their own call
to `makeAdder:` — proof this is a real closure and not just "read
whatever `n` was most recently," since both `makeAdder:` calls have
already returned by the time `add5`/`add10` are invoked.

### Non-local return

`^` inside a block always returns from the block's **enclosing method**,
never from the block itself — even if the block is invoked from deep
inside some other call (another method, `whileTrue:`'s loop, etc.), and
even through several levels of block nesting:

```
st> Object subclass: #Finder instanceVariableNames: ''
Finder
st> Finder compile: 'firstOver: limit  | i | i := 0. [true] whileTrue: [i := i + 1. i > limit ifTrue: [^i]]'
true
st> Finder new firstOver: 41
42
```

Here `^i` is nested inside an `ifTrue:` block, itself nested inside a
`whileTrue:` body block — two block calls deep, and dynamically several
C-level calls away from `firstOver:`'s own activation — and still
correctly unwinds straight back to it, skipping the rest of `whileTrue:`
entirely. Using `^` where there's no enclosing method at all (e.g. inside
a block defined and invoked directly at the REPL top level) prints
`error: '^' used outside a method`, same non-fatal pattern as `self`/
`super` used outside a method.

### `ifTrue:ifFalse:` and friends

Defined on `True`/`False` (not a generic `Object` protocol — sending
`ifTrue:` to something that isn't a Boolean is a plain `does not
understand` error, there's no implicit truthiness):

```
st> 3 < 4 ifTrue: ['yes'] ifFalse: ['no']
'yes'
st> 3 > 4 ifTrue: ['yes'] ifFalse: ['no']
'no'
st> 3 > 4 ifTrue: ['yes']
nil
```

`ifTrue:`/`ifFalse:` (one block, answers `nil` if the condition doesn't
match) and `ifTrue:ifFalse:` (two blocks) all invoke the matching block
with `value` and answer its result; the non-matching block, if any, is
never evaluated. `and:`/`or:` are the short-circuiting, block-argument
counterparts to eager boolean combination (`&`/`|`, which are **not**
implemented — see [Known limitations](#known-limitations)):

```
st> false and: [1 zork]
false
```

`1 zork` is never sent (no DNU error above) because `and:` on `False`
never evaluates its argument block at all. `not` also exists (`true not`
is `false`, `false not` is `true`).

### `whileTrue:` and `whileFalse:`

Sent to a 0-argument **block** (the loop condition), with another
0-argument block as the argument (the loop body). The condition block is
re-evaluated before every iteration:

```
st> n := 0
st> sum := 0
st> [n < 5] whileTrue: [sum := sum + n. n := n + 1]
st> sum
10
```

`whileFalse:` is the mirror image (loops while the condition block
answers `false`). Both always answer `nil`.

## The class library

Eight classes are bootstrapped in C: the six from Milestone 2, `Class`
(every class's class — see [Reflection](#reflection) below), and now
`Block` (every block literal's class). Any number of further classes can
now be defined at runtime via `subclass:instanceVariableNames:`, as shown
above.

```
Object
├── UndefinedObject   (nil's class)
├── Boolean
│   ├── True
│   └── False
├── SmallInteger
├── String
├── Symbol
├── Block              (every block literal's class)
├── Class              (every class's class, incl. Class itself)
└── ...                (your subclass:instanceVariableNames: classes)
```

Every class inherits `printString` from `Object` unless listed otherwise
below; the default is `'a ClassName'` / `'an ClassName'` (with the article
chosen by whether the class name starts with a vowel). This is also what
the REPL calls to render every result, so anything without a more specific
override prints as `a Foo`.

### Reflection

There's no `isKindOf:`, `respondsTo:`, or method introspection yet, but
every object answers `class`:

```
st> 3 class printString
'SmallInteger'
st> Point class printString
'Class'
```

`class` is defined once, on `Object`, and inherited by everything —
including class objects themselves, since `Class` is (now) a subclass of
`Object` too. That last example is not a typo: there's no real per-class
metaclass here, so *every* class's `class` is the same `Class` object,
unlike real Smalltalk where `Point class` would answer a distinct
`Point class` metaclass. See [Known limitations](#known-limitations).

### Object

| Selector | Behavior |
|---|---|
| `printString` | `'a ClassName'` / `'an ClassName'` — the fallback every other class overrides |
| `class` | The receiver's class, as a class object (see [Reflection](#reflection)). |

### UndefinedObject — the class of `nil`

| Selector | Behavior |
|---|---|
| `printString` | `'nil'` |

### Boolean / True / False

`Boolean` itself has no instances and defines nothing; every other
selector here is defined separately on `True` and `False` (each answering
what you'd expect for its own truth value) — see
[Blocks and control flow](#blocks-and-control-flow) for worked examples
of all of these. Eager `&`/`|` are **not** implemented — see
[Known limitations](#known-limitations).

| Selector | Behavior |
|---|---|
| `printString` | `'true'` / `'false'` |
| `ifTrue:` | Evaluates and answers the block argument's `value` if the receiver is `true`; otherwise `nil`, block not evaluated. |
| `ifFalse:` | Mirror image of `ifTrue:`. |
| `ifTrue:ifFalse:` | Evaluates and answers whichever block matches; the other is never evaluated. |
| `and:` / `or:` | Short-circuiting: `and:`'s block only runs if the receiver is `true`, `or:`'s only if the receiver is `false`. |
| `not` | `true` ↔ `false`. |

### SmallInteger

| Selector | Behavior |
|---|---|
| `+ - * /` | Arithmetic on native `long`s. `/` is **C truncating integer division**, not real Smalltalk's exact-Fraction division: `7 / 2` is `3`, `-7 / 2` is `-3`. Dividing by zero prints an error to stderr and evaluates to `nil` rather than raising an exception (no exception system yet). |
| `= < > <= >=` | Comparisons; answer `true`/`false`. Unchecked: comparing against a non-SmallInteger argument doesn't error, it just reads that oop's bits as if it were a tagged integer and produces a meaningless (usually `false`) result — see [Known limitations](#known-limitations). |
| `negated` | Unary negation. |
| `factorial` | Iterative, not the classic recursive Smalltalk-method definition (there's no method-definition syntax yet to write it that way). Errors (prints to stderr, returns `nil`) on a negative receiver. No overflow checking. |
| `printString` | Decimal string, e.g. `'27'`. |

### String

An owned, immutable-in-practice byte buffer (nothing in the language lets
you mutate one in place yet).

| Selector | Behavior |
|---|---|
| `size` | Length as a SmallInteger. |
| `,` | Concatenation, returns a new String. |
| `=` | Byte-for-byte content equality. |
| `printString` | Source-literal form: wraps in `'single quotes'`, doubling any embedded quotes — round-trips through the reader. |

### Symbol

| Selector | Behavior |
|---|---|
| `asString` | The name without the leading `#`, as a String. |
| `printString` | The name *with* the leading `#`, as a String (e.g. `#foo printString` is `'#foo'`). |

Symbols are interned at parse/eval time (see [Symbols](#symbols)) but
there's no `==` primitive yet to check identity from the REPL directly.

### Block

Every `[ ... ]` literal's class. See
[Blocks and control flow](#blocks-and-control-flow) for the full grammar
and worked examples, including closures and non-local return.

| Selector | Behavior |
|---|---|
| `value` | Invokes a 0-parameter block, answers its result. |
| `value:` | Invokes a 1-parameter block with the given argument. |
| `value:value:` | Invokes a 2-parameter block with the given arguments. |
| `whileTrue:` | Receiver and argument are both 0-param blocks: re-evaluates the receiver before each iteration, running the argument block while it answers `true`. Answers `nil`. |
| `whileFalse:` | Mirror image of `whileTrue:`. |

Sending `value`/`value:`/`value:value:` to a block with a different
number of parameters than the selector implies doesn't crash: it prints
`error: wrong number of block arguments (expected N, got M)` and answers
`nil`. There's no `value:value:value:` or variadic
`valueWithArguments:` yet.

### Class — every class's class

Every class object (`Object`, `Point`, `Animal`, ...) is an instance of
`Class`; sending it these selectors is how you define more of the class
library at runtime. See [Defining classes and methods](#defining-classes-and-methods)
for the full grammar and worked examples.

| Selector | Behavior |
|---|---|
| `new` | A fresh instance, all instance variables `nil`. |
| `subclass:instanceVariableNames:` | Defines and answers a new subclass; binds its name as a global variable. |
| `compile:` | Parses a String as method source and installs it, replacing any existing method with the same selector; answers `true`/`false`. |
| `printString` | The class's own name, e.g. `Point printString` is `'Point'` (**not** `'a Point'` — this overrides the `Object` default). |

## Error handling

There's no exception system. Every failure mode below prints a message to
**stderr** and evaluates the whole enclosing expression to **`nil`**,
without stopping the REPL:

- Sending a selector a class doesn't implement (anywhere in its superclass
  chain): `error: SmallInteger does not understand #foo`
- Referencing a variable that was never assigned:
  `error: undefined variable 'x'`
- Dividing a SmallInteger by zero: `error: division by zero`
- Taking the `factorial` of a negative SmallInteger:
  `error: factorial of a negative number`
- Using `self` or `super` outside a method body: `error: 'self' used
  outside a method` (or `'super'`)
- A malformed `compile:` argument: `error: compile: <message>`, and the
  send answers `false` rather than `nil` (it's the one error case with a
  meaningful non-nil failure value, since `compile:`'s normal success
  value is also a Boolean)
- Sending `value`/`value:`/`value:value:` to a block with the wrong
  number of parameters: `error: wrong number of block arguments
  (expected N, got M)`
- Using `^` where there's no lexically enclosing method at all (e.g. a
  block defined and invoked directly at the REPL top level): `error: '^'
  used outside a method`

A genuine **parse** error (malformed syntax, not a runtime failure) prints
`parse error: <message>` to stdout instead, and also just moves on to the
next line. This applies to expressions typed at the REPL; a parse error
inside a `compile:` argument is the `error: compile: ...` stderr case
above instead, since by then it's a runtime message send, not something
the REPL's own reader rejected.

## Known limitations

Deliberate scope boundaries for this milestone, not bugs — tracked in
`ROADMAP.md`:

- No comments.
- No floats, fractions, or big integers — just native `long`
  SmallIntegers, with no overflow checking. Signed overflow is undefined
  behavior in C; in practice this compiler wraps around silently, but
  don't rely on that.
- Reflection is minimal: `class` exists, but no `isKindOf:`,
  `respondsTo:`, method introspection, or listing a class's instance
  variables/methods from the language itself.
- No real metaclass hierarchy: every class's `class` is the same `Class`
  object (`Point class printString` is `'Class'`, not `'Point class'`),
  and there's no way to define a class-side ("class method") selector —
  `compile:` only ever installs an *instance*-side method.
- No accessor generation: `subclass:instanceVariableNames:` doesn't create
  getters/setters, you write `x  ^x` by hand via `compile:`.
- `new` always allocates the plain instance-variable-array layout; don't
  subclass `String`/`Symbol`/`SmallInteger` (see
  [Defining classes and methods](#defining-classes-and-methods)).
- No `==` (identity comparison) or `~=`/`~~` on anything.
- No eager Boolean `&`/`|` (the short-circuiting, block-based `and:`/`or:`
  exist instead — see
  [Blocks and control flow](#blocks-and-control-flow)).
- Block literals have no `| temp1 temp2 |` declaration of their own in
  this milestone, only parameters — use a temp on the enclosing method.
- No `value:value:value:` or a variadic `valueWithArguments:` for blocks
  (no `Array` yet to pass one as).
- Cascading directly off a bare `super` receiver (`super foo; bar`) loses
  super-dispatch on every cascaded message after the first — unchanged
  from Milestone 3, not fixed by blocks landing.
- No collections (`Array`, `OrderedCollection`, `Dictionary`, ...).
- No exceptions — every runtime error degrades to `nil` (or `false` for
  `compile:`) plus a stderr message, as described above. There's also no
  way to *catch* a non-local return or otherwise intercept one in flight.
- Primitives generally don't type-check their arguments before using them
  (e.g. `SmallInteger>>+` assumes its argument is a SmallInteger, and
  `ifTrue:`/`whileTrue:` assume their block arguments really are Blocks);
  passing the wrong type doesn't reliably error, it just produces a
  nonsense result or, for a wrong-layout heap object, could misread
  memory outright.
- Workspace variables (`x := ...` at the REPL) are one flat global table
  for the whole process. Method arguments/temps, and now block
  parameters, are scoped to their own activation and its lexical chain
  (see [Blocks and control flow](#blocks-and-control-flow)) — genuine
  closures, not dynamic scoping.
- Memory is garbage collected automatically as of this milestone (see
  `../CLAUDE.md` for the mechanism) — there's no language-level way to
  force, disable, or observe a collection (no `Smalltalk collectGarbage`
  or similar), and no way to run cleanup code when an object is
  reclaimed (no finalizers/`#finalize`).

## See also

- `ROADMAP.md` — milestone plan and progress, with REPL examples per
  milestone.
- `../CLAUDE.md` — internal architecture: object representation, dispatch,
  parser structure, and notable bugs already fixed.
- `../examples/` — fuller, runnable programs (`./bin/smalltalk
  examples/point.st`, or `make examples` to check them all at once);
  narrated in `../examples/README.md` since the language has no comment
  syntax of its own.
