/**
 * @file types.h
 * @author Matt Iselin (matthew@theiselins.net)
 * @brief Type system definitions and utilities
 * @version 0.1
 * @date 2024-12-07
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef _MATTC_TYPES_H
#define _MATTC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define TYPE_FLAG_PTR (1U << 0)

enum ast_ty_id {
  AST_TYPE_ERROR = 0,
  AST_TYPE_TBD,  // yet to be determined by type checking pass
  AST_TYPE_INTEGER,
  AST_TYPE_STRING,
  AST_TYPE_CHAR,
  AST_TYPE_FLOAT,
  AST_TYPE_FVEC,
  AST_TYPE_VOID,
  AST_TYPE_ENUM,
  AST_TYPE_STRUCT,
  AST_TYPE_ARRAY,
};

struct ast_ty {
  enum ast_ty_id ty;
  uint64_t flags;
  union {
    struct {
      int is_signed;
      size_t width;
    } integer;
    struct {
      size_t width;
    } fvec;
    struct {
      // TBD - names of enum values
      int tbd;
    } enumty;
    struct {
      // TBD - fields, types of fields
      int tbd;
    } structty;
    struct {
      size_t width;
      struct ast_ty *element_ty;
    } array;
  };
};

struct ast_ty type_tbd(void);
struct ast_ty type_void(void);
struct ast_ty type_error(void);

/**
 * @brief Returns true if the given type is an error.
 */
int type_is_error(struct ast_ty *);

/**
 * @brief Returns true if the given type is a TBD type.
 */
int type_is_tbd(struct ast_ty *);

/**
 * @brief Check if two types are the same type class (e.g. integer, float, etc).
 *
 * Disregards specific type data such as width or element count.
 */
int same_type_class(struct ast_ty *, struct ast_ty *);

/**
 * @brief Check if two types are the same type, including element count and width.
 *
 * This is extremely specific; in many cases it's better to use same_type_class and
 * a widening/narrowing check instead.
 */
int same_type(struct ast_ty *, struct ast_ty *);

/**
 * @brief Check if ty1 is narrower than ty2, i.e. it has fewer bits.
 */
int narrower_type(struct ast_ty *, struct ast_ty *);

/**
 * @brief Check if ty1 is wider than ty2, i.e. it has more bits.
 */
int wider_type(struct ast_ty *, struct ast_ty *);

/**
 * @brief Returns a user-friendly name for the type.
 *
 * Returns static storage - not re-entrant.
 */
const char *type_name(struct ast_ty *);

/**
 * @brief Retrieves a user-friendly name for the type.
 */
void type_name_into(struct ast_ty *, char *, size_t);

/**
 * @brief Can ty1 be casted into ty2?
 */
int can_cast(struct ast_ty *, struct ast_ty *);

#endif
