#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast.h"
#include "clang-c/CXFile.h"
#include "clang-c/CXString.h"
#include "compiler.h"
#include "tokens.h"
#include "types.h"
#include "utility.h"

#define USE_INDEX_INSTEAD_OF_VISITOR 1

struct cimport_file {
  const char *filename;
  struct cimport_file *next;
};

struct cimport_known_type {
  CXType type;
  struct cimport_known_type *next;
};

struct cimport {
  struct compiler *compiler;

  CXIndex index;
  CXIndexAction action;

  struct ast_program *program;

  struct cimport_file *imports;
  struct cimport_known_type *known_types;
};

static uint64_t next = 0;

static int cimport_is_known_type(struct cimport *importer, CXType type);
static void cimport_add_known_type(struct cimport *importer, CXType type);

static struct ast_ty *parse_cursor_type(CXCursor cursor);
static struct ast_ty *parse_type(CXType type);
static struct ast_ty *parse_simple_type_or_custom(CXType type);

static void analyze_function_type(CXType type, struct ast_fdecl *fdecl);

static void collect_struct_fields(CXType type, struct ast_ty *ty);

static enum CXChildVisitResult libclang_visitor_decls(CXCursor cursor, CXCursor parent,
                                                      CXClientData client_data);

static enum CXVisitorResult struct_field_visitor(CXCursor field_cursor, CXClientData client_data) {
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
    return CXVisit_Continue;
  }

  CXSourceLocation loc = clang_getCursorLocation(field_cursor);
  CXFile file;
  unsigned line, column, offset;
  clang_getFileLocation(loc, &file, &line, &column, &offset);

  CXString loc_name = clang_getFileName(file);

  struct ast_struct_field *last = ty->oneof.structty.fields;
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
    return CXVisit_Continue;
  }

  clang_disposeString(loc_name);

  CXString spelling = clang_getCursorSpelling(field_cursor);
  const char *spelling_c = clang_getCString(spelling);

  struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
  struct ast_ty *field_ty = parse_type(field_type);
  field->parsed_ty = *field_ty;
  free(field_ty);
  strncpy(field->name, *spelling_c ? spelling_c : "<unknown-spelling>", 256);

  if (!last) {
    ty->oneof.structty.fields = field;
  } else {
    last->next = field;
  }

  ty->oneof.structty.num_fields++;

  clang_disposeString(spelling);

  return CXVisit_Continue;
}

static void collect_struct_fields(CXType type, struct ast_ty *ty) {
  clang_Type_visitFields(type, struct_field_visitor, ty);

  // C has forward-declared structs that don't require fields, as long as they are used as
  // pointers Haven requires at least one field for each struct. So just add an opaque field if
  // there are no other fields in the C type.
  if (!ty->oneof.structty.num_fields) {
    struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
    field->parsed_ty.ty = AST_TYPE_INTEGER;
    field->parsed_ty.oneof.integer.is_signed = 1;
    field->parsed_ty.oneof.integer.width = 8;
    strncpy(field->name, "<opaque>", 256);
    ty->oneof.structty.fields = field;
    ty->oneof.structty.num_fields = 1;
  }
}

static enum CXChildVisitResult enum_field_visitor(CXCursor field_cursor, CXCursor parent_cursor,
                                                  CXClientData client_data) {
  UNUSED(parent_cursor);

  struct ast_ty *ty = (struct ast_ty *)client_data;

  if (clang_getCursorKind(field_cursor) != CXCursor_EnumConstantDecl) {
    return CXChildVisit_Continue;
  }

  struct ast_enum_field *last = ty->oneof.enumty.fields;
  while (last && last->next) {
    last = last->next;
  }

  struct ast_enum_field *field = calloc(1, sizeof(struct ast_enum_field));
  field->value = clang_getEnumConstantDeclUnsignedValue(field_cursor);

  CXString spelling = clang_getCursorSpelling(field_cursor);
  const char *spelling_c = clang_getCString(spelling);

  strncpy(field->name, *spelling_c ? spelling_c : "<unknown-spelling>", 256);

  if (!last) {
    ty->oneof.enumty.fields = field;
  } else {
    last->next = field;
  }

  ty->oneof.enumty.num_fields++;

  clang_disposeString(spelling);

  return CXChildVisit_Continue;
}

static void collect_enum_fields(CXType type, struct ast_ty *ty) {
  CXCursor decl = clang_getTypeDeclaration(type);
  clang_visitChildren(decl, enum_field_visitor, ty);
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
      into->oneof.integer.is_signed = 0;
      into->oneof.integer.width = (size_t)clang_Type_getSizeOf(type) * 8;
      break;

    case CXType_Char_S:
    case CXType_SChar:
    case CXType_Short:
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Int128:
      into->ty = AST_TYPE_INTEGER;
      into->oneof.integer.is_signed = 1;
      into->oneof.integer.width = (size_t)clang_Type_getSizeOf(type) * 8;
      break;

    case CXType_Float:
    case CXType_Double:      // HACK
    case CXType_LongDouble:  // HACK
      into->ty = AST_TYPE_FLOAT;
      break;

    // not technically built in, but doesn't get a name by any other means
    case CXType_FunctionProto: {
      struct ast_fdecl fdecl;
      memset(&fdecl, 0, sizeof(struct ast_fdecl));

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
        into->oneof.pointer.pointee = parse_simple_type_or_custom(pointee);
      }
    } break;

    case CXType_ConstantArray:
      into->ty = AST_TYPE_ARRAY;
      into->oneof.array.width = (size_t)clang_getArraySize(type);
      into->oneof.array.element_ty = parse_simple_type_or_custom(clang_getArrayElementType(type));
      break;

    case CXType_IncompleteArray:
      into->ty = AST_TYPE_POINTER;
      into->oneof.pointer.pointee = parse_simple_type_or_custom(clang_getArrayElementType(type));
      break;

    default:
      rc = -1;
  }

  clang_disposeString(spelling);
  return rc;
}

static struct ast_ty *parse_type(CXType type) {
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
      collect_struct_fields(type, result);

      // is it actually a union?
      CXCursor decl = clang_getTypeDeclaration(type);
      result->oneof.structty.is_union = clang_getCursorKind(decl) == CXCursor_UnionDecl;
    } break;

    case CXType_Enum: {
      result->ty = AST_TYPE_ENUM;
      result->oneof.enumty.no_wrapped_fields = 1;
      collect_enum_fields(type, result);
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
  return parse_type(type);
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
  fdecl->parsed_function_ty.oneof.function.retty = parse_simple_type_or_custom(return_type);

  // Get the number of arguments
  size_t num_args = (size_t)clang_getNumArgTypes(type);
  fdecl->num_params = num_args;
  fdecl->parsed_function_ty.oneof.function.num_params = num_args;
  if (num_args > 0) {
    fdecl->params = calloc(num_args, sizeof(struct ast_fdecl_param_metadata));
    fdecl->parsed_function_ty.oneof.function.param_types =
        calloc(num_args, sizeof(struct ast_ty *));

    // Get each parameter type
    for (size_t i = 0; i < num_args; i++) {
      CXType param_type = clang_getArgType(type, (unsigned int)i);
      CXString param_type_spelling = clang_getTypeSpelling(param_type);
      clang_disposeString(param_type_spelling);

      // TODO: be nice to use the proper param name if it's present in the prototype
      char *param_name = (char *)malloc(256);
      snprintf(param_name, 256, "p%zd", i);

      fdecl->params[i].name = param_name;
      fdecl->parsed_function_ty.oneof.function.param_types[i] =
          parse_simple_type_or_custom(param_type);
    }
  }

  // Check if the function is variadic
  if (clang_isFunctionTypeVariadic(type)) {
    fdecl->flags |= DECL_FLAG_VARARG;
    fdecl->parsed_function_ty.oneof.function.vararg = 1;
  }
}

// Visitor function to handle declarations
static enum CXChildVisitResult libclang_visitor_decls(CXCursor cursor, CXCursor parent,
                                                      CXClientData client_data) {
  UNUSED(parent);
  struct cimport *importer = (struct cimport *)client_data;
  struct ast_program *program = importer->program;

  CXTranslationUnit TU = clang_Cursor_getTranslationUnit(cursor);
  CXCursor TU_cursor = clang_getTranslationUnitCursor(TU);

  CXSourceLocation loc = clang_getCursorLocation(cursor);

  CXFile file;
  unsigned line, column, offset;
  clang_getFileLocation(loc, &file, &line, &column, &offset);

  CXString loc_name = clang_getFileName(file);
  if (clang_getCString(loc_name)) {
    strncpy(program->loc.file, clang_getCString(loc_name), 256);
  }

  // Preprocessor?
  if (clang_getCursorKind(cursor) == CXCursor_PreprocessingDirective) {
    compiler_log(importer->compiler, LogLevelTrace, "cimport", "it's a preprocessing directive");
  }

  // Check if this is a file-scope declaration
  else if (clang_equalCursors(clang_getCursorSemanticParent(cursor), TU_cursor)) {
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
      while (underlying.kind == CXType_Elaborated) {
        underlying = clang_Type_getNamedType(underlying);
      }

      CXString underlying_name = clang_getTypeSpelling(underlying);
      const char *underlying_name_c = clang_getCString(underlying_name);

      if (!cimport_is_known_type(importer, underlying)) {
        // didn't yet see the type the typedef is referring to; emit it first unless it's a builtin
        if ((underlying.kind < CXType_FirstBuiltin || underlying.kind > CXType_LastBuiltin) &&
            underlying.kind != CXType_Pointer && underlying.kind != CXType_FunctionProto &&
            strncmp("__builtin_", underlying_name_c, 10)) {
          compiler_log(
              importer->compiler, LogLevelTrace, "cimport",
              "need to emit underlying type of typedef '%s' (which is '%s'), not known yet",
              spelling_c, underlying_name_c);

          CXCursor underlying_decl = clang_getTypeDeclaration(underlying);
          if (!clang_Cursor_isNull(underlying_decl)) {
            libclang_visitor_decls(underlying_decl, cursor, importer);
          }
        }
      }

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

      CXType type = clang_getCursorType(cursor);
      cimport_add_known_type(importer, type);
    } else if (kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) {
      CXType type = clang_getCursorType(cursor);
      if (!cimport_is_known_type(importer, type)) {
        // make it.
        decl->type = AST_DECL_TYPE_TYDECL;
        decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
        strncpy(decl->toplevel.tydecl.ident.value.identv.ident, spelling_c, 256);
        struct ast_ty *cursor_ty = parse_cursor_type(cursor);
        decl->toplevel.tydecl.parsed_ty = *cursor_ty;
        free(cursor_ty);

        decl->toplevel.tydecl.parsed_ty.oneof.structty.is_union = kind == CXCursor_UnionDecl;

        cimport_add_known_type(importer, type);
      } else {
        free(decl);
        decl = NULL;
      }
    } else if (kind == CXCursor_EnumDecl) {
      CXType type = clang_getCursorType(cursor);
      if (!cimport_is_known_type(importer, type)) {
        decl->type = AST_DECL_TYPE_TYDECL;
        decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
        strncpy(decl->toplevel.tydecl.ident.value.identv.ident, spelling_c, 256);
        struct ast_ty *cursor_ty = parse_cursor_type(cursor);
        decl->toplevel.tydecl.parsed_ty = *cursor_ty;
        free(cursor_ty);

        cimport_add_known_type(importer, type);
      } else {
        free(decl);
        decl = NULL;
      }
    } else {
      compiler_log(importer->compiler, LogLevelTrace, "cimport", "visitor hit unhandled kind %d",
                   kind);
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
    compiler_log(importer->compiler, LogLevelTrace, "cimport",
                 "skipping due to semantic parent not matching TU");
  }

  clang_disposeString(loc_name);

  return CXChildVisit_Continue;
}

static void indexCallback(CXClientData client_data, const CXIdxDeclInfo *decl) {
  struct cimport *importer = (struct cimport *)client_data;

  // TODO: create empty struct/enum types and use CXIdxEntity_EnumConstant/CXIdxEntity_Field to fill
  // them out that saves us from having to use visitors later on and makes the indexing more
  // beneficial

  // functions should be declarations, but in all other cases we only want definitions
  if (decl->isDefinition == 0) {
    switch (decl->entityInfo->kind) {
      case CXIdxEntity_Function:
      case CXIdxEntity_Variable:
        break;
      default:
        compiler_log(importer->compiler, LogLevelTrace, "cimport", "skipping entity kind %d",
                     decl->entityInfo->kind);
        return;
    }
  }

  const char *kind = NULL;
  switch (decl->entityInfo->kind) {
    case CXIdxEntity_Function:
      kind = "function";
      break;
    case CXIdxEntity_Variable:
      kind = "variable";
      break;
    case CXIdxEntity_Typedef:
      kind = "typedef";
      break;
    case CXIdxEntity_EnumConstant:
      kind = "enum constant";
      break;
    case CXIdxEntity_Struct:
      kind = "struct";
      break;
    case CXIdxEntity_Union:
      kind = "union";
      break;
    case CXIdxEntity_Enum:
      kind = "enum";
      break;
    case CXIdxEntity_Field:
      kind = "field";
      break;

    default:
      kind = "unknown";
  }

  compiler_log(importer->compiler, LogLevelTrace, "cimport", "decl: '%s' %s USR='%s' %s",
               decl->entityInfo->name, decl->isDefinition ? "defn" : "decl", decl->entityInfo->USR,
               kind);

  if (!USE_INDEX_INSTEAD_OF_VISITOR) {
    return;
  }

  CXCursor parent = clang_getCursorSemanticParent(decl->cursor);
  libclang_visitor_decls(decl->cursor, parent, importer);
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

struct cimport *cimport_create(struct compiler *compiler) {
  struct cimport *importer = calloc(1, sizeof(struct cimport));
  importer->index = clang_createIndex(0, 1);
  importer->action = clang_IndexAction_create(importer->index);
  importer->compiler = compiler;
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

  struct cimport_known_type *known_types = importer->known_types;
  while (known_types) {
    struct cimport_known_type *ktnext = known_types->next;
    free(known_types);
    known_types = ktnext;
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
      compiler_log(importer->compiler, LogLevelTrace, "cimport", "importing %s", cursor->filename);
      string_builder_appendf(builder, "#include \"%s\"\n", cursor->filename);
      cursor = cursor->next;
    }
  }

  // no content to import
  if (!string_builder_len(builder)) {
    free_string_builder(builder);
    return 0;
  }

  char merged_filename[] = ".haven.merged.XXXXXX.c";
  int fd = mkstemps(merged_filename, 2);
  if (fd < 0) {
    perror("mkstemps");
    return -1;
  }

  FILE *fp = fdopen(fd, "w");
  if (!fp) {
    perror("fopen");
    close(fd);
    return -1;
  }

  compiler_log(importer->compiler, LogLevelTrace, "cimport", "merged import file:\n%s",
               string_builder_get(builder));

  fwrite(string_builder_get(builder), 1, string_builder_len(builder), fp);
  fclose(fp);

  size_t command_line_args_count = 0;
  const char *const *command_line_args =
      compiler_get_cimport_compiler_flags(importer->compiler, &command_line_args_count);

  CXTranslationUnit unit;
  enum CXErrorCode rc = clang_parseTranslationUnit2(
      importer->index, merged_filename, command_line_args, (int)command_line_args_count, NULL, 0,
      CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_DetailedPreprocessingRecord, &unit);

  for (size_t i = 0; i < command_line_args_count; i++) {
    free((void *)command_line_args[i]);
  }
  free((void *)command_line_args);

  unlink(merged_filename);

  if (rc != CXError_Success) {
    // TODO: get the errors, print em
    compiler_log(importer->compiler, LogLevelError, "cimport",
                 "Unable to parse translation unit (error %d)", rc);
    clang_disposeIndex(importer->index);
    importer->index = NULL;
    return -1;
  }

  importer->program = calloc(1, sizeof(struct ast_program));

  if (USE_INDEX_INSTEAD_OF_VISITOR) {
    int result = clang_indexTranslationUnit(importer->action, importer, &cbs, sizeof(cbs),
                                            CXIndexOpt_None, unit);
    if (result < 0) {
      compiler_log(importer->compiler, LogLevelError, "cimport",
                   "Unable to index translation unit (error %d)", result);

      // TODO: clean up any decls that did get created
      importer->program->decls = NULL;
    }
  } else {
    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(cursor, libclang_visitor_decls, importer);
  }

  if (importer->program->decls) {
    into->ast = importer->program;
  } else {
    free(importer->program);
  }

  importer->program = NULL;

  clang_disposeTranslationUnit(unit);

  free_string_builder(builder);
  return 0;
}

static int cimport_is_known_type(struct cimport *importer, CXType type) {
  struct cimport_known_type *cursor = importer->known_types;
  while (cursor) {
    if (clang_equalTypes(cursor->type, type)) {
      return 1;
    }
    cursor = cursor->next;
  }

  return 0;
}

static void cimport_add_known_type(struct cimport *importer, CXType type) {
  struct cimport_known_type *known = calloc(1, sizeof(struct cimport_known_type));
  known->type = type;

  struct cimport_known_type *cursor = importer->known_types;
  while (cursor && cursor->next) {
    cursor = cursor->next;
  }

  if (cursor) {
    cursor->next = known;
  } else {
    importer->known_types = known;
  }
}
