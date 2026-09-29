#include "vigia_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../third_party/tomlc99/toml.h"

static bool set_error(char *buf, size_t cap, const char *msg)
{
    if (buf != NULL && cap > 0U) {
        (void)snprintf(buf, cap, "%s", msg);
    }
    return false;
}

static bool set_error_field(char *buf, size_t cap, const char *prefix, const char *field)
{
    if (buf != NULL && cap > 0U) {
        (void)snprintf(buf, cap, "%s: %s", prefix, field);
    }
    return false;
}

static bool key_is_known(const char *key, const char *const *known, size_t count)
{
    for (size_t i = 0U; i < count; i++) {
        if (strcmp(key, known[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool check_table_keys(const toml_table_t *tab, const char *const *known, size_t count,
                             char *err, size_t err_cap)
{
    for (int i = 0;; i++) {
        const char *key = toml_key_in(tab, i);
        if (key == NULL) {
            break;
        }
        if (!key_is_known(key, known, count)) {
            return set_error_field(err, err_cap, "unknown configuration key", key);
        }
    }
    return true;
}

static bool resolve_path(char *out, size_t cap, const char *base_dir, const char *path)
{
    if (path[0] == '/') {
        const int w = snprintf(out, cap, "%s", path);
        return w >= 0 && (size_t)w < cap;
    }
    const int w = snprintf(out, cap, "%s/%s", base_dir, path);
    return w >= 0 && (size_t)w < cap;
}

static void free_app_resources(VigiaAppConfig *app)
{
    if (app == NULL) {
        return;
    }
    for (size_t i = 0U; i < app->argc; i++) {
        free(app->argv[i]);
        app->argv[i] = NULL;
    }
    app->argc = 0U;
    for (size_t i = 0U; i < app->app_id_count; i++) {
        free(app->app_ids[i]);
        app->app_ids[i] = NULL;
    }
    app->app_id_count = 0U;
}

void vigia_config_free(VigiaConfig *config)
{
    if (config == NULL) {
        return;
    }
    if (config->launcher.apps != NULL) {
        for (size_t i = 0U; i < config->launcher.app_count; i++) {
            free_app_resources(&config->launcher.apps[i]);
        }
        free(config->launcher.apps);
        config->launcher.apps = NULL;
    }
    config->launcher.app_count = 0U;
}

static bool parse_app_entry(const toml_table_t *app_tab, const char *base_dir,
                            VigiaAppConfig *app, char *err, size_t err_cap)
{
    static const char *const app_keys[] = {"id", "label", "argv", "icon", "app_ids"};
    if (!check_table_keys(app_tab, app_keys, 5U, err, err_cap)) {
        return false;
    }

    toml_datum_t d = toml_string_in(app_tab, "id");
    if (!d.ok || d.u.s[0] == '\0') {
        if (d.ok) free(d.u.s);
        return set_error(err, err_cap, "app missing or empty id");
    }
    if (strlen(d.u.s) >= sizeof app->id) {
        free(d.u.s);
        return set_error(err, err_cap, "app id exceeds capacity");
    }
    (void)snprintf(app->id, sizeof app->id, "%s", d.u.s);
    free(d.u.s);

    d = toml_string_in(app_tab, "label");
    if (!d.ok || d.u.s[0] == '\0') {
        if (d.ok) free(d.u.s);
        return set_error(err, err_cap, "app missing or empty label");
    }
    if (strlen(d.u.s) >= sizeof app->label) {
        free(d.u.s);
        return set_error(err, err_cap, "app label exceeds capacity");
    }
    (void)snprintf(app->label, sizeof app->label, "%s", d.u.s);
    free(d.u.s);

    d = toml_string_in(app_tab, "icon");
    if (!d.ok || d.u.s[0] == '\0') {
        if (d.ok) free(d.u.s);
        return set_error(err, err_cap, "app missing or empty icon");
    }
    if (!resolve_path(app->icon, sizeof app->icon, base_dir, d.u.s)) {
        free(d.u.s);
        return set_error(err, err_cap, "app icon path resolution failed");
    }
    free(d.u.s);

    const toml_array_t *argv_arr = toml_array_in(app_tab, "argv");
    if (argv_arr == NULL) {
        return set_error(err, err_cap, "app missing argv array");
    }
    const int argv_len = toml_array_nelem(argv_arr);
    if (argv_len <= 0 || argv_len > (int)VIGIA_CONFIG_ARGV_MAX) {
        return set_error(err, err_cap, "app argv count out of bounds");
    }
    for (int i = 0; i < argv_len; i++) {
        d = toml_string_at(argv_arr, i);
        if (!d.ok || d.u.s[0] == '\0') {
            if (d.ok) free(d.u.s);
            free_app_resources(app);
            return set_error(err, err_cap, "app argv contains empty or non-string element");
        }
        if (strlen(d.u.s) >= VIGIA_CONFIG_PATH_MAX) {
            free(d.u.s);
            free_app_resources(app);
            return set_error(err, err_cap, "app argument exceeds max length");
        }
        app->argv[app->argc] = strdup(d.u.s);
        free(d.u.s);
        if (app->argv[app->argc] == NULL) {
            free_app_resources(app);
            return set_error(err, err_cap, "out of memory allocating argument");
        }
        app->argc++;
    }

    const toml_array_t *id_arr = toml_array_in(app_tab, "app_ids");
    if (id_arr == NULL) {
        free_app_resources(app);
        return set_error(err, err_cap, "app missing app_ids array");
    }
    const int id_len = toml_array_nelem(id_arr);
    if (id_len <= 0 || id_len > (int)VIGIA_CONFIG_APP_IDS_MAX) {
        free_app_resources(app);
        return set_error(err, err_cap, "app app_ids count out of bounds");
    }
    for (int i = 0; i < id_len; i++) {
        d = toml_string_at(id_arr, i);
        if (!d.ok || d.u.s[0] == '\0') {
            if (d.ok) free(d.u.s);
            free_app_resources(app);
            return set_error(err, err_cap, "app app_ids contains empty or non-string element");
        }
        if (strlen(d.u.s) >= 128U) {
            free(d.u.s);
            free_app_resources(app);
            return set_error(err, err_cap, "app app_id alias exceeds max length");
        }
        app->app_ids[app->app_id_count] = strdup(d.u.s);
        free(d.u.s);
        if (app->app_ids[app->app_id_count] == NULL) {
            free_app_resources(app);
            return set_error(err, err_cap, "out of memory allocating app_id alias");
        }
        app->app_id_count++;
    }
    return true;
}

static bool check_duplicates_and_overlap(const VigiaLauncherConfig *launcher,
                                         char *err, size_t err_cap)
{
    for (size_t i = 0U; i < launcher->app_count; i++) {
        for (size_t j = i + 1U; j < launcher->app_count; j++) {
            if (strcmp(launcher->apps[i].id, launcher->apps[j].id) == 0) {
                return set_error_field(err, err_cap, "duplicate app id", launcher->apps[i].id);
            }
            for (size_t a = 0U; a < launcher->apps[i].app_id_count; a++) {
                for (size_t b = 0U; b < launcher->apps[j].app_id_count; b++) {
                    if (strcmp(launcher->apps[i].app_ids[a], launcher->apps[j].app_ids[b]) == 0) {
                        return set_error_field(err, err_cap, "overlapping app_id assignment",
                                               launcher->apps[i].app_ids[a]);
                    }
                }
            }
        }
    }
    return true;
}

static bool valid_utf8(const char *text, size_t length)
{
    size_t offset = 0U;
    while (offset < length) {
        const unsigned char first = (unsigned char)text[offset++];
        if (first == 0U) {
            return false;
        }
        if (first < 128U) {
            continue;
        }
        size_t continuation = 0U;
        uint32_t value = 0U;
        uint32_t minimum = 0U;
        if (first >= 194U && first <= 223U) {
            continuation = 1U;
            value = first & 31U;
            minimum = 128U;
        } else if (first >= 224U && first <= 239U) {
            continuation = 2U;
            value = first & 15U;
            minimum = 2048U;
        } else if (first >= 240U && first <= 244U) {
            continuation = 3U;
            value = first & 7U;
            minimum = 65536U;
        } else {
            return false;
        }
        if (continuation > length - offset) {
            return false;
        }
        for (size_t i = 0U; i < continuation; i++) {
            const unsigned char next = (unsigned char)text[offset++];
            if ((next & 192U) != 128U) {
                return false;
            }
            value = (value << 6U) | (next & 63U);
        }
        if (value < minimum || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

static bool valid_escapes(const char *text, size_t length)
{
    char quote = '\0';
    bool multiline = false;
    for (size_t i = 0U; i < length; i++) {
        const char current = text[i];
        if (quote == '\0') {
            if (current == '#') {
                while (i + 1U < length && text[i + 1U] != '\n') {
                    i++;
                }
            } else if (current == '\'' || current == '"') {
                quote = current;
                multiline = length - i >= 3U && text[i + 1U] == quote && text[i + 2U] == quote;
                if (multiline) {
                    i += 2U;
                }
            }
            continue;
        }
        if (quote == '"' && current == '\\') {
            if (++i >= length) {
                return false;
            }
            const size_t digits = text[i] == 'u' ? 4U : (text[i] == 'U' ? 8U : 0U);
            if (digits > 0U) {
                if (digits >= length - i) {
                    return false;
                }
                uint32_t value = 0U;
                for (size_t j = 0U; j < digits; j++) {
                    const char hex = text[++i];
                    int digit = -1;
                    if (hex >= '0' && hex <= '9') {
                        digit = hex - '0';
                    } else if (hex >= 'a' && hex <= 'f') {
                        digit = hex - 'a' + 10;
                    } else if (hex >= 'A' && hex <= 'F') {
                        digit = hex - 'A' + 10;
                    }
                    if (digit < 0) {
                        return false;
                    }
                    value = (value << 4U) | (uint32_t)digit;
                }
                if (value == 0U || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) {
                    return false;
                }
            }
        } else if (current == quote) {
            if (!multiline) {
                quote = '\0';
            } else if (length - i >= 3U && text[i + 1U] == quote && text[i + 2U] == quote) {
                while (i + 1U < length && text[i + 1U] == quote) {
                    i++;
                }
                quote = '\0';
            }
        }
    }
    return true;
}

static bool has_key(const toml_table_t *table, const char *key)
{
    for (int i = 0;; i++) {
        const char *found = toml_key_in(table, i);
        if (found == NULL) {
            return false;
        }
        if (strcmp(found, key) == 0) {
            return true;
        }
    }
}

static bool valid_shapes(const toml_table_t *root, char *error, size_t capacity)
{
    static const char *const names[] = { "menu", "osd", "notifications", "launcher", "music" };
    static const char *const integers[] = { "icon_size", "hover_delay_ms", "leave_delay_ms", "display_ms" };
    for (size_t i = 0U; i < sizeof names / sizeof names[0]; i++) {
        if (!has_key(root, names[i])) {
            continue;
        }
        const toml_table_t *table = toml_table_in(root, names[i]);
        if (table == NULL) {
            return set_error_field(error, capacity, "expected configuration table", names[i]);
        }
        if (has_key(table, "enabled") && !toml_bool_in(table, "enabled").ok) {
            return set_error_field(error, capacity, "enabled must be boolean", names[i]);
        }
        for (size_t j = 0U; j < sizeof integers / sizeof integers[0]; j++) {
            if (has_key(table, integers[j]) && !toml_int_in(table, integers[j]).ok) {
                return set_error_field(error, capacity, "expected integer", integers[j]);
            }
        }
        if (has_key(table, "apps") && toml_array_in(table, "apps") == NULL) {
            return set_error(error, capacity, "apps must be an array of tables");
        }
    }
    return true;
}

bool vigia_config_parse(const char *content, size_t length, const char *base_dir,
                        VigiaConfig *config, char *error_buf, size_t error_cap)
{
    if (content == NULL || config == NULL) {
        return set_error(error_buf, error_cap, "null configuration buffer");
    }
    if (length > VIGIA_CONFIG_MAX_SIZE) {
        return set_error(error_buf, error_cap, "configuration exceeds 256 KiB limit");
    }
    if (!valid_utf8(content, length) || !valid_escapes(content, length)) {
        return set_error(error_buf, error_cap, "configuration contains invalid UTF-8, Unicode escape, or NUL");
    }
    if (base_dir != NULL && (strlen(base_dir) >= VIGIA_CONFIG_PATH_MAX || !valid_utf8(base_dir, strlen(base_dir)))) {
        return set_error(error_buf, error_cap, "invalid configuration directory");
    }

    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return set_error(error_buf, error_cap, "out of memory copying configuration");
    }
    memcpy(copy, content, length);
    copy[length] = '\0';

    char toml_err[256] = {0};
    toml_table_t *root = toml_parse(copy, toml_err, sizeof toml_err);
    free(copy);
    if (root == NULL) {
        return set_error(error_buf, error_cap, toml_err[0] != '\0' ? toml_err : "toml parse failed");
    }

    static const char *const root_keys[] = {
        "version", "menu", "osd", "notifications", "launcher", "music"
    };
    if (!check_table_keys(root, root_keys, 6U, error_buf, error_cap)
        || !valid_shapes(root, error_buf, error_cap)) {
        toml_free(root);
        return false;
    }

    toml_datum_t d = toml_int_in(root, "version");
    if (!d.ok || d.u.i != 1) {
        toml_free(root);
        return set_error(error_buf, error_cap, "invalid or missing version (must be 1)");
    }

    VigiaConfig parsed = {0};
    parsed.version = 1;
    parsed.menu_enabled = true;
    parsed.osd_enabled = true;
    parsed.notifications_enabled = true;
    parsed.launcher.icon_size = 24;
    parsed.launcher.hover_delay_ms = 200;
    parsed.launcher.leave_delay_ms = 350;
    parsed.music.enabled = true;
    parsed.music.display_ms = 5000;

    if (base_dir != NULL) {
        (void)snprintf(parsed.config_dir, sizeof parsed.config_dir, "%s", base_dir);
    }

    const toml_table_t *menu_tab = toml_table_in(root, "menu");
    if (menu_tab != NULL) {
        static const char *const menu_keys[] = {"enabled"};
        if (!check_table_keys(menu_tab, menu_keys, 1U, error_buf, error_cap)) {
            toml_free(root);
            return false;
        }
        d = toml_bool_in(menu_tab, "enabled");
        if (d.ok) {
            parsed.menu_enabled = (d.u.b != 0);
        }
    }

    const toml_table_t *osd_tab = toml_table_in(root, "osd");
    if (osd_tab != NULL) {
        static const char *const osd_keys[] = {"enabled"};
        if (!check_table_keys(osd_tab, osd_keys, 1U, error_buf, error_cap)) {
            toml_free(root);
            return false;
        }
        d = toml_bool_in(osd_tab, "enabled");
        if (d.ok) {
            parsed.osd_enabled = (d.u.b != 0);
        }
    }

    const toml_table_t *notify_tab = toml_table_in(root, "notifications");
    if (notify_tab != NULL) {
        static const char *const notify_keys[] = {"enabled"};
        if (!check_table_keys(notify_tab, notify_keys, 1U, error_buf, error_cap)) {
            toml_free(root);
            return false;
        }
        d = toml_bool_in(notify_tab, "enabled");
        if (d.ok) {
            parsed.notifications_enabled = (d.u.b != 0);
        }
    }

    const toml_table_t *launcher_tab = toml_table_in(root, "launcher");
    if (launcher_tab != NULL) {
        static const char *const launcher_keys[] = {
            "enabled", "icon_size", "hover_delay_ms", "leave_delay_ms", "apps"
        };
        if (!check_table_keys(launcher_tab, launcher_keys, 5U, error_buf, error_cap)) {
            toml_free(root);
            return false;
        }
        d = toml_int_in(launcher_tab, "icon_size");
        if (d.ok) {
            if (d.u.i < 24 || d.u.i > 96) {
                toml_free(root);
                return set_error(error_buf, error_cap, "launcher icon_size must be between 24 and 96");
            }
            parsed.launcher.icon_size = (int)d.u.i;
        }
        d = toml_int_in(launcher_tab, "hover_delay_ms");
        if (d.ok) {
            if (d.u.i < 0 || d.u.i > 2000) {
                toml_free(root);
                return set_error(error_buf, error_cap, "launcher hover_delay_ms must be between 0 and 2000");
            }
            parsed.launcher.hover_delay_ms = (int)d.u.i;
        }
        d = toml_int_in(launcher_tab, "leave_delay_ms");
        if (d.ok) {
            if (d.u.i < 0 || d.u.i > 2000) {
                toml_free(root);
                return set_error(error_buf, error_cap, "launcher leave_delay_ms must be between 0 and 2000");
            }
            parsed.launcher.leave_delay_ms = (int)d.u.i;
        }

        const toml_array_t *apps_arr = toml_array_in(launcher_tab, "apps");
        if (apps_arr != NULL) {
            const int apps_len = toml_array_nelem(apps_arr);
            if (apps_len > (int)VIGIA_CONFIG_APPS_MAX) {
                toml_free(root);
                return set_error(error_buf, error_cap, "launcher apps count exceeds 64");
            }
            if (apps_len > 0) {
                parsed.launcher.apps = calloc((size_t)apps_len, sizeof(VigiaAppConfig));
                if (parsed.launcher.apps == NULL) {
                    toml_free(root);
                    return set_error(error_buf, error_cap, "out of memory allocating launcher apps");
                }
            }
            parsed.launcher.app_count = (size_t)apps_len;
            for (int i = 0; i < apps_len; i++) {
                const toml_table_t *app_tab = toml_table_at(apps_arr, i);
                if (app_tab == NULL) {
                    vigia_config_free(&parsed);
                    toml_free(root);
                    return set_error(error_buf, error_cap, "launcher app entry must be a table");
                }
                if (!parse_app_entry(app_tab, parsed.config_dir,
                                     &parsed.launcher.apps[i], error_buf, error_cap)) {
                    vigia_config_free(&parsed);
                    toml_free(root);
                    return false;
                }
            }
        }

        d = toml_bool_in(launcher_tab, "enabled");
        if (d.ok) {
            parsed.launcher.enabled = (d.u.b != 0);
        } else {
            parsed.launcher.enabled = (parsed.launcher.app_count > 0U);
        }
    }

    if (!check_duplicates_and_overlap(&parsed.launcher, error_buf, error_cap)) {
        vigia_config_free(&parsed);
        toml_free(root);
        return false;
    }

    const toml_table_t *music_tab = toml_table_in(root, "music");
    if (music_tab != NULL) {
        static const char *const music_keys[] = {"enabled", "display_ms"};
        if (!check_table_keys(music_tab, music_keys, 2U, error_buf, error_cap)) {
            vigia_config_free(&parsed);
            toml_free(root);
            return false;
        }
        d = toml_bool_in(music_tab, "enabled");
        if (d.ok) {
            parsed.music.enabled = (d.u.b != 0);
        }
        d = toml_int_in(music_tab, "display_ms");
        if (d.ok) {
            if (d.u.i < 1000 || d.u.i > 15000) {
                vigia_config_free(&parsed);
                toml_free(root);
                return set_error(error_buf, error_cap, "music display_ms must be between 1000 and 15000");
            }
            parsed.music.display_ms = (int)d.u.i;
        }
    }

    toml_free(root);
    *config = parsed;
    return true;
}

bool vigia_config_resolve_default_path(char *out_path, size_t capacity)
{
    if (out_path == NULL || capacity == 0U) {
        return false;
    }
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config != NULL && xdg_config[0] == '/') {
        const int w = snprintf(out_path, capacity, "%s/vigia/config.toml", xdg_config);
        return w >= 0 && (size_t)w < capacity;
    }
    const char *home = getenv("HOME");
    if (home != NULL && home[0] == '/') {
        const int w = snprintf(out_path, capacity, "%s/.config/vigia/config.toml", home);
        return w >= 0 && (size_t)w < capacity;
    }
    return false;
}

static void extract_dirname(const char *path, char *out_dir, size_t cap)
{
    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        (void)snprintf(out_dir, cap, ".");
        return;
    }
    if (last_slash == path) {
        (void)snprintf(out_dir, cap, "/");
        return;
    }
    const size_t len = (size_t)(last_slash - path);
    const size_t copy_len = len < cap - 1U ? len : cap - 1U;
    memcpy(out_dir, path, copy_len);
    out_dir[copy_len] = '\0';
}

bool vigia_config_load_file(const char *file_path, VigiaConfig *config,
                            char *error_buf, size_t error_cap)
{
    if (file_path == NULL || config == NULL) {
        return set_error(error_buf, error_cap, "null file path or target config");
    }
    const int fd = open(file_path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return set_error(error_buf, error_cap, strerror(errno));
    }
    struct stat st = {0};
    if (fstat(fd, &st) != 0) {
        (void)close(fd);
        return set_error(error_buf, error_cap, strerror(errno));
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0 || (uintmax_t)st.st_size > VIGIA_CONFIG_MAX_SIZE) {
        (void)close(fd);
        return set_error(error_buf, error_cap, "file size exceeds 256 KiB limit");
    }
    char *buf = malloc((size_t)st.st_size + 1U);
    if (buf == NULL) {
        (void)close(fd);
        return set_error(error_buf, error_cap, "out of memory allocating file buffer");
    }
    const size_t size = (size_t)st.st_size;
    size_t used = 0U;
    bool read_ok = true;
    while (used <= size) {
        const ssize_t got = read(fd, buf + used, size + 1U - used);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            read_ok = got == 0;
            break;
        }
        used += (size_t)got;
    }
    if (close(fd) != 0 || !read_ok || used != size) {
        free(buf);
        return set_error(error_buf, error_cap, "read failed or configuration changed during read");
    }
    buf[size] = '\0';

    char dir[VIGIA_CONFIG_PATH_MAX] = {0};
    extract_dirname(file_path, dir, sizeof dir);
    const bool ok = vigia_config_parse(buf, (size_t)st.st_size, dir, config, error_buf, error_cap);
    free(buf);
    return ok;
}

bool vigia_config_load_default(VigiaConfig *config, const char *explicit_path,
                               char *error_buf, size_t error_cap)
{
    char path[VIGIA_CONFIG_PATH_MAX] = {0};
    if (explicit_path != NULL && explicit_path[0] != '\0') {
        const int w = snprintf(path, sizeof path, "%s", explicit_path);
        if (w < 0 || (size_t)w >= sizeof path) {
            return set_error(error_buf, error_cap, "explicit path too long");
        }
    } else if (!vigia_config_resolve_default_path(path, sizeof path)) {
        return set_error(error_buf, error_cap, "could not resolve config path");
    }

    if (access(path, R_OK) != 0) {
        if (errno != ENOENT) {
            return set_error(error_buf, error_cap, strerror(errno));
        }
        *config = (VigiaConfig){
            .version = 1,
            .menu_enabled = true,
            .osd_enabled = true,
            .notifications_enabled = true,
            .launcher = {
                .enabled = false,
                .icon_size = 24,
                .hover_delay_ms = 200,
                .leave_delay_ms = 350,
                .app_count = 0U,
                .apps = NULL,
            },
            .music = {
                .enabled = true,
                .display_ms = 5000,
            },
        };
        extract_dirname(path, config->config_dir, sizeof config->config_dir);
        return true;
    }
    return vigia_config_load_file(path, config, error_buf, error_cap);
}
