// Copyright (c) 2010 TeamXBMC. All rights reserved.

#include <unistd.h>
#include <queue>
#include <vector>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "ffmpeg_common.h"
#include "ffmpeg_file_protocol.h"
#include "file_reader_util.h"

// based on amcodec
extern "C" {
#include <codec.h>
}  // extern "C"

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
// AppContext - Application state
#define PTS_FREQ    90000
#define UNIT_FREQ   96000
#define AV_SYNC_THRESH PTS_FREQ*30

#define INT64_0     INT64_C(0x8000000000000000)

#define EXTERNAL_PTS (1)
#define SYNC_OUTSIDE (2)

typedef int CODEC_TYPE;
#define CODEC_UNKNOW        (0)
#define CODEC_VIDEO         (1)
#define CODEC_AUDIO         (2)
#define CODEC_COMPLEX       (3)
#define CODEC_SUBTITLE      (4)

#define RW_WAIT_TIME        (20 * 1000) //20ms

#define P_PRE       (0x02000000)
#define F_PRE       (0x03000000)
#define PLAYER_SUCCESS          (0)
#define PLAYER_FAILED           (-(P_PRE|0x01))
#define PLAYER_NOMEM            (-(P_PRE|0x02))
#define PLAYER_EMPTY_P          (-(P_PRE|0x03))

#define PLAYER_WR_FAILED        (-(P_PRE|0x21))
#define PLAYER_WR_EMPTYP        (-(P_PRE|0x22))
#define PLAYER_WR_FINISH        (P_PRE|0x1)

#define PLAYER_PTS_ERROR        (-(P_PRE|0x31))
#define PLAYER_CHECK_CODEC_ERROR  (-(P_PRE|0x39))

#define FREE free
#define MALLOC malloc

#define log_print printf
#define log_error printf


#define HDR_BUF_SIZE 1024
typedef struct hdr_buf {
    char *data;
    int size;
} hdr_buf_t;

typedef struct am_packet {
    CODEC_TYPE    type;
    AVPacket      *avpkt;
    int           avpkt_isvalid;
    int           avpkt_newflag;
    unsigned char *data;
    unsigned char *buf;
    int           data_size;
    int           buf_size;
    hdr_buf_t     *hdr;
    codec_para_t  *codec;
} am_packet_t;

typedef enum {
    STREAM_UNKNOWN = 0,
    STREAM_TS,
    STREAM_PS,
    STREAM_ES,
    STREAM_RM,
    STREAM_AUDIO,
    STREAM_VIDEO,
} pstream_type;

typedef union {
    int64_t      total_bytes;
    unsigned int vpkt_num;
    unsigned int apkt_num;
    unsigned int spkt_num;
} read_write_size;

typedef struct {
    int             has_video;
    vformat_t       video_format;
    signed short    video_index;
    unsigned short  video_pid;
    unsigned int    video_width;
    unsigned int    video_height;
    float           video_ratio;
    int             check_first_pts;
    int             flv_flag;
    int             h263_decodable;
    int64_t         start_time;
    float           video_duration;
    float           video_pts;
    unsigned int    video_rate;
    unsigned int    video_codec_rate;
    vdec_type_t     video_codec_type;
    int             vdec_buf_len;
} v_stream_info_t;

typedef  struct {
    unsigned int read_end_flag: 1;
    unsigned int end_flag: 1;
    unsigned int reset_flag: 1;
    int time_point;
    int check_lowlevel_eagain_cnt;
} p_ctrl_info_t;

typedef struct player_info
{
	int start_time;
	int pts_video;
} player_info_t;

typedef struct
{
  FFmpegFileReader  *demuxer;
  AVCodecContext    *codec_context;
  AVFormatContext   *format_context;
  codec_para_t      vcodec;

  player_info_t     state;
  p_ctrl_info_t     playctrl_info;
  pstream_type      stream_type;
  v_stream_info_t   vstream_info;
  am_packet_t       am_pkt;

  read_write_size   read_size;
  read_write_size   write_size;
} AppContext;

/* g_signal_abort is set to 1 in term/int signal handler */
static unsigned int g_signal_abort = 0;
static void signal_handler(int iSignal)
{
  g_signal_abort = 1;
  printf("Terminating - Program received %s signal\n", \
    (iSignal == SIGINT? "SIGINT" : (iSignal == SIGTERM ? "SIGTERM" : "UNKNOWN")));
}

uint64_t CurrentHostCounter(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return( ((int64_t)now.tv_sec * 1000000000L) + now.tv_nsec );
}

uint64_t CurrentHostFrequency(void)
{
  return( (uint64_t)1000000000L );
}

void dump_extradata(AppContext *ctx)
{
  for (int i=0; i < ctx->codec_context->extradata_size-1; i++) {
    printf("0x%02X,", ctx->codec_context->extradata[i]);
    if ((i & 0xF) == 0xF) printf("\n");
  }
  printf("0x%02X\n", ctx->codec_context->extradata[ctx->codec_context->extradata_size-1]);
}

int osd_blank(const char *path, int cmd)
{
	int fd;
	char  bcmd[16];
	fd = open(path, O_CREAT|O_RDWR | O_TRUNC, 0644);
	if (fd >= 0)
  {
    sprintf(bcmd, "%d", cmd);
    write(fd, bcmd, strlen(bcmd));
    close(fd);
    return 0;
  }
	return -1;
}

static void player_para_init(AppContext *para)
{
    para->state.start_time = -1;
    para->vstream_info.video_index = -1;
    para->vstream_info.start_time = -1;
}

/*************************************************************************/
void am_packet_init(am_packet_t *pkt)
{
  pkt->avpkt  = NULL;
  pkt->avpkt_isvalid = 0;
  pkt->avpkt_newflag = 0;
  pkt->codec  = NULL;
  pkt->hdr    = NULL;
  pkt->buf    = NULL;
  pkt->buf_size = 0;
  pkt->data   = NULL;
  pkt->data_size  = 0;
}

/*************************************************************************/
static int check_vcodec_state(codec_para_t *codec, struct vdec_status *dec, struct buf_status *buf)
{
    int ret;

    ret = codec_get_vbuf_state(codec,  buf);
    if (ret != 0) {
        log_error("codec_get_vbuf_state error: %x\n", -ret);
    }

    ret = codec_get_vdec_state(codec, dec);
    if (ret != 0) {
        log_error("codec_get_vdec_state error: %x\n", -ret);
        ret = PLAYER_CHECK_CODEC_ERROR;
    }

    return ret;
}

int check_in_pts(AppContext *para, am_packet_t *pkt)
{
    int last_duration = 0;
    static int last_v_duration = 0, last_a_duration = 0;
    int64_t pts;
    float time_base_ratio = 0;
    long long start_time = 0;

    if (pkt->type == CODEC_VIDEO) {
        time_base_ratio = para->vstream_info.video_pts;
        start_time = para->vstream_info.start_time;
        last_duration = last_v_duration;
    }

    if (para->stream_type == STREAM_ES && (pkt->type == CODEC_VIDEO || pkt->type == CODEC_AUDIO)) {
        if ((int64_t)INT64_0 != pkt->avpkt->pts) {
            pts = pkt->avpkt->pts * time_base_ratio;
            if (pts < start_time) {
                pts = pts * last_duration;
            }

            if (codec_checkin_pts(pkt->codec, pts) != 0) {
                log_error("ERROR check in pts error!\n");
                return PLAYER_PTS_ERROR;
            }
            //log_print("[check_in_pts:%d]type=%d pkt->pts=%llx pts=%llx start_time=%llx \n",__LINE__,pkt->type,pkt->avpkt->pts,pts, start_time);

        } else if ((int64_t)INT64_0 != pkt->avpkt->dts) {
            pts = pkt->avpkt->dts * time_base_ratio * last_duration;
            //log_print("[check_in_pts:%d]type=%d pkt->dts=%llx pts=%llx time_base_ratio=%.2f last_duration=%d\n",__LINE__,pkt->type,pkt->avpkt->dts,pts,time_base_ratio,last_duration);

            if (codec_checkin_pts(pkt->codec, pts) != 0) {
                log_error("ERROR check in dts error!\n");
                return PLAYER_PTS_ERROR;
            }

            if (pkt->type == CODEC_AUDIO) {
                last_a_duration = pkt->avpkt->duration ? pkt->avpkt->duration : 1;
            } else if (pkt->type == CODEC_VIDEO) {
                last_v_duration = pkt->avpkt->duration ? pkt->avpkt->duration : 1;
            }
        } else {
            if (!para->vstream_info.check_first_pts && pkt->type == CODEC_VIDEO) {
                if (codec_checkin_pts(pkt->codec, 0) != 0) {
                    log_print("ERROR check in 0 to audio pts error!\n");
                    return PLAYER_PTS_ERROR;
                }
            }
        }
        if (pkt->type == CODEC_VIDEO && !para->vstream_info.check_first_pts) {
            para->vstream_info.check_first_pts = 1;
        }
    }
    return PLAYER_SUCCESS;
}

static int check_write_finish(AppContext *para, am_packet_t *pkt)
{
    if (para->playctrl_info.read_end_flag) {
        if ((para->write_size.vpkt_num == para->read_size.vpkt_num) &&
            (para->write_size.apkt_num == para->read_size.apkt_num)) {
            return PLAYER_WR_FINISH;
        }
    }
    return PLAYER_WR_FAILED;
}

static int write_header(AppContext *para, am_packet_t *pkt)
{
    int write_bytes = 0, len = 0;

    if (pkt->hdr && pkt->hdr->size > 0) {
        if ((NULL == pkt->codec) || (NULL == pkt->hdr->data)) {
            log_error("[write_header]codec null!\n");
            return PLAYER_EMPTY_P;
        }
        while (1) {
            write_bytes = codec_write(pkt->codec, pkt->hdr->data + len, pkt->hdr->size - len);
            if (write_bytes < 0 || write_bytes > (pkt->hdr->size - len)) {
                if (-errno != AVERROR(EAGAIN)) {
                    log_print("ERROR:write header failed!\n");
                    return PLAYER_WR_FAILED;
                } else {
                    continue;
                }
            } else {
                len += write_bytes;
                if (len == pkt->hdr->size) {
                    break;
                }
            }
        }
    }
    return PLAYER_SUCCESS;
}

int check_avbuffer_enough(AppContext *para, am_packet_t *pkt)
{
    return 1;
}
int write_av_packet(AppContext *para, am_packet_t *pkt)
{
    int write_bytes = 0, len = 0, ret;
    unsigned char *buf;
    int size ;

    if (pkt->avpkt_newflag) {
        if (pkt->type != CODEC_SUBTITLE) {
            if (pkt->avpkt_isvalid) {
                ret = check_in_pts(para, pkt);
                if (ret != PLAYER_SUCCESS) {
                    log_error("check in pts failed\n");
                    return PLAYER_WR_FAILED;
                }
            }
            if (write_header(para, pkt) == PLAYER_WR_FAILED) {
                log_error("[%s]write header failed!\n", __FUNCTION__);
                return PLAYER_WR_FAILED;
            }
        } else {
            // process_es_subtitle(para, pkt);
        }
        pkt->avpkt_newflag = 0;
    }
	
    buf = pkt->data;
    size = pkt->data_size ;
    if (size == 0 && pkt->avpkt_isvalid) {
        if ((pkt->type == CODEC_VIDEO)) {
            para->write_size.vpkt_num++;
        }
        if (pkt->avpkt) {
            av_free_packet(pkt->avpkt);
        }
        pkt->avpkt_isvalid = 0;
    }
    while (size > 0 && pkt->avpkt_isvalid) {
        write_bytes = codec_write(pkt->codec, (char *)buf, size);
        if (write_bytes < 0 || write_bytes > size) {
            if (-errno != AVERROR(EAGAIN)) {
                para->playctrl_info.check_lowlevel_eagain_cnt = 0;
                log_print("write codec data failed!\n");
                return PLAYER_WR_FAILED;
            } else {
                // EAGAIN to see if buffer full or write time out too much		
                if (check_avbuffer_enough(para, pkt)) {
                  para->playctrl_info.check_lowlevel_eagain_cnt++;
                } else {
                  para->playctrl_info.check_lowlevel_eagain_cnt = 0;
                }
                
                if (para->playctrl_info.check_lowlevel_eagain_cnt > 50) {
                    // reset decoder
                    para->playctrl_info.check_lowlevel_eagain_cnt = 0;
                    para->playctrl_info.reset_flag = 1;
                    para->playctrl_info.end_flag = 1;
                    if (para->state.start_time != -1) {
                        para->playctrl_info.time_point = (para->state.pts_video - para->state.start_time)/ PTS_FREQ;
                    } else {
                        para->playctrl_info.time_point = para->state.pts_video/ PTS_FREQ;
                    }
                    
                    log_print("$$$$$$[type:%d] write blocked, need reset decoder!$$$$$$\n", pkt->type);
                }				
                pkt->data += len;
                pkt->data_size -= len;
                usleep(RW_WAIT_TIME);
                if (para->playctrl_info.check_lowlevel_eagain_cnt > 0) {
                    log_print("[%s]eagain:data_size=%d type=%d rsize=%lld wsize=%lld cnt=%d\n", \
                        __FUNCTION__, pkt->data_size, pkt->type, para->read_size.total_bytes, \
                        para->write_size.total_bytes, para->playctrl_info.check_lowlevel_eagain_cnt);
                }
                return PLAYER_SUCCESS;
            }
        } else {
            para->playctrl_info.check_lowlevel_eagain_cnt = 0;
            len += write_bytes;
            if (len == pkt->data_size) {
                if ((pkt->type == CODEC_VIDEO)) {
                    para->write_size.vpkt_num++;
                }
                if (pkt->avpkt) {
                    av_free_packet(pkt->avpkt);
                }
                pkt->avpkt_isvalid = 0;
                pkt->data_size = 0;
                //log_print("[%s:%d]write finish pkt->data_size=%d\r",__FUNCTION__, __LINE__,pkt->data_size);               
                break;
            } else if (len < pkt->data_size) {
                buf += write_bytes;
                size -= write_bytes;
            } else {
                return PLAYER_WR_FAILED;
            }

        }
    }
    if (check_write_finish(para, pkt) == PLAYER_WR_FINISH) {
        return PLAYER_WR_FINISH;
    }
    return PLAYER_SUCCESS;
}

static int check_size_in_buffer(unsigned char *p, int len)
{
    unsigned int size;
    unsigned char *q = p;
    while ((q + 4) < (p + len)) {
        size = (*q << 24) | (*(q + 1) << 16) | (*(q + 2) << 8) | (*(q + 3));
        if (size & 0xff000000) {
            return 0;
        }

        if (q + size + 4 == p + len) {
            return 1;
        }

        q += size + 4;
    }
    return 0;
}

static int check_size_in_buffer3(unsigned char *p, int len)
{
    unsigned int size;
    unsigned char *q = p;
    while ((q + 3) < (p + len)) {
        size = (*q << 16) | (*(q + 1) << 8) | (*(q + 2));

        if (q + size + 3 == p + len) {
            return 1;
        }

        q += size + 3;
    }
    return 0;
}

static int check_size_in_buffer2(unsigned char *p, int len)
{
    unsigned int size;
    unsigned char *q = p;
    while ((q + 2) < (p + len)) {
        size = (*q << 8) | (*(q + 1));

        if (q + size + 2 == p + len) {
            return 1;
        }

        q += size + 2;
    }
    return 0;
}

static int h264_add_header(unsigned char *buf, int size, am_packet_t *pkt)
{
    char nal_start_code[] = {0x0, 0x0, 0x0, 0x1};
    int nalsize;
    unsigned char* p;
    int tmpi;
    unsigned char* extradata = buf;
    int header_len = 0;
    char* buffer = pkt->hdr->data;

    p = extradata;
    if (size < 4) {
        return PLAYER_FAILED;
    }

    if (size < 10) {
        printf("avcC too short\n");
        return PLAYER_FAILED;
    }

    if (*p != 1) {
        printf(" Unkonwn avcC version %d\n", *p);
        return PLAYER_FAILED;
    }

    int cnt = *(p + 5) & 0x1f; //number of sps
    // printf("number of sps :%d\n", cnt);
    p += 6;
    for (tmpi = 0; tmpi < cnt; tmpi++) {
        nalsize = (*p << 8) | (*(p + 1));
        memcpy(&(buffer[header_len]), nal_start_code, 4);
        header_len += 4;
        memcpy(&(buffer[header_len]), p + 2, nalsize);
        header_len += nalsize;
        p += (nalsize + 2);
    }

    cnt = *(p++); //Number of pps
    // printf("number of pps :%d\n", cnt);
    for (tmpi = 0; tmpi < cnt; tmpi++) {
        nalsize = (*p << 8) | (*(p + 1));
        memcpy(&(buffer[header_len]), nal_start_code, 4);
        header_len += 4;
        memcpy(&(buffer[header_len]), p + 2, nalsize);
        header_len += nalsize;
        p += (nalsize + 2);
    }
    if (header_len >= HDR_BUF_SIZE) {
        printf("header_len %d is larger than max length\n", header_len);
        return 0;
    }
    pkt->hdr->size = header_len;
    pkt->type = CODEC_VIDEO;

    return PLAYER_SUCCESS;
}
static int h264_write_header(AppContext *para, am_packet_t *pkt)
{
    AVCodecContext *avcodec;
    int ret = -1;

    avcodec = para->codec_context;
    ret = h264_add_header(avcodec->extradata, avcodec->extradata_size, pkt);
    if (ret == PLAYER_SUCCESS) {
        //if (ctx->vcodec) {
        if (1) {
            pkt->codec = &para->vcodec;
        } else {
            log_print("[pre_header_feeding]invalid video codec!\n");
            return PLAYER_EMPTY_P;
        }

        pkt->avpkt_newflag = 1;
        ret = write_av_packet(para, pkt);
    }
    return ret;
}

int pre_header_feeding(AppContext *para, am_packet_t *pkt)
{
    int ret;
    if (para->stream_type == STREAM_ES && para->vstream_info.has_video) {
        if (pkt->hdr == NULL) {
            pkt->hdr = (hdr_buf_t*)MALLOC(sizeof(hdr_buf_t));
            pkt->hdr->data = (char *)MALLOC(HDR_BUF_SIZE);
            if (!pkt->hdr->data) {
                log_print("[pre_header_feeding] NOMEM!");
                return PLAYER_NOMEM;
            }
        }

        if (VFORMAT_H264 == para->vstream_info.video_format) {
            ret = h264_write_header(para, pkt);
            if (ret != PLAYER_SUCCESS) {
                return ret;
            }
        }


        if (pkt->hdr) {
            if (pkt->hdr->data) {
                FREE(pkt->hdr->data);
                pkt->hdr->data = NULL;
            }
            FREE(pkt->hdr);
            pkt->hdr = NULL;
        }
    }
    return PLAYER_SUCCESS;
}

int h264_update_frame_header(am_packet_t *pkt)
{
    int nalsize, size = pkt->data_size;
    unsigned char *data = pkt->data;
    unsigned char *p = data;
    if (p != NULL) {
        if (check_size_in_buffer(p, size)) {
            while ((p + 4) < (data + size)) {
                nalsize = (*p << 24) | (*(p + 1) << 16) | (*(p + 2) << 8) | (*(p + 3));
                *p = 0;
                *(p + 1) = 0;
                *(p + 2) = 0;
                *(p + 3) = 1;
                p += (nalsize + 4);
            }
            return PLAYER_SUCCESS;
        } else if (check_size_in_buffer3(p, size)) {
            while ((p + 3) < (data + size)) {
                nalsize = (*p << 16) | (*(p + 1) << 8) | (*(p + 2));
                *p = 0;
                *(p + 1) = 0;
                *(p + 2) = 1;
                p += (nalsize + 3);
            }
            return PLAYER_SUCCESS;
        } else if (check_size_in_buffer2(p, size)) {
            unsigned char *new_data;
            int new_len = 0;

            new_data = (unsigned char *)MALLOC(size + 2 * 1024);
            if (!new_data) {
                return PLAYER_NOMEM;
            }

            while ((p + 2) < (data + size)) {
                nalsize = (*p << 8) | (*(p + 1));
                *(new_data + new_len) = 0;
                *(new_data + new_len + 1) = 0;
                *(new_data + new_len + 2) = 0;
                *(new_data + new_len + 3) = 1;
                memcpy(new_data + new_len + 4, p + 2, nalsize);
                p += (nalsize + 2);
                new_len += nalsize + 4;
            }

            FREE(pkt->buf);

            pkt->buf = new_data;
            pkt->buf_size = size + 2 * 1024;
            pkt->data = pkt->buf;
            pkt->data_size = new_len;
        }
    } else {
        log_error("[%s]invalid pointer!\n", __FUNCTION__);
        return PLAYER_FAILED;
    }
    return PLAYER_SUCCESS;
}

int main(int argc, char * const argv[])
{
	int ret = CODEC_ERROR_NONE;
  std::string input_filename;

  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
      if (strncasecmp(argv[i], "--input", 7) == 0) {
        // check the next arg with the proper value.
        int next = i + 1;
        if (next < argc) {
          input_filename = argv[next];
          i++;
        }
      } else if (strncasecmp(argv[i], "-h", 2) == 0 || strncasecmp(argv[i], "--help", 6) == 0) {
        printf("Usage: %s [OPTIONS]...\n", argv[0]);
        printf("Arguments:\n");
        printf("  --input <filename> \tInput video filename\n");
        exit(0);
      }
    }
  }
  if (input_filename.empty()) {
    printf("no input file specified\n");
    exit(0);
  }

  // install signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // initialize App contex.
  AVPacket avpkt;
  AppContext ctx;
  memset(&ctx, 0, sizeof(ctx));

  player_para_init(&ctx);
  am_packet_init(&ctx.am_pkt);

  ctx.am_pkt.avpkt = &avpkt;
  av_init_packet(ctx.am_pkt.avpkt);

  // create the ffmepg file reader/demuxer
  ctx.demuxer = new FFmpegFileReader(input_filename.c_str());
  if (!ctx.demuxer->Initialize()) {
    fprintf(stderr, "ERROR: Can't initialize FFmpegFileReader\n");
    goto fail;
  }
  
  ctx.codec_context = ctx.demuxer->GetCodecContext();
  ctx.format_context = ctx.demuxer->GetFormatContext();
  if (!ctx.codec_context) {
    fprintf(stderr, "ERROR: Invalid FFmpegFileReader Codec Context\n");
    goto fail;
  }

  dump_extradata(&ctx);

  printf("video width(%d), height(%d), extradata_size(%d)\n",
    (int)ctx.codec_context->width, (int)ctx.codec_context->height, ctx.codec_context->extradata_size);

  AVStream *pStream;
  pStream = ctx.format_context->streams[ctx.demuxer->GetVideoIndex()];
  //ctx.vstream_info.video_pid = (unsigned short)ctx.codec_context->id;
  if (pStream->time_base.den) {
    ctx.vstream_info.start_time     = pStream->start_time * pStream->time_base.num * PTS_FREQ / pStream->time_base.den;
    ctx.vstream_info.video_duration = ((float)pStream->time_base.num / pStream->time_base.den) * UNIT_FREQ;
    ctx.vstream_info.video_pts      = ((float)pStream->time_base.num / pStream->time_base.den) * PTS_FREQ;
  }
  ctx.vstream_info.video_width  = ctx.codec_context->width;
  ctx.vstream_info.video_height = ctx.codec_context->height;
  ctx.vstream_info.video_ratio  = (float)pStream->sample_aspect_ratio.num / pStream->sample_aspect_ratio.den;
  if (ctx.codec_context->time_base.den) {
    ctx.vstream_info.video_codec_rate = (int64_t)UNIT_FREQ * ctx.codec_context->time_base.num / ctx.codec_context->time_base.den;
  }
  if (pStream->r_frame_rate.num) {
    ctx.vstream_info.video_rate = (int64_t)UNIT_FREQ * pStream->r_frame_rate.den / pStream->r_frame_rate.num;
  }


  log_print("time_base.num(%d), pStream->time_base.den(%d)\n",
    pStream->time_base.num, pStream->time_base.den);
    ctx.vstream_info.video_duration = ((float)pStream->time_base.num / pStream->time_base.den) * UNIT_FREQ;
    ctx.vstream_info.video_pts      = ((float)pStream->time_base.num / pStream->time_base.den) * PTS_FREQ;

  printf("\n*********AMLOGIC CODEC PLAYER DEMO************\n\n");
	osd_blank("/sys/class/graphics/fb0/blank", 1);
	osd_blank("/sys/class/graphics/fb1/blank", 1);
	//osd_blank("/sys/class/tsync/enable", 1);
  osd_blank("/sys/class/tsync/enable", 0);

  switch(ctx.codec_context->codec_id)
  {
    case CODEC_ID_H264:
      printf("CODEC_ID_H264\n");

      ctx.vcodec.has_video = 1;
      ctx.vcodec.has_audio = 0;
      ctx.stream_type = STREAM_ES;
      //ctx.vcodec.video_pid = ctx.demuxer->GetVideoIndex();
      ctx.vcodec.video_type = VFORMAT_H264;
      ctx.vcodec.stream_type = STREAM_TYPE_ES_VIDEO;
      //ctx.vcodec.noblock = !!p_para->buffering_enable;
      ctx.vcodec.am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
      ctx.vcodec.am_sysinfo.width  = ctx.vstream_info.video_width;
      ctx.vcodec.am_sysinfo.height = ctx.vstream_info.video_height;
      ctx.vcodec.am_sysinfo.rate   = ctx.vstream_info.video_rate;
      ctx.vcodec.am_sysinfo.ratio  = ctx.vstream_info.video_ratio;
      ctx.vcodec.am_sysinfo.param = (void*)(EXTERNAL_PTS | SYNC_OUTSIDE);

    break;
    case CODEC_ID_MPEG4:
      printf("CODEC_ID_MPEG4\n");
      ctx.vcodec.video_type = VFORMAT_MPEG4;
    break;
    case CODEC_ID_MPEG2VIDEO:
      printf("CODEC_ID_MPEG2VIDEO\n");
      ctx.vcodec.video_type = VFORMAT_MPEG12;
    break;
    default:
      fprintf(stderr, "ERROR: Invalid FFmpegFileReader Codec Format (not h264/mpeg4) = %d\n",
        ctx.codec_context->codec_id);
      goto fail;
    break;
  }

	ret = codec_init(&ctx.vcodec);
	if(ret != CODEC_ERROR_NONE)
	{
		printf("codec init failed, ret=-0x%x", -ret);
		return -1;
	}
	printf("video codec ok!\n");


	ret = codec_init_cntl(&ctx.vcodec);
	if( ret != CODEC_ERROR_NONE )
	{
		printf("codec_init_cntl error\n");
		return -1;
	}

	codec_set_cntl_avthresh(&ctx.vcodec, AV_SYNC_THRESH);
	codec_set_cntl_syncthresh(&ctx.vcodec, ctx.vcodec.has_audio);

  {
    int frame_count, total = 0;
    int64_t bgn_us, end_us;

    ctx.am_pkt.codec = &ctx.vcodec;

    pre_header_feeding(&ctx, &ctx.am_pkt);

    ctx.demuxer->Read(ctx.am_pkt.avpkt);
    printf("byte_count(%d), dts(%llu), pts(%llu)\n",
      ctx.am_pkt.avpkt->size, ctx.am_pkt.avpkt->dts, ctx.am_pkt.avpkt->pts);
    if (!ctx.am_pkt.avpkt->size) {
      fprintf(stderr, "ERROR: Zero bytes read from input\n");
      //goto fail;
    }
    ctx.am_pkt.type = CODEC_VIDEO;
    ctx.am_pkt.data = ctx.am_pkt.avpkt->data;
    ctx.am_pkt.data_size = ctx.am_pkt.avpkt->size;
    ctx.am_pkt.avpkt_newflag = 1;
    ctx.am_pkt.avpkt_isvalid = 1;

    frame_count = 0;
    bool done = false;
    struct buf_status vbuf;
    struct vdec_status vdec;
    float  vlevel;
    while (!g_signal_abort && !done && (frame_count < 5000)) {
      h264_update_frame_header(&ctx.am_pkt);
      
      bgn_us = CurrentHostCounter() * 1000 / CurrentHostFrequency();

      log_print("avpts(%lld), avdts(%lld)\n",
        ctx.am_pkt.avpkt->pts, ctx.am_pkt.avpkt->dts);

      ret = write_av_packet(&ctx, &ctx.am_pkt);

      end_us = CurrentHostCounter() * 1000 / CurrentHostFrequency();

      frame_count++;
      //fprintf(stdout, "decode time(%llu) us\n", end_us-bgn_us);
      check_vcodec_state(&ctx.vcodec, &vdec, &vbuf);
      vlevel = 100.0 * (float)vbuf.data_len / vbuf.size;
      //log_print("buffering_states,vlevel=%d,vsize=%d,level=%f\n", vbuf.data_len, vbuf.size, vlevel);
      usleep(vlevel * 5000);

      ctx.demuxer->Read(ctx.am_pkt.avpkt);
      ctx.am_pkt.type = CODEC_VIDEO;
      ctx.am_pkt.data = ctx.am_pkt.avpkt->data;
      ctx.am_pkt.data_size = ctx.am_pkt.avpkt->size;
      ctx.am_pkt.avpkt_newflag = 1;
      ctx.am_pkt.avpkt_isvalid = 1;

      if (!ctx.am_pkt.data_size) done = true;
      total += ctx.am_pkt.data_size;
    }
  }

fail:
	codec_close_cntl(&ctx.vcodec);
	codec_close(&ctx.vcodec);

  return 0;
}
