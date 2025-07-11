#ifndef QEMU_PATH_H
#define QEMU_PATH_H

void init_paths(const char *prefix);
const char *path(const char *pathname);
const char *pathat(int dirfd, const char *pathname);
char* resolve_with_path_env(const char* path_env, const char* name, char* out);
char* resolve_abs_with_cwd(const char* path, char* out);

#endif
