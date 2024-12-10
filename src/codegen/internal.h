#ifndef _MATTC_CODEGEN_INTERNAL_H
#define _MATTC_CODEGEN_INTERNAL_H

#include <llvm-c-18/llvm-c/Types.h>

#include "ast.h"
#include "codegen.h"

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

struct codegen {
  struct ast_program *ast;

  LLVMContextRef llvm_context;
  LLVMModuleRef llvm_module;
  LLVMBuilderRef llvm_builder;

  LLVMValueRef current_function;
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
};

LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast);
LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast);
LLVMValueRef emit_expr_into(struct codegen *codegen, struct ast_expr *ast, LLVMValueRef into);

void emit_fdecl(struct codegen *codegen, struct ast_fdecl *fdecl);
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

#endif
