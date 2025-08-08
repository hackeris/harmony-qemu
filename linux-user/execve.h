#ifndef _EXECVE_H
#define _EXECVE_H
#include <stdbool.h>
#include <linux/limits.h>

#define BINPRM_BUF_SIZE 256

void setup_for_execve(const char** argv, int optind);

const char* get_qemu_abs_path(void);
const char** get_qemu_argv_prefix(void);

//  find value of PATH env variable in envp
const char *find_path_env_value(const char **envp);

const char *resolve_program_path(int dirfd, const char *p, const char **envp, char* out, int flags);

int size_of_vp(const char** vp);

bool is_elf(const char *buff);

bool is_shebang(const char *buff);

int extract_shebang(const char *host_path,
        char user_path[PATH_MAX], char argument[BINPRM_BUF_SIZE]);

#endif
