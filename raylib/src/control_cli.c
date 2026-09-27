#include "vigia_control.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    VigiaCommand command = VIGIA_COMMAND_TOGGLE;
    const char *const runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (argc != 2) {
        fprintf(stderr, "usage: %s <toggle|reveal|dismiss|recall-toggle|recall-dismiss|launcher-toggle|launcher-dismiss|demo-toast|demo-osd>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!vigia_command_from_text(argv[1], &command)) {
        fprintf(stderr, "vigiactl: unknown command %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (runtime_dir == NULL || runtime_dir[0] == '\0') {
        fputs("vigiactl: XDG_RUNTIME_DIR is unset\n", stderr);
        return EXIT_FAILURE;
    }
    if (!vigia_control_send(runtime_dir, argv[1])) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
