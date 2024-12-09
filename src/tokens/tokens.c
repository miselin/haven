#include "tokens.h"

const char *token_id_to_string(enum token_id token) {
  switch (token) {
    case TOKEN_UNKNOWN:
      return "<unknown>";
    case TOKEN_IDENTIFIER:
      return "<identifier>";
    case TOKEN_INTEGER:
      return "<integer>>";
    case TOKEN_FLOAT:
      return "<float>";
    case TOKEN_STRING:
      return "<string>";
    case TOKEN_CHAR:
      return "'[^']'";
    case TOKEN_PLUS:
      return "+";
    case TOKEN_MINUS:
      return "-";
    case TOKEN_ASTERISK:
      return "*";
    case TOKEN_FSLASH:
      return "/";
    case TOKEN_PERCENT:
      return "%";
    case TOKEN_INCREMENT:
      return "++";
    case TOKEN_DECREMENT:
      return "--";
    case TOKEN_ASSIGN:
      return "=";
    case TOKEN_LPAREN:
      return "(";
    case TOKEN_RPAREN:
      return ")";
    case TOKEN_LBRACE:
      return "{";
    case TOKEN_RBRACE:
      return "}";
    case TOKEN_SEMI:
      return ";";
    case TOKEN_EQUALS:
      return "==";
    case TOKEN_OR:
      return "||";
    case TOKEN_AND:
      return "&&";
    case TOKEN_NOT:
      return "!";
    case TOKEN_LT:
      return "<";
    case TOKEN_LTE:
      return "<=";
    case TOKEN_GT:
      return ">";
    case TOKEN_GTE:
      return ">=";
    case TOKEN_NE:
      return "!=";
    case TOKEN_BITOR:
      return "|";
    case TOKEN_BITAND:
      return "&";
    case TOKEN_BITXOR:
      return "^";
    case TOKEN_COMMA:
      return ",";
    case TOKEN_QUOTE:
      return "\"";
    case TOKEN_APOSTROPHE:
      return "'";
    case TOKEN_COLON:
      return ":";
    case TOKEN_PERIOD:
      return ".";
    case TOKEN_INTO:
      return "=>";
    case TOKEN_LBRACKET:
      return "[";
    case TOKEN_RBRACKET:
      return "]";
    case TOKEN_TILDE:
      return "~";
    case TOKEN_DOTDOT:
      return "..";
    case TOKEN_LSHIFT:
      return "<<";
    case TOKEN_RSHIFT:
      return ">>";
    case TOKEN_POUND:
      return "#";
    case TOKEN_KW_PUB:
      return "pub";
    case TOKEN_KW_FN:
      return "fn";
    case TOKEN_KW_MUT:
      return "mut";
    case TOKEN_KW_IF:
      return "if";
    case TOKEN_KW_ELSE:
      return "else";
    case TOKEN_KW_LET:
      return "let";
    case TOKEN_KW_FOR:
      return "for";
    case TOKEN_KW_WHILE:
      return "while";
    case TOKEN_KW_BREAK:
      return "break";
    case TOKEN_KW_CONTINUE:
      return "continue";
    case TOKEN_KW_MATCH:
      return "match";
    case TOKEN_KW_AS:
      return "as";
    case TOKEN_KW_NEG:
      return "neg";
    case TOKEN_KW_ITER:
      return "iter";
    case TOKEN_KW_REF:
      return "ref";
    case TOKEN_KW_STORE:
      return "store";
    case TOKEN_KW_LOAD:
      return "load";
    case TOKEN_KW_RETURN:
      return "ret";
    case TOKEN_TY_SIGNED:
      return "i[0-9]+";
    case TOKEN_TY_UNSIGNED:
      return "u[0-9]+";
    case TOKEN_TY_FLOAT:
      return "float";
    case TOKEN_TY_FVEC:
      return "fvec[0-9]+";
    case TOKEN_TY_STR:
      return "str";
    case TOKEN_TY_CHAR:
      return "char";
    case TOKEN_TY_VOID:
      return "void";
    case TOKEN_EOF:
      return "<EOF>";
    default:
      return "<str-not-implemented>";
  }
}