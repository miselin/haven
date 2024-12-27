#ifndef _HAVEN_UTILITY_H
#define _HAVEN_UTILITY_H

// ensures alignment to the given number of bytes
#define ALIGNED(x) alignas((x))

// byte offset of the given member in the struct
#define OFFSETOF(x, y) offsetof(x, y)

// the input parameter is not used
#define UNUSED(x) (void)x

#ifdef __APPLE__
#define WEAK __attribute__((weak_import))
#else
#define WEAK __attribute__((weak))
#endif

#endif
