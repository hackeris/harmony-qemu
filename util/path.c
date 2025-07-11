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
static QemuMutex lock;
static QemuMutex strtok_lock;

void init_paths(const char *prefix)
{
    if (prefix[0] == '\0' || !strcmp(prefix, "/")) {
        return;
    }

    if (prefix[0] == '/') {
        base = g_strdup(prefix);
    } else {
        char *cwd = g_get_current_dir();
        base = g_build_filename(cwd, prefix, NULL);
        g_free(cwd);
    }

    hash = g_hash_table_new(g_str_hash, g_str_equal);
    qemu_mutex_init(&lock);
    qemu_mutex_init(&strtok_lock);
}

static const char *relocate_path(const char *name, bool keep_relative_path)
{
    gpointer key, value;
    const char *ret;

    char abspath[PATH_MAX];

    if (!base || !name) {
        //  rnvalid
        return name;
    } else if (strcmp(name, "/") == 0) {
        //  root
        return base;
    } else if (name[0] != '/') {
        //  relative
        if (keep_relative_path) {
            return name;
        }
        getcwd(abspath, sizeof(abspath));
        if (strstr(abspath, base) == &abspath[0]) {
            //  already at rootfs
            return name;
        }
        strcat(abspath, "/");
        strcat(abspath, name);
    } else {
        //  absolute
        if (strstr(name, "/proc/") == name
            || strcmp(name, "/etc/resolv.conf") == 0) {
            //  reuse hosts
            return name;
        }
        strcpy(abspath, name);
    }

    qemu_mutex_lock(&lock);

    /* Have we looked up this file before?  */
    if (g_hash_table_lookup_extended(hash, abspath, &key, &value)) {
        ret = value ? value : name;
    } else {
        char *save = g_strdup(abspath);
        char *full = g_build_filename(base, abspath, NULL);
        g_hash_table_insert(hash, save, full);
        ret = full;
    }

    qemu_mutex_unlock(&lock);
    return ret;
}

const char *pathat(int dirfd, const char *name) {
    const int keep_relative_path = dirfd == AT_FDCWD ? false : true;
    return relocate_path(name, keep_relative_path);
}

/* Look for path in emulation dir, otherwise return name. */
const char *path(const char *name) {
    return relocate_path(name, false);
}

char *resolve_with_path_env(const char *path_env, const char *name, char *out) {

    if (!path_env || !name || !out) return NULL;

    char *path_copy = strdup(path_env);
    if (!path_copy) return NULL;

    if (name[0] == '/') {
        if (access(path(name), F_OK) == 0) {
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

        if (access(path(full_path), F_OK) == 0) {
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