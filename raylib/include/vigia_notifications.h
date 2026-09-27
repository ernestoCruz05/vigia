#ifndef VIGIA_NOTIFICATIONS_H
#define VIGIA_NOTIFICATIONS_H

#include "vigia_model.h"

typedef struct VigiaNotifications VigiaNotifications;

VigiaNotifications *vigia_notifications_create(VigiaState *state);
void vigia_notifications_poll(VigiaNotifications *service);
bool vigia_notifications_owned(const VigiaNotifications *service);
void vigia_notifications_destroy(VigiaNotifications *service);

#endif
