#include "execve.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <linux/limits.h>

char qemu_abs_path[PATH_MAX];

const char **argv_prefix;

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
}

const char *get_qemu_abs_path(void) {
    return qemu_abs_path;
}

const char **get_qemu_argv_prefix(void) {
    return argv_prefix;
}
