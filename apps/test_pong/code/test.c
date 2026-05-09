#include <dreamlink/dreamlink.h>
#include <enDjinn/enj_enDjinn.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define WORLD_W (SCREEN_W * 2)
#define PADDLE_W 24
#define PADDLE_H 120
#define BALL_SIZE 16

typedef enum {
  ROLE_UNDECIDED,
  ROLE_LEFT,   // Player 1 (Physics Master)
  ROLE_RIGHT   // Player 2
} pong_role_t;

typedef struct {
  float ball_x, ball_y;
  float ball_dx, ball_dy;
  float p1_y, p2_y;
  int score1, score2;
} pong_state_t;

typedef struct {
  float paddle_y;
  uint32_t buttons;
} pong_input_t;

static pong_role_t my_role = ROLE_UNDECIDED;
static pong_state_t game_state;
static uint32_t frame_count = 0;
static char status_msg[64] = "Waiting for connection...";
static pvr_sprite_hdr_t sprite_hdr;

// Forward declarations for modes
static enj_mode_t pairing_mode;
static enj_mode_t gameplay_mode;

void reset_ball() {
  game_state.ball_x = (WORLD_W - BALL_SIZE) / 2.0f;
  game_state.ball_y = (SCREEN_H - BALL_SIZE) / 2.0f;
  game_state.ball_dx = 4.0f;
  game_state.ball_dy = 4.0f;
}

void init_pong() {
  memset(&game_state, 0, sizeof(game_state));
  game_state.p1_y = (SCREEN_H - PADDLE_H) / 2.0f;
  game_state.p2_y = (SCREEN_H - PADDLE_H) / 2.0f;
  reset_ball();
}

void update_physics() {
  game_state.ball_x += game_state.ball_dx;
  game_state.ball_y += game_state.ball_dy;

  if (game_state.ball_y <= 0 || game_state.ball_y >= SCREEN_H - BALL_SIZE) {
    game_state.ball_dy = -game_state.ball_dy;
  }

  if (game_state.ball_x <= 0) {
    game_state.score2++;
    reset_ball();
  } else if (game_state.ball_x >= WORLD_W - BALL_SIZE) {
    game_state.score1++;
    reset_ball();
  }

  // P1 Paddle (Left)
  if (game_state.ball_x <= PADDLE_W &&
      game_state.ball_y + BALL_SIZE >= game_state.p1_y &&
      game_state.ball_y <= game_state.p1_y + PADDLE_H) {
    game_state.ball_dx = -game_state.ball_dx;
    game_state.ball_x = (float)PADDLE_W + 0.1f;
  }
  // P2 Paddle (Right)
  if (game_state.ball_x + BALL_SIZE >= WORLD_W - PADDLE_W &&
      game_state.ball_y + BALL_SIZE >= game_state.p2_y &&
      game_state.ball_y <= game_state.p2_y + PADDLE_H) {
    game_state.ball_dx = -game_state.ball_dx;
    game_state.ball_x =
        (float)WORLD_W - (float)PADDLE_W - (float)BALL_SIZE - 0.1f;
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

// --- GAMEPLAY MODE ---

void render_gameplay(void* __unused) {
  enj_qfont_color_set(255, 255, 255);
  enj_font_scale_set(2.0f);
  enj_qfont_write("DreamLink PONG", 20, 20, PVR_LIST_PT_POLY);

  char score_str[32];
  snprintf(score_str, sizeof(score_str), "%d - %d", game_state.score1,
           game_state.score2);
  enj_qfont_write(score_str, SCREEN_W - 150, 20, PVR_LIST_PT_POLY);

  enj_font_scale_set(1.0f);
  enj_qfont_write(status_msg, 20, SCREEN_H - 40, PVR_LIST_PT_POLY);

  float render_offset_x = (my_role == ROLE_RIGHT) ? (float)SCREEN_W : 0.0f;

  float p1_x = 5.0f - render_offset_x;
  if (p1_x > -(float)PADDLE_W && p1_x < (float)SCREEN_W) {
    draw_rect(p1_x, game_state.p1_y, (float)PADDLE_W, (float)PADDLE_H,
              0xFF00FF00); // Green
  }

  float p2_x = ((float)WORLD_W - (float)PADDLE_W - 5.0f) - render_offset_x;
  if (p2_x > -(float)PADDLE_W && p2_x < (float)SCREEN_W) {
    draw_rect(p2_x, game_state.p2_y, (float)PADDLE_W, (float)PADDLE_H,
              0xFFFF0000); // Red
  }

  float ball_rx = game_state.ball_x - render_offset_x;
  if (ball_rx > -(float)BALL_SIZE && ball_rx < (float)SCREEN_W) {
    draw_rect(ball_rx, game_state.ball_y, (float)BALL_SIZE, (float)BALL_SIZE,
              0xFFFFFF00); // Yellow
  }
}

void gameplay_updater(void* __unused) {
  pong_input_t local_in = {0};
  enj_ctrlr_state_t** ctrls = enj_ctrl_get_states();

  if (dc_link_get_state() != LINK_STATE_CONNECTED) {
    // If we lose connection, pop back to pairing
    my_role = ROLE_UNDECIDED;
    enj_mode_pop();
    return;
  }

  if (my_role == ROLE_LEFT) {
    pong_input_t remote_in = {0};
    uint16_t out_len;
    dc_link_sync_frame(frame_count, &game_state, sizeof(game_state),
                       &remote_in, &out_len, 100);

    if (ctrls[0]) {
      if (ctrls[0]->button.UP == ENJ_BUTTON_DOWN) game_state.p1_y -= 8.0f;
      if (ctrls[0]->button.DOWN == ENJ_BUTTON_DOWN) game_state.p1_y += 8.0f;
    }
    if (game_state.p1_y < 0) game_state.p1_y = 0;
    if (game_state.p1_y > SCREEN_H - PADDLE_H) game_state.p1_y = SCREEN_H - PADDLE_H;

    game_state.p2_y = remote_in.paddle_y;
    update_physics();
    snprintf(status_msg, sizeof(status_msg), "Connected - Role: LEFT");
  } else {
    static float local_p2_y = (SCREEN_H - PADDLE_H) / 2.0f;
    if (ctrls[0]) {
      if (ctrls[0]->button.UP == ENJ_BUTTON_DOWN) local_p2_y -= 8.0f;
      if (ctrls[0]->button.DOWN == ENJ_BUTTON_DOWN) local_p2_y += 8.0f;
    }
    if (local_p2_y < 0) local_p2_y = 0;
    if (local_p2_y > SCREEN_H - PADDLE_H) local_p2_y = SCREEN_H - PADDLE_H;
    local_in.paddle_y = local_p2_y;

    uint16_t out_len;
    dc_link_sync_frame(frame_count, &local_in, sizeof(local_in),
                       &game_state, &out_len, 100);
    snprintf(status_msg, sizeof(status_msg), "Connected - Role: RIGHT");
  }
  frame_count++;

  enj_render_list_add(PVR_LIST_PT_POLY, render_gameplay, NULL);
}

static enj_mode_t gameplay_mode = {
    .name = "Pong Gameplay",
    .mode_updater = gameplay_updater,
};

// --- PAIRING MODE ---

void render_pairing(void* __unused) {
  enj_qfont_color_set(255, 255, 255);
  enj_font_scale_set(2.0f);
  enj_qfont_write("PONG - PAIRING", 120, 150, PVR_LIST_PT_POLY);

  enj_font_scale_set(1.0f);
  enj_qfont_write("A: Join as LEFT (P1)", 180, 220, PVR_LIST_PT_POLY);
  enj_qfont_write("B: Join as RIGHT (P2)", 180, 250, PVR_LIST_PT_POLY);

  enj_qfont_write(status_msg, 20, SCREEN_H - 40, PVR_LIST_PT_POLY);
}

void pairing_updater(void* __unused) {
  enj_ctrlr_state_t** ctrls = enj_ctrl_get_states();

  if (ctrls[0]) {
    if (my_role == ROLE_UNDECIDED) {
      if (ctrls[0]->button.A == ENJ_BUTTON_DOWN_THIS_FRAME) {
        my_role = ROLE_LEFT;
        init_pong();
        dc_link_init(500000);
      } else if (ctrls[0]->button.B == ENJ_BUTTON_DOWN_THIS_FRAME) {
        my_role = ROLE_RIGHT;
        dc_link_init(500000);
      }
    }
  }

  if (my_role != ROLE_UNDECIDED) {
    if (dc_link_get_state() == LINK_STATE_CONNECTED) {
      // Transition to gameplay
      frame_count = 0;
      enj_mode_push(&gameplay_mode);
    } else {
      dc_link_handshake(16);
      snprintf(status_msg, sizeof(status_msg), "Waiting for remote console...");
    }
  }

  enj_render_list_add(PVR_LIST_PT_POLY, render_pairing, NULL);
}

static enj_mode_t pairing_mode = {
    .name = "Pong Pairing",
    .mode_updater = pairing_updater,
};

int main(int argc, char** argv) {
  enj_state_init_defaults();
  if (enj_state_startup() != 0) return -1;

  pvr_sprite_cxt_t cxt;
  pvr_sprite_cxt_col(&cxt, PVR_LIST_PT_POLY);
  pvr_sprite_compile(&sprite_hdr, &cxt);

  enj_mode_push(&pairing_mode);
  enj_state_run();
  return 0;
}
