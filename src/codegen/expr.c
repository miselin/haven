#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "types.h"
#include "utility.h"

LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast) {
  if (0) {
    fprintf(stderr, "emit expr: ");
    dump_expr(ast, 0);
    fprintf(stderr, "\n");
  }

  update_debug_loc(codegen, &ast->loc);
  return emit_expr_into(codegen, ast, NULL);
}

LLVMValueRef emit_expr_into(struct codegen *codegen, struct ast_expr *ast, LLVMValueRef into) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      LLVMTypeRef const_ty = ast_ty_to_llvm_ty(codegen, &ast->ty);
      switch (ast->ty.ty) {
        case AST_TYPE_INTEGER:
          return LLVMConstInt(const_ty, ast->constant.constant.value.intv.val, 0);
        case AST_TYPE_CHAR:
          return LLVMConstInt(const_ty, (unsigned long long)ast->constant.constant.value.charv.c,
                              0);
        case AST_TYPE_STRING: {
          LLVMValueRef str = LLVMAddGlobal(
              codegen->llvm_module,
              LLVMArrayType(LLVMInt8TypeInContext(codegen->llvm_context),
                            (unsigned int)ast->constant.constant.value.strv.length + 1),
              "str");
          LLVMSetInitializer(str, LLVMConstStringInContext(
                                      codegen->llvm_context, ast->constant.constant.value.strv.s,
                                      (unsigned int)ast->constant.constant.value.strv.length, 0));
          return str;
        } break;
        case AST_TYPE_FVEC: {
          LLVMValueRef *fields = malloc(sizeof(LLVMValueRef) * ast->list->num_elements);

          unsigned int i = 0;
          int non_const_elements = 0;
          struct ast_expr_list *node = ast->list;
          while (node) {
            fields[i++] = emit_expr(codegen, node->expr);
            if (node->expr->type != AST_EXPR_TYPE_CONSTANT) {
              non_const_elements++;
            }
            node = node->next;
          }

          if (non_const_elements) {
            LLVMValueRef zero =
                LLVMConstNull(LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context), i));

            // add zero to the vector to get it into a temporary
            LLVMValueRef vec_stack =
                new_alloca(codegen, ast_ty_to_llvm_ty(codegen, &ast->ty), "vec");
            LLVMBuildStore(codegen->llvm_builder, zero, vec_stack);
            LLVMValueRef vec = LLVMBuildLoad2(
                codegen->llvm_builder, ast_ty_to_llvm_ty(codegen, &ast->ty), vec_stack, "vec");
            for (size_t j = 0; j < i; j++) {
              LLVMValueRef idx = LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), j, 0);
              vec = LLVMBuildInsertElement(codegen->llvm_builder, vec, fields[j], idx, "element");
            }
            free(fields);
            return vec;
          }

          LLVMValueRef vec = LLVMConstVector(fields, i);
          free(fields);
          return vec;

        } break;
        case AST_TYPE_FLOAT:
          return LLVMConstRealOfStringAndSize(
              LLVMFloatTypeInContext(codegen->llvm_context),
              ast->constant.constant.value.floatv.buf,
              (unsigned int)ast->constant.constant.value.floatv.length);

        case AST_TYPE_ARRAY: {
          LLVMTypeRef inner_ty = ast_ty_to_llvm_ty(codegen, ast->ty.array.element_ty);
          LLVMValueRef *values = malloc(sizeof(LLVMValueRef) * ast->list->num_elements);
          struct ast_expr_list *node = ast->list;
          for (size_t i = 0; i < ast->list->num_elements; i++) {
            values[i] = emit_expr(codegen, node->expr);
            values[i] = cast(codegen, values[i], &node->expr->ty, ast->ty.array.element_ty);

            node = node->next;
          }
          LLVMValueRef array = LLVMConstArray2(inner_ty, values, ast->list->num_elements);
          free(values);
          return array;
        } break;

        default: {
          char buf[256];
          type_name_into(&ast->ty, buf, 256);
          fprintf(stderr, "unhandled constant type %s\n", buf);
        }
      }
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *lookup =
          scope_lookup(codegen->scope, ast->variable.ident.value.identv.ident, 1);

      // temporaries are things like function parameters, and do not require loads
      if (lookup->vdecl->flags & DECL_FLAG_TEMPORARY) {
        return lookup->ref;
      } else if (ast->ty.flags & TYPE_FLAG_PTR) {
        // if we WANT a pointer, don't load it
        return lookup->ref;
      } else if (ast->ty.ty == AST_TYPE_ENUM && !ast->ty.enumty.no_wrapped_fields) {
        // enum is actually a struct -- don't load
        // with no wrapped fields, it's an integer
        return lookup->ref;
      }

      return LLVMBuildLoad2(codegen->llvm_builder, lookup->variable_type, lookup->ref,
                            ast->variable.ident.value.identv.ident);
    } break;

    case AST_EXPR_TYPE_BINARY: {
      if (ast_binary_op_logical(ast->binary.op)) {
        // need to short-circuit L and R sides
        return emit_logical_expr(codegen, &ast->binary, &ast->ty);
      } else if (ast_binary_op_conditional(ast->binary.op)) {
        // need to emit a conditional
        return emit_boolean_expr(codegen, &ast->binary);
      } else {
        return emit_binary_expr(codegen, &ast->binary, &ast->ty);
      }
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return emit_block(codegen, &ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->call.ident.value.identv.ident, 1);

      size_t named_param_count = LLVMCountParams(entry->ref);
      size_t is_complex = (size_t)type_is_complex(&entry->fdecl->retty);

      LLVMTypeRef ret_ty = ast_ty_to_llvm_ty(codegen, &entry->fdecl->retty);

      LLVMValueRef *args = NULL;
      unsigned int num_args = 0;
      if (ast->call.args) {
        args = malloc(sizeof(LLVMValueRef) * (ast->call.args->num_elements + is_complex));
        struct ast_expr_list *node = ast->call.args;
        while (node) {
          args[num_args + is_complex] = emit_expr(codegen, node->expr);
          if (node->expr->ty.ty == AST_TYPE_FLOAT && num_args >= named_param_count) {
            // vararg floats need to be promoted to doubles for C compatibility
            args[num_args + is_complex] = LLVMBuildFPExt(
                codegen->llvm_builder, args[num_args + is_complex], LLVMDoubleType(), "fpext");
          }
          ++num_args;
          node = node->next;
        }
      }

      LLVMValueRef complex = NULL;
      if (is_complex) {
        // build retval
        args[0] = into ? into : new_alloca(codegen, ret_ty, "sret");
        complex = args[0];

        ++num_args;
      }

      LLVMValueRef call = LLVMBuildCall2(codegen->llvm_builder, entry->function_type, entry->ref,
                                         args, num_args, "");

      if (is_complex) {
        unsigned int kind = LLVMGetEnumAttributeKindForName("sret", 4);
        LLVMAttributeRef attr = LLVMCreateTypeAttribute(codegen->llvm_context, kind, ret_ty);
        LLVMAddCallSiteAttribute(call, 1, attr);
      }

      if (args) {
        free(args);
      }
      return is_complex ? complex : call;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      const char *ident = ast->deref.ident.value.identv.ident;
      struct scope_entry *entry = scope_lookup(codegen->scope, ident, 1);

      // vector -> extractelement
      if (LLVMGetTypeKind(entry->variable_type) == LLVMVectorTypeKind) {
        LLVMValueRef ref = entry->ref;

        // rotate through any pointers until we get to the actual vector
        while (LLVMGetTypeKind(LLVMTypeOf(ref)) == LLVMPointerTypeKind) {
          ref = LLVMBuildLoad2(codegen->llvm_builder, entry->variable_type, ref, ident);
        }

        LLVMValueRef index = LLVMConstInt(LLVMInt64TypeInContext(codegen->llvm_context),
                                          (unsigned int)ast->deref.field_idx, 0);
        return LLVMBuildExtractElement(codegen->llvm_builder, ref, index, "deref");
      }

      LLVMTypeRef target_ty = ast_ty_to_llvm_ty(codegen, &ast->ty);

      // struct -> getelementptr
      LLVMValueRef gep =
          LLVMBuildStructGEP2(codegen->llvm_builder, entry->variable_type, entry->ref,
                              (unsigned int)ast->deref.field_idx, "deref.gep");
      return LLVMBuildLoad2(codegen->llvm_builder, target_ty, gep, "deref.load");
    }; break;

    case AST_EXPR_TYPE_VOID:
      return NULL;

    case AST_EXPR_TYPE_CAST: {
      LLVMValueRef expr = emit_expr(codegen, ast->cast.expr);
      return cast(codegen, expr, &ast->cast.expr->ty, &ast->ty);
    } break;

    case AST_EXPR_TYPE_IF: {
      if (ast->ty.ty == AST_TYPE_VOID) {
        emit_void_if(codegen, ast);
        return NULL;
      } else {
        return emit_if(codegen, ast);
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      LLVMValueRef expr = emit_expr(codegen, ast->assign.expr);

      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->call.ident.value.identv.ident, 1);

      if (entry->vdecl->flags & DECL_FLAG_TEMPORARY) {
        // swap the reference to this expression from now on
        entry->ref = expr;
        return expr;
      } else {
        emit_store(codegen, &ast->assign.expr->ty, expr, entry->ref);
      }

      return expr;
    } break;

    case AST_EXPR_TYPE_REF: {
      return emit_expr(codegen, ast->ref.expr);
    } break;

    case AST_EXPR_TYPE_LOAD: {
      LLVMValueRef expr = emit_expr(codegen, ast->load.expr);
      LLVMTypeRef expr_type = ast_ty_to_llvm_ty(codegen, &ast->ty);

      if (type_is_complex(&ast->ty)) {
        // put the result into a temporary and return that instead of the underlying value
        LLVMValueRef temp = into ? into : new_alloca(codegen, expr_type, "load");
        emit_store(codegen, &ast->ty, expr, temp);
        return temp;
      }

      return LLVMBuildLoad2(codegen->llvm_builder, expr_type, expr, "load");
    } break;

    case AST_EXPR_TYPE_UNARY: {
      LLVMValueRef expr = emit_expr(codegen, ast->unary.expr);
      switch (ast->unary.op) {
        case AST_UNARY_OP_NEG:
          if (ast->ty.ty == AST_TYPE_FLOAT) {
            return LLVMBuildFNeg(codegen->llvm_builder, expr, "fneg");
          } else {
            return LLVMBuildNeg(codegen->llvm_builder, expr, "neg");
          }
        case AST_UNARY_OP_NOT: {
          // 0 -> 1, !0 -> 0
          LLVMTypeRef expr_ty = LLVMTypeOf(expr);
          LLVMValueRef cmp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntEQ, expr,
                                           LLVMConstInt(expr_ty, 0, 0), "cmp");
          return LLVMBuildZExt(codegen->llvm_builder, cmp, LLVMTypeOf(expr), "zext");
        } break;
        case AST_UNARY_OP_COMP:
          return LLVMBuildXor(
              codegen->llvm_builder, expr,
              LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), (unsigned)-1, 0), "comp");
      }
    } break;

    case AST_EXPR_TYPE_BOOLEAN: {
      LLVMIntPredicate iop;
      LLVMRealPredicate fop;
      switch (ast->boolean.op) {
        case TOKEN_EQUALS:
          iop = LLVMIntEQ;
          fop = LLVMRealOEQ;
          break;
        case TOKEN_NE:
          iop = LLVMIntNE;
          fop = LLVMRealONE;
          break;
        case TOKEN_LT:
          iop = LLVMIntSLT;
          fop = LLVMRealOLT;
          break;
        case TOKEN_GT:
          iop = LLVMIntSGT;
          fop = LLVMRealOGT;
          break;
        case TOKEN_LTE:
          iop = LLVMIntSLE;
          fop = LLVMRealOLE;
          break;
        case TOKEN_GTE:
          iop = LLVMIntSGE;
          fop = LLVMRealOGE;
          break;
        default:
          fprintf(stderr, "unhandled boolean op %d\n", ast->boolean.op);
          return NULL;
      }

      LLVMValueRef lhs = emit_expr(codegen, ast->boolean.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->boolean.rhs);
      if (ast->boolean.lhs->ty.ty == AST_TYPE_FLOAT) {
        return LLVMBuildFCmp(codegen->llvm_builder, fop, lhs, rhs, "cmp");
      }
      return LLVMBuildICmp(codegen->llvm_builder, iop, lhs, rhs, "cmp");
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->array_index.ident.value.identv.ident, 1);

      LLVMTypeRef target_ty = ast_ty_to_llvm_ty(codegen, &ast->ty);

      LLVMValueRef index = emit_expr(codegen, ast->array_index.index);
      LLVMValueRef gep[] = {LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
                            index};
      LLVMValueRef retrieve =
          LLVMBuildGEP2(codegen->llvm_builder, entry->variable_type, entry->ref, gep, 2, "");
      return LLVMBuildLoad2(codegen->llvm_builder, target_ty, retrieve, "load");
    } break;

    case AST_EXPR_TYPE_MATCH: {
      return emit_match_expr(codegen, &ast->ty, &ast->match);
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      if (codegen->current_function) {
        LLVMTypeRef struct_type = ast_ty_to_llvm_ty(codegen, ast->ty.array.element_ty);
        LLVMValueRef dest = into ? into : new_alloca(codegen, struct_type, "struct");
        struct ast_expr_list *node = ast->list;
        for (size_t i = 0; i < ast->list->num_elements; i++) {
          LLVMValueRef value = emit_expr(codegen, node->expr);
          LLVMValueRef store = LLVMBuildStructGEP2(codegen->llvm_builder, struct_type, dest,
                                                   (unsigned int)i, "struct_field");
          emit_store(codegen, &node->expr->ty, value, store);
          node = node->next;
        }

        return dest;
      }

      // TODO: const initializer
    } break;

    case AST_EXPR_TYPE_NIL: {
      LLVMTypeRef target_ty = ast_ty_to_llvm_ty(codegen, &ast->ty);
      return LLVMConstNull(target_ty);
    } break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      // here, we just emit the tag value as the match
      // the match expression handler handles unwrapping an inner value, if any, and storing it

      // find the field
      struct ast_enum_field *field = ast->ty.enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->enum_init.enum_val_name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      LLVMValueRef tag_value =
          LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), field->value, 0);

      return tag_value;
    } break;

    case AST_EXPR_TYPE_ENUM_INIT: {
      // LLVMTypeRef result_ty = ast_ty_to_llvm_ty(codegen, &ast->enum_init.field_ty);

      // find the field
      struct ast_enum_field *field = ast->ty.enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->enum_init.enum_val_name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      LLVMValueRef tag_value =
          LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), field->value, 0);

      if (ast->ty.enumty.no_wrapped_fields) {
        return tag_value;
      }

      LLVMValueRef inner = emit_expr(codegen, ast->enum_init.inner);

      struct struct_entry *entry =
          kv_lookup(codegen->structs, ast->enum_init.enum_ty_name.value.identv.ident);
      LLVMTypeRef enum_type = entry->type;

      LLVMValueRef storage = into ? into : new_alloca(codegen, enum_type, "enum");
      LLVMValueRef tag = LLVMBuildStructGEP2(codegen->llvm_builder, enum_type, storage, 0, "tag");
      LLVMValueRef buf = LLVMBuildStructGEP2(codegen->llvm_builder, enum_type, storage, 1, "buf");

      LLVMBuildStore(codegen->llvm_builder, tag_value, tag);
      LLVMBuildStore(codegen->llvm_builder, inner, buf);

      return storage;
    } break;

    default:
      fprintf(stderr, "unhandled expression type %d\n", ast->type);
  }

  return NULL;
}
