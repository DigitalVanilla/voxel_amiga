#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sage/sage.h>

#include "voxel_log.h"
#include "voxel_math.h"
#include "voxel_renderer.h"

#define logMessage VoxelLog

// screen definition
#define SCREEN_WIDTH							320
#define SCREEN_HEIGHT   					200
#define SCREEN_DEPTH    					8

// main layer
#define MAIN_LAYER 								0
#define MAIN_LAYER_WIDTH					200
#define MAIN_LAYER_HEIGHT					120

#define MAP_N                     1024
float Z_FAR = 600.0;
float SCALE_FACTOR = 70.0;
float START_DELTA_Z = 0.01;
int HORIZONTAL_DIVISIONS = 1;

#define HEIGHT_MAP_FILENAME	      "maps/map%d.height.raw"
#define COLOR_MAP_FILENAME	      "maps/map%d.color.raw"
#define MAP_PALETTE_FILENAME       "maps/map%d.palette.raw"
#define MAP_DATA_SIZE              ((ULONG)MAP_N * (ULONG)MAP_N)
#define MAP_PALETTE_SIZE           (SSCR_MAXCOLORS * 3)
int file_map_index = 0;

#define KEY_COUNT                    128

#define PLAYER_START_X               512.0f
#define PLAYER_START_Y               512.0f
#define PLAYER_START_HEIGHT          80.0f
#define PLAYER_MIN_HEIGHT            10.0f
#define PLAYER_MAX_HEIGHT            300.0f
#define PLAYER_HEIGHT_STEP           5.0f
#define PLAYER_FORWARD_ACCEL         0.06f
#define PLAYER_FORWARD_BRAKE         0.10f
#define PLAYER_FORWARD_MAX           3.0f
#define PLAYER_FORWARD_SCALE         0.90f
#define PLAYER_PITCH_ACCEL           0.16f
#define PLAYER_PITCH_BRAKE           0.06f
#define PLAYER_PITCH_MAX             2.0f
#define PLAYER_PITCH_BASE            80.0f
#define PLAYER_PITCH_SCALE           20.0f
#define PLAYER_YAW_ACCEL             0.10f
#define PLAYER_YAW_BRAKE             0.10f
#define PLAYER_YAW_MAX               1.0f
#define PLAYER_YAW_SCALE             0.02f
#define PLAYER_ROLL_ACCEL            0.04f
#define PLAYER_ROLL_BRAKE            0.09f
#define PLAYER_ROLL_MAX              1.0f

#define DRAW_DISTANCE_MIN            10.0f
#define DRAW_DISTANCE_MAX            2000.0f
#define DRAW_DISTANCE_STEP           10.0f
#define DEPTH_DELTA_MIN              0.005f
#define DEPTH_DELTA_MAX              0.100f
#define DEPTH_DELTA_STEP             0.001f
#define HEIGHT_SCALE_MIN             10.0f
#define HEIGHT_SCALE_MAX             500.0f
#define HEIGHT_SCALE_STEP            10.0f
#define HORIZONTAL_DIVISIONS_MIN     1
#define HORIZONTAL_DIVISIONS_MAX     4

#define RENDERER_FAST_C              0
#define RENDERER_AMMX_080            1
#define RENDERER_REFERENCE_C         2

typedef struct {
  UBYTE *height_pixels;
  UBYTE *color_pixels;
  UWORD maximum_height;
} map_data_t;

typedef struct {
  BOOL sage_ready;
  BOOL screen_ready;
  BOOL layer_ready;
  BOOL mouse_hidden;
  SAGE_Timer *fps_timer;
  SAGE_Timer *profile_timer;
  ULONG fps_elapsed_us;
  ULONG terrain_time_us;
  ULONG compose_time_us;
  ULONG present_time_us;
  UWORD fps_frames;
  UWORD fps_value;
  UBYTE *ammx_column_buffer;
  BOOL ammx_renderer_ready;
  map_data_t map;
} demo_state_t;

typedef struct {
  BOOL held[KEY_COUNT];
  BOOL pressed[KEY_COUNT];
  BOOL quit_requested;
} input_state_t;

typedef struct {
  float x;
  float y;
  float height;
  float pitch;
  float angle;
  float forward_velocity;
  float pitch_velocity;
  float yaw_velocity;
  float roll_velocity;
} player_t;

demo_state_t demo;
input_state_t input;
player_t player = {
  PLAYER_START_X,
  PLAYER_START_Y,
  PLAYER_START_HEIGHT,
  PLAYER_PITCH_BASE,
  0.0f,
  0.0f,
  0.0f,
  0.0f,
  0.0f
};
voxel_render_stats_t render_stats;

BOOL isNightMode = FALSE;
BOOL isDebugMode = FALSE;
int rendererMode = RENDERER_FAST_C;

ULONG daymode_colormap[SSCR_MAXCOLORS];
ULONG nightmode_colormap[SSCR_MAXCOLORS];

// ******************************************
// GENERAL HELPERS
// ******************************************

void openLog(void) {
  if (!VoxelLogOpen("PROGDIR:voxel_log.txt")) {
    printf("WARNING: unable to create disk or RAM log\n");
  }
  logMessage("voxel_amiga startup\n");
  logMessage("log targets: PROGDIR:voxel_log.txt and RAM:voxel_log.txt\n");
}

void closeLog(void) {
  logMessage("shutdown complete\n");
  VoxelLogClose();
}

void nightmode() {
  SAGE_SetColorMap(nightmode_colormap, 0, 256);  
  SAGE_RefreshColors(0,256);
}

void daymode() {
  SAGE_SetColorMap(daymode_colormap, 0, 256);
  SAGE_RefreshColors(0,256);
}

const char *rendererName(int mode) {
  if (mode == RENDERER_AMMX_080) {
    return "080+AMMX";
  }
  if (mode == RENDERER_REFERENCE_C) {
    return "REFERENCE C";
  }
  return "FAST C";
}

void visualDebug(void) {
  SAGE_PrintFText(10, 10, "fps:%d, Z_FAR: %.0f, SCALE: %.0f", demo.fps_value, Z_FAR, SCALE_FACTOR);
  SAGE_PrintFText(10, 21, "DELTA_Z: %.3f, H_DIVISIONS: %d", START_DELTA_Z, HORIZONTAL_DIVISIONS);
  SAGE_PrintFText(10, 32, "RAYS:%lu SAMPLES:%lu",
                  render_stats.rays, render_stats.depth_samples);
  SAGE_PrintFText(10, 43, "SPANS:%lu PIXELS:%lu",
                  render_stats.spans, render_stats.pixels);
  SAGE_PrintFText(10, 54, "TERRAIN:%luus COMPOSE:%luus",
                  demo.terrain_time_us, demo.compose_time_us);
  SAGE_PrintFText(10, 65, "PRESENT:%luus RENDER:%s",
                  demo.present_time_us,
                  rendererName(rendererMode));
}

// ******************************************
// KEYBOARD
// ******************************************

void beginInputFrame(void) {
  memset(input.pressed, 0, sizeof(input.pressed));
}

void updateKeyState(UWORD type, UWORD code) {
  if (code >= KEY_COUNT) {
    return;
  }
  if (type == SEVT_KEYDOWN) {
    if (!input.held[code]) {
      input.pressed[code] = TRUE;
    }
    input.held[code] = TRUE;
  } else if (type == SEVT_KEYUP) {
    input.held[code] = FALSE;
  }
}

BOOL isKeyHeld(UWORD code) {
  return code < KEY_COUNT && input.held[code];
}

BOOL wasKeyPressed(UWORD code) {
  return code < KEY_COUNT && input.pressed[code];
}

void releaseMap(map_data_t *map) {
  if (map->height_pixels != NULL) {
    SAGE_FreeMem(map->height_pixels);
  }
  if (map->color_pixels != NULL) {
    SAGE_FreeMem(map->color_pixels);
  }
  memset(map, 0, sizeof(*map));
}

BOOL loadRawFile(const char *filename, UBYTE *buffer, ULONG expected_size) {
  FILE *file;
  ULONG bytes_read;
  int extra_byte;

  file = fopen(filename, "rb");
  if (file == NULL) {
    logMessage("unable to open %s\n", filename);
    return FALSE;
  }

  bytes_read = (ULONG)fread(buffer, 1, expected_size, file);
  extra_byte = fgetc(file);
  fclose(file);
  if (bytes_read != expected_size || extra_byte != EOF) {
    logMessage("invalid raw file %s: read %lu, expected %lu bytes\n",
               filename, bytes_read, expected_size);
    return FALSE;
  }
  return TRUE;
}

BOOL loadMap(map_data_t *map, int map_index) {
  map_data_t loaded;
  char height_filename[100];
  char color_filename[100];
  char palette_filename[100];
  UBYTE palette[MAP_PALETTE_SIZE];
  ULONG map_offset;
  int i;
  ULONG red;
  ULONG green;
  ULONG blue;

  memset(&loaded, 0, sizeof(loaded));
  sprintf(height_filename, HEIGHT_MAP_FILENAME, map_index);
  sprintf(color_filename, COLOR_MAP_FILENAME, map_index);
  sprintf(palette_filename, MAP_PALETTE_FILENAME, map_index);

  loaded.height_pixels = (UBYTE *)SAGE_AllocMem(MAP_DATA_SIZE);
  loaded.color_pixels = (UBYTE *)SAGE_AllocMem(MAP_DATA_SIZE);
  if (loaded.height_pixels == NULL || loaded.color_pixels == NULL) {
    logMessage("unable to allocate %lu bytes for map %d\n",
               MAP_DATA_SIZE * 2, map_index);
    releaseMap(&loaded);
    return FALSE;
  }

  logMessage("loading raw height map: %s\n", height_filename);
  if (!loadRawFile(height_filename, loaded.height_pixels, MAP_DATA_SIZE)) {
    releaseMap(&loaded);
    return FALSE;
  }
  loaded.maximum_height = 0;
  for (map_offset = 0; map_offset < MAP_DATA_SIZE; map_offset++) {
    if (loaded.height_pixels[map_offset] > loaded.maximum_height) {
      loaded.maximum_height = loaded.height_pixels[map_offset];
    }
  }
  logMessage("loading raw color map: %s\n", color_filename);
  if (!loadRawFile(color_filename, loaded.color_pixels, MAP_DATA_SIZE)) {
    releaseMap(&loaded);
    return FALSE;
  }

  logMessage("loading raw palette: %s\n", palette_filename);
  if (!loadRawFile(palette_filename, palette, MAP_PALETTE_SIZE)) {
    releaseMap(&loaded);
    return FALSE;
  }

  releaseMap(map);
  *map = loaded;

  for (i=0; i<SSCR_MAXCOLORS; i++) {
    red = palette[i * 3];
    green = palette[i * 3 + 1];
    blue = palette[i * 3 + 2];
    daymode_colormap[i] = red << 16 | green << 8 | blue;
    nightmode_colormap[i] = green << 8;
  }

  SAGE_SetColorMap(daymode_colormap, 0, SSCR_MAXCOLORS);
  SAGE_RefreshColors(0,SSCR_MAXCOLORS);
  logMessage("raw map %d loaded (%lu bytes, maximum height %u)\n",
             map_index, MAP_DATA_SIZE * 2 + MAP_PALETTE_SIZE,
             loaded.maximum_height);
  return TRUE;
}

float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

float updateAxisVelocity(float velocity, BOOL positive, BOOL negative,
                         float acceleration, float braking, float maximum) {
  if (positive && !negative) {
    velocity += velocity < 0.0f ? braking : acceleration;
  } else if (negative && !positive) {
    velocity -= velocity > 0.0f ? braking : acceleration;
  } else if (velocity > braking) {
    velocity -= braking;
  } else if (velocity < -braking) {
    velocity += braking;
  } else {
    velocity = 0.0f;
  }

  return clampFloat(velocity, -maximum, maximum);
}

void updatePlayer(player_t *current_player) {
  BOOL forward;
  BOOL backward;
  BOOL turn_left;
  BOOL turn_right;
  float sin_angle;
  float cos_angle;

  forward = isKeyHeld(SKEY_EN_W);
  backward = isKeyHeld(SKEY_EN_S);
  turn_left = isKeyHeld(SKEY_EN_A);
  turn_right = isKeyHeld(SKEY_EN_D);

  current_player->forward_velocity = updateAxisVelocity(
    current_player->forward_velocity, forward, backward,
    PLAYER_FORWARD_ACCEL, PLAYER_FORWARD_BRAKE, PLAYER_FORWARD_MAX
  );
  current_player->pitch_velocity = updateAxisVelocity(
    current_player->pitch_velocity, backward, forward,
    PLAYER_PITCH_ACCEL, PLAYER_PITCH_BRAKE, PLAYER_PITCH_MAX
  );
  current_player->yaw_velocity = updateAxisVelocity(
    current_player->yaw_velocity, turn_right, turn_left,
    PLAYER_YAW_ACCEL, PLAYER_YAW_BRAKE, PLAYER_YAW_MAX
  );
  current_player->roll_velocity = updateAxisVelocity(
    current_player->roll_velocity, turn_left, turn_right,
    PLAYER_ROLL_ACCEL, PLAYER_ROLL_BRAKE, PLAYER_ROLL_MAX
  );

  if (isKeyHeld(SKEY_EN_UP)) {
    current_player->height += PLAYER_HEIGHT_STEP;
  }
  if (isKeyHeld(SKEY_EN_DOWN)) {
    current_player->height -= PLAYER_HEIGHT_STEP;
  }
  current_player->height = clampFloat(
    current_player->height, PLAYER_MIN_HEIGHT, PLAYER_MAX_HEIGHT
  );

  VoxelFastSinCos(current_player->angle, &sin_angle, &cos_angle);
  current_player->x += cos_angle *
                       current_player->forward_velocity * PLAYER_FORWARD_SCALE;
  current_player->y += sin_angle *
                       current_player->forward_velocity * PLAYER_FORWARD_SCALE;
  if (current_player->x >= MAP_N) {
    current_player->x -= MAP_N;
  } else if (current_player->x < 0.0f) {
    current_player->x += MAP_N;
  }
  if (current_player->y >= MAP_N) {
    current_player->y -= MAP_N;
  } else if (current_player->y < 0.0f) {
    current_player->y += MAP_N;
  }
  current_player->angle += current_player->yaw_velocity * PLAYER_YAW_SCALE;
  if (current_player->angle >= VOXEL_FULL_TURN) {
    current_player->angle -= VOXEL_FULL_TURN;
  } else if (current_player->angle < 0.0f) {
    current_player->angle += VOXEL_FULL_TURN;
  }
  current_player->pitch = PLAYER_PITCH_BASE +
                          current_player->pitch_velocity * PLAYER_PITCH_SCALE;
}

void updateRenderControls(void) {
  if (isKeyHeld(SKEY_EN_Q)) {
    Z_FAR += DRAW_DISTANCE_STEP;
  }
  if (isKeyHeld(SKEY_EN_E)) {
    Z_FAR -= DRAW_DISTANCE_STEP;
  }
  Z_FAR = clampFloat(Z_FAR, DRAW_DISTANCE_MIN, DRAW_DISTANCE_MAX);

  if (isKeyHeld(SKEY_EN_Z)) {
    START_DELTA_Z += DEPTH_DELTA_STEP;
  }
  if (isKeyHeld(SKEY_EN_X)) {
    START_DELTA_Z -= DEPTH_DELTA_STEP;
  }
  START_DELTA_Z = clampFloat(
    START_DELTA_Z, DEPTH_DELTA_MIN, DEPTH_DELTA_MAX
  );

  if (isKeyHeld(SKEY_EN_C)) {
    SCALE_FACTOR += HEIGHT_SCALE_STEP;
  }
  if (isKeyHeld(SKEY_EN_V)) {
    SCALE_FACTOR -= HEIGHT_SCALE_STEP;
  }
  SCALE_FACTOR = clampFloat(
    SCALE_FACTOR, HEIGHT_SCALE_MIN, HEIGHT_SCALE_MAX
  );
}

void enableProfileTimer(void) {
  if (demo.profile_timer != NULL) {
    return;
  }

  logMessage("allocating profiling timer\n");
  demo.profile_timer = SAGE_AllocTimer();
  if (demo.profile_timer != NULL) {
    SAGE_ElapsedTime(demo.profile_timer);
    logMessage("profiling timer ready\n");
  } else {
    logMessage("profiling timer unavailable: %s\n", SAGE_GetErrorString());
  }
}

void handleInputActions(void) {
  if (wasKeyPressed(SKEY_EN_N)) {
    isNightMode = !isNightMode;
    if (isNightMode) {
      nightmode();
    } else {
      daymode();
    }
  }

  if (wasKeyPressed(SKEY_EN_K)) {
    isDebugMode = !isDebugMode;
    if (isDebugMode) {
      enableProfileTimer();
    }
  }

  if (wasKeyPressed(SKEY_EN_L)) {
    HORIZONTAL_DIVISIONS++;
    if (HORIZONTAL_DIVISIONS > HORIZONTAL_DIVISIONS_MAX) {
      HORIZONTAL_DIVISIONS = HORIZONTAL_DIVISIONS_MIN;
    }
  }

  if (wasKeyPressed(SKEY_EN_R)) {
    if (demo.ammx_renderer_ready) {
      rendererMode++;
      if (rendererMode > RENDERER_REFERENCE_C) {
        rendererMode = RENDERER_FAST_C;
      }
    } else {
      rendererMode = rendererMode == RENDERER_FAST_C
        ? RENDERER_REFERENCE_C : RENDERER_FAST_C;
    }
    logMessage("renderer switched to %s\n",
               rendererName(rendererMode));
  }

}

// ******************************************
// VOXEL RENDER
// ******************************************

BOOL drawVoxelTerrain(BOOL trace_first_frame) {
  SAGE_Bitmap *layer_bitmap;
  voxel_render_params_t params;
  float sin_angle;
  float cos_angle;

  if (trace_first_frame) {
    logMessage("first frame: layer lookup begin\n");
  }
  layer_bitmap = SAGE_GetLayerBitmap(MAIN_LAYER);
  if (layer_bitmap == NULL) {
    return FALSE;
  }
  if (trace_first_frame) {
    logMessage("first frame: layer ready (%lux%lux%lu, bpr=%lu)\n",
               layer_bitmap->width, layer_bitmap->height,
               layer_bitmap->depth, layer_bitmap->bpr);
  }

  memset(&params, 0, sizeof(params));
  params.target = layer_bitmap;
  params.height_map = demo.map.height_pixels;
  params.color_map = demo.map.color_pixels;
  params.map_size = MAP_N;
  params.camera_x = player.x;
  params.camera_y = player.y;
  params.camera_height = player.height;
  params.camera_pitch = player.pitch;
  if (trace_first_frame) {
    logMessage("first frame: render trig begin\n");
  }
  VoxelFastSinCos(player.angle, &sin_angle, &cos_angle);
  if (trace_first_frame) {
    logMessage("first frame: render trig complete\n");
  }
  params.camera_sine = sin_angle;
  params.camera_cosine = cos_angle;
  params.camera_roll = player.roll_velocity;
  params.base_vertical_offset = MAIN_LAYER_HEIGHT / 12.0f;
  params.roll_scale = MAIN_LAYER_HEIGHT / 6.0f;
  params.draw_distance = Z_FAR;
  params.height_scale = SCALE_FACTOR;
  params.depth_delta = START_DELTA_Z;
  params.horizontal_divisions = HORIZONTAL_DIVISIONS;
  params.height_map_max = demo.map.maximum_height;
  params.clear_color = 0xff;
  params.column_buffer = demo.ammx_column_buffer;
  if (trace_first_frame) {
    logMessage("first frame: %s renderer begin\n",
               rendererName(rendererMode));
  }
  if (rendererMode == RENDERER_AMMX_080) {
    if (!demo.ammx_renderer_ready ||
        !VoxelRenderFastAMMX(&params, &render_stats)) {
      return FALSE;
    }
  } else if (rendererMode == RENDERER_REFERENCE_C) {
    if (!VoxelRenderC(&params, &render_stats)) {
      return FALSE;
    }
  } else if (!VoxelRenderFastC(&params, &render_stats)) {
    return FALSE;
  }
  if (trace_first_frame) {
    logMessage("first frame: %s renderer complete\n",
               rendererName(rendererMode));
  }
  return TRUE;
}

BOOL composeFrame(void) {
  if (!SAGE_ClearScreen()) {
    return FALSE;
  }
  if (!SAGE_BlitLayerToScreen(
        MAIN_LAYER,
        (SCREEN_WIDTH - MAIN_LAYER_WIDTH) / 2,
        (SCREEN_HEIGHT - MAIN_LAYER_HEIGHT) / 2)) {
    return FALSE;
  }

  if (isDebugMode) {
    visualDebug();
  }
  return TRUE;
}

BOOL refreshFrame(void) {
  return SAGE_RefreshScreen();
}

ULONG sageTimeToMicroseconds(ULONG sage_time) {
  ULONG seconds;
  ULONG microseconds;

  seconds = sage_time >> STIM_SECONDS_SHIFT;
  microseconds = sage_time & STIM_MICRO_MASK;
  return seconds * STIM_TICKS + microseconds;
}

ULONG elapsedMicroseconds(SAGE_Timer *timer) {
  return sageTimeToMicroseconds(SAGE_ElapsedTime(timer));
}

void updateFpsCounter(void) {
  ULONG elapsed;

  if (demo.fps_timer == NULL) {
    return;
  }

  demo.fps_frames++;
  elapsed = SAGE_ElapsedTime(demo.fps_timer);
  if (elapsed != STIM_OVERFLOW) {
    demo.fps_elapsed_us += sageTimeToMicroseconds(elapsed);
    if (demo.fps_elapsed_us >= STIM_TICKS) {
      demo.fps_value = demo.fps_frames;
      demo.fps_frames = 0;
      demo.fps_elapsed_us %= STIM_TICKS;
    }
  }
}

BOOL renderFrame(BOOL trace_first_frame) {
  BOOL terrain_ready;
  BOOL frame_ready;
  BOOL frame_presented;

  if (isDebugMode && demo.profile_timer != NULL) {
    SAGE_ElapsedTime(demo.profile_timer);
    terrain_ready = drawVoxelTerrain(trace_first_frame);
    demo.terrain_time_us = elapsedMicroseconds(demo.profile_timer);
    if (trace_first_frame) {
      logMessage("first frame: composition begin\n");
    }
    frame_ready = terrain_ready && composeFrame();
    if (trace_first_frame) {
      logMessage("first frame: composition complete (%d)\n", frame_ready);
    }
    demo.compose_time_us = elapsedMicroseconds(demo.profile_timer);
    if (trace_first_frame) {
      logMessage("first frame: refresh begin\n");
    }
    frame_presented = frame_ready && refreshFrame();
    if (trace_first_frame) {
      logMessage("first frame: refresh complete (%d)\n", frame_presented);
    }
    demo.present_time_us = elapsedMicroseconds(demo.profile_timer);
  } else {
    terrain_ready = drawVoxelTerrain(trace_first_frame);
    if (trace_first_frame) {
      logMessage("first frame: composition begin\n");
    }
    frame_ready = terrain_ready && composeFrame();
    if (trace_first_frame) {
      logMessage("first frame: composition complete (%d)\n", frame_ready);
      logMessage("first frame: refresh begin\n");
    }
    frame_presented = frame_ready && refreshFrame();
    if (trace_first_frame) {
      logMessage("first frame: refresh complete (%d)\n", frame_presented);
    }
  }
  return frame_presented;
}

void pollInput(void) {
  SAGE_Event* event = NULL;

  beginInputFrame();
  
  // read all events raised by the screen
	while ((event = SAGE_GetEvent()) != NULL) {
		// If we click on mouse button, we stop the loop
  	if (event->type == SEVT_MOUSEBT) {
   		input.quit_requested = TRUE;
  	}
  	// If we press the ESC key, we stop the loop
	else if (event->type == SEVT_RAWKEY && event->code == SKEY_EN_ESC) {
   		input.quit_requested = TRUE;
    	}
   	
    if (event->type == SEVT_KEYDOWN || event->type == SEVT_KEYUP) {
      updateKeyState(event->type, event->code);
    }
  }
}

// ******************************************
// MAIN
// ******************************************

BOOL initDemo(void) {
  memset(&demo, 0, sizeof(demo));
  memset(&input, 0, sizeof(input));
  openLog();

  logMessage("initializing SAGE\n");
  if (!SAGE_Init(SMOD_VIDEO)) {
    printf("SAGE initialization failed: %s\n", SAGE_GetErrorString());
    logMessage("SAGE initialization failed: %s\n", SAGE_GetErrorString());
    return FALSE;
  }
  demo.sage_ready = TRUE;
  logMessage("SAGE initialized\n");

  logMessage("opening %dx%dx%d screen\n",
             SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_DEPTH);
  if (!SAGE_OpenScreen(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_DEPTH, SSCR_STRICTRES)) {
    printf("Unable to open screen: %s\n", SAGE_GetErrorString());
    logMessage("screen failed: %s\n", SAGE_GetErrorString());
    return FALSE;
  }
  demo.screen_ready = TRUE;
  logMessage("screen opened\n");

  logMessage("creating %dx%d voxel layer\n",
             MAIN_LAYER_WIDTH, MAIN_LAYER_HEIGHT);
  if (!SAGE_CreateLayer(MAIN_LAYER, MAIN_LAYER_WIDTH, MAIN_LAYER_HEIGHT)) {
    printf("Unable to create voxel layer: %s\n", SAGE_GetErrorString());
    logMessage("layer failed: %s\n", SAGE_GetErrorString());
    return FALSE;
  }
  demo.layer_ready = TRUE;
  logMessage("voxel layer created\n");

  if (SAGE_ApolloCore()) {
    demo.ammx_column_buffer = (UBYTE *)SAGE_AllocAlignMem(
      (ULONG)MAIN_LAYER_WIDTH * MAIN_LAYER_HEIGHT, 8
    );
    if (demo.ammx_column_buffer != NULL) {
      demo.ammx_renderer_ready = TRUE;
      logMessage("68080 AMMX renderer ready (%lu-byte column buffer)\n",
                 (ULONG)MAIN_LAYER_WIDTH * MAIN_LAYER_HEIGHT);
    } else {
      logMessage("68080 detected, AMMX column buffer unavailable\n");
      SAGE_SetError(SERR_NO_ERROR);
    }
  } else {
    logMessage("68080 not detected, AMMX renderer disabled\n");
  }

  if (!loadMap(&demo.map, file_map_index)) {
    return FALSE;
  }
  logMessage("map %d ready\n", file_map_index);

  demo.fps_timer = SAGE_AllocTimer();
  if (demo.fps_timer != NULL) {
    SAGE_ElapsedTime(demo.fps_timer);
    logMessage("main-loop FPS timer ready\n");
  } else {
    logMessage("FPS timer unavailable: %s\n", SAGE_GetErrorString());
    SAGE_SetError(SERR_NO_ERROR);
  }

  SAGE_VerticalSynchro(TRUE);
  SAGE_HideMouse();
  demo.mouse_hidden = TRUE;
  SAGE_SetTextColor(0,255);
  logMessage("startup complete\n");
  return TRUE;
}

void runDemo(void) {
  BOOL first_frame;

  first_frame = TRUE;
  while (!input.quit_requested) {
    if (first_frame) {
      logMessage("first frame: input poll begin\n");
    }
    pollInput();
    if (first_frame) {
      logMessage("first frame: input poll complete\n");
    }
    if (input.quit_requested) {
      break;
    }
    if (first_frame) {
      logMessage("first frame: input actions begin\n");
    }
    handleInputActions();
    updateRenderControls();
    if (first_frame) {
      logMessage("first frame: player update begin\n");
    }
    updatePlayer(&player);
    if (first_frame) {
      logMessage("first frame: player update complete\n");
    }
    updateFpsCounter();
    if (!renderFrame(first_frame)) {
      printf("Rendering failed: %s\n", SAGE_GetErrorString());
      logMessage("rendering failed: %s\n", SAGE_GetErrorString());
      input.quit_requested = TRUE;
    } else if (first_frame) {
      logMessage("first frame presented\n");
      first_frame = FALSE;
    }
  }
  logMessage("main loop ended\n");
}

void shutdownDemo(void) {
  logMessage("shutdown started\n");
  if (demo.ammx_column_buffer != NULL) {
    SAGE_FreeMem(demo.ammx_column_buffer);
    demo.ammx_column_buffer = NULL;
    demo.ammx_renderer_ready = FALSE;
    logMessage("AMMX column buffer released\n");
  }
  releaseMap(&demo.map);
  logMessage("map released\n");

  if (demo.layer_ready) {
    SAGE_ReleaseLayer(MAIN_LAYER);
    demo.layer_ready = FALSE;
    logMessage("layer released\n");
  }
  if (demo.mouse_hidden) {
    SAGE_ShowMouse();
    demo.mouse_hidden = FALSE;
    logMessage("mouse restored\n");
  }
  if (demo.screen_ready) {
    SAGE_CloseScreen();
    demo.screen_ready = FALSE;
    logMessage("screen closed\n");
  }
  if (demo.profile_timer != NULL) {
    SAGE_ReleaseTimer(demo.profile_timer);
    demo.profile_timer = NULL;
    logMessage("profiling timer released\n");
  }
  if (demo.fps_timer != NULL) {
    SAGE_ReleaseTimer(demo.fps_timer);
    demo.fps_timer = NULL;
    logMessage("FPS timer released\n");
  }
  if (demo.sage_ready) {
    SAGE_Exit();
    demo.sage_ready = FALSE;
    logMessage("SAGE exited\n");
  }
  closeLog();
}

int main(int argc, char *argv[]) {
  int result;

  (void)argc;
  (void)argv;
  result = 1;
  if (initDemo()) {
    runDemo();
    result = 0;
  }
  shutdownDemo();
  return result;
}
