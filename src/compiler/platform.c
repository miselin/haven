#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "compiler.h"
#include "internal.h"

static int is_dir(const char *path) {
  if (!path || !path[0]) {
    return 0;
  }

  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void add_search_dir_if_dir(struct compiler *compiler, const char *path) {
  if (is_dir(path)) {
    add_search_dir(compiler, path);
  }
}

static char *join_path(const char *base, const char *suffix) {
  size_t base_len = strlen(base);
  size_t suffix_len = strlen(suffix);
  size_t needs_slash = (base_len > 0 && base[base_len - 1] != '/') ? 1u : 0u;

  char *result = calloc(1, base_len + needs_slash + suffix_len + 1);
  memcpy(result, base, base_len);
  if (needs_slash) {
    result[base_len] = '/';
  }
  memcpy(result + base_len + needs_slash, suffix, suffix_len);
  return result;
}

static void maybe_set_default_sysroot(struct compiler *compiler, const char *path) {
  if (compiler->sysroot || !is_dir(path)) {
    return;
  }

  compiler->sysroot = strdup(path);
  compiler_log(compiler, LogLevelDebug, "platform", "default sysroot: %s", compiler->sysroot);
}

static char *next_token(const char **cursor) {
  while (**cursor && isspace((unsigned char)**cursor)) {
    (*cursor)++;
  }

  if (!**cursor) {
    return NULL;
  }

  const char *start = *cursor;
  while (**cursor && !isspace((unsigned char)**cursor)) {
    (*cursor)++;
  }

  size_t len = (size_t)(*cursor - start);
  char *result = calloc(1, len + 1);
  memcpy(result, start, len);
  return result;
}

static void add_resource_dir_include(struct compiler *compiler, const char *resource_dir) {
  if (!resource_dir || !resource_dir[0]) {
    return;
  }

  char *include_dir = join_path(resource_dir, "include");
  add_search_dir_if_dir(compiler, include_dir);
  free(include_dir);
}

static void apply_env_cflags(struct compiler *compiler, const char *flags) {
  if (!flags || !flags[0]) {
    return;
  }

  const char *cursor = flags;
  char *token = NULL;
  while ((token = next_token(&cursor))) {
    if ((strcmp(token, "-I") == 0) || (strcmp(token, "-isystem") == 0) ||
        (strcmp(token, "-idirafter") == 0)) {
      free(token);
      token = next_token(&cursor);
      if (!token) {
        break;
      }

      add_search_dir_if_dir(compiler, token);
      free(token);
      continue;
    }

    if (strcmp(token, "-isysroot") == 0) {
      free(token);
      token = next_token(&cursor);
      if (!token) {
        break;
      }

      maybe_set_default_sysroot(compiler, token);
      free(token);
      continue;
    }

    if (strcmp(token, "-resource-dir") == 0) {
      free(token);
      token = next_token(&cursor);
      if (!token) {
        break;
      }

      add_resource_dir_include(compiler, token);
      free(token);
      continue;
    }

    if ((strncmp(token, "-I", 2) == 0) && token[2]) {
      add_search_dir_if_dir(compiler, token + 2);
    } else if ((strncmp(token, "-isysroot", 9) == 0) && token[9]) {
      maybe_set_default_sysroot(compiler, token + 9);
    } else if ((strncmp(token, "-resource-dir=", 14) == 0) && token[14]) {
      add_resource_dir_include(compiler, token + 14);
    }

    free(token);
  }
}

static char *capture_first_line(const char *program, char *const argv[]) {
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    return NULL;
  }

  pid_t pid = fork();
  if (pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }

    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);

    execvp(program, argv);
    _exit(1);
  }

  close(pipefd[1]);

  if (pid < 0) {
    close(pipefd[0]);
    return NULL;
  }

  char buffer[4096];
  ssize_t len = read(pipefd[0], buffer, sizeof(buffer) - 1);
  close(pipefd[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
      len <= 0) {
    return NULL;
  }

  buffer[len] = '\0';
  while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
    buffer[--len] = '\0';
  }

  return len > 0 ? strdup(buffer) : NULL;
}

static char *discover_clang_resource_dir(void) {
  char *const argv[] = {"clang", "-print-resource-dir", NULL};
  return capture_first_line("clang", argv);
}

#ifdef __APPLE__
static char *discover_darwin_sysroot(void) {
  char *const argv[] = {"xcrun", "--show-sdk-path", NULL};
  return capture_first_line("xcrun", argv);
}
#endif

void compiler_apply_platform_defaults(struct compiler *compiler) {
  apply_env_cflags(compiler, getenv("NIX_CFLAGS_COMPILE"));

#ifdef __APPLE__
  maybe_set_default_sysroot(compiler, getenv("SDKROOT"));

  if (!compiler->sysroot) {
    char *sysroot = discover_darwin_sysroot();
    maybe_set_default_sysroot(compiler, sysroot);
    free(sysroot);
  }

  if (compiler->sysroot) {
    char *usr_include = join_path(compiler->sysroot, "usr/include");
    add_search_dir_if_dir(compiler, usr_include);
    free(usr_include);
  }
#else
  add_search_dir_if_dir(compiler, "/usr/include");
#endif

  char *resource_dir = discover_clang_resource_dir();
  add_resource_dir_include(compiler, resource_dir);
  free(resource_dir);
}
