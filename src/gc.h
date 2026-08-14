#ifndef GC_H
#define GC_H

#include <stddef.h>

#include "object.h"

/* Every gcAlloc()'d block is tagged with one of these so mark/sweep know
 * how to trace and (for GC_KIND_ACTIVATION) how to free it. Oops
 * self-describe their concrete layout via `isa` once you know they ARE an
 * oop (see gc.c's gcMarkOop()), so this only needs to distinguish "an oop"
 * from "an Activation" (activation.h) from "an oop[] array" -- the two
 * heap-allocated things in this VM that aren't themselves a tagged oop but
 * still need tracing/freeing.
 *
 * GC_KIND_OOP_ARRAY exists for exactly one reason: a message send's
 * evaluated arguments (built in eval.c's AST_KEYWORD_SEND/AST_CASCADE,
 * since a keyword selector's arity isn't known until parse time, unlike a
 * binary send's fixed-size on-stack `oop args[1]`). That array is handed
 * down into a primitive as a plain `oop *`, and some primitives
 * (Block>>whileTrue:/whileFalse:) hold onto an argument from it across
 * many nested calls that can each trigger a collection. If the array were
 * plain malloc'd, none of gcMarkOop()'s roots (env, symbols, the
 * activation chain) would ever reach into it, and conservative stack
 * scanning only checks whether a stack word IS a live GCHeader's payload
 * address -- it doesn't dereference that payload looking for further
 * oops inside it. Tagging the array itself as GC_KIND_OOP_ARRAY closes
 * that gap: scanning finds the primitive's own `oop *args` parameter
 * (an ordinary stack-resident local/parameter) pointing at the array,
 * recognizes its kind, and marks every oop inside it too -- see gc.c's
 * markCandidate(). This was a real, initially-missed bug (see CLAUDE.md):
 * `[cond] whileTrue: [body]` could have its `body` block collected out
 * from under the loop after enough iterations, corrupting memory. */
typedef enum { GC_KIND_OOP, GC_KIND_ACTIVATION, GC_KIND_OOP_ARRAY } GCKind;

/* Call once, as early as possible in main() -- before bootstrapClasses()
 * or anything else can allocate -- passing the address of a local
 * variable in main() itself. Conservative stack scanning (see gc.c) needs
 * an approximate "bottom of the stack" to scan up from; a local declared
 * near the top of main() is close enough (nothing interesting is ever
 * live below it). */
void gcInit(void *stackBottom);

/* Allocates a zeroed block of `size` bytes, GC-managed: it may be moved
 * onto the free list and reclaimed by a future collection once nothing
 * reachable from the roots points to it anymore. May trigger a collection
 * before allocating. `kind` must be GC_KIND_ACTIVATION for an
 * activation.h Activation and GC_KIND_OOP for anything reachable as a
 * tagged `oop` (Object/StringObject/SymbolObject/ClassObject/BlockObject)
 * -- gcAlloc() itself doesn't care which concrete oop layout it is, only
 * gcMarkOop() needs to know that, via `isa`. */
void *gcAlloc(GCKind kind, size_t size);

/* Marks o (and, transitively, everything reachable from it) as live for
 * the current collection. Exported so environment.c/symbol.c can mark
 * their own root tables (workspace variables, the interned-symbol table)
 * without gc.c needing to know their internals -- see envMarkRoots()/
 * symbolMarkRoots(). A no-op on a SmallInteger. */
void gcMarkOop(oop o);

/* Forces an immediate collection regardless of the allocation threshold,
 * and lets tests move that threshold -- both purely for deterministic
 * testing (see tests/test_main.c); production code never needs either,
 * since gcAlloc() triggers collections on its own. */
void gcCollectNow(void);
void gcSetThreshold(size_t bytes);

size_t gcBytesAllocated(void);
size_t gcLiveCount(void);

#endif
