#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <doslib.h>
#include <iocslib.h>
#include "himem.h"
#include "pcm8pp.h"

#define PROGRAM_NAME     "S44BGP.X"
#define PROGRAM_VERSION  "0.1.0 (2023/03/19)"

#define MAX_MUSIC (16)

static int16_t g_num_music;
static int16_t g_current_music;
static int16_t* g_music_addr[ MAX_MUSIC ];
static uint32_t g_music_size[ MAX_MUSIC ];
static uint8_t g_music_name[ MAX_MUSIC ][ 256 ];
static int16_t g_paused;
static uint32_t g_int_counter;

static void __attribute__((interrupt)) __timer_d_interrupt_handler__(void) {

  if (g_int_counter == 0) {
    if (B_SFTSNS() & 0x0002) {                // CTRL key
      int32_t sense_code1 = BITSNS(0x0a);
      int32_t sense_code2 = BITSNS(0x0b);
      if (sense_code2 & 0x01) {               // CTRL + XF4 (pause/resume)
        //B_PRINT("CTRL+XF4");
        if (g_paused) {
          pcm8pp_resume();
          B_PUTMES(6, 0, 31, 64, "\x81\xf4RESUMED");
          g_paused = 0;
        } else {
          pcm8pp_pause();
          B_PUTMES(6, 0, 31, 64, "\x81\xf4PAUSED");
          g_paused = 1;
        }
        g_int_counter = 16;
      } else if (sense_code2 & 0x02) {        // CTRL + XF5 (next)
        //B_PRINT("CTRL+XF5");
        g_current_music = (g_current_music + 1) % g_num_music;
        int16_t pcm8pp_volume = 7;
        int16_t pcm8pp_pan = 0x03;
        int16_t pcm8pp_freq = 0x1d;   // 44.1kHz stereo
        uint32_t pcm8pp_channel_mode = ( pcm8pp_volume << 16 ) | ( pcm8pp_freq << 8 ) | pcm8pp_pan;
        pcm8pp_play(0, pcm8pp_channel_mode, g_music_size[ g_current_music ], 44100*256, g_music_addr[ g_current_music ]);
        B_PUTMES(6, 0, 31, 64, "\x81\xf4");
        B_PUTMES(6, 1, 31, 64, g_music_name[ g_current_music ]);
        g_paused = 0;
        g_int_counter = 16;
      }
    }
  } else if (g_int_counter > 0) {
    g_int_counter--;
  }

}

static uint8_t* check_keep_process(const uint8_t* exec_name) {

  uint8_t* psp = (uint8_t*)GETPDB() - 16;
//  uint8_t* psp = (uint8_t*)_PSP;    // _PSP has the same address as GETPDB()

  // find root process
  for (;;) {
    uint32_t parent = B_LPEEK((uint32_t*)(psp + 4));
    if (parent == 0) {
//      printf("no more parent.\n");
      break;
    }
//    printf("parent=%08X\n", parent);
    psp = (uint8_t*)parent;
  }

  // check memory blocks
  for (;;) {
//    printf("checking process at %08X\n",psp);
    if (B_BPEEK((uint8_t*)(psp + 4)) == 0xff) {
//      printf("found keep process.\n");
//      printf("[%s]\n",psp + 196);
      if (stricmp(psp + 196, exec_name) == 0) {   // assuming this keep process is in the user land
        return psp + 16;    // return PDB
      }
    }
    uint32_t child = B_LPEEK((uint32_t*)(psp + 12));
    if (child == 0) {
//      printf("no more process to check.\n");
      break;
    }
    psp = (uint8_t*)child;
  }

  return NULL;
}

int32_t main(int32_t argc, uint8_t* argv[]) {

  // default exit code
  int32_t rc = -1;

  // file read pointer
  FILE* fp = NULL;

  // credit
  B_PRINT("S44BGP.X - 16bit PCM data background player version " PROGRAM_VERSION " by tantan\r\n");

  // init buffer table
  memset(g_music_addr, 0, sizeof(int16_t*) * MAX_MUSIC);
  memset(g_music_size, 0, sizeof(uint32_t) * MAX_MUSIC);

  // check command line
  if (argc < 2) {
    B_PRINT("usage: s44bgp [-r] <file1.s44> [<file2.s44> ...]\r\n");
    goto exit;
  }

  // remove s44bgp
  if (argv[1][0] == '-' && argv[1][1] == 'r') {
    // self keep check
    uint8_t* pdp = check_keep_process(PROGRAM_NAME);
    if (pdp != NULL) {
      pcm8pp_pause();
      pcm8pp_stop();
      B_PUTMES(0, 0, 31, 64, "                                                                ");
      TIMERDST(0,0,0);
      MFREE((uint32_t)pdp);
      B_PRINT("removed " PROGRAM_NAME " successfully.\r\n");
      rc = 0;
      goto exit;
    } else {
      B_PRINT(PROGRAM_NAME " is not running.\r\n");
      rc = 1;
      goto exit;
    }
  }

  // check high memory driver availability
  if (!himem_isavailable()) {
    B_PRINT("error: high memory is not available.\r\n");
    goto exit;
  }

  // check pcm8pp
  if (!pcm8pp_keepchk()) {
    B_PRINT("error: pcm8pp.x is not running.\r\n");
    goto exit;
  }

  // load s44 data to high memory
  g_num_music = argc - 1;
  for (int16_t i = 0; i < g_num_music; i++) {
    uint8_t* s44_filename = argv[i+1];
    uint8_t* s44_fileext = s44_filename + strlen(s44_filename) - 4;
    if (stricmp(s44_fileext, ".s44") != 0) {
      printf("error: not s44 format data file. (%s)\n", s44_filename);
      goto exit;
    }
    fp = fopen(s44_filename, "rb");
    if (fp == NULL) {
      printf("error: file open error. (%s)\n", s44_filename);
      goto exit;
    }
    printf("Loading %s into high memory...\n", s44_filename);
    fseek(fp, 0, SEEK_END);
    size_t data_len = ftell(fp) / sizeof(int16_t);
    fseek(fp, 0, SEEK_SET);
    g_music_addr[i] = himem_malloc(data_len * sizeof(int16_t), 1);
    if (g_music_addr[i] == NULL) goto exit;
    size_t read_len = 0;
    do {
      size_t len = fread(g_music_addr[i] + read_len, sizeof(int16_t), data_len - read_len, fp);
      if (len == 0) break;
      read_len += len;
    } while (read_len < data_len);
    fclose(fp);
    fp = NULL;
    g_music_size[i] = sizeof(int16_t) * data_len;
    strcpy(g_music_name[i], s44_filename);
  }

  // initialize counters
  g_current_music = 0;
  g_paused = 0;
  g_int_counter = 0;

  // set timer-D interrupt handler
  if (TIMERDST((uint8_t*)(__timer_d_interrupt_handler__), 7, 200) != 0) {
    B_PRINT("error: timer-D interrupt is being used by other applications. (CONFIG.SYS PROCESS= ?)\r\n");
    goto exit;
  }

  // start pcm8pp play
  int16_t pcm8pp_volume = 7;
  int16_t pcm8pp_pan = 0x03;
  int16_t pcm8pp_freq = 0x1d;   // 44.1kHz stereo
  uint32_t pcm8pp_channel_mode = ( pcm8pp_volume << 16 ) | ( pcm8pp_freq << 8 ) | pcm8pp_pan;
  pcm8pp_play(0, pcm8pp_channel_mode, g_music_size[ g_current_music ], 44100*256, g_music_addr[ g_current_music ]);
  B_PUTMES(6, 0, 31, 64, "\x81\xf4");
  B_PUTMES(6, 1, 31, 64, g_music_name[ g_current_music ]);

//  B_PRINT(PROGRAM_NAME " started.\r\n");

  rc = 0;

  // keep self process
  extern int _HEND;
  extern int _PSP;
  KEEPPR(_HEND - _PSP - 0xf0, rc);

exit:

  if (fp != NULL) {
    fclose(fp);
    fp = NULL;
  }

  for (int16_t i = 0; i < MAX_MUSIC; i++) {
    if (g_music_addr[i] != NULL) {
      himem_free(g_music_addr[i], 1);
      g_music_addr[i] = NULL;
    }
  }

  return rc;
}