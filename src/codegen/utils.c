#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Types.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "types.h"
#include "utility.h"

LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name) {
#if 0
  LLVMBasicBlockRef current_block = LLVMGetInsertBlock(codegen->llvm_builder);

  // insert allocas at the start of the entry block
  if (codegen->last_alloca) {
    LLVMValueRef next = LLVMGetNextInstruction(codegen->last_alloca);
    if (next) {
      LLVMPositionBuilderBefore(codegen->llvm_builder, next);
    } else {
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);
    }
  } else {
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);
  }
#endif

  LLVMValueRef var = LLVMBuildAlloca(codegen->llvm_builder, type, name);
  codegen->last_alloca = var;

#if 0
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, current_block);
#endif

  return var;
}

LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                  struct ast_ty *to) {
  if (same_type(from, to)) {
    return value;
  }

  LLVMTypeRef dest_ty = ast_ty_to_llvm_ty(codegen, to);

  if (!same_type_class(from, to, TYPE_FLAG_MASK_ALL ^ TYPE_FLAG_CONSTANT)) {
    if (from->ty == AST_TYPE_INTEGER) {
      return LLVMBuildSIToFP(codegen->llvm_builder, value, dest_ty, "fpconv");
    } else {
      return LLVMBuildFPToSI(codegen->llvm_builder, value, dest_ty, "sconv");
    }
  }

  if (narrower_type(from, to)) {
    // TODO: sext if signed integer
    if (from->ty == AST_TYPE_INTEGER && from->integer.is_signed) {
      return LLVMBuildSExtOrBitCast(codegen->llvm_builder, value, dest_ty, "widening");
    }
    return LLVMBuildZExtOrBitCast(codegen->llvm_builder, value, dest_ty, "widening");
  } else {
    return LLVMBuildTruncOrBitCast(codegen->llvm_builder, value, dest_ty, "narrowing");
  }
}

LLVMTypeRef ast_ty_to_llvm_ty(struct codegen *codegen, struct ast_ty *ty) {
  LLVMTypeRef inner = NULL;

  switch (ty->ty) {
    case AST_TYPE_INTEGER:
      switch (ty->integer.width) {
        case 1:
          inner = LLVMInt1TypeInContext(codegen->llvm_context);
          break;
        case 8:
          inner = LLVMInt8TypeInContext(codegen->llvm_context);
          break;
        case 16:
          inner = LLVMInt16TypeInContext(codegen->llvm_context);
          break;
        case 32:
          inner = LLVMInt32TypeInContext(codegen->llvm_context);
          break;
        case 64:
          inner = LLVMInt64TypeInContext(codegen->llvm_context);
          break;
        default:
          inner = LLVMIntTypeInContext(codegen->llvm_context, (unsigned int)ty->integer.width);
      }
      break;
    case AST_TYPE_CHAR:
      inner = LLVMInt8TypeInContext(codegen->llvm_context);
      break;
    case AST_TYPE_STRING:
      inner = LLVMPointerTypeInContext(codegen->llvm_context, 0);
      break;
    case AST_TYPE_FLOAT:
      inner = LLVMFloatTypeInContext(codegen->llvm_context);
      break;
    case AST_TYPE_FVEC:
      inner = LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context),
                             (unsigned int)ty->fvec.width);
      break;
    case AST_TYPE_VOID:
      inner = LLVMVoidTypeInContext(codegen->llvm_context);
      break;
    case AST_TYPE_ARRAY:
      inner = LLVMArrayType(ast_ty_to_llvm_ty(codegen, ty->array.element_ty),
                            (unsigned int)ty->array.width);
      break;
    case AST_TYPE_STRUCT: {
      struct struct_entry *entry = kv_lookup(codegen->structs, ty->name);
      if (!entry) {
        fprintf(stderr, "struct %s not found in codegen\n", ty->name);
        return NULL;
      }
      inner = entry->type;
    } break;
    case AST_TYPE_NIL:
      inner = LLVMVoidTypeInContext(codegen->llvm_context);
      break;
    case AST_TYPE_ENUM: {
      struct struct_entry *entry = kv_lookup(codegen->structs, ty->name);
      if (!entry) {
        fprintf(stderr, "enum %s not found in codegen\n", ty->name);
        return NULL;
      }
      inner = entry->type;
    } break;
    default:
      fprintf(stderr, "unhandled type %d in conversion to LLVM TypeRef\n", ty->ty);
      return NULL;
  }

  if (ty->flags & TYPE_FLAG_PTR) {
    return LLVMPointerTypeInContext(codegen->llvm_context, 0);
  }

  return inner;
}

void update_debug_loc(struct codegen *codegen, struct lex_locator *loc) {
  if (loc) {
    LLVMContextRef context = codegen->llvm_context;
    LLVMMetadataRef scope = codegen->compile_unit;
    if (codegen->current_function_metadata) {
      if (codegen->current_block) {
        scope = codegen->current_block->scope_metadata;
      } else {
        scope = codegen->current_function_metadata;
      }
    }
    LLVMMetadataRef metadata = LLVMDIBuilderCreateDebugLocation(
        context, (unsigned)loc->line + 1, (unsigned)loc->column + 1, scope, NULL);

    LLVMSetCurrentDebugLocation2(codegen->llvm_builder, metadata);
  } else {
    LLVMSetCurrentDebugLocation2(codegen->llvm_builder, NULL);
  }
}

void emit_store(struct codegen *codegen, struct ast_ty *ty, LLVMValueRef value, LLVMValueRef ptr) {
  if (!type_is_complex(ty)) {
    LLVMBuildStore(codegen->llvm_builder, value, ptr);
    return;
  }

  LLVMTypeRef memcpy_types[3] = {
      LLVMPointerTypeInContext(codegen->llvm_context, 0),
      LLVMPointerTypeInContext(codegen->llvm_context, 0),
      LLVMInt32TypeInContext(codegen->llvm_context),
  };

  // need to use the intrinsic instead
  unsigned int memcpy_id = LLVMLookupIntrinsicID("llvm.memcpy", 11);
  fprintf(stderr, "intrinsic id: %d\n", memcpy_id);
  LLVMValueRef memcpy_func =
      LLVMGetIntrinsicDeclaration(codegen->llvm_module, memcpy_id, memcpy_types, 3);
  LLVMTypeRef func_type = LLVMGlobalGetValueType(memcpy_func);
  fprintf(stderr, "memcpy_func: %p\n", (void *)memcpy_func);
  LLVMValueRef args[4] = {
      ptr,                                                                            // dest
      value,                                                                          // src
      LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), type_size(ty), 0),  // len
      LLVMConstInt(LLVMInt1TypeInContext(codegen->llvm_context), 0, 0),               // isvolatile
  };
  LLVMBuildCall2(codegen->llvm_builder, func_type, memcpy_func, args, 4, "");
}
