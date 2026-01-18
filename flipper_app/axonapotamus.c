#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/popup.h>
#include <notification/notification_messages.h>
#include <extra_beacon.h>

#include "axonapotamus_icons.h"

#define TAG "Axonapotamus"

// Service UUID 0xFE6C (little-endian for BLE)
#define AXON_SERVICE_UUID_LOW  0x6C
#define AXON_SERVICE_UUID_HIGH 0xFE

// Axon OUI prefix (00:25:DF)
#define AXON_OUI_0 0x00
#define AXON_OUI_1 0x25
#define AXON_OUI_2 0xDF

// Base payload (24 bytes)
static const uint8_t BASE_PAYLOAD[] = {
    0x01, 0x58, 0x38, 0x37, 0x30, 0x30, 0x32, 0x46,
    0x50, 0x34, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
    0xCE, 0x1B, 0x33, 0x00, 0x00, 0x02, 0x00, 0x00
};

#define PAYLOAD_SIZE 24
#define SEND_INTERVAL_MS 500
#define BEACON_DURATION_MS 30

typedef struct {
    uint16_t fuzz_value;
    uint8_t fuzz_bytes[4];
    bool is_running;
    bool fuzz_enabled;
} AxonapotamusModel;

typedef enum {
    AxonapotamusViewStartup,
    AxonapotamusViewMain,
} AxonapotamusViewId;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Popup* startup_popup;
    View* main_view;
    NotificationApp* notifications;

    FuriTimer* send_timer;

    uint8_t fuzz_payload[PAYLOAD_SIZE];
    uint8_t current_mac[6];
} Axonapotamus;

// Generate random MAC with Axon OUI prefix
static void axonapotamus_randomize_mac(Axonapotamus* app) {
    // Use Axon OUI (00:25:DF) for first 3 bytes
    app->current_mac[0] = AXON_OUI_0;
    app->current_mac[1] = AXON_OUI_1;
    app->current_mac[2] = AXON_OUI_2;

    // Random bytes for last 3 (device-specific portion)
    furi_hal_random_fill_buf(&app->current_mac[3], 3);
}

static void axonapotamus_send_single_packet(Axonapotamus* app, const uint8_t* payload) {
    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 20,
        .max_adv_interval_ms = 20,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypePublic,
        .address = {
            app->current_mac[0], app->current_mac[1], app->current_mac[2],
            app->current_mac[3], app->current_mac[4], app->current_mac[5]
        },
    };

    furi_hal_bt_extra_beacon_stop();

    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        return;
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

    memcpy(&adv_data[adv_len], payload, PAYLOAD_SIZE);
    adv_len += PAYLOAD_SIZE;

    if(!furi_hal_bt_extra_beacon_set_data(adv_data, adv_len)) {
        return;
    }

    if(!furi_hal_bt_extra_beacon_start()) {
        return;
    }

    // Brief broadcast window
    furi_delay_ms(BEACON_DURATION_MS);
    furi_hal_bt_extra_beacon_stop();
}

static void axonapotamus_send_timer_callback(void* context) {
    Axonapotamus* app = context;

    bool should_send = false;
    bool send_fuzz = false;

    with_view_model(
        app->main_view,
        AxonapotamusModel * model,
        {
            should_send = model->is_running;
            send_fuzz = model->fuzz_enabled;

            if(should_send && send_fuzz) {
                model->fuzz_value++;

                // Build fuzz payload
                memcpy(app->fuzz_payload, BASE_PAYLOAD, PAYLOAD_SIZE);
                app->fuzz_payload[10] = (model->fuzz_value >> 8) & 0xFF;
                app->fuzz_payload[11] = model->fuzz_value & 0xFF;
                app->fuzz_payload[20] = (model->fuzz_value >> 4) & 0xFF;
                app->fuzz_payload[21] = (model->fuzz_value << 4) & 0xFF;

                // Store for display
                model->fuzz_bytes[0] = app->fuzz_payload[10];
                model->fuzz_bytes[1] = app->fuzz_payload[11];
                model->fuzz_bytes[2] = app->fuzz_payload[20];
                model->fuzz_bytes[3] = app->fuzz_payload[21];
            }
        },
        send_fuzz);  // Redraw if fuzz enabled

    if(should_send) {
        // Send primary payload
        axonapotamus_send_single_packet(app, BASE_PAYLOAD);
        notification_message(app->notifications, &sequence_blink_cyan_10);

        // If fuzz enabled, also send fuzz payload
        if(send_fuzz) {
            axonapotamus_send_single_packet(app, app->fuzz_payload);
            notification_message(app->notifications, &sequence_blink_magenta_10);
        }
    }
}

static void axonapotamus_draw_callback(Canvas* canvas, void* model_ptr) {
    AxonapotamusModel* model = model_ptr;

    canvas_clear(canvas);

    // Title
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "AXONAPOTAMUS");

    // Divider line
    canvas_draw_line(canvas, 0, 14, 128, 14);

    // Status
    canvas_set_font(canvas, FontSecondary);
    if(model->is_running) {
        canvas_draw_str(canvas, 4, 26, "Status: RUNNING");
    } else {
        canvas_draw_str(canvas, 4, 26, "Status: STOPPED");
    }

    // Fuzz option
    canvas_draw_str(canvas, 4, 38, "Fuzz:");
    if(model->fuzz_enabled) {
        canvas_draw_box(canvas, 32, 30, 10, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 34, 38, "x");
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str(canvas, 46, 38, "ON");
    } else {
        canvas_draw_frame(canvas, 32, 30, 10, 10);
        canvas_draw_str(canvas, 46, 38, "OFF");
    }

    // Fuzz bytes display (only when running and fuzz enabled)
    if(model->is_running && model->fuzz_enabled) {
        char fuzz_str[32];
        snprintf(fuzz_str, sizeof(fuzz_str), "Fuzz: %02X %02X / %02X %02X",
                 model->fuzz_bytes[0], model->fuzz_bytes[1],
                 model->fuzz_bytes[2], model->fuzz_bytes[3]);
        canvas_draw_str(canvas, 4, 50, fuzz_str);
    }

    // Instructions
    canvas_set_font(canvas, FontSecondary);
    if(model->is_running) {
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Stop  [< >] Fuzz");
    } else {
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Start  [< >] Fuzz");
    }
}

static bool axonapotamus_input_callback(InputEvent* event, void* context) {
    Axonapotamus* app = context;

    if(event->type != InputTypePress && event->type != InputTypeRepeat) {
        return false;
    }

    if(event->key == InputKeyBack) {
        with_view_model(
            app->main_view,
            AxonapotamusModel * model,
            {
                if(model->is_running) {
                    model->is_running = false;
                    furi_timer_stop(app->send_timer);
                    furi_hal_bt_extra_beacon_stop();
                    notification_message(app->notifications, &sequence_reset_rgb);
                }
            },
            false);
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }

    with_view_model(
        app->main_view,
        AxonapotamusModel * model,
        {
            switch(event->key) {
                case InputKeyOk:
                    if(model->is_running) {
                        // Stop
                        model->is_running = false;
                        furi_timer_stop(app->send_timer);
                        furi_hal_bt_extra_beacon_stop();
                        notification_message(app->notifications, &sequence_reset_rgb);
                    } else {
                        // Start - randomize MAC for this session
                        axonapotamus_randomize_mac(app);
                        model->is_running = true;
                        model->fuzz_value = 0;
                        notification_message(app->notifications, &sequence_blink_cyan_100);
                        furi_timer_start(app->send_timer, furi_ms_to_ticks(SEND_INTERVAL_MS));
                    }
                    break;

                case InputKeyLeft:
                    model->fuzz_enabled = false;
                    break;

                case InputKeyRight:
                    if(!model->fuzz_enabled) {
                        model->fuzz_enabled = true;
                        notification_message(app->notifications, &sequence_single_vibro);
                    }
                    break;

                default:
                    break;
            }
        },
        true);

    return true;
}

static void axonapotamus_startup_popup_callback(void* context) {
    Axonapotamus* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewMain);
}

static Axonapotamus* axonapotamus_alloc(void) {
    Axonapotamus* app = malloc(sizeof(Axonapotamus));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    memcpy(app->fuzz_payload, BASE_PAYLOAD, PAYLOAD_SIZE);

    // Initialize with random MAC
    axonapotamus_randomize_mac(app);

    // Create timer
    app->send_timer = furi_timer_alloc(axonapotamus_send_timer_callback, FuriTimerTypePeriodic, app);

    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Startup popup with dolphin
    app->startup_popup = popup_alloc();
    popup_set_icon(app->startup_popup, 48, 0, &I_DolphinDone_80x58);
    popup_set_header(app->startup_popup, "This is best app.", 0, 2, AlignLeft, AlignTop);
    popup_set_text(app->startup_popup, "Clearly superior.", 0, 52, AlignLeft, AlignTop);
    popup_set_timeout(app->startup_popup, 2000);
    popup_set_context(app->startup_popup, app);
    popup_set_callback(app->startup_popup, axonapotamus_startup_popup_callback);
    popup_enable_timeout(app->startup_popup);
    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewStartup, popup_get_view(app->startup_popup));

    // Main view
    app->main_view = view_alloc();
    view_allocate_model(app->main_view, ViewModelTypeLocking, sizeof(AxonapotamusModel));
    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, axonapotamus_draw_callback);
    view_set_input_callback(app->main_view, axonapotamus_input_callback);

    // Initialize model
    with_view_model(
        app->main_view,
        AxonapotamusModel * model,
        {
            model->is_running = false;
            model->fuzz_enabled = false;
            model->fuzz_value = 0;
            memset(model->fuzz_bytes, 0, sizeof(model->fuzz_bytes));
        },
        true);

    view_dispatcher_add_view(app->view_dispatcher, AxonapotamusViewMain, app->main_view);

    return app;
}

static void axonapotamus_free(Axonapotamus* app) {
    furi_timer_stop(app->send_timer);
    furi_hal_bt_extra_beacon_stop();
    furi_timer_free(app->send_timer);

    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewStartup);
    popup_free(app->startup_popup);

    view_dispatcher_remove_view(app->view_dispatcher, AxonapotamusViewMain);
    view_free(app->main_view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t axonapotamus_app(void* p) {
    UNUSED(p);

    Axonapotamus* app = axonapotamus_alloc();

    view_dispatcher_switch_to_view(app->view_dispatcher, AxonapotamusViewStartup);
    view_dispatcher_run(app->view_dispatcher);

    axonapotamus_free(app);

    return 0;
}
