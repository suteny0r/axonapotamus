#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_version.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>
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

// Scan info text (scrollable)
// ~21 chars per line on Flipper screen
static const char* SCAN_INFO_TEXT =
    "SCAN NOT AVAILABLE\n"
    "\n"
    "BLE scanning is not\n"
    "supported in Flipper\n"
    "stock firmware. Only\n"
    "advertising APIs are\n"
    "exposed, not scanner.\n"
    "\n"
    "To find Axon devices:\n"
    "\n"
    "1. Use nRF Connect or\n"
    "LightBlue app on your\n"
    "phone to scan BLE.\n"
    "\n"
    "2. Look for MAC addr\n"
    "starting with:\n"
    "00:25:DF (Axon OUI)\n"
    "\n"
    "3. Axon cameras use\n"
    "Service UUID: 0xFE6C\n";

typedef enum {
    AxonapotamusViewSubmenu,
    AxonapotamusViewPopup,
    AxonapotamusViewTextBox,
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
    TextBox* text_box;
    NotificationApp* notifications;

    FuriTimer* fuzz_timer;
    FuriTimer* single_blink_timer;
    uint16_t fuzz_value;
    uint8_t current_payload[PAYLOAD_SIZE];

    bool is_transmitting;
    bool is_fuzzing;
    bool is_on_popup;
} Axonapotamus;

// Forward declarations
static void axonapotamus_stop_transmit(Axonapotamus* app);
static void axonapotamus_single_blink_callback(void* context);

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
    // Use Flipper's real BLE MAC address
    const uint8_t* mac = furi_hal_version_get_ble_mac();

    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 50,
        .max_adv_interval_ms = 150,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypePublic,
        .address = {mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]},
    };

    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        FURI_LOG_E(TAG, "Failed to set beacon config");
        return false;
    }

    uint8_t adv_data[31];
    uint8_t adv_len = 0;

    // Flags
    adv_data[adv_len++] = 0x02;
    adv_data[adv_len++] = 0x01;
    adv_data[adv_len++] = 0x06;

    // Service Data (UUID 0xFE6C + payload)
    uint8_t service_data_len = 1 + 2 + PAYLOAD_SIZE;
    adv_data[adv_len++] = service_data_len;
    adv_data[adv_len++] = 0x16;
    adv_data[adv_len++] = AXON_SERVICE_UUID_LOW;
    adv_data[adv_len++] = AXON_SERVICE_UUID_HIGH;

    memcpy(&adv_data[adv_len], app->current_payload, PAYLOAD_SIZE);
    adv_len += PAYLOAD_SIZE;

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

static void axonapotamus_stop_advertising(void) {
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

        if(fuzz_mode) {
            notification_message(app->notifications, &sequence_blink_magenta_100);
            furi_timer_start(app->fuzz_timer, furi_ms_to_ticks(FUZZ_INTERVAL_MS));
            popup_set_header(app->popup, "FUZZ TX ACTIVE", 64, 20, AlignCenter, AlignCenter);
        } else {
            notification_message(app->notifications, &sequence_blink_cyan_100);
            furi_timer_start(app->single_blink_timer, furi_ms_to_ticks(FUZZ_INTERVAL_MS));
            popup_set_header(app->popup, "TX ACTIVE", 64, 20, AlignCenter, AlignCenter);
        }
        popup_set_text(app->popup, "Broadcasting Axon Signal\nPress Back to stop", 64, 40, AlignCenter, AlignCenter);
    } else {
        popup_set_header(app->popup, "TX FAILED", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(app->popup, "Could not start BLE beacon", 64, 40, AlignCenter, AlignCenter);
    }

    app->is_on_popup = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewPopup);
}

static void axonapotamus_stop_transmit(Axonapotamus* app) {
    furi_timer_stop(app->fuzz_timer);
    furi_timer_stop(app->single_blink_timer);

    if(app->is_fuzzing) {
        app->is_fuzzing = false;
    }

    if(app->is_transmitting) {
        axonapotamus_stop_advertising();
        app->is_transmitting = false;
        notification_message(app->notifications, &sequence_reset_rgb);
    }
}

static void axonapotamus_fuzz_timer_callback(void* context) {
    Axonapotamus* app = context;

    if(!app->is_transmitting || !app->is_fuzzing) return;

    furi_hal_bt_extra_beacon_stop();

    app->fuzz_value++;

    axonapotamus_update_payload_with_fuzz(app);

    axonapotamus_start_advertising(app);

    notification_message(app->notifications, &sequence_blink_magenta_10);

    FURI_LOG_D(TAG, "Fuzz value: 0x%04X", app->fuzz_value);
}

static void axonapotamus_single_blink_callback(void* context) {
    Axonapotamus* app = context;

    if(!app->is_transmitting || app->is_fuzzing) return;

    notification_message(app->notifications, &sequence_blink_cyan_10);
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
            text_box_set_text(app->text_box, SCAN_INFO_TEXT);
            text_box_set_focus(app->text_box, TextBoxFocusStart);
            app->is_on_popup = true;
            view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewTextBox);
            break;
    }
}

static bool axonapotamus_navigation_callback(void* context) {
    Axonapotamus* app = context;

    // If we're on popup or text_box view, go back to submenu
    if(app->is_on_popup) {
        axonapotamus_stop_transmit(app);
        app->is_on_popup = false;
        view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewSubmenu);
        return true;  // We handled it
    }

    // On submenu, let default behavior happen (exit app)
    return false;
}

static bool axonapotamus_custom_callback(void* context, uint32_t event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static Axonapotamus* axonapotamus_alloc(void) {
    Axonapotamus* app = malloc(sizeof(Axonapotamus));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    // Initialize state
    app->is_transmitting = false;
    app->is_fuzzing = false;
    app->is_on_popup = false;
    app->fuzz_value = 0;
    memcpy(app->current_payload, BASE_PAYLOAD, PAYLOAD_SIZE);

    // Create timers
    app->fuzz_timer = furi_timer_alloc(axonapotamus_fuzz_timer_callback, FuriTimerTypePeriodic, app);
    app->single_blink_timer = furi_timer_alloc(axonapotamus_single_blink_callback, FuriTimerTypePeriodic, app);

    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, axonapotamus_navigation_callback);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, axonapotamus_custom_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Submenu
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "Transmit (Single)", AxonapotamusSubmenuIndexTransmit, axonapotamus_submenu_callback, app);
    submenu_add_item(app->submenu, "Transmit (Fuzz)", AxonapotamusSubmenuIndexFuzz, axonapotamus_submenu_callback, app);
    submenu_add_item(app->submenu, "Scan for Axon", AxonapotamusSubmenuIndexScan, axonapotamus_submenu_callback, app);
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewSubmenu, submenu_get_view(app->submenu));

    // Popup (for TX status)
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewPopup, popup_get_view(app->popup));

    // TextBox (for scrollable scan info)
    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewTextBox, text_box_get_view(app->text_box));

    return app;
}

static void axonapotamus_free(Axonapotamus* app) {
    axonapotamus_stop_transmit(app);

    furi_timer_free(app->fuzz_timer);
    furi_timer_free(app->single_blink_timer);

    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewTextBox);

    submenu_free(app->submenu);
    popup_free(app->popup);
    text_box_free(app->text_box);
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
