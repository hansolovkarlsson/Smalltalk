#ifndef BYTECODE_H
#define BYTECODE_H

#include "object.h"

/* PUSH_LITERAL only ever carries a SmallInteger: makeSmallInteger() is a
 * static-inline bit-twiddle (object.h), no heap allocation, so embedding
 * the resulting oop directly into compiled (permanent, never GC-traced)
 * code is safe forever. A String or Symbol literal is NOT precomputed
 * this way -- PUSH_STRING_LITERAL/PUSH_SYMBOL_LITERAL instead carry the
 * raw name (a permanent C string owned by the AST/intern table) and
 * construct the actual oop at *run* time, once pushed onto the VM's own
 * operand stack where it's immediately visible to conservative GC
 * scanning. A gcAlloc'd oop embedded in compiled code the collector never
 * traces into would become invisible to it the moment compilation
 * finished -- this is why that distinction matters, not just a style
 * choice (see CLAUDE.md). */
typedef enum {
    OP_PUSH_LITERAL,        /* operand.literal: a SmallInteger oop */
    OP_PUSH_STRING_LITERAL, /* operand.stringValue: raw C string, makeString()'d at runtime */
    OP_PUSH_SYMBOL_LITERAL, /* operand.symbolName: interned, internSymbol()'d at runtime */
    OP_PUSH_NIL,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_PUSH_SELF,   /* also used for a bare `super` not itself a send's receiver -- see compiler.c */
    OP_PUSH_VAR,    /* operand.varName: interned; resolved dynamically (activation chain, then global env) */
    OP_STORE_VAR,   /* operand.varName; peeks (doesn't pop) TOS -- assignment is an expression */
    OP_PUSH_BLOCK,  /* operand.block: BlockTemplate*, builds a BlockObject capturing the current frame */
    OP_SEND,        /* operand.send: {selector, argc}; stack has [receiver, arg1..argN] */
    OP_SEND_SUPER,  /* operand.send; stack has [arg1..argN] only -- receiver is always currentActivation->self */
    OP_POP,
    OP_DUP,
    OP_RETURN /* pops TOS, non-local return (longjmp) to the nearest lexically enclosing method */
} Opcode;

typedef struct Instr {
    Opcode op;
    union {
        oop literal;
        const char *stringValue;
        const char *symbolName;
        const char *varName;
        struct {
            const char *selector;
            int argc;
        } send;
        struct BlockTemplate *block;
    } operand;
} Instr;

/* A compiled method or block body: a flat instruction sequence -- the
 * "bytecode" in the classic sense, even though instructions here are a
 * struct array rather than packed bytes. That's a deliberate
 * simplification (see CLAUDE.md): operand encoding/decoding (varints,
 * alignment, endianness) is real complexity orthogonal to this
 * milestone's actual goal, a dispatch loop over compiled code instead of
 * walking an AST. Permanent once created, like the AstNode trees it
 * replaces as the thing actually executed -- never freed, never
 * GC-traced (it holds no oop values directly; see the Opcode doc comment
 * above for why literals are handled the way they are specifically to
 * keep that true). */
typedef struct CompiledCode {
    Instr *instrs;
    int count;
} CompiledCode;

/* Everything OP_PUSH_BLOCK needs to build a BlockObject (block.h) at
 * runtime: parameter names/count plus the block's own compiled body. One
 * of these exists per block *literal* in the source (compiled once, when
 * its enclosing method/block/top-level expression is compiled), shared by
 * every BlockObject created from it -- once per evaluation of that
 * PUSH_BLOCK instruction, exactly like the AstNode block-literal node it
 * replaces was already shared across invocations before this milestone. */
typedef struct BlockTemplate {
    char **paramNames; /* interned, paramCount entries; aliases the AST's own array */
    int paramCount;
    CompiledCode *code;
} BlockTemplate;

#endif
