#ifndef _EXECVE_H
#define _EXECVE_H

void setup_for_execve(const char** argv, int optind);

const char* get_qemu_abs_path(void);
const char** get_qemu_argv_prefix(int* n_prefix);

//  find value of PATH env variable in envp
const char *find_path_env_value(const char **envp);

#endif
