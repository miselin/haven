#ifndef _HAVEN_CODEGEN_INTERNAL_H
#define _HAVEN_CODEGEN_INTERNAL_H

#include <llvm-c/TargetMachine.h>
#include <llvm-c/Types.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"

struct scope_entry {
  struct ast_vdecl *vdecl;
  struct ast_fdecl *fdecl;

  LLVMTypeRef function_type;
  LLVMTypeRef *param_types;
  LLVMTypeRef variable_type;

  LLVMValueRef ref;
};

struct struct_entry {
  LLVMTypeRef type;
};

struct defer_entry {
  struct ast_expr *expr;
  struct defer_entry *next;
};

struct codegen_block {
  LLVMMetadataRef scope_metadata;
  struct codegen_block *parent;
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

  LLVMValueRef retval;
  LLVMBasicBlockRef return_block;

  // stack of defer expressions to run at the end of the current function
  struct defer_entry *defer_head;

  struct codegen_block *current_block;
  LLVMMetadataRef file_metadata;
  LLVMMetadataRef compile_unit;

  LLVMTargetRef llvm_target;
  LLVMTargetMachineRef llvm_target_machine;
  LLVMTargetDataRef llvm_data_layout;

  struct compiler *compiler;
};

LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast);
LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast);
LLVMValueRef emit_expr_into(struct codegen *codegen, struct ast_expr *ast, LLVMValueRef into);

void emit_fdecl(struct codegen *codegen, struct ast_fdecl *fdecl, struct lex_locator *at);
void emit_vdecl(struct codegen *codegen, struct ast_vdecl *vdecl);

LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                  struct ast_ty *to);

LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name);

LLVMTypeRef ast_ty_to_llvm_ty(struct codegen *codegen, struct ast_ty *ty);

// emit the expression variant of if (i.e. returns a value) - requires else
LLVMValueRef emit_if(struct codegen *codegen, struct ast_expr *ast);

// emit the statement variant of if (i.e. does not return a value) - does not require else
void emit_void_if(struct codegen *codegen, struct ast_expr *ast);

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

// Emits a store, whether through a store instruction or a memcpy intrinsic, based on the type
void emit_store(struct codegen *codegen, struct ast_ty *ty, LLVMValueRef value, LLVMValueRef ptr);

int initialize_llvm();
void shutdown_llvm();

#endif
