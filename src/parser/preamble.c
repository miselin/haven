#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "parse.h"

int parser_add_preamble(struct parser *parser) {
  // we'll insert all of these at the start of the AST
  struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
  struct ast_toplevel *preamble = decl;

  // __va_list_tag type
  // TODO: platform specific
  decl->type = AST_DECL_TYPE_TYDECL;
  lexer_locate(parser->lexer, &decl->loc);
  decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
  strncpy(decl->toplevel.tydecl.ident.value.identv.ident, "__va_list_tag", 256);
  decl->toplevel.tydecl.parsed_ty.ty = AST_TYPE_STRUCT;
  strncpy(decl->toplevel.tydecl.parsed_ty.name, "__va_list_tag", 256);
  decl->toplevel.tydecl.parsed_ty.oneof.structty.fields =
      calloc(1, sizeof(struct ast_struct_field));
  decl->toplevel.tydecl.parsed_ty.oneof.structty.fields->parsed_ty.ty = AST_TYPE_INTEGER;
  decl->toplevel.tydecl.parsed_ty.oneof.structty.fields->parsed_ty.oneof.integer.is_signed = 1;
  decl->toplevel.tydecl.parsed_ty.oneof.structty.fields->parsed_ty.oneof.integer.width = 64;
  strncpy(decl->toplevel.tydecl.parsed_ty.oneof.structty.fields->name, "gp_offset", 256);
  decl->toplevel.tydecl.parsed_ty.oneof.structty.num_fields = 1;

  // __builtin_va_list is an array of one __va_list_tag
  decl->next = calloc(1, sizeof(struct ast_toplevel));
  decl = decl->next;
  decl->type = AST_DECL_TYPE_TYDECL;
  lexer_locate(parser->lexer, &decl->loc);
  decl->toplevel.tydecl.ident.ident = TOKEN_IDENTIFIER;
  strncpy(decl->toplevel.tydecl.ident.value.identv.ident, "__builtin_va_list", 256);
  decl->toplevel.tydecl.parsed_ty.ty = AST_TYPE_ARRAY;
  decl->toplevel.tydecl.parsed_ty.oneof.array.width = 1;
  decl->toplevel.tydecl.parsed_ty.oneof.array.element_ty = calloc(1, sizeof(struct ast_ty));
  decl->toplevel.tydecl.parsed_ty.oneof.array.element_ty->ty = AST_TYPE_CUSTOM;
  strncpy(decl->toplevel.tydecl.parsed_ty.oneof.array.element_ty->name, "__va_list_tag", 256);

  decl->next = parser->ast.decls;
  parser->ast.decls = preamble;

  return 0;
}
