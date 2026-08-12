# Language Reference

What the interpreter actually accepts and does right now — Milestone 2.
This documents observable REPL behavior, not internals; see `CLAUDE.md` for
how it's implemented and `ROADMAP.md` for what's coming next. Every example
below was run against the built interpreter, not written from memory.

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

`nil`, `true`, and `false` are reserved literal tokens recognized by the
parser itself, not ordinary variables — you can't reassign them (`nil :=
3` doesn't parse as an assignment; `nil` wins and `:= 3` is leftover,
unconsumed input, silently ignored — see below).

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

## The class library

Six classes exist, all bootstrapped in C — none are user-definable yet.

```
Object
├── UndefinedObject   (nil's class)
├── Boolean
│   ├── True
│   └── False
├── SmallInteger
├── String
└── Symbol
```

Every class inherits `printString` from `Object` unless listed otherwise
below; the default is `'a ClassName'` / `'an ClassName'` (with the article
chosen by whether the class name starts with a vowel). This is also what
the REPL calls to render every result, so anything without a more specific
override prints as `a Foo`.

### Object

| Selector | Behavior |
|---|---|
| `printString` | `'a ClassName'` / `'an ClassName'` — the fallback every other class overrides |

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

A genuine **parse** error (malformed syntax, not a runtime failure) prints
`parse error: <message>` to stdout instead, and also just moves on to the
next line.

## Known limitations

Deliberate scope boundaries for this milestone, not bugs — tracked in
`ROADMAP.md`:

- No comments.
- No floats, fractions, or big integers — just native `long`
  SmallIntegers, with no overflow checking. Signed overflow is undefined
  behavior in C; in practice this compiler wraps around silently, but
  don't rely on that.
- No reflection: no `class`, `isKindOf:`, `respondsTo:`, etc.
- No `==` (identity comparison) or `~=`/`~~` on anything.
- No control flow at all: no `ifTrue:ifFalse:`, `and:`, `or:`,
  `whileTrue:`, no blocks to build them from.
- No collections (`Array`, `OrderedCollection`, `Dictionary`, ...).
- No user-defined classes or methods — the six classes above are it.
- No exceptions — every runtime error degrades to `nil` plus a stderr
  message, as described above.
- Primitives generally don't type-check their arguments before using them
  (e.g. `SmallInteger>>+` assumes its argument is a SmallInteger); passing
  the wrong type doesn't reliably error, it just produces a nonsense
  result.
- Variables are one flat global table for the whole process — no scoping.

## See also

- `ROADMAP.md` — milestone plan and progress, with REPL examples per
  milestone.
- `CLAUDE.md` — internal architecture: object representation, dispatch,
  parser structure, and notable bugs already fixed.
