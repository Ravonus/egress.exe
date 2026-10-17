/* The title and the screen after a run ends (the KEEL start-up is src/boot/keel_boot.c). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "look.h"
#include "tutorial.h"
#include "voice.h"
#include "gen/art.h"
#include "clock.h"
#include "boss_api.h"
#include "title3d/t3d.h"
#include "resume.h"
#include "run.h"
#include "ui.h"

static void screen_reset(void) {
  if (screen_dark)
    screen_dark = 0;
  else
    DISPLAY_OFF;
  HIDE_SPRITES;
  HIDE_WIN;
  hud_line = 144u;
  move_bkg(0, 0);
  {
    uint8_t i;
    for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  }
}

static uint8_t wait_frames(uint8_t n, uint8_t skip_on_press) {
  while (n--) {
    frame_wait();
    sound_tick();
    input_poll();
    if (skip_on_press && (joy_pressed & (J_A | J_START))) return 1;
  }
  return 0;
}

/* The cinematic title leads to the lobby menu; all options are controller operable. */
uint8_t menu_active,menu_cursor;
static void menu_controls(uint8_t page) {
  uint8_t k;
  static const char *const actions[6]={"TAP A: STRIKE","HOLD A: CHARGE","TAP B: SHOOT","HOLD B: CHARGE","A>B: COUNTER","B>A: RUSH"};
  static const char *const movement[6]={"DIR TWICE: EVADE","HOLD DIR: RUN","SELECT: ELEMENT","HOLD SELECT: RESET","START A: BENCH","REST: STAMINA"};
  ui_page(page ? "MOVEMENT / TOOLS" : "A / B ACTIONS","L/R PAGE    B BACK");
  ui_fill(0u,0u,4u,20u,12u,UI_DARK);ui_frame(0u,0u,3u,20u,14u,UI_DARK);
  for(k=0u;k<6u;k++)text_at(0u,1u,(uint8_t)(4u+k*2u),page ? movement[k] : actions[k],UI_DARK);
}
static void menu_row(uint8_t k) {
  uint8_t pal=k==menu_cursor ? UI_LIGHT : UI_DARK,y=(uint8_t)(5u+k*2u);
  static const char *const label[6]={"CONTINUE","NEW RUN","CONTROLS","TUTORIAL","CLOCK / SETTINGS","NARRATOR"};
  ui_fill(0u,1u,y,18u,1u,pal);
  text_at(0u,1u,y,k==menu_cursor ? ">" : " ",pal);text_at(0u,3u,y,label[k],pal);
  if(!k && !resume_ready)text_at(0u,16u,y,"--",pal);
  if(k==5u)text_at(0u,15u,y,voice_on ? "ON" : "OFF",pal);
}
static void menu_hint(void) {
  static const char *const hint[6]={"RETURN TO YOUR HEIR","A NEW GENERATION","LEARN YOUR MOVES","ENTER THE COURSE","TIME / DATE / CLOCK","VOICE REACTIONS"};
  ui_fill(0u,0u,3u,20u,1u,UI_DARK);text_at(0u,1u,3u,hint[menu_cursor],UI_DARK);
}
static void menu_draw(void) {
  uint8_t k;
  ui_page("EGRESS.EXE","A SELECT   B TITLE");
  ui_fill(0u,0u,4u,20u,13u,UI_DARK);ui_frame(0u,0u,4u,20u,13u,UI_DARK);
  for(k=0u;k<6u;k++)menu_row(k);
  menu_hint();
}
static uint8_t menu_abandon(void) {
  ui_page("LEAVE THIS HEIR?","B BACK   A NEW RUN");
  ui_fill(0u,0u,5u,20u,7u,UI_LIGHT);ui_frame(0u,0u,5u,20u,7u,UI_LIGHT);
  text_at(0,1u,7u,"COUNTS AS AN END.",UI_LIGHT);text_at(0,1u,9u,"THE LINE CONTINUES.",UI_LIGHT);
  while(1) {
    frame_wait();sound_tick();input_poll();
    if(joy_pressed&J_B) return 0u;
    if(joy_pressed&(J_A|J_START)) {resume_new=1u;return 1u;}
  }
}
static uint8_t start_menu(void) {
  uint8_t page=0u;
  screen_reset();art_copy_tables(&meta_tile[0][0],meta_pal,pal_bg,pal_sp,people);art_load_game();look_load(BIOME_OFFICE);ui_load();ui_colours();
  menu_cursor=resume_ready ? 0u : 1u;menu_draw();SHOW_BKG;pal_commit(8u);DISPLAY_ON;menu_active=1u;
  while(1) {
    frame_wait();sound_tick();input_poll();
    if(joy_pressed&J_B) {menu_active=0u;fade_to(8u,0u);return 0u;}
    if(joy_pressed&(J_UP|J_DOWN)) {
      uint8_t old=menu_cursor;
      menu_cursor=(joy_pressed&J_UP) ? (menu_cursor ? menu_cursor-1u : 5u) : (menu_cursor==5u ? 0u : menu_cursor+1u);
      menu_row(old);menu_row(menu_cursor);menu_hint();sfx(SFX_STEP);
    }
    if(!(joy_pressed&(J_A|J_START))) continue;
    if(!menu_cursor) {if(resume_ready) break;sfx(SFX_DENY);continue;}
    if(menu_cursor==1u) {if(!resume_ready || menu_abandon()) break;menu_draw();}
    else if(menu_cursor==2u) {
      page=0u;menu_controls(page);
      while(1) {
        frame_wait();sound_tick();input_poll();if(joy_pressed&J_B) break;
        if(joy_pressed&(J_LEFT|J_RIGHT)) {page^=1u;menu_controls(page);sfx(SFX_STEP);}
      }
      menu_draw();
    } else if(menu_cursor==3u) {tutorial_on=1u;break;}
    else if(menu_cursor==4u) {setup_screen();tutorial_save();screen_reset();art_load_game();look_load(BIOME_OFFICE);ui_load();ui_colours();menu_draw();pal_commit(8u);SHOW_BKG;DISPLAY_ON;}
    else {voice_on^=1u;if(!voice_on) voice_stop();tutorial_save();menu_row(5u);sfx(SFX_STEP);}
  }
  menu_active=0u;fade_to(8u,0u);screen_dark=1u;return 1u;
}

/* The PRESS START title is the 3D walk in src/title3d (the logo, PRESS START, the clock row and the seed are drawn
 * there). It returns with every palette black and the LCD on, after START/A (play) or SELECT (SETUP). */
void title_screen(void) BANKED {
  uint16_t previous,rolled;
  while (1) {
    game_mode = MODE_TITLE;
    screen_reset();
    resume_probe();
    if (title3d_run() == T3D_SELECT) { /* SETUP: the clock (and later the other settings) */
      setup_screen();
      continue;
    }
    if(!start_menu()) continue;
    if(tutorial_on==1u)return;
    previous=run_seed;save_load();
    rolled=(uint16_t)(DIV_REG ^ (frame_count << 3) ^ (t3d_seed << 8) ^ t3d_vbl ^ previous ^ (meta.runs*0x2b9u));
    rolled^=rolled<<7;rolled^=rolled>>9;rolled^=rolled<<8;
    if(!rolled || rolled==previous)rolled=previous==65535u ? 1u : (uint16_t)(previous+1u);
    run_seed=dbg_seed ? dbg_seed : rolled;
    if (clock_needs_ask()) clock_screen(1); /* a new save asks once: WHAT IS TODAY? */
    return;
  }
}

static const char *const last_words[] = {
    "THE CARPET KEEPS IT.",
    "THE HUM GOT LOUDER.",
    "SAVED OVER, AGAIN.",
    "THE DOOR WAS DRAWN.",
};

void dead_screen(void) BANKED {
  char caption[20];
  game_mode = MODE_DEAD;
  screen_reset();
  art_load_game();
  clear_map(0, 4);
  text_at(0, 3, 4, "YOU STOPPED.", 4);
  if (boss_floor && boss_result == 2u) {
    boss_death_caption(caption);
    text_at(0, 0, 7, caption, 4);
  } else text_at(0, 0, 7, last_words[(uint8_t)(run_seed + floor_no) & 3u], 4);
  text_at(0, 3, 10, "FLOOR", 4);
  num_at(0, 12, 10, floor_no, 2, 4);
  text_at(0, 3, 13, "PRESS A", 4);
  pal_commit(0);
  SHOW_BKG;
  DISPLAY_ON;
  fade_to(0, 8);
  wait_frames(40u, 0);
  while (1) {
    frame_wait();
    sound_tick();
    input_poll();
    if (joy_pressed & (J_A | J_START)) break;
  }
  fade_to(8, 0);
}
