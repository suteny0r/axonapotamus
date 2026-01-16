#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification_messages.h>
#include <bt/bt_service/bt.h>
#include <extra_beacon.h>

#define TAG "Axonapotamus"

// Axon Signal Protocol Constants
#define AXON_OUI_0 0x00
#define AXON_OUI_1 0x25
#define AXON_OUI_2 0xDF

// Service UUID 0xFE6C (little-endian for BLE)
#define AXON_SERVICE_UUID_LOW  0x6C
#define AXON_SERVICE_UUID_HIGH 0xFE

// Base payload (24 bytes)
static const uint8_t BASE_PAYLOAD[] = {
    0x01, 0x58, 0x38, 0x37, 0x30, 0x30, 0x32, 0x46,
    0x50, 0x34, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
    0xCE, 0x1B, 0x33, 0x00, 0x00, 0x02, 0x00, 0x00
};

#define PAYLOAD_SIZE 24
#define FUZZ_INTERVAL_MS 500

typedef enum {
    AxonapotamusViewSubmenu,
    AxonapotamusViewTransmit,
    AxonapotamusViewScan,
} AxonapotamusView;

typedef enum {
    AxonapotamusSubmenuIndexTransmit,
    AxonapotamusSubmenuIndexFuzz,
    AxonapotamusSubmenuIndexScan,
} AxonapotamusSubmenuIndex;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Popup* popup;
    NotificationApp* notifications;

    FuriTimer* fuzz_timer;
    uint16_t fuzz_value;
    uint8_t current_payload[PAYLOAD_SIZE];

    bool is_transmitting;
    bool is_fuzzing;
    bool is_scanning;
} Axonapotamus;

// Forward declarations
static void axonapotamus_submenu_callback(void* context, uint32_t index);
static uint32_t axonapotamus_exit_callback(void* context);
static uint32_t axonapotamus_submenu_back_callback(void* context);
static void axonapotamus_fuzz_timer_callback(void* context);

static void axonapotamus_update_payload_with_fuzz(Axonapotamus* app) {
    memcpy(app->current_payload, BASE_PAYLOAD, PAYLOAD_SIZE);

    // Fuzz bytes at positions 10-11 (command bytes)
    app->current_payload[10] = (app->fuzz_value >> 8) & 0xFF;
    app->current_payload[11] = app->fuzz_value & 0xFF;

    // Fuzz bytes at positions 20-21
    app->current_payload[20] = (app->fuzz_value >> 4) & 0xFF;
    app->current_payload[21] = (app->fuzz_value << 4) & 0xFF;
}

static bool axonapotamus_start_advertising(Axonapotamus* app) {
    // Build BLE advertisement data with service data
    // Format: Flags + Service Data (UUID + payload)

    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 50,
        .max_adv_interval_ms = 150,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypePublic,
    };

    // Set extra beacon config
    furi_hal_bt_extra_beacon_set_config(&config);

    // Build advertisement data
    // AD Structure: Length, Type, Data
    uint8_t adv_data[31];
    uint8_t adv_len = 0;

    // Flags (required for BLE advertising)
    adv_data[adv_len++] = 0x02;  // Length
    adv_data[adv_len++] = 0x01;  // Type: Flags
    adv_data[adv_len++] = 0x06;  // LE General Discoverable, BR/EDR Not Supported

    // Service Data (UUID 0xFE6C + payload)
    // Length = 1 (type) + 2 (UUID) + payload_size
    uint8_t service_data_len = 1 + 2 + PAYLOAD_SIZE;
    adv_data[adv_len++] = service_data_len;
    adv_data[adv_len++] = 0x16;  // Type: Service Data - 16 bit UUID
    adv_data[adv_len++] = AXON_SERVICE_UUID_LOW;   // UUID low byte
    adv_data[adv_len++] = AXON_SERVICE_UUID_HIGH;  // UUID high byte

    // Copy payload
    memcpy(&adv_data[adv_len], app->current_payload, PAYLOAD_SIZE);
    adv_len += PAYLOAD_SIZE;

    // Set and start the extra beacon
    if(!furi_hal_bt_extra_beacon_set_data(adv_data, adv_len)) {
        FURI_LOG_E(TAG, "Failed to set beacon data");
        return false;
    }

    if(!furi_hal_bt_extra_beacon_start()) {
        FURI_LOG_E(TAG, "Failed to start beacon");
        return false;
    }

    FURI_LOG_I(TAG, "Advertising started");
    return true;
}

static void axonapotamus_stop_advertising(Axonapotamus* app) {
    UNUSED(app);
    furi_hal_bt_extra_beacon_stop();
    FURI_LOG_I(TAG, "Advertising stopped");
}

static void axonapotamus_start_transmit(Axonapotamus* app, bool fuzz_mode) {
    if(app->is_transmitting) return;

    memcpy(app->current_payload, BASE_PAYLOAD, PAYLOAD_SIZE);
    app->fuzz_value = 0;
    app->is_fuzzing = fuzz_mode;

    if(axonapotamus_start_advertising(app)) {
        app->is_transmitting = true;
        notification_message(app->notifications, &sequence_blink_blue_100);

        if(fuzz_mode) {
            furi_timer_start(app->fuzz_timer, furi_ms_to_ticks(FUZZ_INTERVAL_MS));
            popup_set_header(app->popup, "FUZZ TX ACTIVE", 64, 20, AlignCenter, AlignCenter);
        } else {
            popup_set_header(app->popup, "TX ACTIVE", 64, 20, AlignCenter, AlignCenter);
        }
        popup_set_text(app->popup, "Broadcasting Axon Signal\nPress Back to stop", 64, 40, AlignCenter, AlignCenter);
    } else {
        popup_set_header(app->popup, "TX FAILED", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(app->popup, "Could not start BLE beacon", 64, 40, AlignCenter, AlignCenter);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewTransmit);
}

static void axonapotamus_stop_transmit(Axonapotamus* app) {
    if(!app->is_transmitting) return;

    if(app->is_fuzzing) {
        furi_timer_stop(app->fuzz_timer);
        app->is_fuzzing = false;
    }

    axonapotamus_stop_advertising(app);
    app->is_transmitting = false;
    notification_message(app->notifications, &sequence_reset_blue);
}

static void axonapotamus_fuzz_timer_callback(void* context) {
    Axonapotamus* app = context;

    if(!app->is_transmitting || !app->is_fuzzing) return;

    // Stop current advertising
    furi_hal_bt_extra_beacon_stop();

    // Increment fuzz value (wraps at 0xFFFF)
    app->fuzz_value++;

    // Update payload
    axonapotamus_update_payload_with_fuzz(app);

    // Restart with new payload
    axonapotamus_start_advertising(app);

    // Blink to show activity
    notification_message(app->notifications, &sequence_blink_cyan_10);

    FURI_LOG_D(TAG, "Fuzz value: 0x%04X", app->fuzz_value);
}

static void axonapotamus_submenu_callback(void* context, uint32_t index) {
    Axonapotamus* app = context;

    switch(index) {
        case AxonapotamusSubmenuIndexTransmit:
            axonapotamus_start_transmit(app, false);
            break;
        case AxonapotamusSubmenuIndexFuzz:
            axonapotamus_start_transmit(app, true);
            break;
        case AxonapotamusSubmenuIndexScan:
            popup_set_header(app->popup, "SCAN", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "Scanning for Axon devices\n(OUI 00:25:DF)\n\nNot yet implemented", 64, 40, AlignCenter, AlignCenter);
            view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewScan);
            break;
    }
}

static uint32_t axonapotamus_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t axonapotamus_submenu_back_callback(void* context) {
    Axonapotamus* app = context;
    axonapotamus_stop_transmit(app);
    return AxonapotamusViewSubmenu;
}

static Axonapotamus* axonapotamus_alloc(void) {
    Axonapotamus* app = malloc(sizeof(Axonapotamus));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    // Initialize state
    app->is_transmitting = false;
    app->is_fuzzing = false;
    app->is_scanning = false;
    app->fuzz_value = 0;
    memcpy(app->current_payload, BASE_PAYLOAD, PAYLOAD_SIZE);

    // Create fuzz timer
    app->fuzz_timer = furi_timer_alloc(axonapotamus_fuzz_timer_callback, FuriTimerTypePeriodic, app);

    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Submenu
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "Transmit (Single)", AxonapotamusSubmenuIndexTransmit, axonapotamus_submenu_callback, app);
    submenu_add_item(app->submenu, "Transmit (Fuzz)", AxonapotamusSubmenuIndexFuzz, axonapotamus_submenu_callback, app);
    submenu_add_item(app->submenu, "Scan for Axon", AxonapotamusSubmenuIndexScan, axonapotamus_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), axonapotamus_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewSubmenu, submenu_get_view(app->submenu));

    // Popup for transmit/scan views
    app->popup = popup_alloc();
    view_set_previous_callback(popup_get_view(app->popup), axonapotamus_submenu_back_callback);
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewTransmit, popup_get_view(app->popup));
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewScan, popup_get_view(app->popup));

    return app;
}

static void axonapotamus_free(Axonapotamus* app) {
    // Stop any active operations
    axonapotamus_stop_transmit(app);

    // Free timer
    furi_timer_free(app->fuzz_timer);

    // Remove and free views
    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewTransmit);
    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewScan);

    submenu_free(app->submenu);
    popup_free(app->popup);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t axonapotamus_app(void* p) {
    UNUSED(p);

    Axonapotamus* app = axonapotamus_alloc();

    FURI_LOG_I(TAG, "Axonapotamus started");

    view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewSubmenu);
    view_dispatcher_run(app->view_dispatcher);

    axonapotamus_free(app);

    FURI_LOG_I(TAG, "Axonapotamus stopped");

    return 0;
}
