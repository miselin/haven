#ifndef _HAVEN_CODEGEN_INTERNAL_H
#define _HAVEN_CODEGEN_INTERNAL_H

#include <llvm-c/TargetMachine.h>
#include <llvm-c/Types.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "lex.h"

struct scope_entry {
  struct ast_vdecl *vdecl;
  struct ast_fdecl *fdecl;

  LLVMTypeRef function_type;
  LLVMTypeRef *param_types;
  LLVMTypeRef variable_type;

  LLVMValueRef ref;

  // some scope entries point to a block instead of a variable (e.g. loop labels)
  LLVMBasicBlockRef block;

  int is_intrinsic;
};

struct struct_entry {
  LLVMTypeRef type;
};

struct defer_entry {
  LLVMBasicBlockRef llvm_block;
  LLVMBasicBlockRef llvm_block_after;
  struct defer_entry *next;
};

// tracks boxes that were created in the current function for automatic deref
struct box_entry {
  LLVMValueRef box;
  struct box_entry *next;
};

struct codegen_block {
  LLVMMetadataRef scope_metadata;
  struct codegen_block *parent;
};

struct codegen_preamble {
  LLVMValueRef new_box;
  LLVMTypeRef new_box_type;
  LLVMValueRef new_empty_box;
  LLVMTypeRef new_empty_box_type;
  LLVMValueRef box_ref;
  LLVMTypeRef box_ref_type;
  LLVMValueRef box_unref;
  LLVMTypeRef box_unref_type;
};

struct codegen_compileunit {
  LLVMMetadataRef file_metadata;
  LLVMMetadataRef compile_unit;
};

struct codegen {
  struct ast_program *ast;

  LLVMContextRef llvm_context;
  LLVMModuleRef llvm_module;
  LLVMBuilderRef llvm_builder;
  LLVMDIBuilderRef llvm_dibuilder;

  LLVMValueRef current_function;
  LLVMMetadataRef current_function_metadata;
  LLVMBasicBlockRef entry_block;
  LLVMValueRef last_alloca;

  // name -> LLVMValueRef
  struct kv *locals;

  // name -> LLVMTypeRef for declared functions
  struct kv *functions;

  // we keep a scope to track LLVM refs for variables/functions
  struct scope *scope;

  // struct type names -> their definition
  struct kv *structs;

  // filenames -> their compile unit metadata
  struct kv *compile_units;

  LLVMValueRef retval;
  LLVMBasicBlockRef return_block;

  // stack of defer expressions to run at the end of the current function
  struct defer_entry *defer_head;

  struct codegen_block *current_block;

  LLVMTargetRef llvm_target;
  LLVMTargetMachineRef llvm_target_machine;
  LLVMTargetDataRef llvm_data_layout;

  struct compiler *compiler;

  struct codegen_preamble preamble;

  struct box_entry *boxes;
};

LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast);
LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast, LLVMValueRef into);
LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast);
LLVMValueRef emit_lvalue(struct codegen *codegen, struct ast_expr *ast);
LLVMValueRef emit_expr_into(struct codegen *codegen, struct ast_expr *ast, LLVMValueRef into);

void emit_fdecl(struct codegen *codegen, struct ast_fdecl *fdecl, struct lex_locator *at);
void emit_vdecl(struct codegen *codegen, struct ast_vdecl *vdecl);

LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                  struct ast_ty *to);

LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name);

// Get the underlying LLVM type for an AST type
// For types such as enums, this will return the storage type (pointer) not the enum type itself
LLVMTypeRef ast_ty_to_llvm_ty(struct codegen *codegen, struct ast_ty *ty);

// Get the underlying type for types which have differing storage types to their underlying types
// For example, enums with wrapped fields are carried as pointers, but accessed using a struct type
LLVMTypeRef ast_underlying_ty_to_llvm_ty(struct codegen *codegen, struct ast_ty *ty);

LLVMValueRef emit_if(struct codegen *codegen, struct ast_expr *ast);

LLVMValueRef emit_logical_expr(struct codegen *codegen, struct ast_expr_binary *binary,
                               struct ast_ty *ty);
LLVMValueRef emit_boolean_expr(struct codegen *codegen, struct ast_expr_binary *binary);
LLVMValueRef emit_binary_expr(struct codegen *codegen, struct ast_expr_binary *binary,
                              struct ast_ty *ty);

LLVMValueRef emit_match_expr(struct codegen *codegen, struct ast_ty *ty,
                             struct ast_expr_match *match);

void codegen_internal_enter_scope(struct codegen *codegen, struct lex_locator *at,
                                  int lexical_block);
void codegen_internal_leave_scope(struct codegen *codegen, int lexical_block);

void update_debug_loc(struct codegen *codegen, struct lex_locator *loc);

void emit_preamble(struct codegen *codegen);

// Emits a store, whether through a store instruction or a memcpy intrinsic, based on the type
void emit_store(struct codegen *codegen, struct ast_ty *ty, LLVMValueRef value, LLVMValueRef ptr);

LLVMTypeRef emit_enum_type(struct codegen *codegen, struct ast_ty *ty);

int extract_constant_int(struct ast_expr *expr, int64_t *into);

LLVMValueRef call_intrinsic(struct codegen *codegen, const char *intrinsic_name,
                            const char *inst_name, size_t num_types, size_t num_args, ...);
LLVMValueRef build_intrinsic(struct codegen *codegen, const char *intrinsic_name,
                             LLVMTypeRef *out_ftype, size_t num_types, ...);
LLVMValueRef build_intrinsic2(struct codegen *codegen, const char *intrinsic_name,
                              LLVMTypeRef *out_ftype, LLVMTypeRef *types, size_t num_types);

LLVMValueRef const_i32(struct codegen *codegen, int32_t val);

LLVMValueRef create_scale_vector(struct codegen *codegen, size_t count, LLVMValueRef scale);

LLVMTypeRef codegen_pointer_type(struct codegen *codegen);
LLVMTypeRef codegen_i32_type(struct codegen *codegen);

LLVMAttributeRef codegen_string_attribute(struct codegen *codegen, const char *attr_name,
                                          const char *attr_value);
LLVMAttributeRef codegen_enum_attribute(struct codegen *codegen, const char *attr_name,
                                        uint64_t value);
LLVMAttributeRef codegen_type_attribute(struct codegen *codegen, const char *attr_name,
                                        LLVMTypeRef type);

// Get the struct type for a box type
LLVMTypeRef codegen_box_type(struct codegen *codegen, struct ast_ty *ty);

void codegen_box_ref(struct codegen *codegen, LLVMValueRef box, int already_deref);
void codegen_box_unref(struct codegen *codegen, LLVMValueRef box, int already_deref);

struct codegen_compileunit *codegen_get_compileunit(struct codegen *codegen,
                                                    struct lex_locator *loc);

int initialize_llvm(void);
void shutdown_llvm(void);

// Uniquely identify the given type in a string format that can be used as a name
void mangle_type(struct ast_ty *ty, char *buf, size_t len);

void configure_llvm(struct compiler *compiler);

#endif
