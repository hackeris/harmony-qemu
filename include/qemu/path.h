#ifndef QEMU_PATH_H
#define QEMU_PATH_H

void init_paths(const char *prefix);
const char *path(const char *pathname);
char* resolve_path(const char* path_env, const char* name, char* out);

#endif
