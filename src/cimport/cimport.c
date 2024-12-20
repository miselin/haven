#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "clang-c/CXString.h"
#include "parse.h"
#include "tokens.h"
#include "types.h"
#include "utility.h"

static uint64_t next = 0;

struct ast_ty parse_cursor_type(CXCursor cursor);
struct ast_ty parse_type(CXType type);

static void collect_struct_fields(CXCursor cursor, struct ast_ty *ty);

static enum CXChildVisitResult libclang_visitor_decls(CXCursor cursor, CXCursor parent,
                                                      CXClientData client_data);

enum CXChildVisitResult struct_field_visitor(CXCursor field_cursor, CXCursor parent,
                                             CXClientData client_data) {
  UNUSED(parent);
  struct ast_ty *ty = (struct ast_ty *)client_data;

  /**
   * example from clang -Xclang -ast-dump

|-RecordDecl 0x21690fa0 <<source>:2:1, line:8:1> line:2:8 struct foo definition
| |-FieldDecl 0x21691058 <line:3:5, col:9> col:9 tag 'int'
| |-RecordDecl 0x216910a8 <line:4:5, line:7:5> line:4:5 union definition
| | |-FieldDecl 0x21691168 <line:5:9, col:13> col:13 x 'int'
| | `-FieldDecl 0x216911d0 <line:6:9, col:15> col:15 f 'float'
| `-FieldDecl 0x21691278 <line:4:5, line:7:7> col:7 __value 'union (unnamed union <snip>)'

   */

  if (clang_getCursorKind(field_cursor) != CXCursor_FieldDecl) {
    // TODO: we actually want to handle RecordDecl for anonymous types, but not as a field
    fprintf(stderr, "got kind %d that is not FieldDecl\n", clang_getCursorKind(field_cursor));
    return CXChildVisit_Continue;
  }

  struct ast_struct_field *last = ty->structty.fields;
  while (last && last->next) {
    last = last->next;
  }

  CXType field_type = clang_getCursorType(field_cursor);
  if (field_type.kind == CXType_Elaborated) {
    field_type = clang_Type_getNamedType(field_type);
  }

  CXCursor field_type_decl = clang_getTypeDeclaration(field_type);

  if (clang_Cursor_isAnonymous(field_type_decl)) {
    fprintf(stderr, "anonymous struct/union not handled yet...\n");
    return CXChildVisit_Continue;
  }

  CXString spelling = clang_getCursorSpelling(field_cursor);
  const char *spelling_c = clang_getCString(spelling);

  struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
  field->ty = calloc(1, sizeof(struct ast_ty));
  *field->ty = parse_type(field_type);
  strncpy(field->name, *spelling_c ? spelling_c : "<unknown-spelling>", 256);

  if (field->ty->ty == AST_TYPE_STRUCT) {
    collect_struct_fields(field_cursor, field->ty);
  }

  if (!last) {
    ty->structty.fields = field;
  } else {
    last->next = field;
  }

  ty->structty.num_fields++;

  clang_disposeString(spelling);

  return CXChildVisit_Continue;
}

static enum CXChildVisitResult struct_field_visitor2(CXCursor field_cursor, CXCursor parent,
                                                     CXClientData client_data) {
  UNUSED(parent);
  clang_visitChildren(field_cursor, struct_field_visitor, client_data);
  return CXChildVisit_Continue;
}

static void collect_struct_fields(CXCursor cursor, struct ast_ty *ty) {
  clang_visitChildren(cursor, struct_field_visitor2, ty);

  // C has forward-declared structs that don't require fields, as long as they are used as pointers
  // Haven requires at least one field for each struct. So just add an opaque field if there are no
  // other fields in the C type.
  if (!ty->structty.num_fields) {
    struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
    field->ty = calloc(1, sizeof(struct ast_ty));
    field->ty->ty = AST_TYPE_INTEGER;
    field->ty->integer.is_signed = 1;
    field->ty->integer.width = 8;
    strncpy(field->name, "<opaque>", 256);
    ty->structty.fields = field;
  }
}

struct ast_ty parse_type(CXType type) {
  struct ast_ty result;
  memset(&result, 0, sizeof(struct ast_ty));

  // TODO: are there cases where we don't want to do this indirection?
  if (type.kind == CXType_Elaborated) {
    type = clang_Type_getNamedType(type);
  }

  type = clang_getUnqualifiedType(type);

  CXString spelling = clang_getTypeSpelling(type);

  const char *spelling_c = clang_getCString(spelling);

  // remove type tags
  while (1) {
    if (!strncmp(spelling_c, "enum ", 5)) {
      spelling_c += 5;
    } else if (!strncmp(spelling_c, "struct ", 7)) {
      spelling_c += 7;
    } else {
      break;
    }
  }

  strncpy(result.name, spelling_c, 256);

  switch (type.kind) {
    case CXType_Invalid:
      result.ty = AST_TYPE_ERROR;
      break;

    case CXType_Void:
      result.ty = AST_TYPE_VOID;
      break;

    case CXType_Bool:
    case CXType_Char_U:
    case CXType_UChar:
    case CXType_UShort:
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UInt128:
      result.ty = AST_TYPE_INTEGER;
      result.integer.is_signed = 0;
      result.integer.width = (size_t)clang_Type_getSizeOf(type) * 8;
      break;

    case CXType_Char_S:
    case CXType_SChar:
    case CXType_Short:
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Int128:
      result.ty = AST_TYPE_INTEGER;
      result.integer.is_signed = 1;
      result.integer.width = (size_t)clang_Type_getSizeOf(type) * 8;
      break;

    case CXType_Float:
    case CXType_Double:      // HACK
    case CXType_LongDouble:  // HACK
      result.ty = AST_TYPE_FLOAT;
      break;

    case CXType_Pointer:
      result = parse_type(clang_getPointeeType(type));
      result.flags |= TYPE_FLAG_PTR;
      break;

    case CXType_Enum:
      result.ty = AST_TYPE_ERROR;
      break;

    case CXType_Elaborated:
      // "struct S" as a reference not a definition
      result.ty = AST_TYPE_CUSTOM;
      strncpy(result.name, spelling_c, 256);
      break;

    case CXType_Record:
      result.ty = AST_TYPE_STRUCT;
      // caller must call collect_struct_fields on the decl's cursor
      break;

    case CXType_Typedef:
      result.ty = AST_TYPE_CUSTOM;
      strncpy(result.name, spelling_c, 256);
      break;

    case CXType_ConstantArray:
      result.ty = AST_TYPE_ARRAY;
      result.array.width = (size_t)clang_getArraySize(type);
      result.array.element_ty = calloc(1, sizeof(struct ast_ty));
      *result.array.element_ty = parse_type(clang_getArrayElementType(type));
      break;

    default:
      fprintf(stderr, "unhandled type kind %d\n", type.kind);
      result.ty = AST_TYPE_ERROR;
      break;
  }

  clang_disposeString(spelling);
  return result;
}

struct ast_ty parse_cursor_type(CXCursor cursor) {
  CXType type = clang_getCursorType(cursor);
  return parse_type(type);
}

static void analyze_function_type(CXType type, struct ast_fdecl *fdecl) {
  // Check if the type is a function type
  if (type.kind != CXType_FunctionProto && type.kind != CXType_FunctionNoProto) {
    return;
  }

  // Get the return type
  CXType return_type = clang_getResultType(type);

  fdecl->retty = parse_type(return_type);

  // Get the number of arguments
  size_t num_args = (size_t)clang_getNumArgTypes(type);
  fdecl->num_params = num_args;
  if (num_args > 0) {
    fdecl->params = calloc(num_args, sizeof(struct ast_vdecl *));

    // Get each parameter type
    for (size_t i = 0; i < num_args; i++) {
      CXType param_type = clang_getArgType(type, (unsigned int)i);
      CXString param_type_spelling = clang_getTypeSpelling(param_type);
      clang_disposeString(param_type_spelling);

      // TODO: be nice to use the proper param name if it's present in the prototype
      fdecl->params[i] = calloc(1, sizeof(struct ast_vdecl));
      fdecl->params[i]->ty = parse_type(param_type);
      fdecl->params[i]->flags = 0;
      fdecl->params[i]->ident.ident = TOKEN_IDENTIFIER;
      snprintf(fdecl->params[i]->ident.value.identv.ident, 256, "p%zd", i);
    }
  }

  // Check if the function is variadic
  if (clang_isFunctionTypeVariadic(type)) {
    fdecl->flags |= DECL_FLAG_VARARG;
  }
}

// Visitor function to handle declarations
static enum CXChildVisitResult libclang_visitor_decls(CXCursor cursor, CXCursor parent,
                                                      CXClientData client_data) {
  UNUSED(parent);
  struct ast_program *program = (struct ast_program *)client_data;

  CXTranslationUnit TU = clang_Cursor_getTranslationUnit(cursor);
  CXCursor TU_cursor = clang_getTranslationUnitCursor(TU);

  // Check if this is a file-scope declaration
  if (clang_equalCursors(clang_getCursorSemanticParent(cursor), TU_cursor)) {
    // Extract relevant information
    CXString spelling = clang_getCursorSpelling(cursor);
    const char *spelling_c = clang_getCString(spelling);

    struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));

    enum CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_FunctionDecl) {
      decl->type = AST_DECL_TYPE_FDECL;
      decl->fdecl.flags |= DECL_FLAG_PUB;
      decl->fdecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->fdecl.ident.value.identv.ident, spelling_c, 256);
      analyze_function_type(clang_getCursorType(cursor), &decl->fdecl);
    } else if (kind == CXCursor_VarDecl) {
      decl->type = AST_DECL_TYPE_VDECL;
      decl->vdecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->vdecl.ident.value.identv.ident, spelling_c, 256);
      decl->vdecl.ty = parse_cursor_type(cursor);
      decl->vdecl.flags = DECL_FLAG_PUB;  // TODO: mut etc
    } else if (kind == CXCursor_TypedefDecl) {
      UNUSED(next);
      CXType underlying = clang_getTypedefDeclUnderlyingType(cursor);
      if (underlying.kind == CXType_Elaborated) {
        underlying = clang_Type_getNamedType(underlying);
      }

      CXString underlying_name = clang_getTypeSpelling(underlying);

      decl->type = AST_DECL_TYPE_TYDECL;
      decl->tydecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->tydecl.ident.value.identv.ident, spelling_c, 256);
      decl->tydecl.ty.ty = AST_TYPE_CUSTOM;
      decl->tydecl.ty = parse_type(underlying);

      clang_disposeString(underlying_name);

      if (underlying.kind == CXType_Record) {
        collect_struct_fields(cursor, &decl->tydecl.ty);
      }
    } else {
      free(decl);
      decl = NULL;
    }

    if (decl) {
      struct ast_toplevel *last_decl = program->decls;
      while (last_decl && last_decl->next) {
        last_decl = last_decl->next;
      }

      if (last_decl) {
        last_decl->next = decl;
      } else {
        program->decls = decl;
      }
    }

    clang_disposeString(spelling);
  } else {
    fprintf(stderr, "skipping cursor kind %s\n",
            clang_getCString(clang_getCursorKindSpelling(clang_getCursorKind(cursor))));
  }

  return CXChildVisit_Continue;
}

int cimport(struct parser *parser, const char *filename) {
  CXIndex index = clang_createIndex(0, 0);
  CXTranslationUnit unit = clang_parseTranslationUnit(index, filename, NULL, 0, NULL, 0,
                                                      CXTranslationUnit_SkipFunctionBodies);

  if (!unit) {
    // TODO: get the errors, print em
    fprintf(stderr, "Unable to parse translation unit. Quitting.\n");
    clang_disposeIndex(index);
    return 1;
  }

  struct ast_program *program = calloc(1, sizeof(struct ast_program));

  CXCursor cursor = clang_getTranslationUnitCursor(unit);
  clang_visitChildren(cursor, libclang_visitor_decls, program);

  if (program->decls) {
    parser_merge_program(parser, program);
  }

  free(program);

  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
  return 0;
}
