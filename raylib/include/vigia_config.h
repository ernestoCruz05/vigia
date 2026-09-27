#ifndef VIGIA_CONFIG_H
#define VIGIA_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define VIGIA_CONFIG_MAX_SIZE (256U * 1024U)
#define VIGIA_CONFIG_PATH_MAX 4096U
#define VIGIA_CONFIG_APPS_MAX 64U
#define VIGIA_CONFIG_ARGV_MAX 64U
#define VIGIA_CONFIG_APP_IDS_MAX 16U

typedef struct {
    char id[64];
    char label[128];
    char icon[VIGIA_CONFIG_PATH_MAX];
    size_t argc;
    char *argv[VIGIA_CONFIG_ARGV_MAX];
    size_t app_id_count;
    char *app_ids[VIGIA_CONFIG_APP_IDS_MAX];
} VigiaAppConfig;

typedef struct {
    bool enabled;
    int icon_size;
    int hover_delay_ms;
    int leave_delay_ms;
    size_t app_count;
    VigiaAppConfig *apps;
} VigiaLauncherConfig;

typedef struct {
    bool enabled;
    int display_ms;
} VigiaMusicConfig;

typedef struct {
    int version;
    bool menu_enabled;
    bool osd_enabled;
    bool notifications_enabled;
    VigiaLauncherConfig launcher;
    VigiaMusicConfig music;
    char config_dir[VIGIA_CONFIG_PATH_MAX];
} VigiaConfig;

bool vigia_config_parse(const char *content, size_t length, const char *base_dir,
                        VigiaConfig *config, char *error_buf, size_t error_cap);

bool vigia_config_resolve_default_path(char *out_path, size_t capacity);

bool vigia_config_load_file(const char *file_path, VigiaConfig *config,
                            char *error_buf, size_t error_cap);

bool vigia_config_load_default(VigiaConfig *config, const char *explicit_path,
                               char *error_buf, size_t error_cap);

void vigia_config_free(VigiaConfig *config);

#endif
