#ifndef QEMU_PATH_H
#define QEMU_PATH_H

void init_paths(const char *prefix);
char *relocate_path_at(int dirfd, const char *name, char *out, bool follow_symlink);
char *restore_path(const char* name, char* out);
char* resolve_with_path_env(const char* path_env, const char* name, char* out);
char* resolve_abs_with_cwd(const char* path, char* out);

#endif
