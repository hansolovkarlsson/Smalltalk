#include "compiler.h"

#include <stdlib.h>

typedef struct {
    Instr *instrs;
    int count;
    int capacity;
} CodeBuilder;

static void cbInit(CodeBuilder *cb) {
    cb->instrs = NULL;
    cb->count = 0;
    cb->capacity = 0;
}

static Instr *cbEmit(CodeBuilder *cb, Opcode op) {
    if (cb->count == cb->capacity) {
        cb->capacity = cb->capacity ? cb->capacity * 2 : 8;
        cb->instrs = realloc(cb->instrs, sizeof(Instr) * (size_t)cb->capacity);
    }
    Instr *ins = &cb->instrs[cb->count++];
    ins->op = op;
    return ins;
}

static CompiledCode *cbFinish(CodeBuilder *cb) {
    CompiledCode *code = malloc(sizeof(CompiledCode));
    code->instrs = cb->instrs;
    code->count = cb->count;
    return code;
}

static int isSuperNode(AstNode *node) {
    return node->type == AST_SUPER;
}

static void compileExpr(AstNode *node, CodeBuilder *cb);

/* Shared by unary/binary/keyword sends (the only difference between them
 * is how many arg AstNodes there are): a `super` receiver never gets
 * itself evaluated/pushed -- the dispatch receiver is always
 * currentActivation->self at runtime (see eval.c's vmRun() OP_SEND_SUPER),
 * matching how the tree-walking evaluator this replaces special-cased
 * isSuperNode(recvNode) before ever evaluating it. */
static void compileSendArgsAndEmit(CodeBuilder *cb, AstNode *recvNode, const char *selector,
                                    AstNode **argNodes, int argc) {
    Opcode op;
    if (isSuperNode(recvNode)) {
        op = OP_SEND_SUPER;
    } else {
        compileExpr(recvNode, cb);
        op = OP_SEND;
    }
    for (int i = 0; i < argc; i++) {
        compileExpr(argNodes[i], cb);
    }
    Instr *ins = cbEmit(cb, op);
    ins->operand.send.selector = selector;
    ins->operand.send.argc = argc;
}

static void compileExpr(AstNode *node, CodeBuilder *cb) {
    switch (node->type) {
        case AST_INT_LITERAL:
            cbEmit(cb, OP_PUSH_LITERAL)->operand.literal = makeSmallInteger(node->as.intValue);
            return;
        case AST_NIL_LITERAL:
            cbEmit(cb, OP_PUSH_NIL);
            return;
        case AST_TRUE_LITERAL:
            cbEmit(cb, OP_PUSH_TRUE);
            return;
        case AST_FALSE_LITERAL:
            cbEmit(cb, OP_PUSH_FALSE);
            return;
        case AST_STRING_LITERAL:
            cbEmit(cb, OP_PUSH_STRING_LITERAL)->operand.stringValue = node->as.stringValue;
            return;
        case AST_SYMBOL_LITERAL:
            cbEmit(cb, OP_PUSH_SYMBOL_LITERAL)->operand.symbolName = node->as.symbolName;
            return;
        case AST_SELF:
        case AST_SUPER:
            /* A bare `super` (not itself a send's receiver) evaluates to
             * self's value, same as `self` -- only a message *sent to*
             * `super` dispatches differently (see compileSendArgsAndEmit). */
            cbEmit(cb, OP_PUSH_SELF);
            return;
        case AST_VARIABLE_REF:
            cbEmit(cb, OP_PUSH_VAR)->operand.varName = node->as.variableName;
            return;
        case AST_ASSIGNMENT:
            compileExpr(node->as.assignment.value, cb);
            cbEmit(cb, OP_STORE_VAR)->operand.varName = node->as.assignment.name;
            return;
        case AST_UNARY_SEND:
            compileSendArgsAndEmit(cb, node->as.unarySend.receiver, node->as.unarySend.selector, NULL, 0);
            return;
        case AST_BINARY_SEND: {
            AstNode *argArr[1];
            argArr[0] = node->as.binarySend.arg;
            compileSendArgsAndEmit(cb, node->as.binarySend.receiver, node->as.binarySend.selector, argArr,
                                    1);
            return;
        }
        case AST_KEYWORD_SEND:
            compileSendArgsAndEmit(cb, node->as.keywordSend.receiver, node->as.keywordSend.selector,
                                    node->as.keywordSend.args, node->as.keywordSend.argCount);
            return;
        case AST_CASCADE: {
            /* Limitation carried over unchanged from the tree-walking
             * evaluator: if the cascade's receiver expression is literally
             * `super`, every cascaded message still dispatches via plain
             * OP_SEND (classOf(self)-based), never OP_SEND_SUPER -- see
             * the matching comment that used to live on eval()'s
             * AST_CASCADE case. Cascading directly off a bare `super`
             * receiver is rare enough that this narrow gap was never
             * worth threading super-dispatch through cascade compilation
             * too, in either the old evaluator or this one. */
            compileExpr(node->as.cascade.receiver, cb);
            int n = node->as.cascade.messageCount;
            for (int i = 0; i < n; i++) {
                CascadeMessage *m = &node->as.cascade.messages[i];
                int isLast = (i == n - 1);
                if (!isLast) cbEmit(cb, OP_DUP);
                for (int j = 0; j < m->argCount; j++) {
                    compileExpr(m->args[j], cb);
                }
                Instr *ins = cbEmit(cb, OP_SEND);
                ins->operand.send.selector = m->selector;
                ins->operand.send.argc = m->argCount;
                if (!isLast) cbEmit(cb, OP_POP);
            }
            return;
        }
        case AST_BLOCK_LITERAL: {
            BlockTemplate *tmpl = malloc(sizeof(BlockTemplate));
            tmpl->paramNames = node->as.blockLiteral.paramNames;
            tmpl->paramCount = node->as.blockLiteral.paramCount;
            tmpl->code = compileBlockBody(node->as.blockLiteral.statements, node->as.blockLiteral.statementCount);
            cbEmit(cb, OP_PUSH_BLOCK)->operand.block = tmpl;
            return;
        }
        case AST_RETURN:
            /* Only ever reached as a top-level method/block statement
             * (see compileStatements() below), never nested inside
             * another expression -- the grammar has no way to produce
             * that. compileStatements() handles '^' itself rather than
             * dispatching here, so this is unreachable in practice; kept
             * only as a safe fallback matching the old eval()'s. */
            compileExpr(node->as.returnValue, cb);
            return;
    }
}

/* Shared by a method body, a block body, and a top-level expression
 * (wrapped as a one-statement "block"): a '.'-separated statement
 * sequence where any statement may be '^expr', compiled to jump out via
 * OP_RETURN regardless of its position (matching the old
 * runStatementSequence(), which stopped at the first '^' it reached
 * during execution -- anything textually after one was already
 * unreachable at runtime, so it doesn't matter that we still compile it
 * normally here). isMethodBody controls only what happens when execution
 * falls off the end without hitting a '^': methods answer self (ignoring
 * the last statement's value), blocks/top-level answer the last
 * statement's value (or nil if there are no statements at all). */
static void compileStatements(AstNode **stmts, int count, CodeBuilder *cb, int isMethodBody) {
    for (int i = 0; i < count; i++) {
        AstNode *stmt = stmts[i];
        int isLast = (i == count - 1);

        if (stmt->type == AST_RETURN) {
            compileExpr(stmt->as.returnValue, cb);
            cbEmit(cb, OP_RETURN);
            continue;
        }

        compileExpr(stmt, cb);
        if (!isLast) {
            cbEmit(cb, OP_POP);
        } else if (isMethodBody) {
            cbEmit(cb, OP_POP);
            cbEmit(cb, OP_PUSH_SELF);
        }
        /* else: last statement of a block/top-level body -- leave its
         * value as the result. */
    }

    if (count == 0) {
        cbEmit(cb, isMethodBody ? OP_PUSH_SELF : OP_PUSH_NIL);
    }
}

CompiledCode *compileMethodBody(AstNode **statements, int statementCount) {
    CodeBuilder cb;
    cbInit(&cb);
    compileStatements(statements, statementCount, &cb, 1);
    return cbFinish(&cb);
}

CompiledCode *compileBlockBody(AstNode **statements, int statementCount) {
    CodeBuilder cb;
    cbInit(&cb);
    compileStatements(statements, statementCount, &cb, 0);
    return cbFinish(&cb);
}

CompiledCode *compileTopLevelExpression(AstNode *expr) {
    CodeBuilder cb;
    cbInit(&cb);
    AstNode *stmts[1];
    stmts[0] = expr;
    compileStatements(stmts, 1, &cb, 0);
    return cbFinish(&cb);
}
