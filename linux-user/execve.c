#include "execve.h"
#include "qemu/path.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h>

static char qemu_abs_path[PATH_MAX];

static const char** argv_prefix;
static int n_argv_prefix;

void setup_for_execve(const char** argv, int optind)
{
    ssize_t ret = readlink("/proc/self/exe", qemu_abs_path, PATH_MAX - 1);
    assert(ret > 0);
    qemu_abs_path[ret] = '\0';

    argv_prefix = calloc(optind + 1, sizeof(char*));
    argv_prefix[0] = qemu_abs_path;
    for (int i = 1; i < optind; i++) {
        argv_prefix[i] = argv[i];
    }
    argv_prefix[optind] = NULL;
    n_argv_prefix = optind;

    char** ld_prefix = argv_prefix;
    while (*ld_prefix != NULL && strcmp(*ld_prefix, "-L") != 0) {
        ld_prefix++;
    }
    if (*ld_prefix != NULL && ld_prefix[1] != NULL) {
        char* abs_ld_prefix = calloc(PATH_MAX, sizeof(char));
        ld_prefix[1] = resolve_abs_with_cwd(ld_prefix[1], abs_ld_prefix);
    }
}

const char* get_qemu_abs_path(void)
{
    return qemu_abs_path;
}

const char** get_qemu_argv_prefix(void)
{
    return argv_prefix;
}

const char* find_path_env_value(const char** envp)
{
    const char** pathenv = envp;
    while (*pathenv != NULL && strstr(*pathenv, "PATH=") != *pathenv) {
        pathenv += 1;
    }
    if (*pathenv != NULL) {
        return *pathenv + 5;
    }
    else {
        return NULL;
    }
}

const char* resolve_program_path(int dirfd, const char* p, const char** envp, char* out, int flags)
{
    //	TODO: support AT_EMPTY_PATH in flags

    if (p[0] != '/' && p[0] != '.') {
        const char* path_value = find_path_env_value(envp);
        if (path_value != NULL) {
            char prog[PATH_MAX] = {0};
            if (resolve_with_path_env(path_value, dirfd, p, prog)) {
                return relocate_path_at(dirfd, prog, out, !(flags & AT_SYMLINK_NOFOLLOW));
            }
        }
    }

    return relocate_path_at(dirfd, p, out, !(flags & AT_SYMLINK_NOFOLLOW));
}

int size_of_vp(const char** vp)
{
    int c = 0;
    while (vp[c] != NULL) { c++; };
    return c;
}

bool is_elf(const char* buff)
{
    return buff[0] == 0x7f && buff[1] == 0x45 && buff[2] == 0x4c && buff[3] == 0x46;
}

bool is_shebang(const char* buff)
{
    return buff[0] == '#' && buff[1] == '!';
}

/**
 * copied and modified from proot
 */
int extract_shebang(const char* host_path,
                    char user_path[PATH_MAX], char argument[BINPRM_BUF_SIZE])
{
    char tmp2[2];
    char tmp;

    size_t current_length;
    size_t i;

    int status;
    int fd;

    /* Assumption.  */
    assert(BINPRM_BUF_SIZE < PATH_MAX);

    argument[0] = '\0';

    /* Inspect the executable.  */
    fd = open(host_path, O_RDONLY);
    if (fd < 0)
        return -errno;

    status = read(fd, tmp2, 2 * sizeof(char));
    if (status < 0) {
        status = -errno;
        goto end;
    }
    if ((size_t)status < 2 * sizeof(char)) {
        /* EOF */
        status = 0;
        goto end;
    }

    /* Check if it really is a script text. */
    if (tmp2[0] != '#' || tmp2[1] != '!') {
        status = 0;
        goto end;
    }
    current_length = 2;
    user_path[0] = '\0';

    /* Skip leading spaces. */
    do {
        status = read(fd, &tmp, sizeof(char));
        if (status < 0) {
            status = -errno;
            goto end;
        }
        if ((size_t)status < sizeof(char)) {
            /* EOF */
            status = -ENOEXEC;
            goto end;
        }

        current_length++;
    }
    while ((tmp == ' ' || tmp == '\t') && current_length < BINPRM_BUF_SIZE);

    /* Slurp the interpreter path until the first space or end-of-line. */
    for (i = 0; current_length < BINPRM_BUF_SIZE; current_length++, i++) {
        switch (tmp) {
        case ' ':
        case '\t':
            /* Remove spaces in between the interpreter
             * and the hypothetical argument. */
            user_path[i] = '\0';
            break;

        case '\n':
        case '\r':
            /* There is no argument. */
            user_path[i] = '\0';
            argument[0] = '\0';
            status = 1;
            goto end;

        default:
            /* There is an argument if the previous
             * character in user_path[] is '\0'. */
            if (i > 1 && user_path[i - 1] == '\0')
                goto argument;
            else
                user_path[i] = tmp;
            break;
        }

        status = read(fd, &tmp, sizeof(char));
        if (status < 0) {
            status = -errno;
            goto end;
        }
        if ((size_t)status < sizeof(char)) {
            /* EOF */
            user_path[i] = '\0';
            argument[0] = '\0';
            status = 1;
            goto end;
        }
    }

    /* The interpreter path is too long, truncate it. */
    user_path[i] = '\0';
    argument[0] = '\0';
    status = 1;
    goto end;

argument:

    /* Slurp the argument until the end-of-line. */
    for (i = 0; current_length < BINPRM_BUF_SIZE; current_length++, i++) {
        switch (tmp) {
        case '\n':
        case '\r':
            argument[i] = '\0';

            /* Remove trailing spaces. */
            for (i--; i > 0 && (argument[i] == ' ' || argument[i] == '\t'); i--)
                argument[i] = '\0';

            status = 1;
            goto end;

        default:
            argument[i] = tmp;
            break;
        }

        status = read(fd, &tmp, sizeof(char));
        if (status < 0) {
            status = -errno;
            goto end;
        }
        if ((size_t)status < sizeof(char)) {
            /* EOF */
            argument[0] = '\0';
            status = 1;
            goto end;
        }
    }

    /* The argument is too long, truncate it. */
    argument[i] = '\0';
    status = 1;

end:
    close(fd);

    /* Did an error occur or isn't a script? */
    if (status <= 0)
        return status;

    return 1;
}
