#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/Types.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "types.h"
#include "utility.h"

LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast) {
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emit expr %d", ast->type);

  update_debug_loc(codegen, &ast->loc);

  return emit_expr_into(codegen, ast, NULL);
}

LLVMValueRef emit_expr_into(struct codegen *codegen, struct ast_expr *ast, LLVMValueRef into) {
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emit expr %d (into %p)", ast->type,
               (void *)into);

  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      LLVMTypeRef const_ty = ast_ty_to_llvm_ty(codegen, ast->ty);
      switch (ast->ty->ty) {
        case AST_TYPE_INTEGER:
          return LLVMConstInt(const_ty, ast->expr.constant.constant.value.intv.val, 0);
        case AST_TYPE_STRING: {
          LLVMValueRef str = LLVMAddGlobal(
              codegen->llvm_module,
              LLVMArrayType(LLVMInt8TypeInContext(codegen->llvm_context),
                            (unsigned int)ast->expr.constant.constant.value.strv.length + 1),
              "str");
          LLVMSetInitializer(str,
                             LLVMConstStringInContext(
                                 codegen->llvm_context, ast->expr.constant.constant.value.strv.s,
                                 (unsigned int)ast->expr.constant.constant.value.strv.length, 0));
          LLVMSetLinkage(str, LLVMInternalLinkage);
          return str;
        } break;
        case AST_TYPE_FVEC: {
          LLVMValueRef *fields = malloc(sizeof(LLVMValueRef) * ast->expr.list->num_elements);

          unsigned int i = 0;
          int non_const_elements = 0;
          struct ast_expr_list *node = ast->expr.list;
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
                new_alloca(codegen, ast_ty_to_llvm_ty(codegen, ast->ty), "vec");
            LLVMBuildStore(codegen->llvm_builder, zero, vec_stack);
            LLVMValueRef vec = LLVMBuildLoad2(
                codegen->llvm_builder, ast_ty_to_llvm_ty(codegen, ast->ty), vec_stack, "vec");

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
              ast->expr.constant.constant.value.floatv.buf,
              (unsigned int)ast->expr.constant.constant.value.floatv.length);

        case AST_TYPE_ARRAY: {
          LLVMTypeRef inner_ty = ast_ty_to_llvm_ty(codegen, ast->ty->array.element_ty);
          LLVMValueRef *values = malloc(sizeof(LLVMValueRef) * ast->expr.list->num_elements);
          struct ast_expr_list *node = ast->expr.list;
          for (size_t i = 0; i < ast->expr.list->num_elements; i++) {
            values[i] = emit_expr(codegen, node->expr);
            values[i] = emit_cast(codegen, values[i], node->expr->ty, ast->ty->array.element_ty);

            node = node->next;
          }
          LLVMValueRef array = LLVMConstArray2(inner_ty, values, ast->expr.list->num_elements);
          free(values);
          return array;
        } break;

        case AST_TYPE_MATRIX: {
          // size_t total_elements = ast->ty->matrix.cols * ast->ty->matrix.rows;
          //  LLVMValueRef zero = LLVMConstNull(LLVMVectorType(
          //  LLVMFloatTypeInContext(codegen->llvm_context), (unsigned int)total_elements));

          // declare void @llvm.matrix.column.major.store.*(vectorty %In, ptrty %Ptr, i64 %Stride,
          // i1 <IsVolatile>, i32 <Rows>, i32 <Cols>)

          LLVMTypeRef vec_ty = ast_ty_to_llvm_ty(codegen, ast->ty);
          LLVMValueRef vec_stack = new_alloca(codegen, vec_ty, "vec");
          // LLVMBuildStore(codegen->llvm_builder, zero, vec_stack);
          // LLVMValueRef vec = LLVMBuildLoad2(codegen->llvm_builder,
          //                                 ast_ty_to_llvm_ty(codegen, ast->ty), vec_stack,
          //                                 "vec");

          LLVMValueRef vec = LLVMBuildLoad2(codegen->llvm_builder,
                                            ast_ty_to_llvm_ty(codegen, ast->ty), vec_stack, "vec");

          // LLVMTypeRef row_ty = LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context),
          // (unsigned int)ast->ty->matrix.cols);

          struct ast_expr_list *node = ast->expr.list;
          for (size_t j = 0; j < ast->expr.list->num_elements; j++) {
            LLVMValueRef expr = emit_expr(codegen, node->expr);

            /*

            LLVMValueRef indicies[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
                LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), j *
            ast->ty->matrix.cols, 0),
            };

            LLVMValueRef row = LLVMBuildGEP2(codegen->llvm_builder, row_ty, vec_stack, indicies, 2,
                                             "matrix.create.row");
            emit_store(codegen, &node->expr->ty, expr, row);
            */

            for (size_t col = 0; col < ast->ty->matrix.cols; col++) {
              LLVMValueRef col_idx =
                  LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), col, 0);
              LLVMValueRef matrix_idx = LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context),
                                                     j * ast->ty->matrix.cols + col, 0);
              LLVMValueRef element =
                  LLVMBuildExtractElement(codegen->llvm_builder, expr, col_idx, "element");
              vec = LLVMBuildInsertElement(codegen->llvm_builder, vec, element, matrix_idx,
                                           "element");
            }

            node = node->next;
          }

          // LLVMBuildStore(codegen->llvm_builder, vec, vec_stack);
          return vec;
        } break;

        default: {
          char buf[256];
          type_name_into(ast->ty, buf, 256);
          fprintf(stderr, "unhandled constant type %s\n", buf);
        }
      }
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *lookup =
          scope_lookup(codegen->scope, ast->expr.variable.ident.value.identv.ident, 1);

      if (lookup->variable_type) {
        // temporaries are things like function parameters, and do not require loads
        if (lookup->flags & DECL_FLAG_TEMPORARY) {
          return lookup->ref;
        } else if (ast->ty->flags & TYPE_FLAG_REFERENCE) {
          // refs require the address of the variable, not the value
          return lookup->ref;
        } else if (ast->ty->ty == AST_TYPE_ENUM && !ast->ty->enumty.no_wrapped_fields) {
          // enum is actually a struct -- don't load
          // with no wrapped fields, it's an integer
          return lookup->ref;
        } else if (ast->ty->ty == AST_TYPE_STRUCT) {
          // don't load structs, they need to be accessed via GEP
          return lookup->ref;
        } else if (ast->ty->ty == AST_TYPE_ARRAY) {
          // don't load arrays, they need to be accessed via GEP
          return lookup->ref;
        }

        return LLVMBuildLoad2(codegen->llvm_builder, lookup->variable_type, lookup->ref,
                              ast->expr.variable.ident.value.identv.ident);
      } else {
        // just return the function's ref
        return lookup->ref;
      }
    } break;

    case AST_EXPR_TYPE_BINARY: {
      if (ast_binary_op_logical(ast->expr.binary.op)) {
        // need to short-circuit L and R sides
        return emit_logical_expr(codegen, &ast->expr.binary, ast->ty);
      } else if (ast_binary_op_conditional(ast->expr.binary.op)) {
        // need to emit a conditional
        return emit_boolean_expr(codegen, &ast->expr.binary);
      } else {
        return emit_binary_expr(codegen, &ast->expr.binary, ast->ty);
      }
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return emit_block(codegen, &ast->expr.block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      LLVMValueRef call_target = NULL;
      LLVMTypeRef llvm_function_ty = NULL;
      if (ast->expr.call.fdecl) {
        struct scope_entry *entry =
            scope_lookup(codegen->scope, ast->expr.call.fdecl->ident.value.identv.ident, 1);
        call_target = entry->ref;
        llvm_function_ty = entry->function_type;
      } else {
        struct scope_entry *entry =
            scope_lookup(codegen->scope, ast->expr.call.ident.value.identv.ident, 1);
        call_target = entry->ref;
        llvm_function_ty = ast_llvm_function_ty(codegen, ast->expr.call.function_ty);
      }

      size_t named_param_count = LLVMCountParams(call_target);
      size_t is_complex = (size_t)type_is_complex(ast->expr.call.function_ty->function.retty);

      LLVMTypeRef ret_ty = ast_ty_to_llvm_ty(codegen, ast->expr.call.function_ty->function.retty);

      LLVMValueRef *args = NULL;
      unsigned int num_args = 0;
      if (ast->expr.call.args) {
        args = malloc(sizeof(LLVMValueRef) * (ast->expr.call.args->num_elements + is_complex));
        struct ast_expr_list *node = ast->expr.call.args;
        while (node) {
          args[num_args + is_complex] = emit_expr(codegen, node->expr);
          if (node->expr->ty->ty == AST_TYPE_FLOAT && num_args >= named_param_count) {
            // vararg floats need to be promoted to doubles for C compatibility
            args[num_args + is_complex] = LLVMBuildFPExt(
                codegen->llvm_builder, args[num_args + is_complex], LLVMDoubleType(), "fpext");
          }
          if (node->expr->ty->ty == AST_TYPE_BOX) {
            // need to ref the box as it's being passed to a function
            compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                         "box ref due to EXPR_TYPE_CALL");
            codegen_box_ref(codegen, args[num_args + is_complex], 1);
          }
          ++num_args;
          node = node->next;
        }
      }

      LLVMValueRef complex = NULL;
      if (is_complex) {
        if (!args) {
          args = malloc(sizeof(LLVMValueRef));
        }
        // build retval
        args[0] = into ? into : new_alloca(codegen, ret_ty, "sret");
        complex = args[0];

        ++num_args;
      }

      LLVMValueRef call =
          LLVMBuildCall2(codegen->llvm_builder, llvm_function_ty, call_target, args, num_args, "");

      if (is_complex) {
        unsigned int kind = LLVMGetEnumAttributeKindForName("sret", 4);
        LLVMAttributeRef attr = LLVMCreateTypeAttribute(codegen->llvm_context, kind, ret_ty);
        LLVMAddCallSiteAttribute(call, 1, attr);
      }

      if (ast->ty->ty == AST_TYPE_BOX) {
        compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                     "moving box return value into a stack variable");

        // no need for another ref, functions ref on return
        LLVMValueRef stack_box = new_alloca(codegen, codegen_pointer_type(codegen), "box");
        LLVMBuildStore(codegen->llvm_builder, call, stack_box);

        struct box_entry *box = calloc(1, sizeof(struct box_entry));
        box->box = stack_box;
        box->next = codegen->boxes;
        codegen->boxes = box;

        // call = stack_box;
      }

      if (args) {
        free(args);
      }
      return is_complex ? complex : call;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      char name[512];

      struct ast_ty *target_ty = ast->expr.deref.target->ty;
      if (target_ty->ty == AST_TYPE_POINTER) {
        target_ty = ptr_pointee_type(target_ty);
      } else if (target_ty->ty == AST_TYPE_BOX) {
        target_ty = box_pointee_type(target_ty);
      }

      // vector -> extractelement
      if (target_ty->ty == AST_TYPE_FVEC) {
        LLVMValueRef target = emit_expr(codegen, ast->expr.deref.target);
        snprintf(name, 512, "deref.vec.%zd", ast->expr.deref.field_idx);

        LLVMValueRef index = LLVMConstInt(LLVMInt64TypeInContext(codegen->llvm_context),
                                          (unsigned int)ast->expr.deref.field_idx, 0);
        return LLVMBuildExtractElement(codegen->llvm_builder, target, index, name);
      }

      LLVMTypeRef result_ty = ast_ty_to_llvm_ty(codegen, ast->ty);
      LLVMValueRef lvalue = emit_lvalue(codegen, ast);
      if (ast->ty->ty == AST_TYPE_ENUM && !ast->ty->enumty.no_wrapped_fields) {
        return lvalue;
      } else if (ast->ty->ty == AST_TYPE_ARRAY) {
        // we rarely want to actually load/move the underlying array
        return lvalue;
      }
      return LLVMBuildLoad2(codegen->llvm_builder, result_ty, lvalue, "deref");
    }; break;

    case AST_EXPR_TYPE_VOID:
      return NULL;

    case AST_EXPR_TYPE_CAST: {
      compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast...");
      LLVMValueRef expr = emit_expr(codegen, ast->expr.cast.expr);
      compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                   "cast expression emitted [%p %p]...", (void *)ast->expr.cast.expr->ty,
                   (void *)ast->ty);
      LLVMValueRef result = emit_cast(codegen, expr, ast->expr.cast.expr->ty, ast->ty);
      compiler_log(codegen->compiler, LogLevelDebug, "codegen", "cast emitted...");
      return result;
    } break;

    case AST_EXPR_TYPE_IF: {
      if (ast->ty->ty == AST_TYPE_VOID) {
        emit_if(codegen, ast);
        return NULL;
      } else {
        return emit_if(codegen, ast);
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      LLVMValueRef lhs = emit_lvalue(codegen, ast->expr.assign.lhs);
      if (!lhs) {
        return NULL;
      }

      if (ast->expr.assign.lhs->ty->ty == AST_TYPE_BOX) {
        compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                     "box unref due to EXPR_TYPE_ASSIGN [lhs]");
        codegen_box_unref(codegen, lhs, 0);
      }

      LLVMValueRef expr = emit_expr(codegen, ast->expr.assign.expr);

      if (ast->expr.assign.expr->ty->ty == AST_TYPE_BOX) {
        // need to ref the box in flight
        compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                     "box ref due to EXPR_TYPE_ASSIGN [rhs]");
        codegen_box_ref(codegen, expr, 1);
      }

      emit_store(codegen, ast->ty, expr, lhs);

      return lhs;
    } break;

    case AST_EXPR_TYPE_REF: {
      // return the reference (which is usually an alloca or GEP result)
      return emit_lvalue(codegen, ast->expr.ref.expr);
    } break;

    case AST_EXPR_TYPE_LOAD: {
      LLVMValueRef expr = emit_expr(codegen, ast->expr.load.expr);
      LLVMTypeRef expr_type = ast_ty_to_llvm_ty(codegen, ast->ty);

      if (type_is_complex(ast->ty)) {
        // put the result into a temporary and return that instead of the underlying value
        LLVMValueRef temp = into ? into : new_alloca(codegen, expr_type, "load");
        emit_store(codegen, ast->ty, expr, temp);
        return temp;
      }

      return LLVMBuildLoad2(codegen->llvm_builder, expr_type, expr, "load");
    } break;

    case AST_EXPR_TYPE_UNARY: {
      LLVMValueRef expr = emit_expr(codegen, ast->expr.unary.expr);
      switch (ast->expr.unary.op) {
        case AST_UNARY_OP_NEG:
          if (ast->ty->ty == AST_TYPE_FLOAT) {
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

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      LLVMValueRef val = emit_lvalue(codegen, ast);

      // certain types need to stay pointers
      if (ast->ty->ty == AST_TYPE_ENUM && !ast->ty->enumty.no_wrapped_fields) {
        return val;
      } else if (ast->ty->ty == AST_TYPE_STRUCT) {
        return val;
      }

      return LLVMBuildLoad2(codegen->llvm_builder, ast_ty_to_llvm_ty(codegen, ast->ty), val,
                            "array.index.load");
    } break;

    case AST_EXPR_TYPE_MATCH: {
      return emit_match_expr(codegen, ast->ty, &ast->expr.match);
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      if (codegen->current_function) {
        LLVMTypeRef struct_type = ast_ty_to_llvm_ty(codegen, ast->ty);
        LLVMValueRef dest = into ? into : new_alloca(codegen, struct_type, "struct");
        struct ast_expr_list *node = ast->expr.list;
        for (size_t i = 0; i < ast->expr.list->num_elements; i++) {
          LLVMValueRef value = emit_expr(codegen, node->expr);
          LLVMValueRef store = LLVMBuildStructGEP2(codegen->llvm_builder, struct_type, dest,
                                                   (unsigned int)i, "struct_field");
          emit_store(codegen, node->expr->ty, value, store);
          node = node->next;
        }

        return dest;
      }

      // TODO: const initializer
    } break;

    case AST_EXPR_TYPE_NIL: {
      LLVMTypeRef target_ty = ast_ty_to_llvm_ty(codegen, ast->ty);
      return LLVMConstNull(target_ty);
    } break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      // here, we just emit the tag value as the match
      // the match expression handler handles unwrapping an inner value, if any, and storing it

      // find the field
      struct ast_enum_field *field = ast->ty->enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->expr.enum_init.enum_val_name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      LLVMValueRef tag_value =
          LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), field->value, 0);

      return tag_value;
    } break;

    case AST_EXPR_TYPE_ENUM_INIT: {
      // LLVMTypeRef result_ty = ast_ty_to_llvm_ty(codegen, ast->expr.enum_init.field_ty);

      // find the field
      struct ast_enum_field *field = ast->ty->enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->expr.enum_init.enum_val_name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      LLVMValueRef tag_value =
          LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), field->value, 0);

      if (ast->ty->enumty.no_wrapped_fields) {
        return tag_value;
      }

      LLVMValueRef inner =
          ast->expr.enum_init.inner ? emit_expr(codegen, ast->expr.enum_init.inner) : NULL;

      struct struct_entry *entry =
          kv_lookup(codegen->structs, ast->expr.enum_init.enum_ty_name.value.identv.ident);
      LLVMTypeRef enum_type = entry->type;

      LLVMValueRef storage = into ? into : new_alloca(codegen, enum_type, "enum");
      LLVMValueRef tag = LLVMBuildStructGEP2(codegen->llvm_builder, enum_type, storage, 0, "tag");

      LLVMBuildStore(codegen->llvm_builder, tag_value, tag);
      if (inner) {
        LLVMValueRef buf = LLVMBuildStructGEP2(codegen->llvm_builder, enum_type, storage, 1, "buf");
        emit_store(codegen, ast->expr.enum_init.inner->ty, inner, buf);
        // LLVMBuildStore(codegen->llvm_builder, inner, buf);
      }

      return storage;
    } break;

    case AST_EXPR_TYPE_UNION_INIT: {
      LLVMTypeRef ty = ast_ty_to_llvm_ty(codegen, ast->ty);
      LLVMValueRef result = into ? into : new_alloca(codegen, ty, "union");

      LLVMValueRef inner = emit_expr(codegen, ast->expr.union_init.inner);

      emit_store(codegen, ast->expr.union_init.inner->ty, inner, result);
      return result;
    } break;

    case AST_EXPR_TYPE_SIZEOF: {
      LLVMTypeRef result_ty = ast_ty_to_llvm_ty(codegen, ast->expr.sizeof_expr.resolved);
      return const_i32(codegen, (int32_t)LLVMABISizeOfType(codegen->llvm_data_layout, result_ty));
    } break;

    case AST_EXPR_TYPE_BOX: {
      LLVMTypeRef box_type = codegen_box_type(codegen, ast->ty);

      LLVMTypeRef out_ty = ast_ty_to_llvm_ty(codegen, ast->ty);
      LLVMValueRef box = into ? into : new_alloca(codegen, out_ty, "box");

      LLVMValueRef box_size =
          const_i32(codegen, (int32_t)LLVMABISizeOfType(codegen->llvm_data_layout, box_type));

      if (ast->expr.box_expr.expr) {
        LLVMValueRef expr_size = const_i32(
            codegen,
            (int32_t)LLVMABISizeOfType(codegen->llvm_data_layout,
                                       ast_ty_to_llvm_ty(codegen, ast->expr.box_expr.expr->ty)));

        LLVMValueRef value = emit_expr(codegen, ast->expr.box_expr.expr);
        LLVMValueRef tmp = new_alloca(codegen, LLVMTypeOf(value), "box.tmp");
        emit_store(codegen, ast->expr.box_expr.expr->ty, value, tmp);

        LLVMValueRef args[] = {tmp, box_size, expr_size};
        LLVMValueRef result = LLVMBuildCall2(codegen->llvm_builder, codegen->preamble.new_box_type,
                                             codegen->preamble.new_box, args, 3, "box.new");
        LLVMBuildStore(codegen->llvm_builder, result, box);
      } else {
        LLVMValueRef args[] = {box_size};
        LLVMValueRef result =
            LLVMBuildCall2(codegen->llvm_builder, codegen->preamble.new_empty_box_type,
                           codegen->preamble.new_empty_box, args, 1, "box.new");
        LLVMBuildStore(codegen->llvm_builder, result, box);
      }

      struct box_entry *entry = calloc(1, sizeof(struct box_entry));
      entry->box = box;
      entry->next = codegen->boxes;
      codegen->boxes = entry;

      return box;
    } break;

    case AST_EXPR_TYPE_UNBOX: {
      LLVMTypeRef result_type = ast_ty_to_llvm_ty(codegen, ast->ty);
      LLVMTypeRef box_type = codegen_box_type(codegen, ast->expr.box_expr.expr->ty);

      LLVMValueRef box = emit_expr(codegen, ast->expr.box_expr.expr);

      // get the inner value
      LLVMValueRef ptr = LLVMBuildStructGEP2(codegen->llvm_builder, box_type, box, 1, "box.value");
      return LLVMBuildLoad2(codegen->llvm_builder, result_type, ptr, "box.unbox");
    } break;

    default:
      fprintf(stderr, "unhandled expression type %d\n", ast->type);
  }

  return NULL;
}
