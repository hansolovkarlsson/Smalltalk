#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "bytecode.h"

/* Compiles a method's statement list (MethodNode.statements, parser.c's
 * parseMethod()) into bytecode. Falling off the end without an explicit
 * '^' answers self, not the last statement's value -- real Smalltalk
 * semantics, matching the tree-walking evaluator this replaces. */
CompiledCode *compileMethodBody(AstNode **statements, int statementCount);

/* Compiles a block literal's statement list. Falling off the end without
 * an explicit '^' answers the last statement's value (nil if the block
 * body is empty) -- genuinely different from a method's default, not
 * just a formatting choice; see compiler.c. */
CompiledCode *compileBlockBody(AstNode **statements, int statementCount);

/* Compiles a single top-level expression (one REPL line, or one
 * tests/test_main.c evalString() call) as if it were a one-statement
 * block body -- same "answers its own value" semantics, used only
 * because a bare expression isn't wrapped in [ ]. */
CompiledCode *compileTopLevelExpression(AstNode *expr);

#endif
