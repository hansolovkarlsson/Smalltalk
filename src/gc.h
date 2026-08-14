#ifndef GC_H
#define GC_H

#include <stddef.h>

#include "object.h"

/* Every gcAlloc()'d block is tagged with one of these so mark/sweep know
 * how to trace and (for GC_KIND_ACTIVATION) how to free it. Oops
 * self-describe their concrete layout via `isa` once you know they ARE an
 * oop (see gc.c's gcMarkOop()), so this only needs to distinguish "an oop"
 * from "an Activation" (activation.h), the one heap-allocated thing in
 * this VM that isn't itself a tagged oop but still needs tracing/freeing.
 *
 * A third kind, GC_KIND_OOP_ARRAY, existed through Milestone 5 for message-
 * send argument arrays, which back then were separately malloc'd (eval.c's
 * old AST_KEYWORD_SEND/AST_CASCADE) and reachable only via conservative
 * scanning -- which finds a stack word that IS a live header's address,
 * but never dereferences into what that header's payload contains looking
 * for further oops. A primitive like Block>>whileTrue: holding onto an
 * argument across many loop iterations could have it collected out from
 * under a still-running loop as a result (a real, initially-missed bug,
 * see CLAUDE.md's Milestone 5 history). Milestone 6's bytecode VM (eval.c's
 * vmRun()) obsoletes this by construction: OP_SEND's args are a slice of
 * vmRun()'s own C-local operand stack, not a separate heap allocation, so
 * conservative scanning already sees their contents directly, the same
 * way it always saw an ordinary stack-resident local or parameter. */
typedef enum { GC_KIND_OOP, GC_KIND_ACTIVATION } GCKind;

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
