#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ast.h"
#include "compiler.h"
#include "kv.h"

// Returns a path to a temp file that should be unlinked after use
static const char *preprocess(const char *header);

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "not enough parameters provided\n");
    return 1;
  }

  const char *header = argv[1];

  const char *preprocessed = preprocess(header);
  if (!preprocessed) {
    return 1;
  }

  char merged_filename[] = ".hdr2haven.XXXXXX.hv";
  int fd = mkstemps(merged_filename, 3);
  if (fd < 0) {
    perror("mkstemps");
    return 1;
  }

  FILE *fp = fdopen(fd, "w");
  if (!fp) {
    perror("fopen");
    close(fd);
    return 1;
  }

  struct kv *already_defined = new_kv();

  // Pull in simple #define before the rest of cimport (which ignores them)
  FILE *src = fopen(preprocessed, "r");
  char *linebuf = NULL;
  size_t count = 0;
  while (!feof(src)) {
    if (getline(&linebuf, &count, src) < 0) {
      if (feof(src)) {
        break;
      }
      perror("getline");
      break;
    }

    if (!count) {
      continue;
    }

    char name[256];
    char buf[256];

    size_t u64 = 0;
    float f = 0.0f;
    if (sscanf(linebuf, "#define %[a-zA-Z_0-9] %zd", name, &u64) == 2) {
      if (kv_lookup(already_defined, name) == NULL) {
        kv_insert(already_defined, name, (void *)1);
        fprintf(fp, "pub data i64 %s = %zd;\n", name, u64);
      }
    } else if (sscanf(linebuf, "#define %[a-zA-Z_0-9] %f", name, &f) == 2) {
      if (kv_lookup(already_defined, name) == NULL) {
        kv_insert(already_defined, name, (void *)1);
        fprintf(fp, "pub data float %s = %f;\n", name, f);
      }
    } else if (sscanf(linebuf, "#define %[a-zA-Z_0-9] \"%[^\"]\"", name, buf) == 2) {
      if (kv_lookup(already_defined, name) == NULL) {
        kv_insert(already_defined, name, (void *)1);
        fprintf(fp, "pub data str %s = \"%s\";\n", name, buf);
      }
    }
  }
  free(linebuf);
  fclose(src);

  destroy_kv(already_defined);

  fprintf(fp, "cimport \"%s\";\n", preprocessed);
  fflush(fp);
  fclose(fp);

  argv[1] = merged_filename;

  struct compiler *compiler = new_compiler(argc, (const char **)argv);
  if (!compiler) {
    return 1;
  }

  compiler_set_input_file(compiler, merged_filename);

  int rc = compiler_run(compiler, PassTypecheck);
  if (rc == 0) {
    emit_ast_as_code_expanded(compiler_get_ast(compiler), stdout);
  }

  destroy_compiler(compiler);

  // unlink(merged_filename);
  // unlink(preprocessed);
  return rc;
}

static const char *preprocess(const char *header) {
  static char tmpfile[] = ".hdr2haven.XXXXXX.c";
  int fd = mkstemps(tmpfile, 2);
  if (fd < 0) {
    perror("mkstemps");
    return NULL;
  }

  int writepipe[2];
  if (pipe(writepipe) < 0) {
    perror("pipe");
    return NULL;
  }

  pid_t pid = fork();
  if (pid == 0) {
    close(writepipe[1]);
    dup2(writepipe[0], STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    execlp("clang", "clang", "-E", "-dD", "-P", "-", NULL);

    fprintf(stderr, "failed to exec clang: %s\n", strerror(errno));
    exit(1);
  } else if (pid < 0) {
    perror("fork");
    return NULL;
  }

  close(writepipe[0]);

  char buf[256];
  int sz = snprintf(buf, 256, "#include <%s>\n", header);
  if (write(writepipe[1], buf, (size_t)sz) != sz) {
    perror("write");
    return NULL;
  }

  close(writepipe[1]);

  int rc = 0;
  if (waitpid(pid, &rc, 0) < 0) {
    fprintf(stderr, "failed to wait for clang: %s\n", strerror(errno));
    return NULL;
  }

  if (rc != 0) {
    fprintf(stderr, "clang exited with non-zero status code\n");
    return NULL;
  }

  return tmpfile;
}
