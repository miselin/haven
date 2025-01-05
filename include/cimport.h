#ifndef _HAVEN_CIMPORT_H
#define _HAVEN_CIMPORT_H

#ifdef __cplusplus
extern "C" {
#endif

struct cimport;
struct ast_import;

struct cimport *cimport_create(void);
void cimport_destroy(struct cimport *importer);

// Track the given C file as a C import.
int cimport(struct cimport *importer, const char *filename);

// Finalize all C imports, generating AST nodes for them. All previous cimport() calls
// are merged into one set of includes to permit include guards and other optimizations.
// This function is usually called after parsing completes and no more C imports are expected.
int cimport_finalize(struct cimport *importer, struct ast_import *into);

#ifdef __cplusplus
}
#endif

#endif
