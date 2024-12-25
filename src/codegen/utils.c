#include <llvm-c-18/llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "types.h"
#include "utility.h"

LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name) {
  LLVMBasicBlockRef current_block = LLVMGetInsertBlock(codegen->llvm_builder);

  // default to end of entry block in case no instructions have been added yet
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);

  LLVMValueRef inst = LLVMGetFirstInstruction(codegen->entry_block);
  if (inst) {
    // find the first non-alloca instruction in the entry block
    while (inst && LLVMGetInstructionOpcode(inst) == LLVMAlloca) {
      inst = LLVMGetNextInstruction(inst);
    }

    // and now insert the alloca before the first non-alloca instruction
    if (inst) {
      LLVMPositionBuilderBefore(codegen->llvm_builder, inst);
    }
  }

  LLVMValueRef var = LLVMBuildAlloca(codegen->llvm_builder, type, name);
  codegen->last_alloca = var;

  LLVMPositionBuilderAtEnd(codegen->llvm_builder, current_block);

  return var;
}

LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                  struct ast_ty *to) {
  if (same_type(from, to)) {
    return value;
  }

  if (from->ty == AST_TYPE_POINTER && to->ty == AST_TYPE_POINTER) {
    // pointer to pointer cast, no IR cast needed
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
    case AST_TYPE_CUSTOM:
      fprintf(stderr, "custom type %s not resolved before codegen\n", ty->name);
      return NULL;
    case AST_TYPE_FUNCTION: {
      LLVMTypeRef retty = ast_ty_to_llvm_ty(codegen, ty->function.retty);
      LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * ty->function.num_args);
      for (size_t i = 0; i < ty->function.num_args; i++) {
        param_types[i] = ast_ty_to_llvm_ty(codegen, ty->function.args[i]);
      }
      inner = LLVMFunctionType(retty, param_types, (unsigned int)ty->function.num_args,
                               ty->function.vararg);
      free(param_types);
    } break;
    case AST_TYPE_MATRIX: {
      // matrix is actually a flat fvec (Rows * Cols)
      inner = LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context),
                             (unsigned int)(ty->matrix.cols * ty->matrix.rows));
    } break;
    case AST_TYPE_POINTER:
      return LLVMPointerTypeInContext(codegen->llvm_context, 0);
    case AST_TYPE_BOX:
      return LLVMPointerTypeInContext(codegen->llvm_context, 0);
    default:
      fprintf(stderr, "unhandled type %d in conversion to LLVM TypeRef\n", ty->ty);
      return NULL;
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
  if (!value) {
    return;
  }

  if (LLVMIsNull(value) || !type_is_complex(ty)) {
    LLVMBuildStore(codegen->llvm_builder, value, ptr);
    return;
  }

  // use LLVM's size, not ours, as we may have padding
  LLVMTypeRef llvm_ty = ast_ty_to_llvm_ty(codegen, ty);
  uint64_t ty_size = LLVMABISizeOfType(codegen->llvm_data_layout, llvm_ty);

  call_intrinsic(codegen, "llvm.memcpy", "", 3, 4, codegen_pointer_type(codegen),
                 codegen_pointer_type(codegen), codegen_i32_type(codegen), ptr, value,
                 const_i32(codegen, (int32_t)ty_size),
                 LLVMConstInt(LLVMInt1TypeInContext(codegen->llvm_context), 0, 0));
}

int extract_constant_int(struct ast_expr *expr, int64_t *into) {
  while (expr->type == AST_EXPR_TYPE_CAST) {
    expr = expr->cast.expr;
  }

  if (expr->type != AST_EXPR_TYPE_CONSTANT) {
    return -1;
  }

  *into = (int64_t)expr->constant.constant.value.intv.val;
  return 0;
}

LLVMValueRef call_intrinsic(struct codegen *codegen, const char *intrinsic_name,
                            const char *inst_name, size_t num_types, size_t num_args, ...) {
  va_list ap;
  va_start(ap, num_args);
  LLVMTypeRef *types = malloc(sizeof(LLVMTypeRef) * num_types);
  for (size_t i = 0; i < num_types; i++) {
    types[i] = va_arg(ap, LLVMTypeRef);
  }

  LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * num_args);
  for (size_t i = 0; i < num_args; i++) {
    args[i] = va_arg(ap, LLVMValueRef);
  }

  va_end(ap);

  unsigned int intrinsic_id = LLVMLookupIntrinsicID(intrinsic_name, strlen(intrinsic_name));
  LLVMValueRef intrinsic_decl =
      LLVMGetIntrinsicDeclaration(codegen->llvm_module, intrinsic_id, types, num_types);
  LLVMTypeRef intrinsic_func = LLVMGlobalGetValueType(intrinsic_decl);

  LLVMValueRef result = LLVMBuildCall2(codegen->llvm_builder, intrinsic_func, intrinsic_decl, args,
                                       (unsigned int)num_args, inst_name);

  free(types);
  free(args);

  return result;
}

LLVMValueRef const_i32(struct codegen *codegen, int32_t val) {
  return LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), (unsigned long long)val, 0);
}

LLVMValueRef create_scale_vector(struct codegen *codegen, size_t count, LLVMValueRef scale) {
  LLVMTypeRef vecty =
      LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context), (unsigned int)count);
  LLVMValueRef zero = LLVMConstNull(vecty);
  LLVMValueRef undef = LLVMGetUndef(vecty);
  LLVMValueRef bvec = LLVMBuildInsertElement(
      codegen->llvm_builder, undef, scale,
      LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0), "broadcast");
  return LLVMBuildShuffleVector(codegen->llvm_builder, bvec, undef, zero, "shuffle");
}

LLVMTypeRef codegen_pointer_type(struct codegen *codegen) {
  return LLVMPointerTypeInContext(codegen->llvm_context, 0);
}

LLVMTypeRef codegen_i32_type(struct codegen *codegen) {
  return LLVMInt32TypeInContext(codegen->llvm_context);
}

LLVMAttributeRef codegen_string_attribute(struct codegen *codegen, const char *attr_name,
                                          const char *attr_value) {
  return LLVMCreateStringAttribute(codegen->llvm_context, attr_name, (unsigned)strlen(attr_name),
                                   attr_value, (unsigned)strlen(attr_value));
}

LLVMAttributeRef codegen_enum_attribute(struct codegen *codegen, const char *attr_name,
                                        uint64_t value) {
  unsigned int kind = LLVMGetEnumAttributeKindForName(attr_name, strlen(attr_name));
  if (!kind) {
    return NULL;
  }

  return LLVMCreateEnumAttribute(codegen->llvm_context, kind, value);
}

LLVMAttributeRef codegen_type_attribute(struct codegen *codegen, const char *attr_name,
                                        LLVMTypeRef type) {
  unsigned int kind = LLVMGetEnumAttributeKindForName(attr_name, strlen(attr_name));
  if (!kind) {
    return NULL;
  }

  return LLVMCreateTypeAttribute(codegen->llvm_context, kind, type);
}
