#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sage/sage.h>

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

#define HEIGHT_MAP_FILENAME	      "maps/map%d.height.gif"
#define COLOR_MAP_FILENAME	      "maps/map%d.color.gif"
int file_map_index = 0;

SAGE_Picture *height_map_picture, *color_map_picture;

BOOL isNightMode = FALSE;
BOOL isDebugMode = FALSE;
BOOL mustExit = FALSE;

UBYTE* heightmap = NULL;
UBYTE* colormap = NULL;
ULONG daymode_colormap[SSCR_MAXCOLORS];
ULONG nightmode_colormap[SSCR_MAXCOLORS];

// ******************************************
// GENERAL HELPERS
// ******************************************

void fillAreaLayer(int index, int layer_width, int x, int y, int width, int height, int color) {
  SAGE_Bitmap *bitmap = SAGE_GetLayerBitmap(index);
  UBYTE *buffer = (UBYTE *)bitmap->bitmap_buffer;
  int i, j, current_x, current_y;

  for (i=0; i<width; i++) {
    for (j=0; j<height; j++) {
      current_x = x + i;
      current_y = y + j;
      buffer[(layer_width * current_y) + current_x] = (UBYTE)color;
    }
  }
}

void clearLayerBitmap(int index, UBYTE color) {
  int x, y;
  SAGE_Bitmap *bitmap = SAGE_GetLayerBitmap(index);
  UBYTE *buffer8 = (UBYTE *)bitmap->bitmap_buffer;

  for (y=0; y<MAIN_LAYER_HEIGHT; y++) {
    for (x=0; x<MAIN_LAYER_WIDTH; x++) {
      buffer8[(MAIN_LAYER_WIDTH * y) + x] = (UBYTE)color;
    }
  }
}

void putPixel(int layer, int index, int color) {
  SAGE_Bitmap *bitmap = SAGE_GetLayerBitmap(layer);
  UBYTE *buffer = bitmap->bitmap_buffer;
  buffer[index] = (UBYTE)color;
}

void nightmode() {
  SAGE_SetColorMap(nightmode_colormap, 0, 256);  
  SAGE_RefreshColors(0,256);
}

void daymode() {
  SAGE_SetColorMap(daymode_colormap, 0, 256);
  SAGE_RefreshColors(0,256);
}

void visualDebug(void) {
  SAGE_PrintFText(10, 10, "fps:%d, Z_FAR: %.0f, SCALE: %.0f", SAGE_GetFps(), Z_FAR, SCALE_FACTOR);
  SAGE_PrintFText(10, 21, "DELTA_Z: %.3f, H_DIVISIONS: %d", START_DELTA_Z, HORIZONTAL_DIVISIONS);
}

// ******************************************
// KEYBOARD
// ******************************************

#define KEY_NBR       128
#define KEY_US_W	    0
#define KEY_US_A	    1
#define KEY_US_S	    2
#define KEY_US_D	    3
#define KEY_US_U	    4
#define KEY_US_J      5
#define KEY_US_Q	    6
#define KEY_US_E      7
#define KEY_US_Z	    8
#define KEY_US_X      9
#define KEY_US_C	    10
#define KEY_US_V      11
#define KEY_US_N      12
#define KEY_US_K      13
#define KEY_US_L      14
#define KEY_US_LEFT   15
#define KEY_US_RIGHT	16
#define KEY_US_UP   	17
#define KEY_US_DOWN 	18
#define KEY_US_1      19
#define KEY_US_2      20
#define KEY_US_3      21
#define KEY_US_4      22
#define KEY_US_5      23
#define KEY_US_6      24
#define KEY_US_7      25
#define KEY_US_8      26
#define KEY_US_9      27
#define KEY_US_0      28

UBYTE keyboard_state[KEY_NBR];

SAGE_KeyScan keys[KEY_NBR] = {
  { SKEY_EN_W, FALSE },
  { SKEY_EN_A, FALSE },
  { SKEY_EN_S, FALSE },
  { SKEY_EN_D, FALSE },
  { SKEY_EN_U, FALSE },
  { SKEY_EN_J, FALSE },
  { SKEY_EN_Q, FALSE },
  { SKEY_EN_E, FALSE },
  { SKEY_EN_Z, FALSE },
  { SKEY_EN_X, FALSE },
  { SKEY_EN_C, FALSE },
  { SKEY_EN_V, FALSE }, 
  { SKEY_EN_N, FALSE }, 
  { SKEY_EN_LEFT, FALSE },
  { SKEY_EN_RIGHT, FALSE },
  { SKEY_EN_UP, FALSE },
  { SKEY_EN_DOWN, FALSE },
  { SKEY_EN_1, FALSE },
  { SKEY_EN_2, FALSE },
  { SKEY_EN_3, FALSE },
  { SKEY_EN_4, FALSE },
  { SKEY_EN_5, FALSE },
  { SKEY_EN_6, FALSE },
  { SKEY_EN_7, FALSE },
  { SKEY_EN_8, FALSE },
  { SKEY_EN_9, FALSE },
  { SKEY_EN_0, FALSE }
};

void key_status(UWORD type, UWORD code) {
	if (type == SEVT_KEYDOWN) {
		keys[code].key_pressed = TRUE;
	}
	if (type == SEVT_KEYUP) {
		keys[code].key_pressed = FALSE;
	}
}

BOOL is_key_pressed(UWORD code) {
  return keys[code].key_pressed;
}

// ******************************************
// PLAYER (aka Camera)
// ******************************************

typedef struct {
  float x;
  float y;
  
  float height;
  float pitch;
  float angle;
  
  float forward_vel;
  float forward_acc;
  float forward_brk;
  float forward_max;
  
  float pitch_vel;
  float pitch_acc;
  float pitch_brk;
  float pitch_max;
  
  float yaw_vel;
  float yaw_acc;
  float yaw_brk;
  float yaw_max;
  
  float lift_vel;
  float lift_acc;
  float lift_brk;
  float lift_max;
  
  float strafe_vel;
  float strafe_acc;
  float strafe_brk;
  float strafe_max;
  
  float roll_vel;
  float roll_acc;
  float roll_brk;
  float roll_max;
} player_t;

player_t player = {
  // x,y
  512.0f,
  512.0f,
  
  // height, pitch, angle
  80.0f,
  0.0f,
  0.0f,
  
  // forward
  0.0f, 0.06f, 0.1f, 3.0f,
  // pitch
  0.0f, 0.16f, 0.06f, 2.0f,
  // yaw
  0.0f, 0.10f, 0.10f, 1.0f,
  // lift
  0.0f, 0.06f, 0.07f, 1.0f,
  // strafe
  0.0f, 0.05f, 0.09f, 1.0f,
  // roll
  0.0f, 0.04f, 0.09f, 1.0f,
};

void loadMapHeight() {
  char result[100];
  const char *template = "maps/map0.color.gif";//"maps/map%d.height.gif";
  
  const char *placeholder = "%d";
  const char *found = strstr(template, placeholder);
  
  size_t prefixLength = found - template;
  char prefix[100];
  char suffix[100];
  
  strncpy(prefix, template, prefixLength);
  prefix[prefixLength] = '\0';
  strcpy(suffix, found + strlen(placeholder));
  sprintf(result, "%s%d%s", prefix, file_map_index, suffix);
  printf("result: %s\n", result);
  
  height_map_picture = SAGE_LoadPicture(HEIGHT_MAP_FILENAME);
  heightmap = (UBYTE *)SAGE_GetBitmapBuffer(height_map_picture->bitmap);
  
  SAGE_ReleasePicture(heightmap);
}

void loadMapColor() {
  int i, a, r, g, b, count;
  char result[100];
  const char *template = "maps/map0.height.gif"; //"maps/map%d.color.gif";
  
  const char *placeholder = "%d";
  const char *found = strstr(template, placeholder);
  
  size_t prefixLength = found - template;
  char prefix[100];
  char suffix[100];
  
  strncpy(prefix, template, prefixLength);
  prefix[prefixLength] = '\0';
  strcpy(suffix, found + strlen(placeholder));
  sprintf(result, "%s%d%s", prefix, file_map_index, suffix);
  printf("result: %s\n", result);
  
  color_map_picture = SAGE_LoadPicture(COLOR_MAP_FILENAME);
  colormap = (UBYTE *)SAGE_GetBitmapBuffer(color_map_picture->bitmap);

  for (i=0; i<SSCR_MAXCOLORS; i++) {
    daymode_colormap[i] = color_map_picture->color_map[i];
    
    a = (daymode_colormap[i] >> 24) & 0xFF;
    r = (daymode_colormap[i] >> 16) & 0xFF;
    g = (daymode_colormap[i] >> 8) & 0xFF;
    b = daymode_colormap[i] & 0xFF;
    
    nightmode_colormap[i] = (a << 24) | (0 << 16) | (g << 8) | 0;
  }
  
  SAGE_LoadPictureColorMap(color_map_picture);
  SAGE_RefreshColors(0,SSCR_MAXCOLORS);
  SAGE_ReleasePicture(color_map_picture);
}

void player_move(player_t* player) {
  // Move front or back (accounting for acceleration & deacceleration)
  if (is_key_pressed(SKEY_EN_W) && player->forward_vel < player->forward_max) {
    player->forward_vel += (player->forward_vel < 0) ? player->forward_brk : player->forward_acc;
  } 
  else if (is_key_pressed(SKEY_EN_S) && player->forward_vel > -player->forward_max) {
    player->forward_vel -= (player->forward_vel > 0) ? player->forward_brk : player->forward_acc; 
  } 
  else {
    if (player->forward_vel - player->forward_brk > 0) {
      player->forward_vel -= player->forward_brk;
    } else if (player->forward_vel + player->forward_brk < 0) {
      player->forward_vel += player->forward_brk;
    } else {
      player->forward_vel = 0;
    }
  }
  
	if (is_key_pressed(SKEY_EN_W) && player->pitch_vel > -player->pitch_max) {
    player->pitch_vel -= (player->pitch_vel > 0) ? player->pitch_brk : player->pitch_acc;
  } 
  else if (is_key_pressed(SKEY_EN_S) && player->pitch_vel < player->pitch_max - player->pitch_acc) {
    player->pitch_vel += (player->pitch_vel < 0) ? player->pitch_brk : player->pitch_acc;
  } 
  else if (!is_key_pressed(SKEY_EN_W) && !is_key_pressed(SKEY_EN_S)) {
    if (player->pitch_vel - player->pitch_brk > 0) {
      player->pitch_vel -= player->pitch_brk;
    } 
    else if (player->pitch_vel + player->pitch_brk < 0) {
      player->pitch_vel += player->pitch_brk;
    } 
    else {
      player->pitch_vel = 0;
    }
  }

  // Yaw left or right (accounting for acceleration & deacceleration)
  if (is_key_pressed(SKEY_EN_A) && player->yaw_vel > -player->yaw_max) {
    player->yaw_vel -= (player->yaw_vel > 0) ? player->yaw_brk : player->yaw_acc;
  } 
  else if (is_key_pressed(SKEY_EN_D) && player->yaw_vel < player->yaw_max) {
    player->yaw_vel += (player->yaw_vel < 0) ? player->yaw_brk : player->yaw_acc;
  } 
  else {
    if (player->yaw_vel - player->yaw_brk > 0) {
      player->yaw_vel -= player->yaw_brk;
    } 
    else if (player->yaw_vel + player->yaw_brk < 0) {
      player->yaw_vel += player->yaw_brk;
    } 
    else {
      player->yaw_vel = 0.0f;
    }
  }

  // Roll left or right (accounting for acceleration & deacceleration)
  if (is_key_pressed(SKEY_EN_D) && player->roll_vel > -player->roll_max) {
    player->roll_vel -= (player->roll_vel > 0) ? player->roll_brk : player->roll_acc;
  } 
  else if (is_key_pressed(SKEY_EN_A) && player->roll_vel < player->roll_max - player->roll_acc) {
    player->roll_vel += (player->roll_vel < 0) ? player->roll_brk : player->roll_acc;
  } 
  else if (!is_key_pressed(SKEY_EN_A) && !is_key_pressed(SKEY_EN_D)) {
    if (player->roll_vel - player->roll_brk > 0) {
      player->roll_vel -= player->roll_brk;
    }
    else if (player->roll_vel + player->roll_brk < 0) {
      player->roll_vel += player->roll_brk;
    } 
    else {
      player->roll_vel = 0.0f;
    }
  }

  // Move up and down (accounting for acceleration & deacceleration)
  /*if (is_key_pressed(SKEY_EN_U) && player->lift_vel < player->lift_max) {
    player->lift_vel += (player->lift_vel < 0) ? player->lift_brk : player->lift_acc;
  } 
  else if (is_key_pressed(SKEY_EN_J) && player->lift_vel > -player->lift_max) {
    player->lift_vel -= (player->lift_vel > 0) ? player->lift_brk : player->lift_acc;
  } 
  else {
    if (player->lift_vel - player->lift_brk > 0) {
      player->lift_vel -= player->lift_brk;
    } 
    else if (player->lift_vel + player->lift_brk < 0) {
      player->lift_vel += player->lift_brk;
    } 
    else {
      player->lift_vel = 0.0f;
    }
  }*/
  
  if (keys[SKEY_EN_UP].key_pressed) {
		player->height += 5.0;
		
		if (player->height >= 300.0) {
  		player->height = 300.0;
		}
	}
	if (keys[SKEY_EN_DOWN].key_pressed) {
		player->height -= 5.0;
		
		if (player->height <= 10.0) {
  		player->height = 10.0;
		}
	}

	if (keys[SKEY_EN_Q].key_pressed) {
		Z_FAR += 10.0;
	}
	if (keys[SKEY_EN_E].key_pressed) {
		Z_FAR -= 10.0;
	}
	
	if (keys[SKEY_EN_Z].key_pressed) {
		START_DELTA_Z += 0.001;
	}
	if (keys[SKEY_EN_X].key_pressed) {
		START_DELTA_Z -= 0.001;
		if (START_DELTA_Z < 0.005) {
  		START_DELTA_Z = 0.005;
  	}
	}
	
	if (keys[SKEY_EN_C].key_pressed) {
		SCALE_FACTOR += 10;
	}
	if (keys[SKEY_EN_V].key_pressed) {
		SCALE_FACTOR -= 10;
		if (SCALE_FACTOR < 10) {
  		SCALE_FACTOR = 10;
  	}
	}
	
  if (is_key_pressed(SKEY_EN_N)) {
    if (isNightMode) {
    daymode();
      
    }
    else {
      nightmode();
    }
    
    isNightMode = !isNightMode;
  }

  if (is_key_pressed(SKEY_EN_K)) {
    isDebugMode = !isDebugMode;
  }
  
  if (is_key_pressed(SKEY_EN_L)) {
    HORIZONTAL_DIVISIONS ++;
    
    if (HORIZONTAL_DIVISIONS > 4) {
      HORIZONTAL_DIVISIONS = 1;
    }
  }
  
  // update player position, angle, height and pitch
  player->x += cos(player->angle) * player->forward_vel * 0.9f;
  player->y += sin(player->angle) * player->forward_vel * 0.9f;
  
  player->x += cos(1.57f + player->angle) * player->strafe_vel * 0.5f;
  player->y += sin(1.57f + player->angle) * player->strafe_vel * 0.5f;
  
  player->angle += player->yaw_vel * 0.02f;
  
  player->height += player->lift_vel * 1.4f;
  
  player->pitch = (player->pitch_vel * 20.0f) + 80.0f;
}

// ******************************************
// VOXEL RENDER
// ******************************************

void updateVoxel() {
  float sinangle, cosangle;
  float plx, ply;
  float prx, pry;
  int i, y, z, map_offset, height_on_screen;
  float deltax, deltay, deltaz, rx, ry, max_height, tilt;
  
  UBYTE urgb = 0xff;
   
  clearLayerBitmap(MAIN_LAYER, urgb);
  
  sinangle = sin(player.angle);
  cosangle = cos(player.angle);
  
  plx = cosangle * Z_FAR + sinangle * Z_FAR;
  ply = sinangle * Z_FAR - cosangle * Z_FAR;
  
  prx = cosangle * Z_FAR - sinangle * Z_FAR;
  pry = sinangle * Z_FAR + cosangle * Z_FAR;
    
  for(i=0; i<MAIN_LAYER_WIDTH; i+=HORIZONTAL_DIVISIONS) {
    deltax = (plx + (prx - plx) / MAIN_LAYER_WIDTH * i) / Z_FAR;
    deltay = (ply + (pry - ply) / MAIN_LAYER_WIDTH * i) / Z_FAR;
    deltaz = 1.0;
    
    rx = player.x;
    ry = player.y;
      
    max_height = MAIN_LAYER_HEIGHT;
    
    for(z=1; z<Z_FAR; z+=deltaz) {
      rx += deltax;
      ry += deltay;
      
      map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));
      
      height_on_screen = (int)((player.height - (UBYTE)(heightmap[map_offset])) / z * SCALE_FACTOR + player.pitch);
      
      if (height_on_screen < 0) {
        height_on_screen = 0;
      }
      if (height_on_screen > MAIN_LAYER_HEIGHT) {
        height_on_screen = MAIN_LAYER_HEIGHT - 1;
      }
       
      if (height_on_screen < max_height) {
        tilt = (player.roll_vel * (i / (float)MAIN_LAYER_WIDTH - 0.5f) + 0.5f) * MAIN_LAYER_HEIGHT / 6.0f;
        
        for (y = (height_on_screen + tilt); y<(max_height + tilt); y++) {
          if (y>=0) {
            putPixel(MAIN_LAYER, (MAIN_LAYER_WIDTH*y)+i, colormap[map_offset]);
            
            if (HORIZONTAL_DIVISIONS >= 2) {
              putPixel(MAIN_LAYER, (MAIN_LAYER_WIDTH*y)+(i+1), colormap[map_offset]);
            }
            
            if (HORIZONTAL_DIVISIONS >= 3) {
              putPixel(MAIN_LAYER, (MAIN_LAYER_WIDTH*y)+(i+2), colormap[map_offset]);
            }
            
            if (HORIZONTAL_DIVISIONS >= 4) {
              putPixel(MAIN_LAYER, (MAIN_LAYER_WIDTH*y)+(i+3), colormap[map_offset]);
            }
          }
        }
        
        max_height = height_on_screen;
      }
      
      deltaz += START_DELTA_Z;
    }
  }
}

void renderVoxel() {
	SAGE_ClearScreen();
  SAGE_BlitLayerToScreen(MAIN_LAYER, (SCREEN_WIDTH - MAIN_LAYER_WIDTH) / 2, (SCREEN_HEIGHT - MAIN_LAYER_HEIGHT) / 2);
  
  if (isDebugMode) {
    visualDebug();
  }
  
  SAGE_RefreshScreen();
}

void updateKeyboardKeysListener(void) {
  SAGE_Event* event = NULL;
  int keysIndex = 0;
  
  // read all events raised by the screen
	while ((event = SAGE_GetEvent()) != NULL) {
		// If we click on mouse button, we stop the loop
  	if (event->type == SEVT_MOUSEBT) {
   		mustExit = TRUE;
  	}
  	// If we press the ESC key, we stop the loop
		else if (event->type == SEVT_RAWKEY && event->code == SKEY_EN_ESC) {
   		mustExit = TRUE;
   	}
   	
   	key_status(event->type, event->code);
  }
}

// ******************************************
// MAIN
// ******************************************

void main(int argc, char* argv[]) {
	// init the SAGE system
  if (SAGE_Init(SMOD_VIDEO|SMOD_INTERRUPTION)) {
    
    // open the screen
    if (SAGE_OpenScreen(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_DEPTH, SSCR_STRICTRES)) {	
      SAGE_VerticalSynchro(TRUE);
  		SAGE_HideMouse();
  		
      SAGE_SetTextColor(0,255);

  	 	SAGE_EnableFrameCount(TRUE);
      
      SAGE_CreateLayer(MAIN_LAYER, MAIN_LAYER_WIDTH, MAIN_LAYER_HEIGHT);
      
      loadMapColor();
      loadMapHeight();
      
      /*height_map_picture = SAGE_LoadPicture(HEIGHT_MAP_FILENAME);
      color_map_picture = SAGE_LoadPicture(COLOR_MAP_FILENAME);
        
      heightmap = (UBYTE *)SAGE_GetBitmapBuffer(height_map_picture->bitmap);
      colormap = (UBYTE *)SAGE_GetBitmapBuffer(color_map_picture->bitmap);

      for (i=0; i<SSCR_MAXCOLORS; i++) {
        daymode_colormap[i] = color_map_picture->color_map[i];
        
        a = (daymode_colormap[i] >> 24) & 0xFF;
        r = (daymode_colormap[i] >> 16) & 0xFF;
        g = (daymode_colormap[i] >> 8) & 0xFF;
        b = daymode_colormap[i] & 0xFF;
        
        nightmode_colormap[i] = (a << 24) | (0 << 16) | (g << 8) | 0;
      }
      
      SAGE_LoadPictureColorMap(color_map_picture);
      SAGE_RefreshColors(0,SSCR_MAXCOLORS);
      SAGE_ReleasePicture(heightmap);
      SAGE_ReleasePicture(color_map_picture);
      */
      while(!mustExit) {
        updateKeyboardKeysListener();
        player_move(&player);
        updateVoxel();
        renderVoxel();
      }
    }
  }
  
	SAGE_ShowMouse();
	SAGE_CloseScreen();
	SAGE_ReleaseLayer(MAIN_LAYER);
	SAGE_Exit();
}