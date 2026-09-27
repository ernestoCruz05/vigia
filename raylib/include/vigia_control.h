#ifndef VIGIA_CONTROL_H
#define VIGIA_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "vigia_model.h"

#define VIGIA_CONTROL_PATH_CAP 108

#define VIGIA_CONTROL_DISCARD_BUDGET 8U

typedef struct {
    int fd;
    int lock_fd;
    dev_t socket_device;
    ino_t socket_inode;
    dev_t lock_device;
    ino_t lock_inode;
    char path[VIGIA_CONTROL_PATH_CAP];
    char lock_path[VIGIA_CONTROL_PATH_CAP];
} VigiaControl;

bool vigia_command_from_text(const char *text, VigiaCommand *command);
bool vigia_control_path(char *out, size_t capacity, const char *runtime_dir);
bool vigia_control_open(VigiaControl *control, const char *runtime_dir);
bool vigia_control_receive(const VigiaControl *control, VigiaCommand *command);
bool vigia_control_send(const char *runtime_dir, const char *text);
bool vigia_control_send_raw(const char *runtime_dir, const char *data,
                            size_t length);
void vigia_control_close(VigiaControl *control);

#endif
