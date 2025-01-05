#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast.h"
#include "clang-c/CXFile.h"
#include "clang-c/CXString.h"
#include "tokens.h"
#include "types.h"
#include "utility.h"

struct cimport_file {
  const char *filename;
  struct cimport_file *next;
};

struct cimport {
  CXIndex index;
  CXIndexAction action;

  struct cimport_file *imports;
};

static uint64_t next = 0;

static struct ast_ty *parse_cursor_type(CXCursor cursor);
static struct ast_ty *parse_type(CXType type, CXCursor cursor);
static struct ast_ty *parse_simple_type_or_custom(CXType type);

static void analyze_function_type(CXType type, struct ast_fdecl *fdecl);

static void collect_struct_fields(CXCursor cursor, struct ast_ty *ty);

static enum CXChildVisitResult libclang_visitor_decls(CXCursor cursor, CXCursor parent,
                                                      CXClientData client_data);

static enum CXChildVisitResult struct_field_visitor(CXCursor field_cursor, CXCursor parent,
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
    fprintf(stderr, "skipping non-field cursor kind %d\n", clang_getCursorKind(field_cursor));
    fprintf(stderr, " -> StructDecl is %d, UnionDecl is %d\n", CXCursor_StructDecl,
            CXCursor_UnionDecl);
    return CXChildVisit_Continue;
  }

  CXSourceLocation loc = clang_getCursorLocation(field_cursor);
  CXFile file;
  unsigned line, column, offset;
  clang_getFileLocation(loc, &file, &line, &column, &offset);

  CXString loc_name = clang_getFileName(file);

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
    fprintf(stderr, "anonymous struct/union not handled yet (needed at %s:%d:%d)...\n",
            clang_getCString(loc_name), line, column);
    clang_disposeString(loc_name);
    return CXChildVisit_Continue;
  }

  clang_disposeString(loc_name);

  CXString spelling = clang_getCursorSpelling(field_cursor);
  const char *spelling_c = clang_getCString(spelling);

  struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
  struct ast_ty *field_ty = parse_type(field_type, field_type_decl);
  field->parsed_ty = *field_ty;
  free(field_ty);
  strncpy(field->name, *spelling_c ? spelling_c : "<unknown-spelling>", 256);

  if (!last) {
    ty->structty.fields = field;
  } else {
    last->next = field;
  }

  ty->structty.num_fields++;

  clang_disposeString(spelling);

  return CXChildVisit_Continue;
}

static void collect_struct_fields(CXCursor cursor, struct ast_ty *ty) {
  clang_visitChildren(cursor, struct_field_visitor, ty);

  // C has forward-declared structs that don't require fields, as long as they are used as pointers
  // Haven requires at least one field for each struct. So just add an opaque field if there are no
  // other fields in the C type.
  if (!ty->structty.num_fields) {
    struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
    field->parsed_ty.ty = AST_TYPE_INTEGER;
    field->parsed_ty.integer.is_signed = 1;
    field->parsed_ty.integer.width = 8;
    strncpy(field->name, "<opaque>", 256);
    ty->structty.fields = field;
    ty->structty.num_fields = 1;
  }
}

static enum CXChildVisitResult enum_field_visitor(CXCursor field_cursor, CXCursor parent,
                                                  CXClientData client_data) {
  UNUSED(parent);
  struct ast_ty *ty = (struct ast_ty *)client_data;

  if (clang_getCursorKind(field_cursor) != CXCursor_EnumConstantDecl) {
    return CXChildVisit_Continue;
  }

  struct ast_enum_field *last = ty->enumty.fields;
  while (last && last->next) {
    last = last->next;
  }

  struct ast_enum_field *field = calloc(1, sizeof(struct ast_enum_field));
  field->value = clang_getEnumConstantDeclUnsignedValue(field_cursor);

  CXString spelling = clang_getCursorSpelling(field_cursor);
  const char *spelling_c = clang_getCString(spelling);

  strncpy(field->name, *spelling_c ? spelling_c : "<unknown-spelling>", 256);

  if (!last) {
    ty->enumty.fields = field;
  } else {
    last->next = field;
  }

  ty->enumty.num_fields++;

  clang_disposeString(spelling);

  return CXChildVisit_Continue;
}

static void collect_enum_fields(CXCursor cursor, struct ast_ty *ty) {
  clang_visitChildren(cursor, enum_field_visitor, ty);
}

static int parse_simple_type(CXType type, struct ast_ty *into) {
  int rc = 0;

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
    } else if (!strncmp(spelling_c, "union ", 6)) {
      spelling_c += 6;
    } else {
      break;
    }
  }

  strncpy(into->name, spelling_c, 256);

  switch (type.kind) {
    case CXType_Invalid:
      into->ty = AST_TYPE_ERROR;
      break;

    case CXType_Void:
      into->ty = AST_TYPE_VOID;
      break;

    case CXType_Bool:
    case CXType_Char_U:
    case CXType_UChar:
    case CXType_UShort:
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UInt128:
      into->ty = AST_TYPE_INTEGER;
      into->integer.is_signed = 0;
      into->integer.width = (size_t)clang_Type_getSizeOf(type) * 8;
      break;

    case CXType_Char_S:
    case CXType_SChar:
    case CXType_Short:
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Int128:
      into->ty = AST_TYPE_INTEGER;
      into->integer.is_signed = 1;
      into->integer.width = (size_t)clang_Type_getSizeOf(type) * 8;
      break;

    case CXType_Float:
    case CXType_Double:      // HACK
    case CXType_LongDouble:  // HACK
      into->ty = AST_TYPE_FLOAT;
      break;

    // not technically built in, but doesn't get a name by any other means
    case CXType_FunctionProto: {
      struct ast_fdecl fdecl;
      analyze_function_type(type, &fdecl);

      *into = fdecl.parsed_function_ty;

      for (size_t i = 0; i < fdecl.num_params; i++) {
        free(fdecl.params[i].name);
      }
      free(fdecl.params);
    } break;

    case CXType_Pointer: {
      CXType pointee = clang_getPointeeType(type);
      if (pointee.kind == CXType_FunctionProto) {
        // fn ptrs are AST_TYPE_FUNCTION
        parse_simple_type(pointee, into);
      } else {
        // true pointer type
        into->ty = AST_TYPE_POINTER;
        into->pointer.pointee = parse_simple_type_or_custom(pointee);
      }
    } break;

    case CXType_ConstantArray:
      into->ty = AST_TYPE_ARRAY;
      into->array.width = (size_t)clang_getArraySize(type);
      into->array.element_ty = parse_simple_type_or_custom(clang_getArrayElementType(type));
      break;

    case CXType_IncompleteArray:
      into->ty = AST_TYPE_POINTER;
      into->pointer.pointee = parse_simple_type_or_custom(clang_getArrayElementType(type));
      break;

    default:
      rc = -1;
  }

  clang_disposeString(spelling);
  return rc;
}

static struct ast_ty *parse_type(CXType type, CXCursor cursor) {
  struct ast_ty *result = calloc(1, sizeof(struct ast_ty));

  if (parse_simple_type(type, result) == 0) {
    return result;
  }

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
    } else if (!strncmp(spelling_c, "union ", 6)) {
      spelling_c += 6;
    } else {
      break;
    }
  }

  strncpy(result->name, spelling_c, 256);

  switch (type.kind) {
    case CXType_Elaborated:
      // "struct S" as a reference not a definition
      result->ty = AST_TYPE_CUSTOM;
      strncpy(result->name, spelling_c, 256);
      break;

    case CXType_Record: {
      result->ty = AST_TYPE_STRUCT;
      collect_struct_fields(cursor, result);
    } break;

    case CXType_Enum: {
      result->ty = AST_TYPE_ENUM;
      result->enumty.no_wrapped_fields = 1;
      collect_enum_fields(cursor, result);
    } break;

    case CXType_Typedef:
      result->ty = AST_TYPE_CUSTOM;
      strncpy(result->name, spelling_c, 256);
      break;

    default:
      fprintf(stderr, "cimport: unhandled type kind %d\n", type.kind);
      result->ty = AST_TYPE_ERROR;
      break;
  }

  clang_disposeString(spelling);
  return result;
}

static struct ast_ty *parse_simple_type_or_custom(CXType type) {
  struct ast_ty *result = calloc(1, sizeof(struct ast_ty));
  int rc = parse_simple_type(type, result);
  if (rc == 0) {
    return result;
  }

  result->ty = AST_TYPE_CUSTOM;

  CXString spelling = clang_getTypeSpelling(type);
  const char *spelling_c = clang_getCString(spelling);

  while (1) {
    if (!strncmp(spelling_c, "enum ", 5)) {
      spelling_c += 5;
    } else if (!strncmp(spelling_c, "struct ", 7)) {
      spelling_c += 7;
    } else if (!strncmp(spelling_c, "union ", 6)) {
      spelling_c += 6;
    } else if (!strncmp(spelling_c, "const ", 6)) {
      spelling_c += 6;
    } else {
      break;
    }
  }

  strncpy(result->name, spelling_c, 256);
  clang_disposeString(spelling);

  return result;
}

static struct ast_ty *parse_cursor_type(CXCursor cursor) {
  CXType type = clang_getCursorType(cursor);
  return parse_type(type, cursor);
}

static void analyze_function_type(CXType type, struct ast_fdecl *fdecl) {
  // Check if the type is a function type
  if (type.kind != CXType_FunctionProto && type.kind != CXType_FunctionNoProto) {
    return;
  }

  // Get the return type
  CXType return_type = clang_getResultType(type);

  fdecl->flags |= DECL_FLAG_EXTERN | DECL_FLAG_IMPURE;

  fdecl->parsed_function_ty.ty = AST_TYPE_FUNCTION;
  fdecl->parsed_function_ty.function.retty = parse_simple_type_or_custom(return_type);

  // Get the number of arguments
  size_t num_args = (size_t)clang_getNumArgTypes(type);
  fdecl->num_params = num_args;
  fdecl->parsed_function_ty.function.num_params = num_args;
  if (num_args > 0) {
    fdecl->params = calloc(num_args, sizeof(struct ast_fdecl_param_metadata));
    fdecl->parsed_function_ty.function.param_types = calloc(num_args, sizeof(struct ast_ty *));

    // Get each parameter type
    for (size_t i = 0; i < num_args; i++) {
      CXType param_type = clang_getArgType(type, (unsigned int)i);
      CXString param_type_spelling = clang_getTypeSpelling(param_type);
      clang_disposeString(param_type_spelling);

      // TODO: be nice to use the proper param name if it's present in the prototype
      char *param_name = (char *)malloc(256);
      snprintf(param_name, 256, "p%zd", i);

      fdecl->params[i].name = param_name;
      fdecl->parsed_function_ty.function.param_types[i] = parse_simple_type_or_custom(param_type);
    }
  }

  // Check if the function is variadic
  if (clang_isFunctionTypeVariadic(type)) {
    fdecl->flags |= DECL_FLAG_VARARG;
    fdecl->parsed_function_ty.function.vararg = 1;
  }
}

// Visitor function to handle declarations
static enum CXChildVisitResult libclang_visitor_decls(CXCursor cursor, CXCursor parent,
                                                      CXClientData client_data) {
  /**
   * New algorithm we need here...
   *
   * We need to basically iterate through all the different declarations that matter.
   * All types of declaration (functions, variables, etc)
   * Then, for each spelling, we need to find the most specific declaration (i.e. not an
   * anonymous or forward declaration) and use that as the generated ast_toplevel node.
   *
   * 1. Load all declarations, storing them in a table with a list of declarations for that name
   * 2. Iterate through the list and find the most specific declaration
   * 3. Emit an ast_toplevel for it
   *
   * recursive type defs? edge cases abound
   */

  UNUSED(parent);
  struct ast_program *program = (struct ast_program *)client_data;

  CXTranslationUnit TU = clang_Cursor_getTranslationUnit(cursor);
  CXCursor TU_cursor = clang_getTranslationUnitCursor(TU);

  CXSourceLocation loc = clang_getCursorLocation(cursor);

  CXFile file;
  unsigned line, column, offset;
  clang_getFileLocation(loc, &file, &line, &column, &offset);

  CXString loc_name = clang_getFileName(file);

  strncpy(program->loc.file, clang_getCString(loc_name), 256);

  // Check if this is a file-scope declaration
  if (clang_equalCursors(clang_getCursorSemanticParent(cursor), TU_cursor)) {
    // Extract relevant information
    CXString spelling = clang_getCursorSpelling(cursor);
    const char *spelling_c = clang_getCString(spelling);

    struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
    strncpy(decl->loc.file, clang_getCString(loc_name), 256);
    decl->loc.line = line;
    decl->loc.column = column;

    enum CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_FunctionDecl) {
      decl->type = AST_DECL_TYPE_FDECL;
      decl->toplevel.fdecl.flags |= DECL_FLAG_PUB;
      decl->toplevel.fdecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->toplevel.fdecl.ident.value.identv.ident, spelling_c, 256);
      analyze_function_type(clang_getCursorType(cursor), &decl->toplevel.fdecl);
    } else if (kind == CXCursor_VarDecl) {
      decl->type = AST_DECL_TYPE_VDECL;
      decl->toplevel.vdecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->toplevel.vdecl.ident.value.identv.ident, spelling_c, 256);
      struct ast_ty *cursor_ty = parse_cursor_type(cursor);
      decl->toplevel.vdecl.parser_ty = *cursor_ty;
      free(cursor_ty);
      decl->toplevel.vdecl.flags = DECL_FLAG_PUB;  // TODO: mut etc
    } else if (kind == CXCursor_TypedefDecl) {
      UNUSED(next);
      CXType underlying = clang_getTypedefDeclUnderlyingType(cursor);
      if (underlying.kind == CXType_Elaborated) {
        underlying = clang_Type_getNamedType(underlying);
      }

      CXString underlying_name = clang_getTypeSpelling(underlying);
      const char *underlying_name_c = clang_getCString(underlying_name);

      while (1) {
        if (!strncmp(underlying_name_c, "struct ", 7)) {
          underlying_name_c += 7;
        } else if (!strncmp(underlying_name_c, "union ", 6)) {
          underlying_name_c += 6;
        } else {
          break;
        }
      }

      // don't emit an alias for `typedef struct <name> { } <name>;`
      if (strcmp(spelling_c, underlying_name_c) != 0) {
        decl->type = AST_DECL_TYPE_TYDECL;
        decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
        strncpy(decl->toplevel.tydecl.ident.value.identv.ident, spelling_c, 256);

        if (parse_simple_type(underlying, &decl->toplevel.tydecl.parsed_ty) < 0) {
          decl->toplevel.tydecl.parsed_ty.ty = AST_TYPE_CUSTOM;
          strncpy(decl->toplevel.tydecl.parsed_ty.name, underlying_name_c, 256);
        }
      } else {
        free(decl);
        decl = NULL;
      }

      clang_disposeString(underlying_name);
    } else if (kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) {
      // make it.
      decl->type = AST_DECL_TYPE_TYDECL;
      decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->toplevel.tydecl.ident.value.identv.ident, spelling_c, 256);
      struct ast_ty *cursor_ty = parse_cursor_type(cursor);
      decl->toplevel.tydecl.parsed_ty = *cursor_ty;
      free(cursor_ty);

      decl->toplevel.tydecl.parsed_ty.structty.is_union = kind == CXCursor_UnionDecl;
    } else if (kind == CXCursor_EnumDecl) {
      decl->type = AST_DECL_TYPE_TYDECL;
      decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
      strncpy(decl->toplevel.tydecl.ident.value.identv.ident, spelling_c, 256);
      struct ast_ty *cursor_ty = parse_cursor_type(cursor);
      decl->toplevel.tydecl.parsed_ty = *cursor_ty;
      free(cursor_ty);
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

  clang_disposeString(loc_name);

  return CXChildVisit_Continue;
}

static void indexCallback(CXClientData client_data, const CXIdxDeclInfo *decl) {
  UNUSED(client_data);

  if (!decl->isDefinition) {
    return;
  }

  fprintf(stderr, "decl: '%s' USR='%s' ", decl->entityInfo->name, decl->entityInfo->USR);
  switch (decl->entityInfo->kind) {
    case CXIdxEntity_Function:
      fprintf(stderr, "function\n");
      break;
    case CXIdxEntity_Variable:
      fprintf(stderr, "variable\n");
      break;
    case CXIdxEntity_Typedef:
      fprintf(stderr, "typedef\n");
      break;
    case CXIdxEntity_EnumConstant:
      fprintf(stderr, "enum constant\n");
      break;
    case CXIdxEntity_Struct:
      fprintf(stderr, "struct\n");
      break;
    case CXIdxEntity_Union:
      fprintf(stderr, "union\n");
      break;
    case CXIdxEntity_Enum:
      fprintf(stderr, "enum\n");
      break;
    case CXIdxEntity_Field:
      fprintf(stderr, "field\n");
      break;

    default:
      fprintf(stderr, "unknown %d\n", decl->entityInfo->kind);
  }
}

static int abortQuery(CXClientData client_data, void *reserved) {
  UNUSED(client_data);
  UNUSED(reserved);
  return 0;
}

static void diagnostic(CXClientData client_data, CXDiagnosticSet diags, void *reserved) {
  UNUSED(client_data);
  UNUSED(diags);
  UNUSED(reserved);
}

static CXIdxClientFile enteredMainFile(CXClientData client_data, CXFile mainFile, void *reserved) {
  UNUSED(client_data);
  UNUSED(mainFile);
  UNUSED(reserved);
  return NULL;
}

static CXIdxClientFile ppIncludedFile(CXClientData client_data, const CXIdxIncludedFileInfo *info) {
  UNUSED(client_data);
  UNUSED(info);
  return NULL;
}

static CXIdxClientASTFile importedASTFile(CXClientData client_data,
                                          const CXIdxImportedASTFileInfo *info) {
  UNUSED(client_data);
  UNUSED(info);
  return NULL;
}

CXIdxClientContainer startedTranslationUnit(CXClientData client_data, void *reserved) {
  UNUSED(client_data);
  UNUSED(reserved);
  return NULL;
}

void indexEntityReference(CXClientData client_data, const CXIdxEntityRefInfo *info) {
  UNUSED(client_data);
  UNUSED(info);
}

IndexerCallbacks cbs = {
    abortQuery,     diagnostic,           enteredMainFile,
    ppIncludedFile, importedASTFile,      startedTranslationUnit,
    indexCallback,  indexEntityReference,
};

struct cimport *cimport_create(void) {
  struct cimport *importer = calloc(1, sizeof(struct cimport));
  importer->index = clang_createIndex(0, 0);
  importer->action = clang_IndexAction_create(importer->index);
  return importer;
}

void cimport_destroy(struct cimport *importer) {
  struct cimport_file *cursor = importer->imports;
  while (cursor) {
    struct cimport_file *next_file = cursor->next;
    free((void *)cursor->filename);
    free(cursor);
    cursor = next_file;
  }
  clang_IndexAction_dispose(importer->action);
  clang_disposeIndex(importer->index);
  free(importer);
}

int cimport(struct cimport *importer, const char *filename) {
  struct cimport_file *prev = NULL;
  struct cimport_file *cursor = importer->imports;
  while (cursor) {
    prev = cursor;
    cursor = cursor->next;
  }

  struct cimport_file *import = calloc(1, sizeof(struct cimport_file));
  import->filename = strdup(filename);
  if (prev) {
    prev->next = import;
  } else {
    importer->imports = import;
  }

  return 0;
}

int cimport_finalize(struct cimport *importer, struct ast_import *into) {
  // build the import list
  struct string_builder *builder = new_string_builder();
  {
    struct cimport_file *cursor = importer->imports;
    while (cursor) {
      fprintf(stderr, "importing %s\n", cursor->filename);
      string_builder_appendf(builder, "#include \"%s\"\n", cursor->filename);
      cursor = cursor->next;
    }
  }

  FILE *fp = fopen(".haven.merged.c", "w");
  if (!fp) {
    perror("fopen");
    return -1;
  }

  fwrite(string_builder_get(builder), 1, string_builder_len(builder), fp);
  fclose(fp);

  CXTranslationUnit unit;
  enum CXErrorCode rc =
      clang_parseTranslationUnit2(importer->index, ".haven.merged.c", NULL, 0, NULL, 0,
                                  CXTranslationUnit_SkipFunctionBodies, &unit);

  unlink(".haven.merged.c");

  if (rc != CXError_Success) {
    // TODO: get the errors, print em
    fprintf(stderr, "Unable to parse translation unit (error %d). Quitting.\n", rc);
    clang_disposeIndex(importer->index);
    return -1;
  }

  fprintf(stderr, "about to index merged TU\n");
  clang_indexTranslationUnit(importer->action, NULL, &cbs, sizeof(cbs), CXIndexOpt_None, unit);

  fprintf(stderr, "indexing TU complete\n");

  struct ast_program *program = calloc(1, sizeof(struct ast_program));

  CXCursor cursor = clang_getTranslationUnitCursor(unit);
  clang_visitChildren(cursor, libclang_visitor_decls, program);

  if (program->decls) {
    into->ast = program;
  } else {
    free(program);
  }

  clang_disposeTranslationUnit(unit);

  free_string_builder(builder);
  return 0;
}
