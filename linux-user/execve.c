#include "execve.h"
#include "qemu/path.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <linux/limits.h>

static char qemu_abs_path[PATH_MAX];

static const char **argv_prefix;
static int n_argv_prefix;

void setup_for_execve(const char **argv, int optind) {
    ssize_t ret = readlink("/proc/self/exe", qemu_abs_path, PATH_MAX - 1);
    assert(ret > 0);
    qemu_abs_path[ret] = '\0';

    argv_prefix = calloc(optind + 1, sizeof(char *));
    argv_prefix[0] = qemu_abs_path;
    for (int i = 1; i < optind; i++) {
        argv_prefix[i] = argv[i];
    }
    argv_prefix[optind] = NULL;
    n_argv_prefix = optind;

    char** ld_prefix = argv_prefix;
    while (*ld_prefix != NULL && strcmp(*ld_prefix, "-L") != 0) {
        ld_prefix ++;
    }
    if (*ld_prefix != NULL && ld_prefix[1] != NULL) {
        char* abs_ld_prefix = calloc(PATH_MAX, sizeof(char));
        ld_prefix[1] = resolve_abs_with_cwd(ld_prefix[1], abs_ld_prefix);
    }
}

const char *get_qemu_abs_path(void) {
    return qemu_abs_path;
}

const char **get_qemu_argv_prefix(int *pn_argv_prefix) {
    if (pn_argv_prefix != NULL) {
        *pn_argv_prefix = n_argv_prefix;
    }
    return argv_prefix;
}

const char *find_path_env_value(const char **envp) {
    const char **pathenv = envp;
    while (*pathenv != NULL && strstr(*pathenv, "PATH=") != *pathenv) {
        pathenv += 1;
    }
    if (*pathenv != NULL) {
        return *pathenv + 5;
    } else {
        return NULL;
    }
}
