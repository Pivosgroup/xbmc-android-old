/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "system.h"

#if defined(TARGET_AMLOGIC)
#include "Amlogic.h"

#include "DVDClock.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

#include <unistd.h>
#include <queue>
#include <vector>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

// amcodec include
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

#define log_print printf
#define log_error printf


#define HDR_BUF_SIZE 1024
typedef struct hdr_buf {
    char *data;
    int size;
} hdr_buf_t;

typedef struct am_packet {
    AVPacket      avpkt;
    int64_t       avpts;
    int64_t       avdts;
    int           avduration;
    int           isvalid;
    int           newflag;
    unsigned char *data;
    unsigned char *buf;
    int           data_size;
    int           buf_size;
    hdr_buf_t     *hdr;
    codec_para_t  *codec;
} am_packet_t;

typedef enum {
    AM_STREAM_UNKNOWN = 0,
    AM_STREAM_TS,
    AM_STREAM_PS,
    AM_STREAM_ES,
    AM_STREAM_RM,
    AM_STREAM_AUDIO,
    AM_STREAM_VIDEO,
} pstream_type;

typedef union {
    int64_t      total_bytes;
    unsigned int vpkt_num;
    unsigned int spkt_num;
} read_write_size;

typedef struct {
    int             has_video;
    vformat_t       video_format;
    signed short    video_index;
    unsigned short  video_pid;
    int             check_first_pts;
    int             flv_flag;
    int             h263_decodable;
    int             vdec_buf_len;
} v_stream_info_t;

typedef  struct {
    unsigned int read_end_flag: 1;
    unsigned int end_flag: 1;
    unsigned int reset_flag: 1;
    int check_lowlevel_eagain_cnt;
} p_ctrl_info_t;

typedef struct am_private_t
{
  am_packet_t       am_pkt;
  codec_para_t      vcodec;
  v_stream_info_t   vstream_info;

  pstream_type      stream_type;
  p_ctrl_info_t     playctrl_info;

  read_write_size   read_size;
  read_write_size   write_size;

  unsigned int      video_width;
  unsigned int      video_height;
  float             video_ratio;
  unsigned int      video_rate;
  float             time_base_ratio;
  int               extrasize;
  uint8_t           *extradata;
} am_private_t;

#ifndef FBIOPUT_OSD_SRCCOLORKEY
#define  FBIOPUT_OSD_SRCCOLORKEY    0x46fb
#endif

#ifndef FBIOPUT_OSD_SRCKEY_ENABLE
#define  FBIOPUT_OSD_SRCKEY_ENABLE  0x46fa
#endif

#ifndef FBIOPUT_OSD_SET_GBL_ALPHA
#define  FBIOPUT_OSD_SET_GBL_ALPHA  0x4500
#endif

#ifndef FBIOGET_OSD_GET_GBL_ALPHA
#define  FBIOGET_OSD_GET_GBL_ALPHA  0x4501
#endif

#ifndef FBIOPUT_OSD_2X_SCALE
#define  FBIOPUT_OSD_2X_SCALE       0x4502
#endif
/*************************************************************************/
int player_video_alpha_en(unsigned enable)
{
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd >= 0) {
        uint32_t gbl_alpha_old, gbl_alpha;

        ioctl(fd, FBIOGET_OSD_GET_GBL_ALPHA, &gbl_alpha_old);
        printf("player_video_alpha_en alpha(0x%x)\n", gbl_alpha_old);
        if (enable) {
            gbl_alpha = 0;
            ioctl(fd, FBIOPUT_OSD_SET_GBL_ALPHA, &gbl_alpha);
        } else {
            gbl_alpha = 0xff;
            ioctl(fd, FBIOPUT_OSD_SET_GBL_ALPHA, &gbl_alpha);
        }
        close(fd);
        return PLAYER_SUCCESS;
    }
    return PLAYER_FAILED;
}

int player_video_overlay_en(unsigned enable)
{
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd >= 0) {
        unsigned myKeyColor = 0;
        unsigned myKeyColor_en = enable;

        if (myKeyColor_en) {
            myKeyColor = 0xff;/*set another value to solved the bug in kernel..remove later*/
            ioctl(fd, FBIOPUT_OSD_SRCCOLORKEY, &myKeyColor);
            myKeyColor = 0;
            ioctl(fd, FBIOPUT_OSD_SRCCOLORKEY, &myKeyColor);
            ioctl(fd, FBIOPUT_OSD_SRCKEY_ENABLE, &myKeyColor_en);
        } else {
            ioctl(fd, FBIOPUT_OSD_SRCKEY_ENABLE, &myKeyColor_en);
        }
        close(fd);
        return PLAYER_SUCCESS;
    }
    return PLAYER_FAILED;
}

int set_sysfs_int(const char *path, int val)
{
    int fd;
    int bytes;
    char  bcmd[16];
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd >= 0) {
        sprintf(bcmd, "%d", val);
        bytes = write(fd, bcmd, strlen(bcmd));
        close(fd);
        return 0;
    }
    return -1;
}

int get_sysfs_int(const char *path)
{
    int fd;
    int val = 0;
    char  bcmd[16];
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        read(fd, bcmd, sizeof(bcmd));
        val = strtol(bcmd, NULL, 16);
        close(fd);
    }
    return val;
}

int set_black_policy(int blackout)
{
    return set_sysfs_int("/sys/class/video/blackout_policy", blackout);
}
int set_tsync_enable(int enable)
{
    return set_sysfs_int("/sys/class/tsync/enable", enable);
}
/*************************************************************************/
static void player_para_init(am_private_t *para)
{
    para->vstream_info.video_index = -1;
}

static void am_packet_init(am_packet_t *pkt)
{
  pkt->isvalid = 0;
  pkt->newflag = 0;
  pkt->codec  = NULL;
  pkt->hdr    = NULL;
  pkt->buf    = NULL;
  pkt->buf_size = 0;
  pkt->data   = NULL;
  pkt->data_size  = 0;
}
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

int check_in_pts(am_private_t *para, am_packet_t *pkt)
{
    int last_duration = 0;
    static int last_v_duration = 0;
    int64_t pts;

    last_duration = last_v_duration;

    if (para->stream_type == AM_STREAM_ES) {
        if ((int64_t)INT64_0 != pkt->avpts) {
            pts = pkt->avpts * para->time_base_ratio;

            if (codec_checkin_pts(pkt->codec, pts) != 0) {
                log_error("ERROR check in pts error!\n");
                return PLAYER_PTS_ERROR;
            }

        } else if ((int64_t)INT64_0 != pkt->avdts) {
            pts = pkt->avdts * para->time_base_ratio * last_duration;
            //log_print("[check_in_pts:%d]pkt->dts=%llx pts=%llx time_base_ratio=%.2f last_duration=%d\n",__LINE__,pkt->avdts,pts,time_base_ratio,last_duration);

            if (codec_checkin_pts(pkt->codec, pts) != 0) {
                log_error("ERROR check in dts error!\n");
                return PLAYER_PTS_ERROR;
            }

            last_v_duration = pkt->avduration ? pkt->avduration : 1;
        } else {
            if (!para->vstream_info.check_first_pts) {
                if (codec_checkin_pts(pkt->codec, 0) != 0) {
                    //log_print("ERROR check in 0 to audio pts error!\n");
                    return PLAYER_PTS_ERROR;
                }
            }
        }
        if (!para->vstream_info.check_first_pts) {
            para->vstream_info.check_first_pts = 1;
        }
    }
    return PLAYER_SUCCESS;
}

static int check_write_finish(am_private_t *para, am_packet_t *pkt)
{
    if (para->playctrl_info.read_end_flag) {
        if ((para->write_size.vpkt_num == para->read_size.vpkt_num)) {
            return PLAYER_WR_FINISH;
        }
    }
    return PLAYER_WR_FAILED;
}

static int write_header(am_private_t *para, am_packet_t *pkt)
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
                    //log_print("ERROR:write header failed!\n");
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

int check_avbuffer_enough(am_private_t *para, am_packet_t *pkt)
{
    return 1;
}

int write_av_packet(am_private_t *para, am_packet_t *pkt)
{
    int write_bytes = 0, len = 0, ret;
    unsigned char *buf;
    int size ;

    if (pkt->newflag) {
        if (pkt->isvalid) {
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
        pkt->newflag = 0;
    }
	
    buf = pkt->data;
    size = pkt->data_size ;
    if (size == 0 && pkt->isvalid) {
        para->write_size.vpkt_num++;
        pkt->isvalid = 0;
    }
    while (size > 0 && pkt->isvalid) {
        write_bytes = codec_write(pkt->codec, (char *)buf, size);
        if (write_bytes < 0 || write_bytes > size) {
            if (-errno != AVERROR(EAGAIN)) {
                para->playctrl_info.check_lowlevel_eagain_cnt = 0;
                //log_print("write codec data failed!\n");
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
                    
                    //log_print("$$$$$$ write blocked, need reset decoder!$$$$$$\n");
                }				
                pkt->data += len;
                pkt->data_size -= len;
                usleep(RW_WAIT_TIME);
                if (para->playctrl_info.check_lowlevel_eagain_cnt > 0) {
                    //log_print("[%s]eagain:data_size=%d rsize=%lld wsize=%lld cnt=%d\n", \
                    //    __FUNCTION__, pkt->data_size, para->read_size.total_bytes, \
                    //    para->write_size.total_bytes, para->playctrl_info.check_lowlevel_eagain_cnt);
                }
                return PLAYER_SUCCESS;
            }
        } else {
            para->playctrl_info.check_lowlevel_eagain_cnt = 0;
            len += write_bytes;
            if (len == pkt->data_size) {
                para->write_size.vpkt_num++;
                pkt->isvalid = 0;
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

    return PLAYER_SUCCESS;
}
static int h264_write_header(am_private_t *para, am_packet_t *pkt)
{
    int ret = -1;

    ret = h264_add_header(para->extradata, para->extrasize, pkt);
    if (ret == PLAYER_SUCCESS) {
        //if (ctx->vcodec) {
        if (1) {
            pkt->codec = &para->vcodec;
        } else {
            //log_print("[pre_header_feeding]invalid video codec!\n");
            return PLAYER_EMPTY_P;
        }

        pkt->newflag = 1;
        ret = write_av_packet(para, pkt);
    }
    return ret;
}

int pre_header_feeding(am_private_t *para, am_packet_t *pkt)
{
    int ret;
    if (para->stream_type == AM_STREAM_ES && para->vstream_info.has_video) {
        if (pkt->hdr == NULL) {
            pkt->hdr = (hdr_buf_t*)malloc(sizeof(hdr_buf_t));
            pkt->hdr->data = (char *)malloc(HDR_BUF_SIZE);
            if (!pkt->hdr->data) {
                //log_print("[pre_header_feeding] NOMEM!");
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
                free(pkt->hdr->data);
                pkt->hdr->data = NULL;
            }
            free(pkt->hdr);
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

            new_data = (unsigned char *)malloc(size + 2 * 1024);
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

            free(pkt->buf);

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

/*************************************************************************/
CAmlogic::CAmlogic()
{
  am_private = new am_private_t;
  memset(am_private, 0, sizeof(am_private_t));
  player_para_init(am_private);
  am_packet_init(&am_private->am_pkt);
  m_pts = 0;
}


CAmlogic::~CAmlogic()
{
  free(am_private->extradata);
  am_private->extradata = NULL;
  delete am_private;
  am_private = NULL;
}

bool CAmlogic::OpenDecoder(CDVDStreamInfo &hints)
{
  am_private->video_width    = hints.width;
  am_private->video_height   = hints.height;
  am_private->video_ratio    = hints.aspect;
  am_private->video_rate     = (int64_t)UNIT_FREQ * hints.fpsscale / hints.fpsrate;
  am_private->time_base_ratio= hints.timebase * PTS_FREQ;
  am_private->extrasize      = hints.extrasize;
  am_private->extradata      = (uint8_t*)malloc(hints.extrasize);
  memcpy(am_private->extradata, hints.extradata, hints.extrasize);

  printf("video_rate(%d), time_base_ratio(%f)\n", am_private->video_rate, am_private->time_base_ratio);

  switch(hints.codec)
  {
    case CODEC_ID_H264:
      printf("CODEC_ID_H264\n");
      am_private->stream_type = AM_STREAM_ES;
      am_private->vcodec.has_video   = 1;
      am_private->vcodec.video_type  = VFORMAT_H264;
      am_private->vcodec.stream_type = STREAM_TYPE_ES_VIDEO;
      //am_private->vcodec.noblock = 1;
      am_private->vcodec.am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
      am_private->vcodec.am_sysinfo.width  = am_private->video_width;
      am_private->vcodec.am_sysinfo.height = am_private->video_height;
      am_private->vcodec.am_sysinfo.rate   = am_private->video_rate;
      am_private->vcodec.am_sysinfo.ratio  = am_private->video_ratio;
      am_private->vcodec.am_sysinfo.param = (void*)(EXTERNAL_PTS | SYNC_OUTSIDE);
     break;
    case CODEC_ID_MPEG4:
      printf("CODEC_ID_MPEG4\n");
      am_private->vcodec.video_type = VFORMAT_MPEG4;
    break;
    case CODEC_ID_MPEG2VIDEO:
      printf("CODEC_ID_MPEG2VIDEO\n");
      am_private->vcodec.video_type = VFORMAT_MPEG12;
    break;
    default:
      fprintf(stderr, "ERROR: Unknown Codec Format = %d\n", hints.codec);
    break;
  }

	int ret = codec_init(&am_private->vcodec);
	if(ret != CODEC_ERROR_NONE)
	{
		printf("codec init failed, ret=-0x%x", -ret);
		return false;
	}
	printf("video codec ok!\n");


	ret = codec_init_cntl(&am_private->vcodec);
	if( ret != CODEC_ERROR_NONE )
	{
		printf("codec_init_cntl error\n");
		return false;
	}

	codec_set_cntl_avthresh(&am_private->vcodec, AV_SYNC_THRESH);
	//codec_set_cntl_syncthresh(&am_private->vcodec, am_private->vcodec.has_audio);

  //set_black_policy(0);
  //player_video_alpha_en(true);
  //player_video_overlay_en(true);
  set_sysfs_int("/sys/class/tsync/enable", 0);
  
  am_private->am_pkt.codec = &am_private->vcodec;
  pre_header_feeding(am_private, &am_private->am_pkt);
  m_pts = 0;

  return true;
}

void CAmlogic::CloseDecoder(void)
{
  //set_black_policy(1);
  //player_video_alpha_en(false);
  //player_video_overlay_en(false);

	codec_close_cntl(&am_private->vcodec);
	codec_close(&am_private->vcodec);
}

void CAmlogic::Reset(void)
{
}

int CAmlogic::Decode(unsigned char *pData, size_t size, double dts, double pts)
{
  if (pData)
  {
    am_private->am_pkt.data = pData;
    am_private->am_pkt.data_size = size;
    am_private->am_pkt.newflag = 1;
    am_private->am_pkt.isvalid = 1;
    am_private->am_pkt.avpts = 0.5 + (pts * PTS_FREQ / (am_private->time_base_ratio * DVD_TIME_BASE));
    am_private->am_pkt.avdts = 0.5 + (dts * PTS_FREQ / (am_private->time_base_ratio * DVD_TIME_BASE));

    //CLog::Log(LOGDEBUG, "pts(%f), dts(%f), avpts(%lld), avdts(%lld)",
    //  pts, dts, am_private->am_pkt.avpts, am_private->am_pkt.avdts);

    h264_update_frame_header(&am_private->am_pkt);
    write_av_packet(am_private, &am_private->am_pkt);

    struct buf_status vbuf;
    struct vdec_status vdec;
    check_vcodec_state(&am_private->vcodec, &vdec, &vbuf);
    float vlevel = 100.0 * (float)vbuf.data_len / vbuf.size;
    usleep(vlevel * 5000);

    //CLog::Log(LOGDEBUG, "buffering_states,vlevel=%d,vsize=%d,level=%f",
    //  vbuf.data_len, vbuf.size, vlevel);
  }
  
  int cur_pts = codec_get_vpts(&am_private->vcodec) & ~0x2a000000;
  if (cur_pts != m_pts)
  {
    //printf("CAmlogic::Decode m_pts(%lld), cur_pts(%d)\n", m_pts, cur_pts);
    m_pts = cur_pts;
    return VC_PICTURE;
  }

  return VC_BUFFER;
}

bool CAmlogic::GetPicture(DVDVideoPicture *pDvdVideoPicture)
{
  // we want the current pts from hardware.
  double pts = (double)(codec_get_vpts(&am_private->vcodec) & ~0x2a000000) / am_private->time_base_ratio;

  pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
  pDvdVideoPicture->pts = pts * (double)DVD_TIME_BASE * (am_private->time_base_ratio / (double)PTS_FREQ);
  pDvdVideoPicture->iDuration = (DVD_TIME_BASE / am_private->time_base_ratio);
  
  //printf("m_pts(%lld), pts(%f), pDvdVideoPicture->pts(%f), pDvdVideoPicture->iDuration(%f)\n",
  //  m_pts, pts, pDvdVideoPicture->pts, pDvdVideoPicture->iDuration);

  //pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
  //pDvdVideoPicture->pts = DVD_NOPTS_VALUE;
  pDvdVideoPicture->iDuration = (DVD_TIME_BASE / (30.0 * 1000.0/1001.0));
  pDvdVideoPicture->iFlags = DVP_FLAG_ALLOCATED;
  pDvdVideoPicture->format = DVDVideoPicture::FMT_AMLREF;

  return true;
}

void CAmlogic::SetDropState(bool bDrop)
{
  //printf("CAmlogic::SetDropState(%d)\n", bDrop);
}

#endif
