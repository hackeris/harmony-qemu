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
}

/* Look for path in emulation dir, otherwise return name. */
const char *path(const char *name)
{
    gpointer key, value;
    const char *ret;

    char abspath[PATH_MAX];

    if (!base || !name || name[0] != '/') {
        getcwd(abspath, sizeof(abspath));
        strcat(abspath, "/");
        strcat(abspath, name);
    } else if (strcmp(name, "/") == 0) {
        return base;
    } else {
        strcpy(abspath, name);
    }

    qemu_mutex_lock(&lock);

    /* Have we looked up this file before?  */
    if (g_hash_table_lookup_extended(hash, abspath, &key, &value)) {
        ret = value ? value : name;
    } else {
        char *save = g_strdup(abspath);
        char *full = g_build_filename(base, abspath, NULL);

        /* Look for the path; record the result, pass or fail.  */
        if (access(full, F_OK) == 0) {
            /* Exists.  */
            g_hash_table_insert(hash, save, full);
            ret = full;
        } else {
            /* Does not exist.  */
            g_free(full);
            g_hash_table_insert(hash, save, NULL);
            ret = name;
        }
    }

    qemu_mutex_unlock(&lock);
    return ret;
}

char *resolve_path(const char *path_env, const char *name, char *out) {

    if (!path_env || !name || !out) return NULL;

    char *path_copy = strdup(path_env);
    if (!path_copy) return NULL;

    char *dir = strtok(path_copy, ":");
    char full_path[PATH_MAX];

    while (dir) {
        if (name[0] == '/') {
            if (access(path(name), F_OK) == 0) {
                strncpy(out, name, PATH_MAX);
                free(path_copy);
                return out;
            }
            break;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);

        if (access(path(full_path), F_OK) == 0) {
            strncpy(out, full_path, PATH_MAX);
            free(path_copy);
            return out;
        }

        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}
