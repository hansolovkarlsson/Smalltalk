# Language Reference

What the interpreter actually accepts and does right now — Milestone 3.
This documents observable REPL behavior, not internals; see `../CLAUDE.md`
for how it's implemented and `ROADMAP.md` for what's coming next. Every
example below was run against the built interpreter, not written from
memory.

## Running it

```
$ make && ./smalltalk
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
activation, same way C's own call stack works), though there's no `[ ... ]`
block syntax yet to write loops or conditionals *inside* a method (see
[Known limitations](#known-limitations)), so a recursive method needs a
non-recursive way to terminate, which the language doesn't yet have — in
practice this makes recursive methods hard to write usefully until
Milestone 4 lands blocks and `ifTrue:ifFalse:`.

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

## The class library

Seven classes are bootstrapped in C: the six from Milestone 2 plus `Class`
(every class's class — see [Reflection](#reflection) below). Any number of
further classes can now be defined at runtime via `subclass:
instanceVariableNames:`, as shown above.

```
Object
├── UndefinedObject   (nil's class)
├── Boolean
│   ├── True
│   └── False
├── SmallInteger
├── String
├── Symbol
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

`Boolean` itself has no instances and defines nothing; `True` and `False`
each override `printString`. No `ifTrue:ifFalse:`, `&`, `|`, `not`, or
anything else yet — see [Known limitations](#known-limitations).

| Selector | Class | Behavior |
|---|---|---|
| `printString` | `True` | `'true'` |
| `printString` | `False` | `'false'` |

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
- Cascading directly off a bare `super` receiver (`super foo; bar`) loses
  super-dispatch on every cascaded message after the first — a documented
  gap, not planned to be fixed before blocks land and cascade parsing gets
  revisited anyway.
- `new` always allocates the plain instance-variable-array layout; don't
  subclass `String`/`Symbol`/`SmallInteger` (see
  [Defining classes and methods](#defining-classes-and-methods)).
- No `==` (identity comparison) or `~=`/`~~` on anything.
- No control flow at all: no `ifTrue:ifFalse:`, `and:`, `or:`,
  `whileTrue:`, no blocks to build them from — which also means a
  recursive method has no way to terminate itself yet.
- No collections (`Array`, `OrderedCollection`, `Dictionary`, ...).
- No exceptions — every runtime error degrades to `nil` (or `false` for
  `compile:`) plus a stderr message, as described above.
- Primitives generally don't type-check their arguments before using them
  (e.g. `SmallInteger>>+` assumes its argument is a SmallInteger); passing
  the wrong type doesn't reliably error, it just produces a nonsense
  result.
- Workspace variables (`x := ...` at the REPL) are one flat global table
  for the whole process. Method arguments/temps *are* scoped to their own
  activation now (see [Defining classes and methods](#defining-classes-and-methods)),
  but there's still no block-local scoping, since there are no blocks.

## See also

- `ROADMAP.md` — milestone plan and progress, with REPL examples per
  milestone.
- `../CLAUDE.md` — internal architecture: object representation, dispatch,
  parser structure, and notable bugs already fixed.
- `../examples/` — fuller, runnable programs (`./smalltalk
  examples/point.st`, or `make examples` to check them all at once);
  narrated in `../examples/README.md` since the language has no comment
  syntax of its own.
