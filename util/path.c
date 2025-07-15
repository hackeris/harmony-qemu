/* Code to mangle pathnames into those matching a given prefix.
   eg. open("/lib/foo.so") => open("/usr/gnemul/i386-linux/lib/foo.so");

   The assumption is that this area does not change.
*/
#include "qemu/osdep.h"
#include <sys/param.h>
#include <dirent.h>
#include "qemu/cutils.h"
#include "qemu/path.h"
#include "qemu/thread.h"

static const char *base;
static GHashTable *hash;
static QemuMutex strtok_lock;

void init_paths(const char *prefix)
{
    if (prefix[0] == '\0' || !strcmp(prefix, "/")) {
        return;
    }

    char* tmp_base;
    if (prefix[0] == '/') {
        tmp_base = g_strdup(prefix);
    } else {
        char *cwd = g_get_current_dir();
        tmp_base = g_build_filename(cwd, prefix, NULL);
        g_free(cwd);
    }
    char real[PATH_MAX];
    realpath(tmp_base, real);
    g_free(tmp_base);
    base = g_strdup(real);

    hash = g_hash_table_new(g_str_hash, g_str_equal);
    qemu_mutex_init(&strtok_lock);
}

static bool skip_relocation(const char *name) {
    return strstr(name, "/proc/") == name
           || strcmp(name, "/etc/resolv.conf") == 0
           || strcmp(name, "/proc") == 0;
}

const char *do_relocate_path(const char *name, bool keep_relative_path, char* out)
{
    if (!base || !name) {
        //  invalid
        goto use_original;
    }
    if (keep_relative_path && name[0] != '/') {
        //  keep relative
        goto use_original;
    }
    if (skip_relocation(name)) {
        //  reuse hosts
        goto use_original;
    }

    char abspath[PATH_MAX];
    if (name[0] != '/') {
        //  relative to absolute
        getcwd(abspath, sizeof(abspath));
        strcat(abspath, "/");
        strcat(abspath, name);
    } else {
        //  absolute
        strcpy(abspath, name);
    }
    if (strstr(abspath, base) == &abspath[0]) {
        //  already at rootfs
        goto use_original;
    }

    strcpy(out, base);
    strcat(out, "/");
    strcat(out, abspath);
    return out;

use_original:
    strcpy(out, name);
    return out;
}

static bool convert_to_abs_path(int dirfd, const char *path, char* out) {

    if (path[0] == '/') {
        strcpy(out, path);
        return true;
    }
    if (dirfd == AT_FDCWD) {
        do_relocate_path(path, false, out);
        return true;
    }

    char dir_path[PATH_MAX] = {0};
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "/proc/self/fd/%d", dirfd);
    ssize_t len = readlink(tmp, dir_path, sizeof(dir_path) - 1);
    if (len <= 0) {
        return false;
    }
    dir_path[len] = '\0';

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, path);
    do_relocate_path(full_path, false, out);
    return true;
}

static bool is_link(const char* hostpath) {

    struct stat s;
    if (lstat(hostpath, &s) < 0) {
        return false;
    }

    return S_ISLNK(s.st_mode);
}

static const char *get_linkat(int dirfd, const char *name, char* out) {

    if (skip_relocation(name)) {
        return strcpy(out, name);
    }

    char abspath[PATH_MAX];

    if (!convert_to_abs_path(dirfd, name, abspath)) {
        return strcpy(out, name);
    }

    while (is_link(abspath)) {

        char tmp[PATH_MAX];
        ssize_t l = readlink(abspath, tmp, sizeof(tmp));
        if (l < 0) {
            return do_relocate_path(abspath, false, out);
        }

        tmp[l] = '\0';
        if (tmp[0] == '/') {
            strcpy(abspath, tmp);
        } else {
            size_t i = strlen(abspath);
            while (abspath[--i] != '/') {}
            abspath[i + 1] = '\0';

            strcat(abspath, "/");
            strcat(abspath, tmp);

            char real[PATH_MAX];
            realpath(abspath, real);
            strcpy(abspath, real);
        }

        do_relocate_path(abspath, false, tmp);
        strcpy(abspath, tmp);
    }

    return strcpy(out, abspath);
}

char *relocate_path_at(int dirfd, const char *name, char *out, bool follow_symlink) {

    const int keep_relative_path = dirfd == AT_FDCWD ? false : true;

    char tmp[PATH_MAX];
    do_relocate_path(name, keep_relative_path, tmp);

    if (follow_symlink) {
        get_linkat(dirfd, tmp, out);
    } else {
        strcpy(out, tmp);
    }

    return out;
}

char *resolve_with_path_env(const char *path_env, const char *name, char *out) {

    if (!path_env || !name || !out) return NULL;

    char *path_copy = strdup(path_env);
    if (!path_copy) return NULL;

    if (name[0] == '/') {
        char reloc[PATH_MAX];
        const int r = access(relocate_path_at(AT_FDCWD, name, reloc, true), F_OK);
        if (r == 0) {
            strncpy(out, name, PATH_MAX);
            return out;
        }
    }

    char *ret = NULL;

    qemu_mutex_lock(&strtok_lock);

    char *dir = strtok(path_copy, ":");
    char full_path[PATH_MAX];

    while (dir) {

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);

        char reloc[PATH_MAX];
        const int r = access(relocate_path_at(AT_FDCWD, full_path, reloc, true), F_OK);
        if (r == 0) {
            strncpy(out, full_path, PATH_MAX);
            ret = out;
            break;
        }

        dir = strtok(NULL, ":");
    }

    qemu_mutex_unlock(&strtok_lock);

    free(path_copy);
    return ret;
}

char* resolve_abs_with_cwd(const char* path, char* out) {
    if (path[0] != '/') {
        char *cwd = g_get_current_dir();
        char *abs_base = g_build_filename(cwd, path, NULL);
        strcpy(out, abs_base);
        g_free(cwd);
        g_free(abs_base);
        return out;
    } else {
        strcpy(out, path);
        return out;
    }
}