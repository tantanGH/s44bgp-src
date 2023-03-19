#ifndef __H_KMD__
#define __H_KMD__

#include <stdint.h>
#include <stddef.h>

#define KMD_POS_X_MIN (0)
#define KMD_POS_X_MAX (30)
#define KMD_POS_Y_MIN (0)
#define KMD_POS_Y_MAX (2)
#define KMD_MAX_MESSAGE_LEN (62)
#define KMD_MAX_LINE_LEN (256)

typedef struct {
  int16_t pos_x;
  int16_t pos_y;
  uint32_t start_msec;
  uint32_t end_msec;
  uint8_t message[ KMD_MAX_MESSAGE_LEN + 1 ];
} KMD_EVENT;

typedef struct {
  size_t current_event_ofs;
  size_t num_events;
  KMD_EVENT* events;
  uint8_t tag_title[ KMD_MAX_MESSAGE_LEN + 1 ];
  uint8_t tag_artist[ KMD_MAX_MESSAGE_LEN + 1 ];
  uint8_t tag_album[ KMD_MAX_MESSAGE_LEN + 1 ];
} KMD_HANDLE;

int32_t kmd_init(KMD_HANDLE* kmd, FILE* fp);
void kmd_close(KMD_HANDLE* kmd);
KMD_EVENT* kmd_next_event(KMD_HANDLE* kmd);

#endif