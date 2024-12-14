/**
 * @file typecheck.h
 * @author Matt Iselin (matthew@theiselins.net)
 * @brief MattC type checking pass
 * @version 0.1
 * @date 2024-12-06
 *
 * @copyright Copyright (c) 2024
 *
 * The type checking pass serves two main purposes. The first is to ensure that
 * types are used correctly as a semantic pass. The second is to annotate the
 * AST with types for use in the code generation pass. After this pass, the AST
 * is fully annotated and ready for code generation.
 */

#ifndef _MATTC_TYPECHECK_H
#define _MATTC_TYPECHECK_H

#include "ast.h"

struct typecheck;

struct typecheck *new_typecheck(struct ast_program *, struct compiler *);

/**
 * @brief Run the type checking pass on the AST associated with the type
 * checker.
 *
 * @return int 0 if successful, non-zero otherwise.
 */
int typecheck_run(struct typecheck *);

void destroy_typecheck(struct typecheck *);

#endif
