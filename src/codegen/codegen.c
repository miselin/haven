
#include "codegen.h"

#include <libgen.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Support.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/Types.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "compiler/internal.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "tokens.h"
#include "types.h"
#include "utility.h"

static void emit_ast(struct codegen *codegen, struct ast_program *ast);
static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast);
static LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast, LLVMValueRef into);

struct codegen *new_codegen(struct ast_program *ast, struct compiler *compiler) {
  if (initialize_llvm() != 0) {
    return NULL;
  }

  struct codegen *result = calloc(1, sizeof(struct codegen));
  result->ast = ast;
  result->llvm_context = LLVMContextCreate();
  result->llvm_module = LLVMModuleCreateWithNameInContext(ast->loc.file, result->llvm_context);
  result->llvm_builder = LLVMCreateBuilderInContext(result->llvm_context);
  result->llvm_dibuilder = LLVMCreateDIBuilder(result->llvm_module);
  result->compiler = compiler;

  LLVMContextSetDiscardValueNames(result->llvm_context, 0);

  result->functions = new_kv();

  result->structs = new_kv();

  char *dup1 = NULL, *dup2 = NULL;
  char *filename = NULL;
  char *dir = NULL;
  {
    dup1 = strdup(ast->loc.file);
    dir = dirname(dup1);
  }
  {
    dup2 = strdup(ast->loc.file);
    filename = basename(dup2);
  }

  // TODO: split file into dir + file
  result->file_metadata =
      LLVMDIBuilderCreateFile(result->llvm_dibuilder, filename, strlen(filename), dir, strlen(dir));
  result->compile_unit = LLVMDIBuilderCreateCompileUnit(
      result->llvm_dibuilder, LLVMDWARFSourceLanguageC, result->file_metadata, COMPILER_IDENT, 5, 0,
      "", 0, 0, "", 0, LLVMDWARFEmissionFull, 0, 0, 0, "", 0, "", 0);

  LLVMAddModuleFlag(
      result->llvm_module, LLVMModuleFlagBehaviorOverride, "Debug Info Version", 18,
      LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(result->llvm_context), 3, 0)));

  LLVMAddModuleFlag(
      result->llvm_module, LLVMModuleFlagBehaviorOverride, "Dwarf Version", 13,
      LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(result->llvm_context), 2, 0)));

  free(dup1);
  free(dup2);

  char *triple = LLVMGetDefaultTargetTriple();

  char *error = NULL;
  LLVMGetTargetFromTriple(triple, &result->llvm_target, &error);
  if (error) {
    fprintf(stderr, "error getting target: %s\n", error);
  }
  LLVMDisposeMessage(error);

  LLVMCodeGenOptLevel opt_level = LLVMCodeGenLevelDefault;
  LLVMRelocMode reloc = LLVMRelocPIC;

  switch (compiler_get_opt_level(compiler)) {
    case OptNone:
      opt_level = LLVMCodeGenLevelNone;
      break;
    case OptLight:
      opt_level = LLVMCodeGenLevelLess;
      break;
    case OptNormal:
      opt_level = LLVMCodeGenLevelDefault;
      break;
    case OptAggressive:
      opt_level = LLVMCodeGenLevelAggressive;
      break;
    default:
      break;
  }

  switch (compiler_get_relocations_type(compiler)) {
    case RelocsStatic:
      reloc = LLVMRelocStatic;
      break;
    case RelocsPIC:
      reloc = LLVMRelocPIC;
      break;
    default:
      break;
  }

  char *host_features = LLVMGetHostCPUFeatures();
  result->llvm_target_machine =
      LLVMCreateTargetMachine(result->llvm_target, triple, "generic", host_features, opt_level,
                              reloc, LLVMCodeModelDefault /* code model */);
  LLVMSetTarget(result->llvm_module, triple);

  result->llvm_data_layout = LLVMCreateTargetDataLayout(result->llvm_target_machine);
  char *layout = LLVMCopyStringRepOfTargetData(result->llvm_data_layout);
  LLVMSetDataLayout(result->llvm_module, layout);
  LLVMDisposeMessage(layout);

  LLVMDisposeMessage(host_features);
  LLVMDisposeMessage(triple);

  configure_llvm(compiler);

  codegen_internal_enter_scope(result, &ast->loc, 0);

  return result;
}

int codegen_run(struct codegen *codegen) {
  emit_preamble(codegen);

  emit_ast(codegen, codegen->ast);

  char *error = NULL;
  int rc = LLVMVerifyModule(codegen->llvm_module, LLVMReturnStatusAction, &error);
  if (rc) {
    fprintf(stderr,
            "Internal module verification failed:\n%s\n\nGenerated IR that failed verification "
            "follows:\n\n",
            error);

    codegen_emit_ir(codegen, stderr);
  }
  LLVMDisposeMessage(error);

  if (rc == 0) {
    // verification completed, run unconditional optimizations that tidy up the IR ready for further
    // optimizations and emission

    LLVMPassBuilderOptionsRef pass_options = LLVMCreatePassBuilderOptions();

    // run function passes
    LLVMErrorRef result = LLVMRunPasses(codegen->llvm_module, "mem2reg,sccp,simplifycfg,dce",
                                        codegen->llvm_target_machine, pass_options);
    if (result != NULL) {
      char *msg = LLVMGetErrorMessage(result);
      fprintf(stderr, "Error running function passes: %s\n", msg);
      LLVMDisposeErrorMessage(msg);
    }

    // run module passes now
    result = LLVMRunPasses(codegen->llvm_module, "globaldce,strip-dead-prototypes",
                           codegen->llvm_target_machine, pass_options);
    if (result != NULL) {
      char *msg = LLVMGetErrorMessage(result);
      fprintf(stderr, "Error running module passes: %s\n", msg);
      LLVMDisposeErrorMessage(msg);
    }

    // after the normal defaults, run opt level optimizations based on opt flags passed to the
    // compiler
    const char *opts = "default<Os>";
    switch (compiler_get_opt_level(codegen->compiler)) {
      case OptNone:
        opts = "default<O0>";
        break;
      case OptLight:
        opts = "default<O1>";
        break;
      case OptNormal:
        opts = "default<O2>";
        break;
      case OptAggressive:
        opts = "default<O3>";
        LLVMPassBuilderOptionsSetLoopInterleaving(pass_options, 1);
        LLVMPassBuilderOptionsSetLoopVectorization(pass_options, 1);
        LLVMPassBuilderOptionsSetLoopUnrolling(pass_options, 1);
        LLVMPassBuilderOptionsSetMergeFunctions(pass_options, 1);
        break;
      default:
        break;
    }

    result = LLVMRunPasses(codegen->llvm_module, opts, codegen->llvm_target_machine, pass_options);
    if (result != NULL) {
      char *msg = LLVMGetErrorMessage(result);
      fprintf(stderr, "Error running module passes: %s\n", msg);
      LLVMDisposeErrorMessage(msg);
    }

    LLVMDisposePassBuilderOptions(pass_options);
  }

  return rc;
}

char *codegen_ir(struct codegen *codegen) {
  return LLVMPrintModuleToString(codegen->llvm_module);
}

void codegen_dispose_ir(char *ir) {
  LLVMDisposeMessage(ir);
}

void destroy_codegen(struct codegen *codegen) {
  void *iter = kv_iter(codegen->structs);
  while (!kv_end(iter)) {
    struct struct_entry *entry = kv_next(&iter);
    free(entry);
  }
  destroy_kv(codegen->structs);

  iter = scope_iter(codegen->scope);
  while (!scope_end(iter)) {
    struct scope_entry *entry = scope_next(&iter);
    if (entry->fdecl) {
      free(entry->param_types);
    }
  }

  destroy_kv(codegen->functions);
  codegen_internal_leave_scope(codegen, 0);
  LLVMDisposeTargetData(codegen->llvm_data_layout);
  LLVMDisposeTargetMachine(codegen->llvm_target_machine);
  LLVMDisposeDIBuilder(codegen->llvm_dibuilder);
  LLVMDisposeBuilder(codegen->llvm_builder);
  LLVMDisposeModule(codegen->llvm_module);
  LLVMContextDispose(codegen->llvm_context);
  free(codegen);

  shutdown_llvm();
}

static void emit_ast(struct codegen *codegen, struct ast_program *ast) {
  update_debug_loc(codegen, &ast->loc);
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    emit_toplevel(codegen, decl);
    decl = decl->next;
  }
}

static LLVMTypeRef emit_struct_type(struct codegen *codegen, struct ast_ty *ty) {
  char buf[256 + 8];
  snprintf(buf, 256 + 8, "%s.%s", ty->structty.is_union ? "union" : "struct", ty->name);

  struct struct_entry *entry = calloc(1, sizeof(struct struct_entry));
  kv_insert(codegen->structs, ty->name, entry);
  entry->type = LLVMStructCreateNamed(codegen->llvm_context, buf);

  unsigned int num_fields = ty->structty.is_union ? 1 : (unsigned int)ty->structty.num_fields;

  size_t size = type_size(ty);

  LLVMTypeRef *element_types = NULL;
  if (ty->structty.is_union) {
    element_types = malloc(sizeof(LLVMTypeRef));
    element_types[0] =
        LLVMArrayType(LLVMInt8TypeInContext(codegen->llvm_context), (unsigned int)size);
  } else {
    element_types = malloc(sizeof(LLVMTypeRef) * ty->structty.num_fields);
    struct ast_struct_field *field = ty->structty.fields;
    for (size_t i = 0; i < ty->structty.num_fields; i++) {
      element_types[i] = ast_ty_to_llvm_ty(codegen, field->ty);
      field = field->next;
    }
  }

  LLVMStructSetBody(entry->type, element_types, num_fields, 0);

  free(element_types);
  return entry->type;
}

LLVMTypeRef emit_enum_type(struct codegen *codegen, struct ast_ty *ty) {
  char buf[256 + 8];
  snprintf(buf, 256 + 8, "enum.%s", ty->name);

  struct struct_entry *entry = calloc(1, sizeof(struct struct_entry));
  kv_insert(codegen->structs, ty->name, entry);

  if (ty->enumty.no_wrapped_fields) {
    // simple integer enum
    entry->type = LLVMInt32TypeInContext(codegen->llvm_context);
    return entry->type;
  }

  entry->type = LLVMStructCreateNamed(codegen->llvm_context, buf);

  struct ast_enum_field *largest_field = NULL;

  size_t total_size = 0;
  struct ast_enum_field *field = ty->enumty.fields;
  while (field) {
    if (field->has_inner) {
      size_t sz = type_size(&field->inner);
      if (sz > total_size) {
        total_size = sz;

        largest_field = field;
      }
    }
    field = field->next;
  }

  LLVMTypeRef *fields = NULL;
  unsigned int num_fields = 0;

  if (largest_field) {
    // largest type + i8 array for the remaining bytes
    size_t bytes_remaining = total_size - type_size(&largest_field->inner);
    if (bytes_remaining) {
      num_fields = 3;
    } else {
      num_fields = 2;
    }

    fields = malloc(sizeof(LLVMTypeRef) * num_fields);
    fields[0] = LLVMInt32TypeInContext(codegen->llvm_context);      // tag
    fields[1] = ast_ty_to_llvm_ty(codegen, &largest_field->inner);  // largest type
    if (bytes_remaining) {
      fields[2] = LLVMArrayType(LLVMInt8TypeInContext(codegen->llvm_context),
                                (unsigned int)bytes_remaining);
    }
  } else {
    // just a tag and a byte array
    num_fields = 2;
    fields = malloc(sizeof(LLVMTypeRef) * num_fields);
    fields[0] = LLVMInt32TypeInContext(codegen->llvm_context);  // tag
    fields[1] = LLVMArrayType(LLVMInt8TypeInContext(codegen->llvm_context),
                              (unsigned int)total_size);  // data storage for largest possible field
  }

  LLVMStructSetBody(entry->type, fields, num_fields, 0);

  free(fields);

  return entry->type;
}

static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast) {
  update_debug_loc(codegen, &ast->loc);
  if (ast->type == AST_DECL_TYPE_FDECL) {
    emit_fdecl(codegen, &ast->fdecl, &ast->loc);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    emit_vdecl(codegen, &ast->vdecl);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.ty.ty == AST_TYPE_STRUCT) {
      // generate the struct type
      emit_struct_type(codegen, &ast->tydecl.ty);
    } else if (ast->tydecl.ty.ty == AST_TYPE_ENUM) {
      emit_enum_type(codegen, &ast->tydecl.ty);
    } else {
      // just make the type alias
      // fprintf(stderr, "unhandled top level type declaration type %d\n", ast->tydecl.ty.ty);
    }
  } else if (ast->type == AST_DECL_TYPE_PREPROC || ast->type == AST_DECL_TYPE_IMPORT) {
    // no-op in codegen
  } else {
    fprintf(stderr, "unhandled toplevel type %d\n", ast->type);
  }
}

LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast) {
  codegen_internal_enter_scope(codegen, &ast->loc, 1);
  update_debug_loc(codegen, &ast->loc);
  struct ast_stmt *stmt = ast->stmt;
  LLVMValueRef last = NULL;
  while (stmt) {
    last = emit_stmt(codegen, stmt, NULL);
    stmt = stmt->next;
  }
  codegen_internal_leave_scope(codegen, 1);
  return last;
}

static LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast, LLVMValueRef into) {
  update_debug_loc(codegen, &ast->loc);
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR: {
      return emit_expr_into(codegen, ast->expr, into);
    } break;

    case AST_STMT_TYPE_LET: {
      const char *ident = ast->let.ident.value.identv.ident;
      LLVMTypeRef var_type = ast_ty_to_llvm_ty(codegen, &ast->let.ty);
      LLVMValueRef var = new_alloca(codegen, var_type, ident);
      LLVMValueRef init = emit_expr_into(codegen, ast->let.init_expr, var);
      if (init != var) {
        emit_store(codegen, &ast->let.init_expr->ty, init, var);
      }

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = &ast->let;
      entry->variable_type = var_type;
      entry->ref = var;
      scope_insert(codegen->scope, ident, entry);
      return var;
    } break;

    case AST_STMT_TYPE_ITER: {
      const char *ident = ast->iter.index_vdecl->ident.value.identv.ident;
      LLVMValueRef start = emit_expr(codegen, ast->iter.range.start);
      LLVMValueRef end = emit_expr(codegen, ast->iter.range.end);
      LLVMValueRef step = ast->iter.range.step
                              ? emit_expr(codegen, ast->iter.range.step)
                              : LLVMConstInt(LLVMInt64TypeInContext(codegen->llvm_context), 1, 0);

      int direction = 1;
      int64_t iter_step = 1;
      if (ast->iter.range.step) {
        if (extract_constant_int(ast->iter.range.step, &iter_step) == 0) {
          if (iter_step < 0) {
            direction = -1;
          }
        }
      }

      LLVMTypeRef var_type = ast_ty_to_llvm_ty(codegen, &ast->iter.index_vdecl->ty);

      codegen_internal_enter_scope(codegen, &ast->loc, 1);

      LLVMValueRef index = new_alloca(codegen, var_type, ident);
      emit_store(codegen, &ast->iter.range.start->ty, start, index);

      // insert the index variable to scope
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = ast->iter.index_vdecl;
      entry->variable_type = var_type;
      entry->ref = index;
      scope_insert(codegen->scope, ident, entry);

      // now build the actual loop

      LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.cond");
      LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.body");
      LLVMBasicBlockRef step_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.step");
      LLVMBasicBlockRef end_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.end");

      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_block);
      LLVMValueRef index_val = LLVMBuildLoad2(codegen->llvm_builder, var_type, index, "index");
      LLVMValueRef cmp = LLVMBuildICmp(
          codegen->llvm_builder, direction > 0 ? LLVMIntSLE : LLVMIntSGE, index_val, end, "cmp");
      LLVMValueRef loop_cond_br =
          LLVMBuildCondBr(codegen->llvm_builder, cmp, body_block, end_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, body_block);
      emit_block(codegen, &ast->iter.block);

      LLVMBuildBr(codegen->llvm_builder, step_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, step_block);
      LLVMValueRef next = LLVMBuildAdd(codegen->llvm_builder, index_val, step, "iter.next");
      emit_store(codegen, &ast->iter.index_vdecl->ty, next, index);
      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

      int64_t istart = 0;
      int64_t iend = 0;
      int rcstart = extract_constant_int(ast->iter.range.start, &istart);
      int rcend = extract_constant_int(ast->iter.range.end, &iend);

      if (rcstart == 0 && rcend == 0) {
        // range is constant, we can annotate an unroll count
        int64_t iters = ((iend - istart) / iter_step) + 1;

        fprintf(stderr, "loop has %ld iterations\n", iters);

        LLVMMetadataRef count_md = LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt64TypeInContext(codegen->llvm_context), (uint64_t)iters, 1));

        LLVMMetadataRef unroll_enable = LLVMMDStringInContext2(
            codegen->llvm_context, "llvm.loop.unroll.enable", strlen("llvm.loop.unroll.enable"));
        LLVMMetadataRef unroll_count_str = LLVMMDStringInContext2(
            codegen->llvm_context, "llvm.loop.unroll.count", strlen("llvm.loop.unroll.count"));

        LLVMMetadataRef unroll_count[2] = {unroll_count_str, count_md};

        LLVMMetadataRef unroll_enable_node =
            LLVMMDNodeInContext2(codegen->llvm_context, &unroll_enable, 1);
        LLVMMetadataRef unroll_count_node =
            LLVMMDNodeInContext2(codegen->llvm_context, unroll_count, 2);

        LLVMMetadataRef mds[] = {unroll_enable_node, unroll_count_node};

        LLVMMetadataRef loop_node = LLVMMDNodeInContext2(codegen->llvm_context, mds, 2);

        LLVMValueRef md_value = LLVMMetadataAsValue(codegen->llvm_context, loop_node);
        LLVMSetMetadata(loop_cond_br,
                        LLVMGetMDKindIDInContext(codegen->llvm_context, "llvm.loop", 9), md_value);
      }

      codegen_internal_leave_scope(codegen, 1);
    } break;

    case AST_STMT_TYPE_RETURN: {
      if (ast->expr) {
        LLVMValueRef ret = emit_expr(codegen, ast->expr);
        emit_store(codegen, &ast->expr->ty, ret, codegen->retval);
      }
      LLVMBuildBr(codegen->llvm_builder, codegen->return_block);
      LLVMBasicBlockRef new_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "after.return");
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, new_block);
    } break;

    case AST_STMT_TYPE_STORE: {
      LLVMValueRef lhs = emit_expr(codegen, ast->store.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->store.rhs);
      emit_store(codegen, &ast->store.rhs->ty, rhs, lhs);
    } break;

    case AST_STMT_TYPE_DEFER: {
      struct defer_entry *entry = calloc(1, sizeof(struct defer_entry));
      entry->expr = ast->expr;
      entry->next = codegen->defer_head;
      codegen->defer_head = entry;
    } break;

    case AST_STMT_TYPE_WHILE: {
      LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "while.cond");
      LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "while.body");
      LLVMBasicBlockRef end_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "while.end");

      struct scope_entry *loop_break = calloc(1, sizeof(struct scope_entry));
      loop_break->block = end_block;

      struct scope_entry *loop_continue = calloc(1, sizeof(struct scope_entry));
      loop_continue->block = cond_block;

      codegen_internal_enter_scope(codegen, &ast->loc, 1);
      scope_insert(codegen->scope, "@loop.break", loop_break);
      scope_insert(codegen->scope, "@loop.continue", loop_continue);

      LLVMBuildBr(codegen->llvm_builder, cond_block);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_block);

      LLVMTypeRef cond_type = ast_ty_to_llvm_ty(codegen, &ast->while_stmt.cond->ty);

      LLVMValueRef cond = emit_expr(codegen, ast->while_stmt.cond);
      LLVMValueRef comp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, cond,
                                        LLVMConstInt(cond_type, 0, 0), "while.cond");
      LLVMBuildCondBr(codegen->llvm_builder, comp, body_block, end_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, body_block);
      emit_block(codegen, &ast->while_stmt.block);
      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
      codegen_internal_leave_scope(codegen, 1);
    } break;

    case AST_STMT_TYPE_BREAK: {
      struct scope_entry *entry = scope_lookup(codegen->scope, "@loop.break", 1);
      LLVMBuildBr(codegen->llvm_builder, entry->block);
    } break;

    case AST_STMT_TYPE_CONTINUE: {
      struct scope_entry *entry = scope_lookup(codegen->scope, "@loop.continue", 1);
      LLVMBuildBr(codegen->llvm_builder, entry->block);
    } break;

    default:
      fprintf(stderr, "unhandled statement type %d\n", ast->type);
  }

  return NULL;
}

void codegen_internal_enter_scope(struct codegen *codegen, struct lex_locator *at,
                                  int lexical_block) {
  codegen->scope = enter_scope(codegen->scope);

  if (!lexical_block) {
    return;
  }

  struct codegen_block *parent = codegen->current_block;
  codegen->current_block = calloc(1, sizeof(struct codegen_block));
  codegen->current_block->parent = parent;
  codegen->current_block->scope_metadata = LLVMDIBuilderCreateLexicalBlock(
      codegen->llvm_dibuilder, parent ? parent->scope_metadata : codegen->current_function_metadata,
      codegen->file_metadata, (unsigned)at->line + 1, (unsigned)at->column + 1);
}

void codegen_internal_leave_scope(struct codegen *codegen, int lexical_block) {
  codegen->scope = exit_scope(codegen->scope);

  if (!lexical_block) {
    return;
  }

  struct codegen_block *block = codegen->current_block;
  codegen->current_block = codegen->current_block->parent;
  free(block);
}
