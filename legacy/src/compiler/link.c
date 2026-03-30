
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "compiler.h"
#include "internal.h"

int compiler_link(struct compiler *compiler, const char *object_file) {
  int rc = 0;
  compiler_log(compiler, LogLevelDebug, "driver", "linking %s", object_file);

  const char *ld = compiler->ld ? compiler->ld : "gcc";

  int has_asan = compiler->flags[0] & FLAG_ASAN;

  // build option list for the linker
  size_t num_opts = 4 + (has_asan ? 1 : 0);

  if (compiler->output_format == OutputLinkedLibrary) {
    num_opts += 1;
  }

  const char **ld_options = malloc(sizeof(char *) * num_opts);
  size_t opt = 0;
  ld_options[opt++] = ld;
  ld_options[opt++] = "-o";
  ld_options[opt++] = compiler->output_file;
  ld_options[opt++] = object_file;
  if (has_asan) {
    ld_options[opt++] = "-fsanitize=address";
  }
  if (compiler->output_format == OutputLinkedLibrary) {
    ld_options[opt++] = "-shared";
  }

  fprintf(stderr, "debug: %s %s %s %s", ld, ld_options[1], ld_options[2], ld_options[3]);

  struct linker_option *lo = compiler->linker_options;
  while (lo) {
    num_opts++;
    ld_options = realloc(ld_options, sizeof(char *) * num_opts);
    ld_options[num_opts - 1] = lo->option;
    fprintf(stderr, " %s", lo->option);
    lo = lo->next;
  }

  fprintf(stderr, "\n");

  pid_t pid = fork();
  if (pid == 0) {
    execvp(ld, (char *const *)ld_options);

    compiler_log(compiler, LogLevelError, "driver", "failed to exec %s: %s", ld, strerror(errno));
    exit(1);
  } else if (pid < 0) {
    compiler_log(compiler, LogLevelError, "driver", "failed to fork: %s", strerror(errno));
    rc = 1;
  } else {
    if (waitpid(pid, &rc, 0) < 0) {
      compiler_log(compiler, LogLevelError, "driver", "failed to wait for %s: %s", ld,
                   strerror(errno));
      rc = 1;
    }
  }

  free(ld_options);

  return rc;
}
