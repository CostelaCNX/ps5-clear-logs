#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PATHS_FILE   "/data/clear_logs.paths"
#define CONFIRM_FILE "/data/clear_logs.confirm"
#define LOG_FILE     "/data/clear_logs.log"
#define MIN_DEPTH    2

static const char *scan_roots[] = {"/data", "/system_data", "/system_data/priv"};
static const char *name_hints[] = {"crash", "core", "dump", "error", "erpt", "log"};

static const char *validated_targets[] = {
    "/system_data/priv/error/history",
    "/system_data/priv/system_logger2/nobackup/database",
    "/system_data/priv/vshlog",
};

static unsigned long long total_bytes;
static unsigned long total_files;
static FILE *logf;

static void logmsg(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (logf) {
        va_start(ap, fmt);
        vfprintf(logf, fmt, ap);
        va_end(ap);
        fflush(logf);
    }
}

static int has_hint(const char *name) {
    char lower[256];
    size_t i;
    for (i = 0; name[i] && i < sizeof(lower) - 1; i++)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[i] = '\0';
    for (i = 0; i < sizeof(name_hints) / sizeof(name_hints[0]); i++)
        if (strstr(lower, name_hints[i]))
            return 1;
    return 0;
}

static int has_sqlite_sibling(const char *dir_path, const char *name) {
    static const char *suffixes[] = {"-journal", "-wal"};
    char candidate[1024];
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        if (snprintf(candidate, sizeof(candidate), "%s/%s%s", dir_path, name, suffixes[i]) >= (int)sizeof(candidate))
            continue;
        struct stat st;
        if (stat(candidate, &st) == 0)
            return 1;
    }
    return 0;
}

static void walk(const char *path, int remove) {
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char full[1024];
        if (snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >= (int)sizeof(full))
            continue;

        struct stat st;
        if (lstat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            walk(full, remove);
            if (remove)
                rmdir(full);
        } else {
            total_files++;
            total_bytes += (unsigned long long)st.st_size;
            size_t namelen = strlen(entry->d_name);
            int is_db = namelen > 3 && !strcmp(entry->d_name + namelen - 3, ".db");
            if (is_db && has_sqlite_sibling(path, entry->d_name))
                logmsg("[note] %s has a pending -journal/-wal; deleting anyway, owner should recreate it\n", full);
            if (remove)
                unlink(full);
        }
    }
    closedir(dir);
}

static int path_is_safe(const char *path) {
    if (path[0] != '/' || strstr(path, ".."))
        return 0;
    int depth = 0;
    for (const char *p = path; *p; p++)
        if (*p == '/' && p[1] && p[1] != '/')
            depth++;
    return depth >= MIN_DEPTH;
}

static void discover(void) {
    logmsg("== discovery (nothing is deleted) ==\n");
    for (size_t r = 0; r < sizeof(scan_roots) / sizeof(scan_roots[0]); r++) {
        DIR *dir = opendir(scan_roots[r]);
        if (!dir) {
            logmsg("[--] %s (not accessible)\n", scan_roots[r]);
            continue;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!has_hint(entry->d_name))
                continue;
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", scan_roots[r], entry->d_name);
            struct stat st;
            if (lstat(full, &st) != 0 || !S_ISDIR(st.st_mode))
                continue;
            total_files = 0;
            total_bytes = 0;
            walk(full, 0);
            logmsg("[ok] %s  files=%lu  size=%llu KiB\n", full, total_files, total_bytes / 1024);
        }
        closedir(dir);
    }
    logmsg("\nThe built-in validated targets clean automatically below.\n");
    logmsg("To clean an extra directory from the list above just this run, list it\n");
    logmsg("(one per line) in %s.\n", PATHS_FILE);
}

static void clean_one(const char *path) {
    char trimmed[1024];
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;
    if (len >= sizeof(trimmed))
        len = sizeof(trimmed) - 1;
    memcpy(trimmed, path, len);
    trimmed[len] = '\0';

    if (!path_is_safe(trimmed)) {
        logmsg("[skip] %s (unsafe path)\n", trimmed);
        return;
    }
    total_files = 0;
    total_bytes = 0;
    walk(trimmed, 1);
    logmsg("[clean] %s  removed=%lu files  freed=%llu KiB\n", trimmed, total_files, total_bytes / 1024);
}

static void clean(void) {
    logmsg("== clean ==\n");
    for (size_t i = 0; i < sizeof(validated_targets) / sizeof(validated_targets[0]); i++)
        clean_one(validated_targets[i]);

    FILE *f = fopen(PATHS_FILE, "r");
    if (!f)
        return;

    logmsg("== clean (extra, from %s) ==\n", PATHS_FILE);
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        clean_one(line);
    }
    fclose(f);

    unlink(CONFIRM_FILE);
}

int main(void) {
    logf = fopen(LOG_FILE, "a");

    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    logmsg("\n---- run at %s ----\n", stamp);

    discover();
    clean();

    logmsg("---- done, see %s for this log ----\n", LOG_FILE);

    if (logf)
        fclose(logf);
    return 0;
}
