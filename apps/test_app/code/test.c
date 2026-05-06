#include <enDjinn/enj_enDjinn.h>
#include "dc_link.h"
#include <stdio.h>

#define MARGIN_LEFT (20 * ENJ_XSCALE)

static uint32_t frame_count = 0;
static char status_text[64] = "Waiting for connection...";
static char remote_text[64] = "Remote Data: N/A";

// Example payload structure
typedef struct {
    uint32_t buttons;
    int x, y;
} game_input_t;

static game_input_t local_input = {0, 0, 0};
static game_input_t remote_input = {0, 0, 0};

// Handle Piggybacked Event Messages
void on_link_message(uint8_t type, const void* data, uint16_t len) {
    if (type == 0x01 && len == sizeof(int)) {
        int val = *(int*)data;
        snprintf(remote_text, sizeof(remote_text), "Remote Event: %d", val);
    }
}

// Draw the screen text using enDjinn's qfont
void render_PT(void *__unused) {
    enj_font_scale_set(3);
    enj_qfont_color_set(255, 255, 255);
    enj_qfont_write("DreamLink Test", MARGIN_LEFT, 40, PVR_LIST_PT_POLY);
    
    enj_font_scale_set(2);
    enj_qfont_color_set(255, 200, 50);
    enj_qfont_write(status_text, MARGIN_LEFT, 100, PVR_LIST_PT_POLY);
    
    enj_font_scale_set(1.5f);
    enj_qfont_color_set(0, 255, 0);
    char local_str[64];
    snprintf(local_str, sizeof(local_str), "Local: X:%d Y:%d Btn:%08x", local_input.x, local_input.y, local_input.buttons);
    enj_qfont_write(local_str, MARGIN_LEFT, 160, PVR_LIST_PT_POLY);

    enj_qfont_color_set(255, 100, 100);
    char remote_str[64];
    snprintf(remote_str, sizeof(remote_str), "Link: X:%d Y:%d Btn:%08x", remote_input.x, remote_input.y, remote_input.buttons);
    enj_qfont_write(remote_str, MARGIN_LEFT, 190, PVR_LIST_PT_POLY);

    enj_qfont_color_set(100, 200, 255);
    enj_qfont_write(remote_text, MARGIN_LEFT, 230, PVR_LIST_PT_POLY);

    enj_qfont_color_set(150, 150, 150);
    char frame_str[64];
    snprintf(frame_str, sizeof(frame_str), "Sync Frame: %lu", frame_count);
    enj_qfont_write(frame_str, MARGIN_LEFT, 300, PVR_LIST_PT_POLY);
}

// Main logic
void main_mode_updater(void *__unused) {
    // 1. Gather Input
    enj_ctrlr_state_t **ctrls = enj_ctrl_get_states();
    if (ctrls[0]) {
        local_input.buttons = ctrls[0]->state.buttons;
        if (ctrls[0]->button.LEFT == ENJ_BUTTON_DOWN) local_input.x--;
        if (ctrls[0]->button.RIGHT == ENJ_BUTTON_DOWN) local_input.x++;
        if (ctrls[0]->button.UP == ENJ_BUTTON_DOWN) local_input.y--;
        if (ctrls[0]->button.DOWN == ENJ_BUTTON_DOWN) local_input.y++;

        // Send a piggybacked event on A press
        if (ctrls[0]->button.A == ENJ_BUTTON_DOWN_THIS_FRAME) {
            int event_val = frame_count % 100;
            dc_link_enqueue_message(0x01, &event_val, sizeof(event_val));
        }
    }

    // 2. Perform Link Sync Step
    if (dc_link_get_state() == LINK_STATE_CONNECTED) {
        uint16_t out_len = 0;
        int status = dc_link_sync_frame(frame_count, &local_input, sizeof(local_input), &remote_input, &out_len, 100);
        
        if (status == 0) {
            snprintf(status_text, sizeof(status_text), "LINK SYNCED (Frame %lu)", frame_count);
            frame_count++;
        } else {
            snprintf(status_text, sizeof(status_text), "LINK SYNC LOST! (Err %d)", status);
            dc_link_init(500000); // Try to reconnect
            dc_link_handshake(10);
        }
    } else {
        // Try doing a quick handshake inside the update loop so we don't totally freeze
        if (dc_link_handshake(16)) { // small timeout
            snprintf(status_text, sizeof(status_text), "HANDSHAKE SUCCEEDED!");
            frame_count = 0;
        } else {
            snprintf(status_text, sizeof(status_text), "SEARCHING FOR CONSOLE...");
        }
    }

    // 3. Queue Render Call
    enj_render_list_add(PVR_LIST_PT_POLY, render_PT, NULL);
}

int main(__unused int argc, __unused char **argv) {
    enj_state_init_defaults();
    if (enj_state_startup() != 0) return -1;

    // Initialize serial connection early
    dc_link_init(500000);
    dc_link_set_message_callback(on_link_message);

    enj_mode_t main_mode = {
        .name = "DreamLink Test",
        .mode_updater = main_mode_updater,
        .data = NULL,
    };
    
    enj_mode_push(&main_mode);
    enj_state_run();
    
    return 0;
}
