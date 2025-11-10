#ifndef _CIMPORT_H
#define _CIMPORT_H

inline int test_defn(int a, int b) {
  // should not be imported
  typedef unsigned char uchar;
  return a + b;
}

int func_decl(int x);

typedef unsigned int uint;

uint with_type_alias(uint x);

struct Point {
  int x;
  int y;
};

typedef struct {
  int x;
  int y;
} AnonPoint;

enum Color { RED, GREEN, BLUE };

#endif
