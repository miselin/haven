#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <stdlib.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "utility.h"

void emit_vdecl(struct codegen *codegen, struct ast_vdecl *vdecl) {
  // emit global variable
  LLVMTypeRef variable_type = ast_ty_to_llvm_ty(codegen, vdecl->ty);
  LLVMValueRef global =
      LLVMAddGlobal(codegen->llvm_module, variable_type, vdecl->ident.value.identv.ident);
  if (vdecl->flags & DECL_FLAG_PUB) {
    LLVMSetLinkage(global, LLVMExternalLinkage);
  } else {
    LLVMSetLinkage(global, LLVMInternalLinkage);
  }

  LLVMSetGlobalConstant(global, (vdecl->flags & DECL_FLAG_MUT) == 0);

  struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
  entry->variable_type = variable_type;
  entry->ref = global;
  entry->flags = vdecl->flags;
  scope_insert(codegen->scope, vdecl->ident.value.identv.ident, entry);

  if (vdecl->init_expr) {
    LLVMSetInitializer(global, emit_expr(codegen, vdecl->init_expr));
  }
}
