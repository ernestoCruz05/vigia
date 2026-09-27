#include "vigia_notifications.h"

#include <dbus/dbus.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NOTIFY_NAME "org.freedesktop.Notifications"
#define NOTIFY_PATH "/org/freedesktop/Notifications"

struct VigiaNotifications {
    DBusConnection *connection;
    VigiaState *state;
    struct { int id; double deadline; } live[VIGIA_MAX_ACTIVE];
    uint32_t next_id;
    double retry_at;
    bool owned;
};

static double now(void)
{
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0.0;
    }
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static bool contains(const VigiaNotification *items, size_t count, int id)
{
    for (size_t i = 0U; i < count; i++) {
        if (items[i].id == id) {
            return true;
        }
    }
    return false;
}

static bool send_message(VigiaNotifications *service, DBusMessage *message)
{
    if (message == NULL) {
        return false;
    }
    const bool sent = dbus_connection_send(service->connection, message, NULL);
    dbus_message_unref(message);
    return sent;
}

static bool closed_signal(VigiaNotifications *service, int id, uint32_t reason)
{
    DBusMessage *signal = dbus_message_new_signal(NOTIFY_PATH, NOTIFY_NAME, "NotificationClosed");
    if (signal == NULL) {
        return false;
    }
    const uint32_t identifier = (uint32_t)id;
    if (!dbus_message_append_args(signal, DBUS_TYPE_UINT32, &identifier,
        DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID)) {
        dbus_message_unref(signal);
        return false;
    }
    return send_message(service, signal);
}

static bool close_live(VigiaNotifications *service, size_t slot, uint32_t reason)
{
    const int id = service->live[slot].id;
    if (id == 0) {
        return true;
    }
    if (!closed_signal(service, id, reason)) {
        return false;
    }
    service->live[slot].id = 0;
    if (reason == 1U) {
        vigia_state_expire(service->state, id);
    } else {
        vigia_state_dismiss(service->state, id);
    }
    return true;
}

static void reconcile(VigiaNotifications *service)
{
    const double time = now();
    for (size_t i = 0U; i < VIGIA_MAX_ACTIVE; i++) {
        if (service->live[i].id == 0) {
            continue;
        }
        if (!contains(service->state->active, service->state->active_count, service->live[i].id)) {
            (void)close_live(service, i, 2U);
        } else if (service->live[i].deadline > 0.0 && time >= service->live[i].deadline) {
            (void)close_live(service, i, 1U);
        }
    }
}

static bool text(DBusMessageIter *iter, char *output, size_t capacity)
{
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) {
        return false;
    }
    const char *value = NULL;
    dbus_message_iter_get_basic(iter, &value);
    if (value == NULL || strlen(value) >= capacity || !dbus_validate_utf8(value, NULL)) {
        return false;
    }
    memcpy(output, value, strlen(value) + 1U);
    dbus_message_iter_next(iter);
    return true;
}

static bool hints(DBusMessageIter *iter, VigiaNotification *item)
{
    DBusMessageIter entries;
    dbus_message_iter_recurse(iter, &entries);
    bool urgency_seen = false;
    bool transient_seen = false;
    size_t count = 0U;
    while (dbus_message_iter_get_arg_type(&entries) != DBUS_TYPE_INVALID) {
        if (++count > 32U) {
            return false;
        }
        DBusMessageIter entry;
        DBusMessageIter value;
        dbus_message_iter_recurse(&entries, &entry);
        char key[128] = {0};
        if (!text(&entry, key, sizeof key) || dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
            return false;
        }
        dbus_message_iter_recurse(&entry, &value);
        if (strcmp(key, "urgency") == 0) {
            if (urgency_seen || dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_BYTE) {
                return false;
            }
            unsigned char urgency = 0U;
            dbus_message_iter_get_basic(&value, &urgency);
            if (urgency > 2U) {
                return false;
            }
            item->critical = urgency == 2U;
            urgency_seen = true;
        } else if (strcmp(key, "transient") == 0) {
            if (transient_seen || dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_BOOLEAN) {
                return false;
            }
            dbus_bool_t transient = false;
            dbus_message_iter_get_basic(&value, &transient);
            item->transient = transient != 0;
            transient_seen = true;
        }
        dbus_message_iter_next(&entries);
    }
    dbus_message_iter_next(iter);
    return true;
}

static bool parse_notify(DBusMessage *message, VigiaNotification *item, uint32_t *replaces, int32_t *timeout)
{
    if (!dbus_message_has_signature(message, "susssasa{sv}i")) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter) || !text(&iter, item->app_name, sizeof item->app_name)) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, replaces);
    dbus_message_iter_next(&iter);
    char icon[4096] = {0};
    if (!text(&iter, icon, sizeof icon) || !text(&iter, item->summary, sizeof item->summary)
        || !text(&iter, item->body, sizeof item->body)) {
        return false;
    }
    DBusMessageIter actions;
    dbus_message_iter_recurse(&iter, &actions);
    size_t count = 0U;
    while (dbus_message_iter_get_arg_type(&actions) != DBUS_TYPE_INVALID) {
        char action[256] = {0};
        if (++count > 32U || !text(&actions, action, sizeof action)) {
            return false;
        }
    }
    if (count % 2U != 0U) {
        return false;
    }
    dbus_message_iter_next(&iter);
    if (!hints(&iter, item)) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, timeout);
    return *timeout >= -1;
}

static DBusHandlerResult error_reply(VigiaNotifications *service, DBusMessage *message, const char *name, const char *detail)
{
    return send_message(service, dbus_message_new_error(message, name, detail))
        ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
}

static DBusHandlerResult notify(VigiaNotifications *service, DBusMessage *message)
{
    VigiaNotification item = {0};
    uint32_t replaces = 0U;
    int32_t timeout = 0;
    if (!parse_notify(message, &item, &replaces, &timeout)) {
        return error_reply(service, message, DBUS_ERROR_INVALID_ARGS, "Invalid or oversized notification");
    }
    size_t slot = VIGIA_MAX_ACTIVE;
    for (size_t i = 0U; i < VIGIA_MAX_ACTIVE; i++) {
        if (replaces != 0U && replaces == (uint32_t)service->live[i].id) {
            item.id = service->live[i].id;
            slot = i;
            break;
        }
    }
    if (item.id == 0) {
        do {
            service->next_id = service->next_id >= (uint32_t)INT_MAX ? 1U : service->next_id + 1U;
            item.id = (int)service->next_id;
        } while (contains(service->state->active, service->state->active_count, item.id)
            || contains(service->state->history, service->state->history_count, item.id));
        for (size_t i = 0U; i < VIGIA_MAX_ACTIVE; i++) {
            if (service->live[i].id == 0) {
                slot = i;
                break;
            }
        }
    }
    time_t timestamp = time(NULL);
    struct tm local = {0};
    if (timestamp != (time_t)-1 && localtime_r(&timestamp, &local) != NULL) {
        if (strftime(item.time, sizeof item.time, "%H:%M", &local) == 0U) {
            item.time[0] = '\0';
        }
    }
    DBusMessage *reply = dbus_message_new_method_return(message);
    const uint32_t id = (uint32_t)item.id;
    if (reply == NULL) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) {
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (!send_message(service, reply)) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    const bool displayed = vigia_state_push(service->state, &item);
    if (displayed && slot < VIGIA_MAX_ACTIVE) {
        const int32_t duration = timeout == -1 ? (item.critical ? 0 : 5000) : timeout;
        service->live[slot].id = item.id;
        service->live[slot].deadline = duration == 0 ? 0.0 : now() + (double)duration / 1000.0;
    } else {
        (void)closed_signal(service, item.id, 1U);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static const char introspection[] =
    "<node><interface name='org.freedesktop.Notifications'>"
    "<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
    "<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
    "<arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
    "<arg type='u' direction='out'/></method>"
    "<method name='CloseNotification'><arg type='u' direction='in'/></method>"
    "<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
    "<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/>"
    "<arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
    "<signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal></interface>"
    "<interface name='org.freedesktop.DBus.Introspectable'><method name='Introspect'>"
    "<arg type='s' direction='out'/></method></interface></node>";

static DBusHandlerResult receive(DBusConnection *connection, DBusMessage *message, void *data)
{
    (void)connection;
    VigiaNotifications *service = data;
    if (!service->owned || dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    reconcile(service);
    if (dbus_message_is_method_call(message, NOTIFY_NAME, "Notify")) {
        return notify(service, message);
    }
    if (dbus_message_is_method_call(message, NOTIFY_NAME, "CloseNotification")) {
        uint32_t id = 0U;
        if (!dbus_message_has_signature(message, "u")
            || !dbus_message_get_args(message, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) {
            return error_reply(service, message, DBUS_ERROR_INVALID_ARGS, "Expected notification ID");
        }
        for (size_t i = 0U; i < VIGIA_MAX_ACTIVE; i++) {
            if (id != 0U && id == (uint32_t)service->live[i].id && !close_live(service, i, 3U)) {
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            }
        }
        return send_message(service, dbus_message_new_method_return(message))
            ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (!dbus_message_has_signature(message, "")) {
        return error_reply(service, message, DBUS_ERROR_INVALID_ARGS, "Expected no arguments");
    }
    DBusMessage *reply = dbus_message_new_method_return(message);
    if (reply == NULL) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    bool appended = false;
    if (dbus_message_is_method_call(message, NOTIFY_NAME, "GetCapabilities")) {
        const char *capabilities[] = {"body", "persistence"};
        const char **values = capabilities;
        appended = dbus_message_append_args(reply, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &values, 2, DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(message, NOTIFY_NAME, "GetServerInformation")) {
        const char *name = "Vigia";
        const char *vendor = "Vigia";
        const char *version = "1";
        const char *spec = "1.2";
        appended = dbus_message_append_args(reply, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &vendor,
            DBUS_TYPE_STRING, &version, DBUS_TYPE_STRING, &spec, DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        const char *xml = introspection;
        appended = dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    } else {
        dbus_message_unref(reply);
        return error_reply(service, message, DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
    }
    if (!appended) {
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    return send_message(service, reply) ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
}

static void disconnect_service(VigiaNotifications *service)
{
    if (service->connection != NULL) {
        dbus_connection_close(service->connection);
        dbus_connection_unref(service->connection);
        service->connection = NULL;
    }
    for (size_t i = 0U; i < VIGIA_MAX_ACTIVE; i++) {
        if (service->live[i].id != 0) {
            vigia_state_expire(service->state, service->live[i].id);
            service->live[i].id = 0;
        }
    }
    service->owned = false;
    service->retry_at = now() + 1.0;
}

static void connect_service(VigiaNotifications *service)
{
    DBusError error;
    dbus_error_init(&error);
    service->connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (service->connection != NULL) {
        dbus_connection_set_exit_on_disconnect(service->connection, false);
        dbus_connection_set_max_message_size(service->connection, 65536L);
        dbus_connection_set_max_received_size(service->connection, 262144L);
        static const DBusObjectPathVTable table = {.message_function = receive};
        if (dbus_connection_register_object_path(service->connection, NOTIFY_PATH, &table, service)) {
            const int result = dbus_bus_request_name(service->connection, NOTIFY_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
            service->owned = result == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
        }
    }
    dbus_error_free(&error);
    if (!service->owned) {
        disconnect_service(service);
    }
}

VigiaNotifications *vigia_notifications_create(VigiaState *state)
{
    if (state == NULL) {
        return NULL;
    }
    VigiaNotifications *service = calloc(1U, sizeof *service);
    if (service != NULL) {
        service->state = state;
    }
    return service;
}

void vigia_notifications_poll(VigiaNotifications *service)
{
    if (service == NULL) {
        return;
    }
    if (service->connection == NULL && now() >= service->retry_at) {
        connect_service(service);
    }
    if (service->connection == NULL) {
        return;
    }
    reconcile(service);
    if (!dbus_connection_read_write(service->connection, 0)) {
        disconnect_service(service);
        return;
    }
    for (unsigned int i = 0U; i < 64U
        && dbus_connection_get_dispatch_status(service->connection) == DBUS_DISPATCH_DATA_REMAINS; i++) {
        if (dbus_connection_dispatch(service->connection) == DBUS_DISPATCH_NEED_MEMORY) {
            break;
        }
    }
    if (!dbus_connection_read_write(service->connection, 0)) {
        disconnect_service(service);
    }
}

bool vigia_notifications_owned(const VigiaNotifications *service)
{
    return service != NULL && service->owned;
}

void vigia_notifications_destroy(VigiaNotifications *service)
{
    if (service != NULL) {
        if (service->owned && dbus_connection_get_is_connected(service->connection)) {
            for (size_t i = 0U; i < VIGIA_MAX_ACTIVE; i++) {
                (void)close_live(service, i, 3U);
            }
            (void)dbus_connection_read_write(service->connection, 0);
        }
        disconnect_service(service);
        free(service);
    }
}
