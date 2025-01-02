#include <libgen.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
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

LLVMValueRef emit_cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                       struct ast_ty *to) {
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast!");

  if (same_type(from, to)) {
    return value;
  }

  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast A");

  if (from->ty == AST_TYPE_POINTER && to->ty == AST_TYPE_POINTER) {
    // pointer to pointer cast, no IR cast needed
    return value;
  }
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast B");

  LLVMTypeRef dest_ty = ast_ty_to_llvm_ty(codegen, to);

  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast C");

  if (!same_type_class(from, to, TYPE_FLAG_MASK_ALL ^ TYPE_FLAG_CONSTANT)) {
    compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast C.1");
    if (from->ty == AST_TYPE_INTEGER) {
      return LLVMBuildSIToFP(codegen->llvm_builder, value, dest_ty, "fpconv");
    } else {
      return LLVMBuildFPToSI(codegen->llvm_builder, value, dest_ty, "sconv");
    }
  }

  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast D");

  if (narrower_type(from, to)) {
    compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast D.1");
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
        compiler_log(codegen->compiler, LogLevelError, "codegen", "struct %s not found in codegen",
                     ty->name);
        return NULL;
      }
      inner = entry->type;
    } break;
    case AST_TYPE_NIL:
      inner = LLVMVoidTypeInContext(codegen->llvm_context);
      break;
    case AST_TYPE_ENUM: {
      if (ty->enumty.no_wrapped_fields) {
        inner = LLVMInt32TypeInContext(codegen->llvm_context);
      } else {
        inner = LLVMPointerTypeInContext(codegen->llvm_context, 0);
      }
    } break;
    case AST_TYPE_CUSTOM:
      compiler_log(codegen->compiler, LogLevelError, "codegen",
                   "custom type %s not resolved before codegen", ty->name);
      return NULL;
    case AST_TYPE_FUNCTION:
      inner = LLVMPointerTypeInContext(codegen->llvm_context, 0);
      break;
    case AST_TYPE_MATRIX: {
      // matrix is actually a flat fvec (Rows * Cols)
      inner = LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context),
                             (unsigned int)(ty->matrix.cols * ty->matrix.rows));
    } break;
    case AST_TYPE_POINTER:
      return LLVMPointerTypeInContext(codegen->llvm_context, 0);
    case AST_TYPE_BOX:
      return LLVMPointerTypeInContext(codegen->llvm_context, 0);
      break;
    default:
      compiler_log(codegen->compiler, LogLevelError, "codegen",
                   "unhandled type %d in conversion to LLVM TypeRef", ty->ty);
      return NULL;
  }

  return inner;
}

LLVMTypeRef ast_underlying_ty_to_llvm_ty(struct codegen *codegen, struct ast_ty *ty) {
  LLVMTypeRef inner = NULL;
  switch (ty->ty) {
    case AST_TYPE_ENUM: {
      struct struct_entry *entry = kv_lookup(codegen->structs, ty->name);
      if (!entry) {
        fprintf(stderr, "enum %s not found in codegen\n", ty->name);
        return NULL;
      }
      inner = entry->type;
    } break;
    default:
      // fall through to the normal type resolver
      break;
  }

  return inner ? inner : ast_ty_to_llvm_ty(codegen, ty);
}

void update_debug_loc(struct codegen *codegen, struct lex_locator *loc) {
  if (loc) {
    struct codegen_compileunit *unit = codegen_get_compileunit(codegen, loc);

    LLVMContextRef context = codegen->llvm_context;
    LLVMMetadataRef scope = unit->compile_unit;
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

  int is_null = LLVMIsNull(value);

  if (is_null || !type_is_complex(ty)) {
    LLVMBuildStore(codegen->llvm_builder, value, ptr);
    return;
  }

  // use LLVM's size, not ours, as we may have padding
  LLVMTypeRef llvm_ty = ast_underlying_ty_to_llvm_ty(codegen, ty);
  uint64_t ty_size = LLVMABISizeOfType(codegen->llvm_data_layout, llvm_ty);

  call_intrinsic(codegen, "llvm.memcpy", "", 3, 4, codegen_pointer_type(codegen),
                 codegen_pointer_type(codegen), codegen_i32_type(codegen), ptr, value,
                 const_i32(codegen, (int32_t)ty_size),
                 LLVMConstInt(LLVMInt1TypeInContext(codegen->llvm_context), 0, 0));
}

int extract_constant_int(struct ast_expr *expr, int64_t *into) {
  while (expr->type == AST_EXPR_TYPE_CAST) {
    expr = expr->expr.cast.expr;
  }

  if (expr->type != AST_EXPR_TYPE_CONSTANT) {
    return -1;
  }

  *into = (int64_t)expr->expr.constant.constant.value.intv.val;
  return 0;
}

LLVMValueRef build_intrinsic2(struct codegen *codegen, const char *intrinsic_name,
                              LLVMTypeRef *out_ftype, LLVMTypeRef *types, size_t num_types) {
  unsigned int intrinsic_id = LLVMLookupIntrinsicID(intrinsic_name, strlen(intrinsic_name));
  if (!intrinsic_id) {
    return NULL;
  }
  LLVMValueRef intrinsic_decl =
      LLVMGetIntrinsicDeclaration(codegen->llvm_module, intrinsic_id, types, num_types);
  LLVMTypeRef intrinsic_func = LLVMGlobalGetValueType(intrinsic_decl);

  *out_ftype = intrinsic_func;
  return intrinsic_decl;
}

LLVMValueRef build_intrinsic(struct codegen *codegen, const char *intrinsic_name,
                             LLVMTypeRef *out_ftype, size_t num_types, ...) {
  va_list ap;
  va_start(ap, num_types);
  LLVMTypeRef *types = malloc(sizeof(LLVMTypeRef) * num_types);
  for (size_t i = 0; i < num_types; i++) {
    types[i] = va_arg(ap, LLVMTypeRef);
  }
  va_end(ap);

  unsigned int intrinsic_id = LLVMLookupIntrinsicID(intrinsic_name, strlen(intrinsic_name));
  LLVMValueRef intrinsic_decl =
      LLVMGetIntrinsicDeclaration(codegen->llvm_module, intrinsic_id, types, num_types);
  LLVMTypeRef intrinsic_func = LLVMGlobalGetValueType(intrinsic_decl);

  free(types);

  *out_ftype = intrinsic_func;
  return intrinsic_decl;
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

LLVMTypeRef codegen_box_type(struct codegen *codegen, struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_BOX) {
    return NULL;
  }

  char name[1024];
  mangle_type(ty->pointer.pointee, name, 1024, "boxed.");
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "mangled type %s for boxing", name);

  struct struct_entry *entry = kv_lookup(codegen->structs, name);
  if (entry) {
    return entry->type;
  }

  LLVMTypeRef wrapped = ast_ty_to_llvm_ty(codegen, ty->pointer.pointee);
  LLVMTypeRef result_ty = LLVMStructCreateNamed(codegen->llvm_context, name);

  // refcount, boxed value
  LLVMTypeRef fields[] = {LLVMInt32TypeInContext(codegen->llvm_context), wrapped};
  LLVMStructSetBody(result_ty, fields, 2,
                    1);  // TODO: make not packed, it's just easier to debug for now

  entry = calloc(1, sizeof(struct struct_entry));
  entry->type = result_ty;
  kv_insert(codegen->structs, name, entry);

  return result_ty;
}

void codegen_box_ref(struct codegen *codegen, LLVMValueRef box, int already_deref) {
  // boxes are ptrs to ptrs
  LLVMValueRef inner =
      already_deref ? box
                    : LLVMBuildLoad2(codegen->llvm_builder, codegen_pointer_type(codegen), box, "");
  LLVMBuildCall2(codegen->llvm_builder, codegen->preamble.box_ref_type, codegen->preamble.box_ref,
                 &inner, 1, "");
}

void codegen_box_unref(struct codegen *codegen, LLVMValueRef box, int already_deref) {
  // boxes are ptrs to ptrs
  LLVMValueRef inner =
      already_deref ? box
                    : LLVMBuildLoad2(codegen->llvm_builder, codegen_pointer_type(codegen), box, "");
  LLVMBuildCall2(codegen->llvm_builder, codegen->preamble.box_unref_type,
                 codegen->preamble.box_unref, &inner, 1, "");
}

struct codegen_compileunit *codegen_get_compileunit(struct codegen *codegen,
                                                    struct lex_locator *loc) {
  if (!loc->file[0]) {
    compiler_log(codegen->compiler, LogLevelError, "codegen", "no file in loc [line=%zd col=%zd]",
                 loc->line, loc->column);
    return NULL;
  }

  struct codegen_compileunit *result = kv_lookup(codegen->compile_units, loc->file);
  if (result) {
    return result;
  }

  result = calloc(1, sizeof(struct codegen_compileunit));

  char *dup1 = NULL, *dup2 = NULL;
  char *filename = NULL;
  char *dir = NULL;
  {
    dup1 = strdup(loc->file);
    dir = dirname(dup1);
  }
  {
    dup2 = strdup(loc->file);
    filename = basename(dup2);
  }

  result->file_metadata = LLVMDIBuilderCreateFile(codegen->llvm_dibuilder, filename,
                                                  strlen(filename), dir, strlen(dir));
  result->compile_unit = LLVMDIBuilderCreateCompileUnit(
      codegen->llvm_dibuilder, LLVMDWARFSourceLanguageC, result->file_metadata, COMPILER_IDENT, 5,
      0, "", 0, 0, "", 0, LLVMDWARFEmissionFull, 0, 0, 0, "", 0, "", 0);

  free(dup1);
  free(dup2);

  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "created compile unit for %s",
               loc->file);
  kv_insert(codegen->compile_units, loc->file, result);

  return result;
}

void codegen_mangle(struct codegen *codegen, struct ast_fdecl *fdecl, char *buf, size_t len) {
  char *triple = LLVMGetTargetMachineTriple(codegen->llvm_target_machine);

  // TODO: put this in a C++ module and use llvm::Triple::getOS()
  if (strstr(triple, "-darwin-")) {
    // need to prefix with an underscore
    *buf++ = '_';
    --len;
  }

  strncpy(buf, fdecl->ident.value.identv.ident, len);

  LLVMDisposeMessage(triple);
}

LLVMTypeRef ast_llvm_function_ty(struct codegen *codegen, struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_FUNCTION) {
    return NULL;
  }

  LLVMTypeRef ret_ty = ast_underlying_ty_to_llvm_ty(codegen, ty->function.retty);

  int rc = type_is_complex(ty->function.retty);
  if (rc < 0) {
    compiler_log(codegen->compiler, LogLevelError, "codegen",
                 "type_is_complex returned a completely unexpected value %d", rc);
    return NULL;
  }

  size_t complex_return = (size_t)rc;

  size_t num_params = ty->function.num_params + complex_return;

  LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * num_params);
  if (complex_return) {
    param_types[0] = LLVMPointerTypeInContext(codegen->llvm_context, 0);
  }
  for (size_t i = 0; i < ty->function.num_params; i++) {
    param_types[i + complex_return] =
        ast_underlying_ty_to_llvm_ty(codegen, ty->function.param_types[i]);
  }
  LLVMTypeRef func_type =
      LLVMFunctionType(ret_ty, param_types, (unsigned int)num_params, ty->function.vararg);
  free(param_types);
  return func_type;
}
