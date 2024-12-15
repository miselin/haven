#ifndef _HAVEN_TOKENS_H
#define _HAVEN_TOKENS_H

enum token_id {
  TOKEN_UNKNOWN = 0,
  TOKEN_IDENTIFIER,   // [a-zA-Z][a-zA-Z0-9_]*
  TOKEN_INTEGER,      // [-]?[0-9]+
  TOKEN_FLOAT,        // you don't want to know
  TOKEN_STRING,       // "[^"]*"
  TOKEN_CHAR,         // '[^']'
  TOKEN_PLUS,         // +
  TOKEN_MINUS,        // -
  TOKEN_ASTERISK,     // *
  TOKEN_FSLASH,       // /
  TOKEN_PERCENT,      // %
  TOKEN_INCREMENT,    // ++
  TOKEN_DECREMENT,    // --
  TOKEN_ASSIGN,       // =
  TOKEN_LPAREN,       // (
  TOKEN_RPAREN,       // )
  TOKEN_LBRACE,       // {
  TOKEN_RBRACE,       // }
  TOKEN_SEMI,         // ;
  TOKEN_EQUALS,       // ==
  TOKEN_OR,           // ||
  TOKEN_AND,          // &&
  TOKEN_NOT,          // !
  TOKEN_LT,           // <
  TOKEN_LTE,          // <=
  TOKEN_GT,           // >
  TOKEN_GTE,          // >=
  TOKEN_NE,           // !=
  TOKEN_BITOR,        // |
  TOKEN_BITAND,       // &
  TOKEN_BITXOR,       // ^
  TOKEN_COMMA,        // ,
  TOKEN_QUOTE,        // "
  TOKEN_APOSTROPHE,   // '
  TOKEN_COLON,        // :
  TOKEN_PERIOD,       // .
  TOKEN_INTO,         // =>
  TOKEN_LBRACKET,     // [
  TOKEN_RBRACKET,     // ]
  TOKEN_TILDE,        // ~
  TOKEN_DOTDOT,       // ..
  TOKEN_LSHIFT,       // <<
  TOKEN_RSHIFT,       // >>
  TOKEN_POUND,        // #
  TOKEN_UNDER,        // _
  TOKEN_COLONCOLON,   // ::
  TOKEN_COMMENTLONG,  // /* ... */
  TOKEN_COMMENTLINE,  // //

  // keywords
  TOKEN_KW_PUB = 100,  // pub
  TOKEN_KW_FN,         // fn
  TOKEN_KW_MUT,        // mut
  TOKEN_KW_IF,         // if
  TOKEN_KW_ELSE,       // else
  TOKEN_KW_LET,        // let
  TOKEN_KW_FOR,        // for
  TOKEN_KW_WHILE,      // while
  TOKEN_KW_BREAK,      // break
  TOKEN_KW_CONTINUE,   // continue
  TOKEN_KW_MATCH,      // match
  TOKEN_KW_AS,         // as
  TOKEN_KW_NEG,        // neg
  TOKEN_KW_ITER,       // iter
  TOKEN_KW_REF,        // ref
  TOKEN_KW_STORE,      // store
  TOKEN_KW_LOAD,       // load
  TOKEN_KW_RETURN,     // ret
  TOKEN_KW_STRUCT,     // struct
  TOKEN_KW_TYPE,       // type
  TOKEN_KW_NIL,        // nil
  TOKEN_KW_DEFER,      // defer
  TOKEN_KW_IMPURE,     // impure
  TOKEN_KW_ENUM,       // enum

  // types
  TOKEN_TY_SIGNED = 200,  // i[0-9]+
  TOKEN_TY_UNSIGNED,      // u[0-9]+
  TOKEN_TY_FLOAT,         // float
  TOKEN_TY_FVEC,          // fvec[0-9]+
  TOKEN_TY_STR,           // str
  TOKEN_TY_CHAR,          // char
  TOKEN_TY_VOID,          // void

  TOKEN_EOF,
};

const char *token_id_to_string(enum token_id);

#endif
