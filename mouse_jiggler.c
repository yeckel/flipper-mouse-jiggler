#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <mouse_jiggler_icons.h>

#define APP_VERSION "1.1"
#define ERROR_MESSAGE_ROTATE_MS 5000

typedef enum {
    UsbStateSwitching,
    UsbStateActive,
    UsbStateError,
} UsbState;

typedef struct {
    UsbState state;
    bool show_unplug_message;
    uint32_t last_message_switch_tick;
} MouseJigglerContext;

static void mouse_jiggler_render_callback(Canvas* canvas, void* ctx) {
    MouseJigglerContext* app_ctx = ctx;
    canvas_clear(canvas);

    canvas_draw_icon(canvas, 0, 0, &I_mouse_jiggler);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 14, 9, "Mouse Jiggler v");
    canvas_draw_str(canvas, 94, 9, APP_VERSION);

    canvas_set_font(canvas, FontSecondary);

    switch(app_ctx->state) {
        case UsbStateSwitching:
            canvas_draw_str(canvas, 0, 33, "Switching to HID mode...");
            canvas_draw_str(canvas, 0, 43, "Please wait");
            break;
        case UsbStateError:
            if(app_ctx->show_unplug_message) {
                canvas_draw_str(canvas, 0, 33, "Try unplugging your Flipper");
                canvas_draw_str(canvas, 0, 43, "and plug it in again");
            } else {
                canvas_draw_str(canvas, 0, 33, "Switch to HID mode failed");
                canvas_draw_str(canvas, 0, 43, "Retrying...");
            }
            break;
        case UsbStateActive:
        default:
            canvas_draw_str(canvas, 0, 33, "GitHub.com/DavidBerdik/");
            canvas_draw_str(canvas, 0, 43, "flipper-mouse-jiggler");
    }

    canvas_draw_str(canvas, 0, 63, "Hold [back] to exit");
}

static void mouse_jiggler_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void mouse_jiggler_jiggle(void* ctx) {
    UNUSED(ctx);

    static short horizontal_travel_dist = 0;
    static short horizontal_movement_cycles = 0;
    static short horizontal_current_cycle = 0;
    static short vertical_travel_dist = 0;
    static short vertical_movement_cycles = 0;
    static short vertical_current_cycle = 0;

    if(horizontal_current_cycle >= horizontal_movement_cycles) {
        horizontal_travel_dist = furi_hal_random_get() % 3 - 1;
        horizontal_movement_cycles = furi_hal_random_get() % 1000 + 1;
        horizontal_current_cycle = 0;
    }

    if(vertical_current_cycle >= vertical_movement_cycles) {
        vertical_travel_dist = furi_hal_random_get() % 3 - 1;
        vertical_movement_cycles = furi_hal_random_get() % 1000 + 1;
        vertical_current_cycle = 0;
    }

    furi_hal_hid_mouse_move(horizontal_travel_dist, vertical_travel_dist);
    horizontal_current_cycle++;
    vertical_current_cycle++;
}

int32_t mouse_jiggler_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    furi_check(event_queue);

    ViewPort* view_port = view_port_alloc();

    FuriTimer* timer = furi_timer_alloc(mouse_jiggler_jiggle, FuriTimerTypePeriodic, event_queue);

    FuriHalUsbInterface* usb_mode_prev = furi_hal_usb_get_config();
    MouseJigglerContext app_ctx = {
        .state = UsbStateSwitching,
        .show_unplug_message = false,
        .last_message_switch_tick = 0,
    };

    view_port_draw_callback_set(view_port, mouse_jiggler_render_callback, &app_ctx);
    view_port_input_callback_set(view_port, mouse_jiggler_input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    bool usb_switch_success = false;

    InputEvent event;

    while(1) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, usb_switch_success ? FuriWaitForever : 100);
        
        if(event_status == FuriStatusOk && event.type == InputTypeLong && event.key == InputKeyBack) {
            break;
        }

        if(!usb_switch_success) {
            if(furi_hal_usb_set_config(&usb_hid, NULL)) {
                usb_switch_success = true;
                app_ctx.state = UsbStateActive;
                furi_timer_start(timer, 3);
            } else {
                if(app_ctx.state != UsbStateError) {
                    app_ctx.last_message_switch_tick = furi_get_tick();
                }
                app_ctx.state = UsbStateError;
                furi_delay_ms(200);
            }
        }

        if(app_ctx.state == UsbStateError) {
            uint32_t now = furi_get_tick();
            if((now - app_ctx.last_message_switch_tick) >= ERROR_MESSAGE_ROTATE_MS) {
                app_ctx.show_unplug_message = !app_ctx.show_unplug_message;
                app_ctx.last_message_switch_tick = now;
            }
        }

        view_port_update(view_port);
    }

    furi_hal_usb_set_config(usb_mode_prev, NULL);

    // remove & free all stuff created by app
    furi_timer_free(timer);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);

    return 0;
}