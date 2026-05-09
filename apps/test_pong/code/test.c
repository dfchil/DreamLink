#include <dreamlink/dreamlink.h>
#include <enDjinn/enj_enDjinn.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define WORLD_H (SCREEN_H * 2)
#define PADDLE_W 120
#define PADDLE_H 24
#define BALL_SIZE 16

typedef enum {
  ROLE_UNDECIDED,
  ROLE_TOP,    // Player 1 (Physics Master)
  ROLE_BOTTOM  // Player 2
} pong_role_t;

typedef struct {
  float ball_x, ball_y;
  float ball_dx, ball_dy;
  float p1_x, p2_x;
  int score1, score2;
} pong_state_t;

typedef struct {
  float paddle_x;
  uint32_t buttons;
} pong_input_t;

static pong_role_t my_role = ROLE_UNDECIDED;
static pong_state_t game_state;
static uint32_t frame_count = 0;
static pvr_sprite_hdr_t sprite_hdr;
static char status_msg[64] = "Press A (TOP) or B (BOTTOM)";

void reset_ball() {
  game_state.ball_x = (SCREEN_W - BALL_SIZE) / 2.0f;
  game_state.ball_y = WORLD_H / 2.0f;
  game_state.ball_dx = 4.0f;
  game_state.ball_dy = 4.0f;
}

void init_pong() {
  memset(&game_state, 0, sizeof(game_state));
  game_state.p1_x = SCREEN_W / 2.0f - PADDLE_W / 2.0f;
  game_state.p2_x = SCREEN_W / 2.0f - PADDLE_W / 2.0f;
  reset_ball();
}

void update_physics() {
  game_state.ball_x += game_state.ball_dx;
  game_state.ball_y += game_state.ball_dy;

  if (game_state.ball_x <= 0 || game_state.ball_x >= SCREEN_W - BALL_SIZE) {
    game_state.ball_dx = -game_state.ball_dx;
  }

  if (game_state.ball_y <= 0) {
    game_state.score2++;
    reset_ball();
  } else if (game_state.ball_y >= WORLD_H - BALL_SIZE) {
    game_state.score1++;
    reset_ball();
  }

  if (game_state.ball_y <= PADDLE_H &&
      game_state.ball_x + BALL_SIZE >= game_state.p1_x &&
      game_state.ball_x <= game_state.p1_x + PADDLE_W) {
    game_state.ball_dy = -game_state.ball_dy;
    game_state.ball_y = (float)PADDLE_H + 1.0f;
  }
  if (game_state.ball_y + BALL_SIZE >= WORLD_H - PADDLE_H &&
      game_state.ball_x + BALL_SIZE >= game_state.p2_x &&
      game_state.ball_x <= game_state.p2_x + PADDLE_W) {
    game_state.ball_dy = -game_state.ball_dy;
    game_state.ball_y =
        (float)WORLD_H - (float)PADDLE_H - (float)BALL_SIZE - 1.0f;
  }
}

void draw_rect(float x, float y, float w, float h, uint32_t color) {
  float corners[4][3] = {
      {x, y, 1.0f},         // Top-left
      {x, y + h, 1.0f},     // Bottom-left
      {x + w, y + h, 1.0f}, // Bottom-right
      {x + w, y, 1.0f}      // Top-right
  };
  sprite_hdr.argb = color;
  enj_draw_sprite(corners, &sprite_hdr, NULL);
}

void render_pong(void* __unused) {
  enj_qfont_color_set(255, 255, 255);
  enj_font_scale_set(2.0f);
  enj_qfont_write("DreamLink PONG", 20, 20, PVR_LIST_PT_POLY);

  char score_str[32];
  snprintf(score_str, sizeof(score_str), "%d - %d", game_state.score1,
           game_state.score2);
  enj_qfont_write(score_str, SCREEN_W - 150, 20, PVR_LIST_PT_POLY);

  enj_font_scale_set(1.0f);
  enj_qfont_write(status_msg, 20, SCREEN_H - 40, PVR_LIST_PT_POLY);

  if (my_role == ROLE_UNDECIDED) return;

  float render_offset_y = (my_role == ROLE_BOTTOM) ? (float)SCREEN_H : 0.0f;

  float p1_y = 5.0f - render_offset_y;
  if (p1_y > -(float)PADDLE_H && p1_y < (float)SCREEN_H) {
    draw_rect(game_state.p1_x, p1_y, (float)PADDLE_W, (float)PADDLE_H,
              0xFF00FF00); // Green
  }

  float p2_y = ((float)WORLD_H - (float)PADDLE_H - 5.0f) - render_offset_y;
  if (p2_y > -(float)PADDLE_H && p2_y < (float)SCREEN_H) {
    draw_rect(game_state.p2_x, p2_y, (float)PADDLE_W, (float)PADDLE_H,
              0xFFFF0000); // Red
  }

  float ball_ry = game_state.ball_y - render_offset_y;
  if (ball_ry > -(float)BALL_SIZE && ball_ry < (float)SCREEN_H) {
    draw_rect(game_state.ball_x, ball_ry, (float)BALL_SIZE, (float)BALL_SIZE,
              0xFFFFFF00); // Yellow
  }
}

void main_mode_updater(void* __unused) {
  pong_input_t local_in = {0};
  enj_ctrlr_state_t** ctrls = enj_ctrl_get_states();

  if (ctrls[0]) {
    local_in.buttons = ctrls[0]->button.raw;
    if (my_role == ROLE_UNDECIDED) {
      if (ctrls[0]->button.A == ENJ_BUTTON_DOWN_THIS_FRAME) {
        my_role = ROLE_TOP;
        init_pong();
        dc_link_init(500000);
      } else if (ctrls[0]->button.B == ENJ_BUTTON_DOWN_THIS_FRAME) {
        my_role = ROLE_BOTTOM;
        dc_link_init(500000);
      }
    }
  }

  if (my_role != ROLE_UNDECIDED) {
    if (dc_link_get_state() == LINK_STATE_CONNECTED) {
      if (my_role == ROLE_TOP) {
        pong_input_t remote_in = {0};
        uint16_t out_len;
        dc_link_sync_frame(frame_count, &game_state, sizeof(game_state),
                           &remote_in, &out_len, 100);

        if (ctrls[0]) {
          if (ctrls[0]->button.LEFT == ENJ_BUTTON_DOWN) game_state.p1_x -= 8.0f;
          if (ctrls[0]->button.RIGHT == ENJ_BUTTON_DOWN)
            game_state.p1_x += 8.0f;
        }
        if (game_state.p1_x < 0) game_state.p1_x = 0;
        if (game_state.p1_x > SCREEN_W - PADDLE_W)
          game_state.p1_x = SCREEN_W - PADDLE_W;

        game_state.p2_x = remote_in.paddle_x;
        update_physics();
        snprintf(status_msg, sizeof(status_msg), "Connected - Role: TOP");
      } else {
        static float local_p2_x = SCREEN_W / 2.0f;
        if (ctrls[0]) {
          if (ctrls[0]->button.LEFT == ENJ_BUTTON_DOWN) local_p2_x -= 8.0f;
          if (ctrls[0]->button.RIGHT == ENJ_BUTTON_DOWN) local_p2_x += 8.0f;
        }
        if (local_p2_x < 0) local_p2_x = 0;
        if (local_p2_x > SCREEN_W - PADDLE_W) local_p2_x = SCREEN_W - PADDLE_W;
        local_in.paddle_x = local_p2_x;

        uint16_t out_len;
        dc_link_sync_frame(frame_count, &local_in, sizeof(local_in),
                           &game_state, &out_len, 100);
        snprintf(status_msg, sizeof(status_msg), "Connected - Role: BOTTOM");
      }
      frame_count++;
    } else {
      if (dc_link_handshake(16)) {
        frame_count = 0;
      }
      snprintf(status_msg, sizeof(status_msg), "Waiting for other console...");
    }
  }

  enj_render_list_add(PVR_LIST_PT_POLY, render_pong, NULL);
}

static enj_mode_t main_mode = {
    .name = "DreamLink PONG",
    .mode_updater = main_mode_updater,
    .data = NULL,
};

int main(int argc, char** argv) {
  enj_state_init_defaults();
  if (enj_state_startup() != 0) return -1;

  // Initialize PVR header for sprites (non-textured, solid color)
  pvr_sprite_cxt_t cxt;
  pvr_sprite_cxt_col(&cxt, PVR_LIST_PT_POLY);
  pvr_sprite_compile(&sprite_hdr, &cxt);

  enj_mode_push(&main_mode);
  enj_state_run();
  return 0;
}
