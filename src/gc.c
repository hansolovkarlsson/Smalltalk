#include "gc.h"
#include "activation.h"
#include "block.h"
#include "class.h"
#include "environment.h"
#include "eval.h"
#include "symbol.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Prepended to every gcAlloc()'d block; the pointer callers get back
 * (and store as an `oop`, or as an `Activation *`) is (header + 1), so
 * existing code that just wants "a malloc'd block" is unaffected -- the
 * header is invisible to it. HEADER_OF()/PAYLOAD_OF() convert between the
 * two views. */
typedef struct GCHeader {
    struct GCHeader *next;
    size_t size;
    GCKind kind;
    int marked;
} GCHeader;

#define HEADER_OF(ptr) (((GCHeader *)(ptr)) - 1)
#define PAYLOAD_OF(header) ((void *)((header) + 1))

/* Only for markCandidate() below: candidate is an unvalidated bit pattern
 * found by conservative stack/register scanning, not necessarily a real
 * pointer into any object at all. Forming an out-of-bounds *pointer* from
 * one -- even just computing it, never dereferencing it -- is undefined
 * behavior in C (a real pointer's arithmetic is only well-defined within
 * or one-past its own object), which is exactly what HEADER_OF()'s plain
 * pointer subtraction would do here. Doing the same subtraction as
 * uintptr_t arithmetic and converting to a pointer only afterward sidesteps
 * that UB while producing the identical address on every mainstream
 * platform -- the standard trick conservative collectors (e.g. the
 * Boehm-Demers-Weiser collector) use for this. headerSetContains() always
 * validates the result before it's ever dereferenced. */
static GCHeader *candidateHeaderOf(void *candidate) {
    uintptr_t addr = (uintptr_t)candidate - sizeof(GCHeader);
    return (GCHeader *)addr;
}

static GCHeader *allocList = NULL;
static size_t bytesAllocated = 0;
static size_t gcThreshold = 64 * 1024;
static void *stackBottom = NULL;

void gcInit(void *sb) {
    stackBottom = sb;
}

void gcSetThreshold(size_t bytes) {
    gcThreshold = bytes;
}

size_t gcBytesAllocated(void) {
    return bytesAllocated;
}

size_t gcLiveCount(void) {
    size_t n = 0;
    for (GCHeader *h = allocList; h; h = h->next) n++;
    return n;
}

static void gcMarkActivation(Activation *act);

void gcMarkOop(oop o) {
    if (oopIsSmallInteger(o) || o == 0) return;
    GCHeader *h = HEADER_OF(o);
    if (h->marked) return;
    h->marked = 1;

    /* Every oop layout starts with `isa` (object.h/CLAUDE.md), so this is
     * safe regardless of which concrete struct o actually points to.
     * String/Symbol/Class have no oop-valued fields to trace further --
     * Symbol's `name` and Class's `thisClass` are plain C pointers into
     * permanent (never gcAlloc'd) storage, not oops. */
    STClass *cls = classOf(o);
    if (cls == StringClass || cls == SymbolClass || cls == ClassClass) return;
    if (cls == BlockClass) {
        gcMarkActivation(((BlockObject *)o)->homeActivation);
        return;
    }
    Object *obj = (Object *)o;
    for (int i = 0; i < cls->instanceVarCount; i++) {
        gcMarkOop(obj->fields[i]);
    }
}

/* Mirrors gcMarkOop() for the one heap-allocated thing that isn't a
 * tagged oop (see gc.h's GCKind doc comment). Reachable either via
 * evalCurrentActivation()'s ->caller chain (activations of calls still in
 * progress) or via a live BlockObject's ->homeActivation and that
 * activation's ->lexicalParent chain (an escaped closure) -- both cases
 * funnel through here. */
static void gcMarkActivation(Activation *act) {
    if (!act) return;
    GCHeader *h = HEADER_OF(act);
    if (h->marked) return;
    h->marked = 1;

    gcMarkOop(act->self);
    for (int i = 0; i < act->argCount; i++) gcMarkOop(act->argValues[i]);
    for (int i = 0; i < act->tempCount; i++) gcMarkOop(act->tempValues[i]);
    gcMarkActivation(act->lexicalParent);
    gcMarkActivation(act->caller);
    gcMarkActivation(act->homeMethodActivation);
}

/* A snapshot of every currently-live header's address, rebuilt at the
 * start of each collection (before anything is swept) so a conservative
 * stack/register scan (see scanConservativeRoots() below) can check a
 * candidate word for *exact* membership in O(1) average instead of just
 * guessing it "looks like" a pointer, the way Boehm-style collectors
 * must when they can't enumerate every live allocation up front. This is
 * what keeps conservative scanning here fully precise rather than
 * merely probably-correct. */
typedef struct HeaderSet {
    GCHeader **slots;
    size_t capacity;
} HeaderSet;

static size_t hashPtr(const void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x;
}

static void headerSetBuild(HeaderSet *set) {
    size_t count = 0;
    for (GCHeader *h = allocList; h; h = h->next) count++;
    size_t capacity = 8;
    while (capacity < count * 4) capacity *= 2; /* keep load factor low */
    set->slots = calloc(capacity, sizeof(GCHeader *));
    set->capacity = capacity;
    for (GCHeader *h = allocList; h; h = h->next) {
        size_t idx = hashPtr(h) % capacity;
        while (set->slots[idx]) idx = (idx + 1) % capacity;
        set->slots[idx] = h;
    }
}

static int headerSetContains(HeaderSet *set, GCHeader *h) {
    size_t idx = hashPtr(h) % set->capacity;
    while (set->slots[idx]) {
        if (set->slots[idx] == h) return 1;
        idx = (idx + 1) % set->capacity;
    }
    return 0;
}

static void headerSetFree(HeaderSet *set) {
    free(set->slots);
}

/* See gc.h's GC_KIND_OOP_ARRAY doc comment: an evaluated message-send
 * argument list, only ever reachable via conservative scanning (never a
 * precise root), which is why marking it happens here rather than
 * alongside gcMarkOop()/gcMarkActivation(). */
static void gcMarkOopArray(GCHeader *h) {
    if (h->marked) return;
    h->marked = 1;
    oop *elems = (oop *)PAYLOAD_OF(h);
    size_t n = h->size / sizeof(oop);
    for (size_t i = 0; i < n; i++) {
        gcMarkOop(elems[i]);
    }
}

static void markCandidate(void *candidate, HeaderSet *set) {
    uintptr_t addr = (uintptr_t)candidate;
    if (addr == 0 || (addr & 1) != 0) return; /* tag bit set -> a SmallInteger, never a real pointer */
    GCHeader *h = candidateHeaderOf(candidate);
    if (!headerSetContains(set, h)) return; /* just some non-pointer bit pattern */
    if (h->kind == GC_KIND_ACTIVATION) {
        gcMarkActivation((Activation *)candidate);
    } else if (h->kind == GC_KIND_OOP_ARRAY) {
        gcMarkOopArray(h);
    } else {
        gcMarkOop((oop)candidate);
    }
}

/* Word-aligned steps: every real allocation this VM ever hands out as a
 * pointer is itself pointer-aligned, so a genuine live pointer can only
 * ever appear at a pointer-aligned offset -- scanning at finer granularity
 * would just waste time re-checking the same bytes without finding
 * anything scanning at coarser granularity could miss. */
static void scanRange(void *lo, void *hi, HeaderSet *set) {
    char *start = (char *)(lo < hi ? lo : hi);
    char *end = (char *)(lo < hi ? hi : lo);
    for (char *p = start; p + sizeof(void *) <= end; p += sizeof(void *)) {
        void *candidate;
        memcpy(&candidate, p, sizeof(void *)); /* avoid unaligned-read/strict-aliasing UB */
        markCandidate(candidate, set);
    }
}

/* Conservative root scan: anything left in a C local variable or a CPU
 * register at the moment a collection is triggered -- e.g. `oop receiver
 * = eval(...)` in eval.c, computed but not yet stored anywhere a precise
 * root walk would find it. setjmp() spills the callee-saved registers
 * (implementation-defined in general, but reliable across every
 * mainstream ABI/compiler this project targets) into `regs`, which gets
 * scanned as part of the stack region right along with it.
 * __builtin_frame_address(0) approximates "the innermost live frame right
 * now"; combined with stackBottom (recorded once, early in main(), see
 * gcInit()), that covers every C stack frame currently in progress,
 * however many calls deep. This is what lets a collection safely trigger
 * from *anywhere*, mid-expression or mid-loop, not just between top-level
 * REPL statements. */
static void scanConservativeRoots(HeaderSet *set) {
    jmp_buf regs;
    (void)setjmp(regs); /* only the register-spilling side effect matters, not which way it returned */
    void *innermost = __builtin_frame_address(0);
    scanRange(innermost, stackBottom, set);
    scanRange(&regs, (char *)&regs + sizeof(regs), set);
}

static void sweep(void) {
    GCHeader **link = &allocList;
    while (*link) {
        GCHeader *h = *link;
        if (!h->marked) {
            *link = h->next;
            if (h->kind == GC_KIND_ACTIVATION) {
                /* argValues/tempValues are plain malloc'd buffers owned
                 * by this activation, not separately gcAlloc'd (see
                 * CLAUDE.md) -- freeing the activation must free them too. */
                Activation *act = (Activation *)PAYLOAD_OF(h);
                free(act->argValues);
                free(act->tempValues);
            }
            bytesAllocated -= h->size;
            free(h);
        } else {
            h->marked = 0; /* reset for the next collection */
            link = &h->next;
        }
    }
}

void gcCollectNow(void) {
    HeaderSet set;
    headerSetBuild(&set);

    gcMarkOop(nilObject);
    gcMarkOop(trueObject);
    gcMarkOop(falseObject);
    envMarkRoots();
    symbolMarkRoots();
    gcMarkActivation(evalCurrentActivation());
    scanConservativeRoots(&set);

    headerSetFree(&set);
    sweep();
}

void *gcAlloc(GCKind kind, size_t size) {
    if (bytesAllocated > gcThreshold) {
        gcCollectNow();
        /* Still mostly full after collecting: growing the threshold now
         * avoids thrashing (re-collecting every few bytes for no benefit)
         * on a program with a genuinely large live working set. */
        if (bytesAllocated > gcThreshold / 2) {
            gcThreshold *= 2;
        }
    }

    GCHeader *h = calloc(1, sizeof(GCHeader) + size);
    h->next = allocList;
    h->size = size;
    h->kind = kind;
    h->marked = 0;
    allocList = h;
    bytesAllocated += size;
    return PAYLOAD_OF(h);
}
