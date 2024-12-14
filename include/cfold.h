/**
 * @file cfold.h
 * @author Matt Iselin (matthew@theiselins.net)
 * @brief Constant folding pass
 * @version 0.1
 * @date 2024-12-07
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef _MATTC_CFOLD_H
#define _MATTC_CFOLD_H

#include "ast.h"

struct cfolder;

struct cfolder *new_cfolder(struct ast_program *, struct compiler *);
int cfolder_run(struct cfolder *);
void destroy_cfolder(struct cfolder *);

#endif
