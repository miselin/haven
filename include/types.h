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

#ifndef _HAVEN_TYPES_H
#define _HAVEN_TYPES_H

#include <stddef.h>
#include <stdint.h>

// Use AST_TYPE_POINTER instead.
// #define TYPE_FLAG_PTR (1U << 0)

// References another type. Used to avoid freeing struct field types in particular.
#define TYPE_FLAG_INDIRECT (1U << 1)

// This type is associated with a constant value. Used for literals to enable implicit
// type conversion for constants.
#define TYPE_FLAG_CONSTANT (1U << 2)

// This type creates a pointer reference. This changes how the inner expression is evaluated.
// In most cases, this will cause variables to resolve to their stack address instead of its value.
#define TYPE_FLAG_REFERENCE (1U << 3)

// All flags that typically matter for type comparison.
// indireect/constant are excluded as they are typically specific to an expression, not a type.
#define TYPE_FLAG_MASK_ALL ((~0U) ^ (TYPE_FLAG_INDIRECT | TYPE_FLAG_CONSTANT | TYPE_FLAG_REFERENCE))

enum ast_ty_id {
  AST_TYPE_ERROR = 0,
  AST_TYPE_TBD,  // yet to be determined by type checking pass
  AST_TYPE_INTEGER,
  AST_TYPE_STRING,
  AST_TYPE_FLOAT,
  AST_TYPE_FVEC,
  AST_TYPE_VOID,
  AST_TYPE_ENUM,
  AST_TYPE_STRUCT,
  AST_TYPE_ARRAY,
  AST_TYPE_CUSTOM,    // unresolved type name that might be a custom type
  AST_TYPE_NIL,       // for integers/floats/chars, zero, for pointers, null
  AST_TYPE_TEMPLATE,  // present in type definitions, replaced in concrete types
  AST_TYPE_FUNCTION,  // for function pointers
  AST_TYPE_MATRIX,
  AST_TYPE_POINTER,  // points at another type
  AST_TYPE_BOX,      // similar to a pointer, but with more logic
};

struct ast_struct_field {
  char name[256];
  struct ast_ty *ty;
  struct ast_struct_field *next;
};

struct ast_ty {
  enum ast_ty_id ty;
  uint64_t flags;
  char name[256];           // filled for custom, struct, templat, and enum types
  char *specialization_of;  // if this is a specialization of a generic type, the original type name
                            // will be here
  union {
    struct {
      int is_signed;
      size_t width;
    } integer;
    struct {
      size_t width;
    } fvec;
    struct {
      struct ast_template_ty *templates;
      struct ast_enum_field *fields;
      size_t num_fields;
      // if 1, no fields have an inner type and the enum is a simple integer enum
      int no_wrapped_fields;
    } enumty;
    struct {
      struct ast_struct_field *fields;
      size_t num_fields;
      int is_union;  // if 1, all the fields consume the same memory space
    } structty;
    struct {
      size_t width;
      struct ast_ty *element_ty;
    } array;
    struct {
      // if 1, the custom type is a template that will be resolved in instantiation
      int is_template;

      // if 1, the custom type is a forward declaration that will be resolved later
      int is_forward_decl;
    } custom;
    struct {
      // both of these are CUSTOM after parsing and real types after type checking
      struct ast_ty *outer;
      struct ast_template_ty *inners;
    } tmpl;
    struct {
      struct ast_ty *retty;
      struct ast_ty **args;
      size_t num_args;
      int vararg;
    } function;
    struct {
      size_t cols;
      size_t rows;
    } matrix;
    struct {
      struct ast_ty *pointee;
    } pointer;
  };
};

struct ast_enum_field {
  char name[256];
  uint64_t value;
  int has_inner;
  struct ast_ty inner;  // optional
  struct ast_enum_field *next;
};

struct ast_template_ty {
  char name[256];
  int is_resolved;
  struct ast_ty resolved;  // only set if is_resolved == 1, when a type is actually specified
  struct ast_template_ty *next;
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
 * @brief Returns true if the given type is a nil tpye.
 */
int type_is_nil(struct ast_ty *);

/**
 * @brief Returns true if the given type is complex, i.e. not plain old data.
 *
 * Complex types require special handling in code generation as they typically have an underlying
 * block of memory.
 */
int type_is_complex(struct ast_ty *);

/**
 * @brief Check if two types are the same type class (e.g. integer, float, etc).
 *
 * Disregards specific type data such as width or element count.
 */
int same_type_class(struct ast_ty *, struct ast_ty *, uint64_t flagmask);

/**
 * @brief Check if the types are compatible (e.g. one is a constant smaller than the other's size).
 */
int compatible_types(struct ast_ty *ty1, struct ast_ty *ty2);

/**
 * @brief Check if two types are the same type, including element count and width.
 *
 * This is extremely specific; in many cases it's better to use same_type_class and
 * a widening/narrowing check instead.
 *
 * @param flagmask A mask to apply to flags during comparison.
 */
int same_type_masked(struct ast_ty *, struct ast_ty *, uint64_t flagmask);

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
int type_name_into(struct ast_ty *, char *, size_t);

/**
 * @brief Retrieves a parseable version of the type.
 */
int type_name_into_as_code(struct ast_ty *, char *, size_t);

/**
 * @brief Can ty1 be casted into ty2?
 */
int can_cast(struct ast_ty *, struct ast_ty *);

/**
 * @brief Return the size required to store the type in bytes.
 */
size_t type_size(struct ast_ty *);

/**
 * @brief Correctly copy a type, including any dynamic memory allocations.
 */
struct ast_ty copy_type(struct ast_ty *);

/**
 * @brief Deeply compare two type objects, including all sub-objects.
 *
 * This matches heap-allocated pointers deep in the type definition, making it useful for deciding
 * whether to clean up a type or not.
 */
int same_type(struct ast_ty *, struct ast_ty *);

/**
 * @brief Wrap the given type in a pointer type.
 */
struct ast_ty ptr_type(struct ast_ty);

/**
 * @brief Wrap the given type in a box type.
 */
struct ast_ty box_type(struct ast_ty);

/**
 * @brief Unwrap a pointer type to get the inner type.
 */
struct ast_ty *ptr_pointee_type(struct ast_ty *);

/**
 * @brief Unwrap a box type to get the inner type.
 */
struct ast_ty *box_pointee_type(struct ast_ty *);

/**
 * @brief Can the type be indexed?
 */
int type_is_indexable(struct ast_ty *);

#endif
