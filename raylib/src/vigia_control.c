#include "vigia_control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define VIGIA_CONTROL_SOCKET_NAME "vigia-ray.sock"
#define VIGIA_CONTROL_LOCK_SUFFIX ".lock"
#define VIGIA_CONTROL_MESSAGE_CAP 64U

static bool vigia_control_socket_address(struct sockaddr_un *address,
                                         const char *path)
{
    const size_t path_length = strlen(path);
    if (path_length >= sizeof address->sun_path) {
        return false;
    }

    *address = (struct sockaddr_un){0};
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, path_length + 1U);
    return true;
}

static bool vigia_control_lock_path(char *out, size_t capacity,
                                    const char *socket_path)
{
    const int written = snprintf(out, capacity, "%s%s", socket_path,
                                 VIGIA_CONTROL_LOCK_SUFFIX);
    return written >= 0 && (size_t)written < capacity;
}


static bool vigia_control_matches(const char *path, dev_t device, ino_t inode)
{
    struct stat status = {0};
    return lstat(path, &status) == 0 && status.st_dev == device
        && status.st_ino == inode;
}

static void vigia_control_remove_owned_socket(const char *path, dev_t device,
                                              ino_t inode)
{
    if (path[0] != '\0' && vigia_control_matches(path, device, inode)) {
        (void)unlink(path);
    }
}

static bool vigia_control_lock(VigiaControl *next)
{
    const int lock_fd = open(next->lock_path,
                             O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                             0600);
    if (lock_fd < 0) {
        perror("vigia-ray: open control lock");
        return false;
    }

    struct stat status = {0};
    if (fstat(lock_fd, &status) != 0) {
        perror("vigia-ray: stat control lock");
        (void)close(lock_fd);
        return false;
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid()) {
        fputs("vigia-ray: control lock must be a user-owned regular file\n",
              stderr);
        (void)close(lock_fd);
        return false;
    }
    if (fchmod(lock_fd, 0600) != 0) {
        perror("vigia-ray: chmod control lock");
        (void)close(lock_fd);
        return false;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            fputs("vigia-ray: control socket is already owned\n", stderr);
        } else {
            perror("vigia-ray: lock control socket");
        }
        (void)close(lock_fd);
        return false;
    }

    next->lock_fd = lock_fd;
    next->lock_device = status.st_dev;
    next->lock_inode = status.st_ino;
    return true;
}

static bool vigia_control_remove_stale_socket(const char *path)
{
    struct stat status = {0};
    if (lstat(path, &status) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        perror("vigia-ray: inspect control socket path");
        return false;
    }
    if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid()) {
        fputs("vigia-ray: control socket path is not an owned socket\n", stderr);
        return false;
    }
    if (unlink(path) != 0) {
        perror("vigia-ray: remove stale control socket");
        return false;
    }
    return true;
}

bool vigia_command_from_text(const char *text, VigiaCommand *command)
{
    if (text == NULL || command == NULL) {
        return false;
    }

    if (strcmp(text, "toggle") == 0) {
        *command = VIGIA_COMMAND_TOGGLE;
    } else if (strcmp(text, "reveal") == 0) {
        *command = VIGIA_COMMAND_REVEAL;
    } else if (strcmp(text, "dismiss") == 0) {
        *command = VIGIA_COMMAND_DISMISS;
    } else if (strcmp(text, "recall-toggle") == 0) {
        *command = VIGIA_COMMAND_RECALL_TOGGLE;
    } else if (strcmp(text, "recall-dismiss") == 0) {
        *command = VIGIA_COMMAND_RECALL_DISMISS;
    } else if (strcmp(text, "demo-toast") == 0) {
        *command = VIGIA_COMMAND_DEMO_TOAST;
    } else if (strcmp(text, "demo-osd") == 0) {
        *command = VIGIA_COMMAND_DEMO_OSD;
    } else if (strcmp(text, "launcher-toggle") == 0) {
        *command = VIGIA_COMMAND_LAUNCHER_TOGGLE;
    } else if (strcmp(text, "launcher-dismiss") == 0) {
        *command = VIGIA_COMMAND_LAUNCHER_DISMISS;
    } else {
        return false;
    }
    return true;
}

bool vigia_control_path(char *out, size_t capacity, const char *runtime_dir)
{
    if (out == NULL || capacity == 0U || runtime_dir == NULL
        || runtime_dir[0] == '\0') {
        return false;
    }
    const int written = snprintf(out, capacity, "%s/%s", runtime_dir,
                                 VIGIA_CONTROL_SOCKET_NAME);
    return written >= 0 && (size_t)written < capacity;
}

bool vigia_control_open(VigiaControl *control, const char *runtime_dir)
{
    VigiaControl next = { .fd = -1, .lock_fd = -1 };
    struct sockaddr_un address = {0};
    struct stat status = {0};
    mode_t previous_mask = 0;
    bool mask_changed = false;
    bool success = false;

    if (control == NULL || !vigia_control_path(next.path, sizeof next.path,
                                                runtime_dir)
        || !vigia_control_lock_path(next.lock_path, sizeof next.lock_path,
                                    next.path)
        || !vigia_control_socket_address(&address, next.path)) {
        return false;
    }

    previous_mask = umask(0077);
    mask_changed = true;
    if (!vigia_control_lock(&next) || !vigia_control_remove_stale_socket(next.path)) {
        goto out;
    }

    next.fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (next.fd < 0) {
        perror("vigia-ray: create control socket");
        goto out;
    }
    if (fcntl(next.fd, F_SETFL, O_NONBLOCK) != 0) {
        perror("vigia-ray: set control socket nonblocking");
        goto out;
    }
    if (bind(next.fd, (const struct sockaddr *)&address, sizeof address) != 0) {
        perror("vigia-ray: bind control socket");
        goto out;
    }
    if (lstat(next.path, &status) != 0 || !S_ISSOCK(status.st_mode)) {
        perror("vigia-ray: stat control socket path");
        goto out;
    }
    next.socket_device = status.st_dev;
    next.socket_inode = status.st_ino;
    if (chmod(next.path, 0600) != 0) {
        perror("vigia-ray: chmod control socket");
        goto out;
    }

    *control = next;
    success = true;
out:
    if (mask_changed) {
        (void)umask(previous_mask);
    }
    if (success) {
        return true;
    }
    vigia_control_remove_owned_socket(next.path, next.socket_device,
                                      next.socket_inode);
    if (next.fd >= 0) {
        (void)close(next.fd);
    }
    if (next.lock_fd >= 0) {
        (void)close(next.lock_fd);
    }
    return false;
}

bool vigia_control_receive(const VigiaControl *control, VigiaCommand *command)
{
    if (control == NULL || control->fd < 0 || command == NULL) {
        return false;
    }

    for (size_t discarded = 0U;
         discarded < VIGIA_CONTROL_DISCARD_BUDGET; discarded++) {
        char message[VIGIA_CONTROL_MESSAGE_CAP + 1U] = {0};
        struct iovec vector = {
            .iov_base = message,
            .iov_len = VIGIA_CONTROL_MESSAGE_CAP,
        };
        struct msghdr header = {
            .msg_iov = &vector,
            .msg_iovlen = 1U,
        };
        const ssize_t received = recvmsg(control->fd, &header,
                                         MSG_DONTWAIT | MSG_TRUNC);
        if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("vigia-ray: receive control command");
            }
            return false;
        }
        if (received == 0 || (size_t)received >= VIGIA_CONTROL_MESSAGE_CAP) {
            continue;
        }

        size_t length = (size_t)received;
        if (message[length - 1U] == '\n') {
            length--;
        }
        message[length] = '\0';
        if (vigia_command_from_text(message, command)) {
            return true;
        }
    }
    return false;
}

bool vigia_control_send_raw(const char *runtime_dir, const char *data,
                            size_t length)
{
    int socket_fd = -1;
    struct sockaddr_un address = {0};
    char path[VIGIA_CONTROL_PATH_CAP] = {0};
    bool success = false;

    if (data == NULL || length == 0U
        || !vigia_control_path(path, sizeof path, runtime_dir)
        || !vigia_control_socket_address(&address, path)) {
        return false;
    }
    socket_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        perror("vigia-ray: create control client");
        goto out;
    }
    if (sendto(socket_fd, data, length, 0,
               (const struct sockaddr *)&address, sizeof address)
        != (ssize_t)length) {
        perror("vigia-ray: send control command");
        goto out;
    }
    success = true;
out:
    if (socket_fd >= 0) {
        (void)close(socket_fd);
    }
    return success;
}

bool vigia_control_send(const char *runtime_dir, const char *text)
{
    if (text == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    if (length == 0U || length >= VIGIA_CONTROL_MESSAGE_CAP) {
        return false;
    }
    return vigia_control_send_raw(runtime_dir, text, length);
}

void vigia_control_close(VigiaControl *control)
{
    if (control == NULL) {
        return;
    }
    vigia_control_remove_owned_socket(control->path, control->socket_device,
                                      control->socket_inode);
    if (control->fd >= 0) {
        (void)close(control->fd);
    }
    if (control->lock_fd >= 0) {
        (void)close(control->lock_fd);
    }
    *control = (VigiaControl){ .fd = -1, .lock_fd = -1 };
}
