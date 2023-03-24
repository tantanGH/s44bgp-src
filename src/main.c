#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <doslib.h>
#include <iocslib.h>
#include "himem.h"
#include "pcm8pp.h"
#include "ym2608_decode.h"
#include "kmd.h"
#include "s44bgp.h"

#define __OPM_TIMER__

static PCM_MUSIC g_pcm_music[ MAX_MUSIC ];
static int16_t g_num_music;
static int16_t g_quiet_mode;
static int16_t g_shuffle_mode;
static int16_t g_opm_timer;

volatile static uint32_t g_pcm8pp_freq;
volatile static int16_t g_current_music;
volatile static int16_t g_paused;
volatile static int32_t g_int_counter;
volatile static uint32_t g_elapsed_time;

#define OPM_REG_PORT  ((uint8_t*)0xE90001)
#define OPM_DATA_PORT ((uint8_t*)0xE90003)

//
//  timer-D / OPM timer-B interrupt handler
//
static void __attribute__((interrupt)) __timer_interrupt_handler__(void) {

  // total play time
  if (!g_paused) {
#ifdef __OPM_TIMER__
    g_elapsed_time += OPM_INTERVAL_MSEC;
#else
    g_elapsed_time += TIMERD_INTERVAL_MSEC;
#endif
  }

#ifdef __OPM_TIMER__
  while (OPMSNS() & 0x80);
  OPMSET(0x14, 0x2a);
#endif

  // check playback stop
  if (g_int_counter == 8) {
    if (!g_paused && pcm8pp_get_data_length(PCM8PP_CHANNEL) == 0) {
      // really ended?
      if (g_elapsed_time < g_pcm_music[ g_current_music ].total_time_msec - 1500) {
        // probablly pcm8pp playback was stopped externally
//        pcm8pp_pause();
        g_paused = 1;
        if (!g_quiet_mode) {
          B_PUTMES(6, 0, 31, 66, SJIS_ONPU "ABORTED. CTRL+XF5 to NEXT.");
        }
      } else {
        // next music
        g_current_music = g_shuffle_mode ? rand() % g_num_music : (g_current_music + 1) % g_num_music;
        PCM_MUSIC* pcm = &(g_pcm_music[ g_current_music ]);
        pcm->kmd.current_event_ofs = 0;
        pcm8pp_play(PCM8PP_CHANNEL, ( pcm->volume << 16 ) | ( g_pcm8pp_freq << 8 ) | 0x03, pcm->buffer_bytes, 44100*256, pcm->buffer);
        if (!g_quiet_mode) {
          B_PUTMES(6, 0, 31, 2, SJIS_ONPU);
          if (pcm->kmd.tag_title[0] != '\0') {
            B_PUTMES(6, 2, 31, 64, pcm->kmd.tag_title);
          } else {
            B_PUTMES(6, 2, 31, 64, pcm->file_name);
          }
        }
        g_paused = 0;
        g_elapsed_time = 0;
      }
    }
  }

  // check pause/resume
#ifdef __OPM_TIMER__
  if (g_int_counter & 0x01) {
#else
  if (g_int_counter == 4) {
#endif
//    uint8_t key1 = *((uint8_t*)0x80e);      // CTRL key
//    uint8_t key2 = *((uint8_t*)0x80b);      // XF4/XF5 key
    if (B_SFTSNS() & 0x02) {                  // CTRL key
//    if (key1 & 0x02) {
      int32_t sense_code = BITSNS(0x0b);
      if (sense_code & 0x01) {                // CTRL + XF4 (pause/resume)
//      if (key2 & 0x01) {                    // XF4
        if (g_paused) {
          pcm8pp_resume();
          if (!g_quiet_mode) {
            PCM_MUSIC* pcm = &(g_pcm_music[ g_current_music ]);
            B_PUTMES(6, 0, 31, 2, SJIS_ONPU);
            if (pcm->kmd.tag_title[0] != '\0') {
              B_PUTMES(6, 2, 31, MAX_DISP_LEN - 2, pcm->kmd.tag_title);
            } else {
              B_PUTMES(6, 2, 31, MAX_DISP_LEN - 2, pcm->file_name);
            }
          }
          g_paused = 0;
        } else {
          pcm8pp_pause();
          if (!g_quiet_mode) {
            B_PUTMES(6, 0, 31, MAX_DISP_LEN, SJIS_ONPU "PAUSED.");
          }
          g_paused = 1;
        }
      } else if (sense_code & 0x02) {         // CTRL + XF5 (skip)
//      } else if (key2 & 0x02) {         // XF5
        pcm8pp_stop();
        g_current_music = g_shuffle_mode ? rand() % g_num_music : (g_current_music + 1) % g_num_music;
        PCM_MUSIC* pcm = &(g_pcm_music[ g_current_music ]);
        pcm->kmd.current_event_ofs = 0;
        pcm8pp_play(PCM8PP_CHANNEL, ( pcm->volume << 16 ) | ( g_pcm8pp_freq << 8 ) | 0x03, pcm->buffer_bytes, 44100*256, pcm->buffer);
        if (!g_quiet_mode) {
          B_PUTMES(6, 0, 31, 2, SJIS_ONPU);
          if (pcm->kmd.tag_title[0] != '\0') {
            B_PUTMES(6, 2, 31, MAX_DISP_LEN - 2, pcm->kmd.tag_title);
          } else {
            B_PUTMES(6, 2, 31, MAX_DISP_LEN - 2, pcm->file_name);
          }
        }
        g_paused = 0;
        g_elapsed_time = 0;
      }
    }
  }

  // check KMD event
#ifdef __OPM_TIMER__
  if (1) {
#else
  if (g_int_counter == 2) {
#endif
    if (!g_quiet_mode) {
      PCM_MUSIC* pcm = &(g_pcm_music[ g_current_music ]);
      KMD_HANDLE* kmd = &(pcm->kmd);
      if (kmd->current_event_ofs < kmd->num_events) {
        KMD_EVENT* event = &(kmd->events[ kmd->current_event_ofs ]);
        if (event->start_msec <= 500) {      // do not show first 0.5 sec KMD events to ensure file name display
          kmd->current_event_ofs++;
        } else if (event->start_msec <= g_elapsed_time) {
          B_PUTMES(6, event->pos_x * 2, 31, MAX_DISP_LEN - event->pos_x * 2, event->message);
          kmd->current_event_ofs++;
        }
      }
    }
  }

  // countdown
  g_int_counter--;
  if (g_int_counter < 0) {
#ifdef __OPM_TIMER__
    g_int_counter = OPM_INTERVAL_COUNT;
#else
    g_int_counter = TIMERD_INTERVAL_COUNT;
#endif
  }

}

//
//  keep process checker
//
static uint8_t* check_keep_process(const uint8_t* exec_name) {

  uint8_t* psp = (uint8_t*)GETPDB() - 16;
//  uint8_t* psp = (uint8_t*)_PSP;    // _PSP has the same address as GETPDB()

  // find root process
  for (;;) {
    uint32_t parent = B_LPEEK((uint32_t*)(psp + 4));
    if (parent == 0) {
      break;
    }
    psp = (uint8_t*)parent;
  }

  // check memory blocks
  for (;;) {
    if (B_BPEEK((uint8_t*)(psp + 4)) == 0xff) {
      if (stricmp(psp + 196, exec_name) == 0) {   // assuming this keep process is in the user land
        return psp + 16;    // return PDB
      }
    }
    uint32_t child = B_LPEEK((uint32_t*)(psp + 12));
    if (child == 0) {
      break;
    }
    psp = (uint8_t*)child;
  }

  return NULL;
}

//
//  show help message
//
static void show_help_message() {
  printf("usage: s44bgp [options] <file1.(s44|a44)> [<file2.(s44|a44)> ...]\n");
  printf("options:\n");
  printf("   -r    ... remove running s44bgp\n");
  printf("   -h    ... show help message\n");
  printf("\n");
  printf("   -i <file> ... indirect file\n");
  printf("\n");
  printf("   -v<n> ... volume (1-12, default:8)\n");
  printf("   -s    ... shuffle mode\n");
  printf("   -q    ... quiet mode\n");
  printf("\n");
  printf("   -2    ... 22.05kHz mode\n");
  printf("   -8    ... 8bit PCM mode\n");
  printf("   -m    ... mono mode\n");
}

//
//  main
//
int32_t main(int32_t argc, uint8_t* argv[]) {

  // default exit code
  int32_t rc = -1;

  // option parameters
  int16_t remove_mode = 0;
  int16_t pcm_volume = 8;
  int16_t pcm_half_rate = 0;
  int16_t pcm_half_bit = 0;
  int16_t pcm_channels = 2;
  int16_t shuffle_mode = 0;
  int16_t quiet_mode = 0;
  int16_t num_music = 0;

  // init PCM_MUSIC array
  memset(g_pcm_music, 0, sizeof(PCM_MUSIC) * MAX_MUSIC);
  for (int16_t i = 0; i < MAX_MUSIC; i++) {
    memcpy(g_pcm_music[i].eye_catch, EYE_CATCH, EYE_CATCH_LEN);
  }
 
  // file read pointer
  FILE* fp = NULL;

  // file read staging buffer
  int16_t* fread_buffer = NULL;

  // ym2608 decode handle
  YM2608_DECODE_HANDLE ym2608_decode = { 0 };

  // credit
  printf("S44BGP.X - 16bit PCM background player for Mercury-UNIT version " PROGRAM_VERSION " by tantan\n");

  // check command line
  if (argc < 2) {
    show_help_message();
    goto exit;
  }

  // parse command lines
  for (int16_t i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && strlen(argv[i]) >= 2) {
      if (argv[i][1] == 'v') {
        pcm_volume = atoi(argv[i]+2);
        if (pcm_volume < 1 || pcm_volume > 12 || strlen(argv[i]) < 3) {
          show_help_message();
          goto exit;
        }
      } else if (argv[i][1] == 'r') {
        remove_mode = 1;
      } else if (argv[i][1] == '2') {
        pcm_half_rate = 1;
      } else if (argv[i][1] == '8') {
        pcm_half_bit = 1;
      } else if (argv[i][1] == 'm') {
        pcm_channels = 1;
      } else if (argv[i][1] == 's') {
        shuffle_mode = 1;
        srand(_PSP);
      } else if (argv[i][1] == 'q') {
        quiet_mode = 1;
      } else if (argv[i][1] == 'i' && i+1 < argc) {

        // indirect file
        fp = fopen(argv[i+1], "r");
        if (fp != NULL) {

          static uint8_t line[ MAX_PATH_LEN + 1 ];
          while (fgets(line, MAX_PATH_LEN, fp) != NULL) {

            for (int16_t i = 0; i < MAX_PATH_LEN; i++) {
              if (line[i] <= ' ') {
                line[i] = '\0';
              }
            }

            if (strlen(line) < 5) continue;

            if (num_music > MAX_MUSIC) {
              printf("error: too many music.\n");
              goto exit;
            }
 
            int16_t volume = pcm_volume;
            for (int16_t i = 0; i < MAX_PATH_LEN; i++) {
              if (line[i] == ',') {
                if (i+2 < MAX_PATH_LEN && line[i+1] == 'v') {
                  int16_t v = atoi(line+i+2);
                  if (v >= 1 && v <= 12) volume = v;
                }
                line[i] = '\0';
                break;
              }
            }
     
            uint8_t* pcm_filename = line;
            uint8_t* pcm_fileext = pcm_filename + strlen(pcm_filename) - 4;
            if (stricmp(pcm_fileext, ".s44") != 0 && stricmp(pcm_fileext, ".a44")) {
              printf("error: not .s44/.a44 data file. (%s)\n", pcm_filename);
              goto exit;
            }
            strcpy(g_pcm_music[ num_music ].file_name, pcm_filename);
            g_pcm_music[ num_music ].volume = volume;
            num_music++;

          }

          fclose(fp);
          fp = NULL;

        }
        i++;
      } else if (argv[i][1] == 'h') {
        show_help_message();
        goto exit;
      } else {
        printf("error: unknown option (%s).\n",argv[i]);
        goto exit;
      }
    } else {

      if (num_music > MAX_MUSIC) {
        printf("error: too many music.\n");
        goto exit;
      }
      
      uint8_t* pcm_filename = argv[i];
      if (strlen(pcm_filename) < 5) {
        printf("error: invalid file name (%s).\n",argv[i]);
      }
      
      uint8_t* pcm_fileext = pcm_filename + strlen(pcm_filename) - 4;
      if (stricmp(pcm_fileext, ".s44") != 0 && stricmp(pcm_fileext, ".a44")) {
        printf("error: not .s44/.a44 data file. (%s)\n", pcm_filename);
        goto exit;
      }
      strcpy(g_pcm_music[ num_music ].file_name, pcm_filename);
      g_pcm_music[ num_music ].volume = pcm_volume;
      num_music++;
    }
  }

  // self keep check
  uint8_t* pdp = check_keep_process(PROGRAM_NAME);

  // remove s44bgp
  if (remove_mode) {
    // self keep check
    if (pdp != NULL) {

      // stop PCM8PP
      pcm8pp_pause();
      pcm8pp_stop();

#ifdef __OPM_TIMER__
      // release OPM timer-B interrupt handle
      while (OPMSNS() & 0x80);
      OPMSET(0x14, 0x00);
      OPMINTST(0);
#else
      // release timer-D interrupt handle
      TIMERDST(0,0,0);
#endif

      // release allocated high memory buffers
      uint8_t* mem_end = (uint8_t*)B_LPEEK((uint32_t*)(pdp - 8));
      uint8_t* check_addr = (uint8_t*)(pdp + 256);
      int16_t num_free = 0;
      while (check_addr < mem_end) {
        if (memcmp(check_addr, EYE_CATCH, EYE_CATCH_LEN) == 0) {
          //printf("found eye catch at %X\n", check_addr);
          uint32_t himem_addr = B_LPEEK((uint32_t*)(check_addr + EYE_CATCH_LEN));
          if (himem_addr != 0) {
            himem_free((void*)himem_addr, 1);
          }
          num_free++;
        }
        check_addr += 2;
        if (num_free >= MAX_MUSIC) break;
      }

      // release program memory itself
      MFREE((uint32_t)pdp);

      // reset function key display
      int32_t fnkmod = C_FNKMOD(-1);
      //C_FNKMOD(2);
      C_FNKMOD(fnkmod);

      printf("removed " PROGRAM_NAME " successfully.\n");

      rc = 0;

    } else {
      printf(PROGRAM_NAME " is not running.\n");
      rc = 1;
    }

    goto exit;
  }

  if (num_music == 0) {
    show_help_message();
    goto exit;
  }

  if (pdp != NULL) {
    printf("error: " PROGRAM_NAME " is already running.\n");
    rc = 1;
    goto exit;
  }

  // check high memory driver availability
  if (!himem_isavailable()) {
    printf("error: high memory driver is not available.\n");
    goto exit;
  }

  // check pcm8pp
  if (!pcm8pp_keepchk()) {
    printf("error: PCM8PP.X is not running.\n");
    goto exit;
  }

  // file read buffer on main memory
  fread_buffer = himem_malloc(FREAD_BUFFER_LEN * sizeof(int16_t) * 2, 0);
  if (fread_buffer == NULL) {
    printf("error: main memory allocation error. (out of memory?)\n");
    goto exit;
  }

  // ym2608 decode handle
  if (ym2608_decode_init(&ym2608_decode, YM2608_DECODE_BUFFER_BYTES, 44100, 2) != 0) {
    printf("error: ym2608 decode buffer allocation error. (out of memory?)\n");
    goto exit;
  }

  // information
  printf("PCM frequency: %d [Hz]\n", pcm_half_rate ? 22050 : 44100);
  printf("PCM channels: %s\n", pcm_channels == 1 ? "mono" : "stereo");
  printf("PCM bits: %d\n", pcm_half_bit ? 8 : 16);
  printf("--\n");
  printf("Available high memory: %d [KB]\n", himem_getsize(1) / 1024);

  // load pcm data to high memory
  for (int16_t i = 0; i < num_music; i++) {

    PCM_MUSIC* pcm = &(g_pcm_music[i]);

    // ym2608 adpcm format?
    uint8_t* pcm_filename = pcm->file_name;
    uint8_t* pcm_fileext = pcm_filename + strlen(pcm_filename) - 4;
    int16_t ym2608 = stricmp(pcm_fileext, ".a44") == 0 ? 1 : 0;

    // kmd
    static uint8_t kmd_filename[ MAX_PATH_LEN ];
    strcpy(kmd_filename, pcm_filename);
    strcpy(kmd_filename + strlen(kmd_filename) - 4, ".kmd");
    fp = fopen(kmd_filename, "r");
    if (fp != NULL) {
      if (kmd_init(&(pcm->kmd), fp) != 0) {
        printf("warn: KMD file read error. (%s)\n", kmd_filename);
      }
      fclose(fp);
      fp = NULL;      
    }

    // open a pcm file
    fp = fopen(pcm_filename, "rb");
    if (fp == NULL) {
      printf("error: file open error. (%s)\n", pcm_filename);
      goto exit;
    }

    // check data length in 16bit unit
    fseek(fp, 0, SEEK_END);
    size_t data_len = ftell(fp) / sizeof(int16_t);
    fseek(fp, 0, SEEK_SET);

    // allocate high memory
    size_t allocate_bytes = data_len * sizeof(int16_t) * (ym2608 ? 4 : 1) / (3 - pcm_channels) / (1 + pcm_half_rate) / (1 + pcm_half_bit);
    pcm->buffer = himem_malloc(allocate_bytes, 1);
    if (pcm->buffer == NULL) {
      printf("error: high memory allocation error. (out of memory?)\n");
      goto exit;
    }

    // total music time
    pcm->total_time_msec = (uint32_t)(data_len * 1000.0 * (ym2608 ? 4 : 1 ) / 44100.0 / 2.0);

    // load data to high memory
    if (!ym2608) {

      // .s44

      if (pcm_channels == 2 && pcm_half_rate == 0 && pcm_half_bit == 0) {

        // 16bit through

        size_t read_len = 0;
        do {

          if (B_SFTSNS() & 0x01) {
            goto cancel;
          }

          size_t len = fread(fread_buffer, sizeof(int16_t), FREAD_BUFFER_LEN, fp);
          if (len == 0) break;

          memcpy(pcm->buffer + read_len, fread_buffer, len * sizeof(int16_t));

          read_len += len;
          printf("\rLoading %s (%4.2f%%) ... [SHIFT] key to cancel.", pcm_filename, read_len * 100.0 / data_len);

        } while (read_len < data_len);

      } else if (pcm_half_bit == 0) {

        // 16bit

        // stereo to mono and/or 44.1 to 22.05 down sampling
        size_t read_len = 0;
        uint32_t num_samples = 0;
        int16_t* gma = pcm->buffer;
        do {

          if (B_SFTSNS() & 0x01) {
            goto cancel;
          }

          size_t len = fread(fread_buffer, sizeof(int16_t), FREAD_BUFFER_LEN, fp);
          if (len == 0) break;

          for (size_t j = 0; j < len/2; j++) {

            // down sampling in half rate mode
            num_samples++;
            if (pcm_half_rate && !(num_samples & 0x01)) continue;

            if (pcm_channels == 1) {
              // stereo to mono
              gma[0] = ( fread_buffer[ j * 2 + 0 ] + fread_buffer[ j * 2 + 1 ] ) / 2;
              gma++;
            } else {
              // stereo
              gma[0] = fread_buffer[ j * 2 + 0 ];
              gma[1] = fread_buffer[ j * 2 + 1 ];
              gma += 2;
            }
          }

          read_len += len;
          printf("\rLoading %s (%4.2f%%) ... [SHIFT] key to cancel.", pcm_filename, read_len * 100.0 / data_len);

        } while (read_len < data_len);

      } else {

        // 8bit

        // stereo to mono and/or 44.1 to 22.05 down sampling
        size_t read_len = 0;
        uint32_t num_samples = 0;
        int8_t* gma = (int8_t*)pcm->buffer;
        do {

          if (B_SFTSNS() & 0x01) {
            goto cancel;
          }

          size_t len = fread(fread_buffer, sizeof(int16_t), FREAD_BUFFER_LEN, fp);
          if (len == 0) break;

          for (size_t j = 0; j < len/2; j++) {

            // down sampling in half rate mode
            num_samples++;
            if (pcm_half_rate && !(num_samples & 0x01)) continue;

            if (pcm_channels == 1) {
              // stereo to mono
              gma[0] = ( fread_buffer[ j * 2 + 0 ] + fread_buffer[ j * 2 + 1 ] ) / 2 / 256;
              gma++;
            } else {
              // stereo
              gma[0] = fread_buffer[ j * 2 + 0 ] / 256;
              gma[1] = fread_buffer[ j * 2 + 1 ] / 256;
              gma += 2;
            }
          }

          read_len += len;
          printf("\rLoading %s (%4.2f%%) ... [SHIFT] key to cancel.", pcm_filename, read_len * 100.0 / data_len);

        } while (read_len < data_len);

      }

    } else {

      // .a44

      if (pcm_half_bit == 0) {

        // 16bit

        // stereo to mono and/or 44.1 to 22.05 down sampling
        size_t read_len = 0;
        uint32_t num_samples = 0;
        int16_t* gma = pcm->buffer;
        do {

          if (B_SFTSNS() & 0x01) {
            goto cancel;
          }

          size_t len = fread(fread_buffer, sizeof(int16_t), YM2608_DECODE_BUFFER_BYTES / 4 / sizeof(int16_t), fp);
          if (len == 0) break;

          size_t decode_len = ym2608_decode_exec(&ym2608_decode, (uint8_t*)fread_buffer, len * sizeof(int16_t));

          for (size_t j = 0; j < decode_len/2; j++) {

            // down sampling in half rate mode
            num_samples++;
            if (pcm_half_rate && !(num_samples & 0x01)) continue;

            int16_t* decode_buffer = ym2608_decode.decode_buffer;
            if (pcm_channels == 1) {
              // stereo to mono
              gma[0] = ( decode_buffer[ j * 2 + 0 ] + decode_buffer[ j * 2 + 1 ] ) / 2;
              gma++;
            } else {
              // stereo
              gma[0] = decode_buffer[ j * 2 + 0 ];
              gma[1] = decode_buffer[ j * 2 + 1 ];
              gma += 2;
            }
          }

          read_len += len;
          printf("\rLoading %s (%4.2f%%) ... [SHIFT] key to cancel.", pcm_filename, read_len * 100.0 / data_len);

        } while (read_len < data_len);

      } else {

        // 8bit

        // stereo to mono and/or 44.1 to 22.05 down sampling
        size_t read_len = 0;
        uint32_t num_samples = 0;
        int8_t* gma = (int8_t*)pcm->buffer;
        do {

          if (B_SFTSNS() & 0x01) {
            goto cancel;
          }

          size_t len = fread(fread_buffer, sizeof(int16_t), YM2608_DECODE_BUFFER_BYTES / 4 / sizeof(int16_t), fp);
          if (len == 0) break;

          size_t decode_len = ym2608_decode_exec(&ym2608_decode, (uint8_t*)fread_buffer, len * sizeof(int16_t));

          for (size_t j = 0; j < decode_len/2; j++) {

            // down sampling in half rate mode
            num_samples++;
            if (pcm_half_rate && !(num_samples & 0x01)) continue;

            int16_t* decode_buffer = ym2608_decode.decode_buffer;
            if (pcm_channels == 1) {
              // stereo to mono
              gma[0] = ( decode_buffer[ j * 2 + 0 ] + decode_buffer[ j * 2 + 1 ] ) / 2 / 256;
              gma++;
            } else {
              // stereo
              gma[0] = decode_buffer[ j * 2 + 0 ] / 256;
              gma[1] = decode_buffer[ j * 2 + 1 ] / 256;
              gma += 2;
            }
          }

          read_len += len;
          printf("\rLoading %s (%4.2f%%) ... [SHIFT] key to cancel.", pcm_filename, read_len * 100.0 / data_len);

        } while (read_len < data_len);

      }

    }

    fclose(fp);
    fp = NULL;

    pcm->buffer_bytes = allocate_bytes;

    printf("\rLoaded %s (%3.1fsec) into high memory.\x1b[K\n", pcm_filename, pcm->total_time_msec / 1000.0);
    printf("Available high memory: %d [KB]\n", himem_getsize(1) / 1024);
  
  }

  // reclaim file read buffer
  if (fread_buffer != NULL) {
    himem_free(fread_buffer, 0);
    fread_buffer = NULL;
  }

  // close ym2608 decode handle
  ym2608_decode_close(&ym2608_decode);

  // global counters
  g_num_music = num_music;
  g_shuffle_mode = shuffle_mode;
  g_quiet_mode = quiet_mode;
  g_paused = 0;
  g_elapsed_time = 0;
  g_current_music = g_shuffle_mode ? rand() % g_num_music : 0;
#ifdef __OPM_TIMER__
  g_int_counter = OPM_INTERVAL_COUNT;
#else
  g_int_counter = TIMERD_INTERVAL_COUNT;
#endif
  g_pcm8pp_freq = pcm_channels == 1 && pcm_half_bit == 0 && pcm_half_rate == 0 ? 0x0d :
                  pcm_channels == 1 && pcm_half_bit == 0 && pcm_half_rate == 1 ? 0x0a :
                  pcm_channels == 1 && pcm_half_bit == 1 && pcm_half_rate == 0 ? 0x15 :
                  pcm_channels == 1 && pcm_half_bit == 1 && pcm_half_rate == 1 ? 0x12 :
                  pcm_channels == 2 && pcm_half_bit == 0 && pcm_half_rate == 0 ? 0x1d : 
                  pcm_channels == 2 && pcm_half_bit == 0 && pcm_half_rate == 1 ? 0x1a :
                  pcm_channels == 2 && pcm_half_bit == 1 && pcm_half_rate == 0 ? 0x25 :
                  pcm_channels == 2 && pcm_half_bit == 1 && pcm_half_rate == 1 ? 0x22 : 0x1d;                      

#ifdef __OPM_TIMER__
  // $14:OPM Timer Control
  // disable timer-A/B count and interrupt
  while (OPMSNS() & 0x80);
  OPMSET(0x14, 0x00);

  // set OPM timer-B interrupt handler
  if (OPMINTST((uint8_t*)(__timer_interrupt_handler__)) != 0) {
    printf("error: opm interrupt is being used by other applications. (any FM driver?)\n");
    goto exit;
  }

  // $12:CLKB
  // CLKB=6   ... Tb(ms) = 1024 * (256 -   6) / 4000 = 64ms
  // CLKB=131 ... Tb(ms) = 1024 * (256 - 131) / 4000 = 32ms
  while (OPMSNS() & 0x80);
//  OPMSET(0x12, 131);
  OPMSET(0x12, 6);

  // $14:OPM Timer Control
  // bit5:timer-B overflow reset bit3:timer-B interrupt enable, bit1:timer-B start
  while (OPMSNS() & 0x80);
  OPMSET(0x14, 0x2a);
#else
  // set timer-D interrupt handler
  if (TIMERDST((uint8_t*)(__timer_interrupt_handler__), 7, 200) != 0) {
    printf("error: timer-D interrupt is being used by other applications. (CONFIG.SYS PROCESS= ?)\n");
    goto exit;
  }
#endif

  // start pcm8pp play
  PCM_MUSIC* current_pcm = &(g_pcm_music[ g_current_music ]);
  pcm8pp_play(PCM8PP_CHANNEL, ( current_pcm->volume << 16 ) | ( g_pcm8pp_freq << 8 ) | 0x03, current_pcm->buffer_bytes, 44100*256, current_pcm->buffer);
  if (!quiet_mode) {
    B_PUTMES(6, 0, 31, 2, SJIS_ONPU);
    if (current_pcm->kmd.tag_title[0] != '\0') {
      B_PUTMES(6, 2, 31, MAX_DISP_LEN - 2, current_pcm->kmd.tag_title);
    } else {
      B_PUTMES(6, 2, 31, MAX_DISP_LEN - 2, current_pcm->file_name);
    }
  }

  printf("--\n");
  printf(PROGRAM_NAME " background playback service started. [CTRL]+[XF4] to pause. [CTRL]+[XF5] to skip.\n");

  rc = 0;

  // keep self process
  extern uint32_t _HEND;
//  extern unsigned int _PSP;
  KEEPPR(_HEND - _PSP - 0xf0, rc);

cancel:
  printf("\r\nCanceled.\n");

exit:

  // close file handle if opened
  if (fp != NULL) {
    fclose(fp);
    fp = NULL;
  }

  // reclaim high memory buffers if opened
  for (int16_t i = 0; i < MAX_MUSIC; i++) {
    PCM_MUSIC* pcm = &(g_pcm_music[i]);
    if (pcm->buffer != NULL) {
      himem_free(pcm->buffer, 1);
      pcm->buffer = NULL;
    }
    kmd_close(&(pcm->kmd));
  }

  // reclaim file read buffer if opened
  if (fread_buffer != NULL) {
    himem_free(fread_buffer, 0);
    fread_buffer = NULL;
  }

  // close ym2608 decoder handle
  ym2608_decode_close(&ym2608_decode);

  return rc;
}