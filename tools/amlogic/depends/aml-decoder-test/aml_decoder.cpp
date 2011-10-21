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
/* MPEG-4 esds (elementary stream descriptor) */
typedef struct {
  int version;
  long flags;

  uint16_t esid;
  uint8_t stream_priority;

  uint8_t  objectTypeId;
  uint8_t  streamType;
  uint32_t bufferSizeDB;
  uint32_t maxBitrate;
  uint32_t avgBitrate;

  int      decoderConfigLen;
  uint8_t* decoderConfig;
} quicktime_esds_t;

unsigned int descrLength(unsigned int len)
{
  int i;
  for(i=1; len>>(7*i); i++);
  return len + 1 + i;
}

void putDescr(ByteIOContext *pb, int tag, unsigned int size)
{
  int i= descrLength(size) - size - 2;
  put_byte(pb, tag);
  for(; i>0; i--)
    put_byte(pb, (size>>(7*i)) | 0x80);
  put_byte(pb, size & 0x7F);
}

void quicktime_write_esds(ByteIOContext *pb, quicktime_esds_t *esds)
{
  //quicktime_atom_t atom;
  int decoderSpecificInfoLen = esds->decoderConfigLen ? descrLength(esds->decoderConfigLen):0;
  //quicktime_atom_write_header(file, &atom, "esds");

/*
  put_byte(pb, 0);  // Version
  put_be24(pb, 0);  // Flags

  // ES descriptor
  putDescr(pb, 0x03, 3 + descrLength(13 + decoderSpecificInfoLen) + descrLength(1));

  put_be16(pb, esds->esid);
  put_byte(pb, esds->stream_priority);
  // DecoderConfig descriptor
  putDescr(pb, 0x04, 13 + esds->decoderConfigLen);
  // Object type indication
  put_byte(pb, esds->objectTypeId); // objectTypeIndication
  put_byte(pb, esds->streamType);   // streamType

  put_be24(pb, esds->bufferSizeDB); // buffer size
  put_be32(pb, esds->maxBitrate);   // max bitrate
  put_be32(pb, esds->avgBitrate);   // average bitrate

  // DecoderSpecific info descriptor
  if (decoderSpecificInfoLen) {
    putDescr(pb, 0x05, esds->decoderConfigLen);
    put_buffer(pb, esds->decoderConfig, esds->decoderConfigLen);
  }
  // SL descriptor
  putDescr(pb, 0x06, 1);
  put_byte(pb, 0x02);
*/

  put_byte(pb, 0);  // Version

  // ES descriptor
  putDescr(pb, 0x03, 3 + descrLength(13 + decoderSpecificInfoLen) + descrLength(1));
  put_be16(pb, esds->esid);
  put_byte(pb, 0x00); // flags (= no flags)

  // DecoderConfig descriptor
  putDescr(pb, 0x04, 13 + decoderSpecificInfoLen);

  // Object type indication
  put_byte(pb, esds->objectTypeId);

  // the following fields is made of 6 bits to identify the streamtype (4 for video, 5 for audio)
  // plus 1 bit to indicate upstream and 1 bit set to 1 (reserved)
  put_byte(pb, esds->streamType); // flags (0x11 = Visualstream)

  put_be24(pb, esds->bufferSizeDB); // buffer size
  //put_byte(pb,  track->enc->rc_buffer_size>>(3+16));    // Buffersize DB (24 bits)
  //put_be16(pb, (track->enc->rc_buffer_size>>3)&0xFFFF); // Buffersize DB

  put_be32(pb, esds->maxBitrate);   // max bitrate
  put_be32(pb, esds->avgBitrate);   // average bitrate
  //put_be32(pb, FFMAX(track->enc->bit_rate, track->enc->rc_max_rate)); // maxbitrate (FIXME should be max rate in any 1 sec window)
  //if(track->enc->rc_max_rate != track->enc->rc_min_rate || track->enc->rc_min_rate==0)
  //    put_be32(pb, 0); // vbr
  //else
  //    put_be32(pb, track->enc->rc_max_rate); // avg bitrate

  // DecoderSpecific info descriptor
  if (decoderSpecificInfoLen) {
    putDescr(pb, 0x05, esds->decoderConfigLen);
    put_buffer(pb, esds->decoderConfig, esds->decoderConfigLen);
  }

  // SL descriptor
  putDescr(pb, 0x06, 1);
  put_byte(pb, 0x02);

  //quicktime_atom_write_footer(file, &atom);
}

quicktime_esds_t* quicktime_set_esds(const uint8_t * decoderConfig, int decoderConfigLen)
{
  // ffmpeg's codec->avctx->extradata, codec->avctx->extradata_size
  // are decoderConfig/decoderConfigLen
  quicktime_esds_t *esds;

  esds = (quicktime_esds_t*)malloc(sizeof(quicktime_esds_t));
  memset(esds, 0, sizeof(quicktime_esds_t));

  esds->version         = 0;
  esds->flags           = 0;
  
  esds->esid            = 0;
  esds->stream_priority = 0;      // 16 ?
  
  esds->objectTypeId    = 32;     // 32 = CODEC_ID_MPEG4, 33 = CODEC_ID_H264
  // the following fields is made of 6 bits to identify the streamtype (4 for video, 5 for audio)
  // plus 1 bit to indicate upstream and 1 bit set to 1 (reserved)
  esds->streamType      = 0x11;
  esds->bufferSizeDB    = 64000;  // Hopefully not important :)
  
  // Maybe correct these later?
  esds->maxBitrate      = 200000; // 0 for vbr
  esds->avgBitrate      = 200000;
  
  esds->decoderConfigLen = decoderConfigLen;
  esds->decoderConfig = (uint8_t*)malloc(esds->decoderConfigLen);
  memcpy(esds->decoderConfig, decoderConfig, esds->decoderConfigLen);
  return esds;
}

void quicktime_esds_dump(quicktime_esds_t * esds)
{
  int i;
  printf("esds: \n");
  printf(" Version:          %d\n",       esds->version);
  printf(" Flags:            0x%06lx\n",  esds->flags);
  printf(" ES ID:            0x%04x\n",   esds->esid);
  printf(" Priority:         0x%02x\n",   esds->stream_priority);
  printf(" objectTypeId:     %d\n",       esds->objectTypeId);
  printf(" streamType:       0x%02x\n",   esds->streamType);
  printf(" bufferSizeDB:     %d\n",       esds->bufferSizeDB);

  printf(" maxBitrate:       %d\n",       esds->maxBitrate);
  printf(" avgBitrate:       %d\n",       esds->avgBitrate);
  printf(" decoderConfigLen: %d\n",       esds->decoderConfigLen);
  printf(" decoderConfig:");
  for(i = 0; i < esds->decoderConfigLen; i++) {
    if(!(i % 16))
      printf("\n ");
    printf("%02x ", esds->decoderConfig[i]);
  }
  printf("\n");
}

// AppContext - Application state
#define PTS_FREQ    90000
#define UNIT_FREQ       96000
#define AV_SYNC_THRESH PTS_FREQ*30

#define HDR_BUF_SIZE 1024
typedef struct hdr_buf {
    char *data;
    int size;
} hdr_buf_t;

typedef struct
{
  int               sourceWidth;
  int               sourceHeight;
  FFmpegFileReader  *demuxer;
  AVCodecContext    *codec_context;
  codec_para_t      apcodec;
  codec_para_t      vpcodec;
  hdr_buf_t         vhdr;
  bool              vhdr_newflag;

} AppContext;

/*
// silly ffmpeg, this should be in libavcodec.a
void av_free_packet(AVPacket *pkt) 
{ 
   if (pkt) { 
     if (pkt->destruct) pkt->destruct(pkt); 
     pkt->data = NULL; pkt->size = 0; 
   } 
}
*/

/* g_signal_abort is set to 1 in term/int signal handler */
static unsigned int g_signal_abort = 0;
static void signal_handler(int iSignal)
{
  g_signal_abort = 1;
  printf("Terminating - Program received %s signal\n", \
    (iSignal == SIGINT? "SIGINT" : (iSignal == SIGTERM ? "SIGTERM" : "UNKNOWN")));
}

////////////////////////////////////////////////////////////////////////////////////////////
void fetch_nal(uint8_t *buffer, int buffer_size, int type, uint8_t **obuff, int *osize)
{
    int i;
    uint8_t *data = buffer;
    uint8_t *nal_buffer;
    int nal_idx = 0;
    int nal_len = 0;
    int nal_type = 0;
    int found = 0;
    int done = 0;
    
    printf("Fetching NAL, type %d\n", type);
    for (i = 0; i < buffer_size - 5; i++) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 
            && data[i + 3] == 1) {
            if (found == 1) {
                nal_len = i - nal_idx;
                done = 1;
                break;
            }

            nal_type = (data[i + 4]) & 0x1f;
            if (nal_type == type)
            {
                found = 1;
                nal_idx = i + 4;
                i += 4;
            }
        }
    }
    
    /* Check if the NAL stops at the end */
    if (found == 1 && done != 0 && i >= buffer_size - 4) {
        nal_len = buffer_size - nal_idx;
        done = 1;
    }
    
    if (done == 1) {
        printf("Found NAL, bytes [%d-%d] len [%d]\n", nal_idx, nal_idx + nal_len - 1, nal_len);
        nal_buffer = (uint8_t*)malloc(nal_len);
        memcpy(nal_buffer, &data[nal_idx], nal_len);
        *obuff = nal_buffer;
        *osize = nal_len;
        //return nal_buffer;
    } else {
        printf("Did not find NAL type %d\n", type);
        *obuff = NULL;
        *osize = 0;
        //return NULL;
    }
}

#define NAL_LENGTH 4
void h264_generate_avcc_atom_data(uint8_t *buffer, int buffer_size, uint8_t **obuff, int *osize)
{
  uint8_t *avcc = NULL;
  uint8_t *avcc_data = NULL;
  int avcc_len = 7;  // Default 7 bytes w/o SPS, PPS data
  int i;

  uint8_t *sps = NULL;
  int sps_size;
  uint8_t *sps_data = NULL;
  int num_sps=0;

  uint8_t *pps = NULL;
  int pps_size;
  int num_pps=0;

  uint8_t profile;
  uint8_t compatibly;
  uint8_t level;

  // 7 = SPS
  fetch_nal(buffer, buffer_size, 7, &sps, &sps_size);
  if (sps) {
      num_sps = 1;
      avcc_len += sps_size + 2;
      sps_data = sps;

      profile     = sps_data[1];
      compatibly  = sps_data[2];
      level       = sps_data[3]; 
      
      printf("SPS: profile=%d, compatibly=%d, level=%d\n", profile, compatibly, level);
  } else {
      printf("No SPS found\n");

      profile     = 66;   // Default Profile: Baseline
      compatibly  = 0;
      level       = 30;   // Default Level: 3.0
  }
  // 8 = PPS
  fetch_nal(buffer, buffer_size, 8, &pps, &pps_size); 
  if (pps) {
      num_pps = 1;
      avcc_len += pps_size + 2;
  } else {
      printf("No PPS found\n");
  }

  avcc = (uint8_t*)malloc(avcc_len);
  avcc_data = avcc;
  avcc_data[0] = 1;             // [0] 1 byte - version
  avcc_data[1] = profile;       // [1] 1 byte - h.264 stream profile
  avcc_data[2] = compatibly;    // [2] 1 byte - h.264 compatible profiles
  avcc_data[3] = level;         // [3] 1 byte - h.264 stream level
  avcc_data[4] = 0xfc | (NAL_LENGTH-1);  // [4] 6 bits - reserved all ONES = 0xfc
                                // [4] 2 bits - NAL length ( 0 - 1 byte; 1 - 2 bytes; 3 - 4 bytes)
  avcc_data[5] = 0xe0 | num_sps;// [5] 3 bits - reserved all ONES = 0xe0
                                // [5] 5 bits - number of SPS    
  i = 6;
  if (num_sps > 0) {
    avcc_data[i++] = sps_size >> 8;
    avcc_data[i++] = sps_size & 0xff;
    memcpy(&avcc_data[i], sps, sps_size);
    i += sps_size;
    free(sps);
  }
  avcc_data[i++] = num_pps;     // [6] 1 byte  - number of PPS
  if (num_pps > 0) {
    avcc_data[i++] = pps_size >> 8;
    avcc_data[i++] = pps_size & 0xff;
    memcpy(&avcc_data[i], pps, pps_size);
    i += pps_size;
    free(pps);
  }
  *obuff = avcc;
  *osize = avcc_len;
}

////////////////////////////////////////////////////////////////////////////////////////////
// TODO: refactor this so as not to need these ffmpeg routines.
// These are not exposed in ffmpeg's API so we dupe them here.
// AVC helper functions for muxers,
//  * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@smartjog.com>
// This is part of FFmpeg
//  * License as published by the Free Software Foundation; either
//  * version 2.1 of the License, or (at your option) any later version.
#define VDA_RB24(x)                          \
  ((((const uint8_t*)(x))[0] << 16) |        \
   (((const uint8_t*)(x))[1] <<  8) |        \
   ((const uint8_t*)(x))[2])

#define VDA_RB32(x)                          \
  ((((const uint8_t*)(x))[0] << 24) |        \
   (((const uint8_t*)(x))[1] << 16) |        \
   (((const uint8_t*)(x))[2] <<  8) |        \
   ((const uint8_t*)(x))[3])

static const uint8_t *my_avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
  const uint8_t *a = p + 4 - ((intptr_t)p & 3);

  for (end -= 3; p < a && p < end; p++)
  {
    if (p[0] == 0 && p[1] == 0 && p[2] == 1)
      return p;
  }

  for (end -= 3; p < end; p += 4)
  {
    uint32_t x = *(const uint32_t*)p;
    if ((x - 0x01010101) & (~x) & 0x80808080) // generic
    {
      if (p[1] == 0)
      {
        if (p[0] == 0 && p[2] == 1)
          return p;
        if (p[2] == 0 && p[3] == 1)
          return p+1;
      }
      if (p[3] == 0)
      {
        if (p[2] == 0 && p[4] == 1)
          return p+2;
        if (p[4] == 0 && p[5] == 1)
          return p+3;
      }
    }
  }

  for (end += 3; p < end; p++)
  {
    if (p[0] == 0 && p[1] == 0 && p[2] == 1)
      return p;
  }

  return end + 3;
}

const uint8_t *my_avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
  const uint8_t *out= my_avc_find_startcode_internal(p, end);
  if (p<out && out<end && !out[-1])
    out--;
  return out;
}

const int my_avc_parse_nal_units(ByteIOContext *pb, const uint8_t *buf_in, int size)
{
  const uint8_t *p = buf_in;
  const uint8_t *end = p + size;
  const uint8_t *nal_start, *nal_end;

  size = 0;
  nal_start = my_avc_find_startcode(p, end);
  while (nal_start < end)
  {
    while (!*(nal_start++));
    nal_end = my_avc_find_startcode(nal_start, end);
    put_be32(pb, nal_end - nal_start);
    put_buffer(pb, nal_start, nal_end - nal_start);
    size += 4 + nal_end - nal_start;
    nal_start = nal_end;
  }
  return size;
}

const int my_avc_parse_nal_units_buf(const uint8_t *buf_in, uint8_t **buf, int *size)
{
  ByteIOContext *pb;
  int ret = url_open_dyn_buf(&pb);
  if (ret < 0)
    return ret;

  my_avc_parse_nal_units(pb, buf_in, *size);

  av_freep(buf);
  *size = url_close_dyn_buf(pb, buf);
  return 0;
}

const int my_isom_write_avcc(ByteIOContext *pb, const uint8_t *data, int len)
{
  // extradata from bytestream h264, convert to avcC atom data for bitstream
  if (len > 6)
  {
    /* check for h264 start code */
    if (VDA_RB32(data) == 0x00000001 || VDA_RB24(data) == 0x000001)
    {
      uint8_t *buf=NULL, *end, *start;
      uint32_t sps_size=0, pps_size=0;
      uint8_t *sps=0, *pps=0;

      int ret = my_avc_parse_nal_units_buf(data, &buf, &len);
      if (ret < 0)
        return ret;
      start = buf;
      end = buf + len;

      /* look for sps and pps */
      while (buf < end)
      {
        unsigned int size;
        uint8_t nal_type;
        size = VDA_RB32(buf);
        nal_type = buf[4] & 0x1f;
        if (nal_type == 7) /* SPS */
        {
          sps = buf + 4;
          sps_size = size;
        }
        else if (nal_type == 8) /* PPS */
        {
          pps = buf + 4;
          pps_size = size;
        }
        buf += size + 4;
      }
      //assert(sps);

      put_byte(pb, 1); /* version */
      // 66 (Base line profile), 77 (main profile), 100 (high profile)
      put_byte(pb, sps[1]); /* h.264 stream profile */
      put_byte(pb, sps[2]); /* h.264 compatible profiles */
      put_byte(pb, sps[3]); /* h.264 stream level */
      put_byte(pb, 0xff); /* 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
      put_byte(pb, 0xe1); /* 3 bits reserved (111) + 5 bits number of sps (00001) */

      put_be16(pb, sps_size);
      put_buffer(pb, sps, sps_size);
      if (pps)
      {
        put_byte(pb, 1); /* number of pps */
        put_be16(pb, pps_size);
        put_buffer(pb, pps, pps_size);
      }
      av_free(start);
    }
    else
    {
      put_buffer(pb, data, len);
    }
  }
  return 0;
}

uint64_t CurrentHostCounter(void)
{
  return( (uint64_t)0);
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

/*************************************************************************/
static int h264_add_header(unsigned char *buf, int size,  hdr_buf_t *hdr)
{
    char nal_start_code[] = {0x0, 0x0, 0x0, 0x1};
    int nalsize;
    unsigned char* p;
    int tmpi;
    unsigned char* extradata = buf;
    int header_len = 0;
    char* buffer = hdr->data;

    p = extradata;
    if (size < 4) {
        return 0;
    }

    if (size < 10) {
        printf("avcC too short\n");
        return 0;
    }

    if (*p != 1) {
        printf(" Unkonwn avcC version %d\n", *p);
        return 0;
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
    hdr->size = header_len;
    return 1;
}

int main(int argc, char * const argv[])
{
	int ret = CODEC_ERROR_NONE;
  bool convert_bytestream = false;
  std::string input_filename;
  float time_base_ratio;
  int video_rate;

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
  AppContext ctx;
  memset(&ctx, 0, sizeof(ctx));

  // create the ffmepg file reader/demuxer
  ctx.demuxer = new FFmpegFileReader(input_filename.c_str());
  if (!ctx.demuxer->Initialize()) {
    fprintf(stderr, "ERROR: Can't initialize FFmpegFileReader\n");
    goto fail;
  }
  
  ctx.codec_context = ctx.demuxer->GetCodecContext();
  if (!ctx.codec_context) {
    fprintf(stderr, "ERROR: Invalid FFmpegFileReader Codec Context\n");
    goto fail;
  }

  ctx.sourceWidth = ctx.codec_context->width;
  ctx.sourceHeight = ctx.codec_context->height;
  printf("video width(%d), height(%d), extradata_size(%d)\n",
    (int)ctx.sourceWidth, (int)ctx.sourceHeight, ctx.codec_context->extradata_size);
  dump_extradata(&ctx);

  time_base_ratio = ((float)ctx.codec_context->time_base.num / ctx.codec_context->time_base.den) * PTS_FREQ;
  //video_rate = UNIT_FREQ * ctx.codec_context->r_frame_rate.den / ctx.codec_context->r_frame_rate.num;

/*
            p_para->vstream_info.video_pid      = (unsigned short)pStream->id;
            if (pStream->time_base.den) {
                p_para->vstream_info.video_duration = ((float)pStream->time_base.num / pStream->time_base.den) * UNIT_FREQ;
            }
            p_para->vstream_info.video_width    = pCodecCtx->width;
            p_para->vstream_info.video_height   = pCodecCtx->height;
            p_para->vstream_info.video_ratio    = (float)pStream->sample_aspect_ratio.num / pStream->sample_aspect_ratio.den;
            p_para->vstream_info.video_rate = UNIT_FREQ * pStream->r_frame_rate.den / pStream->r_frame_rate.num;
*/
  printf("\n*********AMLOGIC CODEC PLAYER DEMO************\n\n");
	osd_blank("/sys/class/graphics/fb0/blank", 1);
	osd_blank("/sys/class/graphics/fb1/blank", 1);
	osd_blank("/sys/class/tsync/enable", 1);

	memset(&ctx.apcodec, 0, sizeof(codec_para_t));	
	memset(&ctx.vpcodec, 0, sizeof(codec_para_t));	

	ctx.vpcodec.has_video = 1;
	//ctx.vpcodec.video_pid = 0x1022;

  switch(ctx.codec_context->codec_id)
  {
    case CODEC_ID_H264:
      printf("CODEC_ID_H264\n");

      ctx.vpcodec.video_type = VFORMAT_H264;
      #define EXTERNAL_PTS (1)
      #define SYNC_OUTSIDE (2)
      ctx.vpcodec.am_sysinfo.param = (void*)(EXTERNAL_PTS | SYNC_OUTSIDE);
      ctx.vpcodec.stream_type = STREAM_TYPE_ES_VIDEO;
      ctx.vpcodec.am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
      ctx.vpcodec.am_sysinfo.rate   = 25;
      ctx.vpcodec.am_sysinfo.width  = ctx.sourceWidth;
      ctx.vpcodec.am_sysinfo.height = ctx.sourceHeight;
      ctx.vpcodec.has_audio = 0;

      if (ctx.codec_context->extradata_size) {
        // valid avcC atom data always starts with the value 1 (version)
        ctx.vhdr.data = (char *)malloc(HDR_BUF_SIZE);

        if ( *ctx.codec_context->extradata == 1 ) {
          printf("using existing avcC atom data\n");
          
          h264_add_header(ctx.codec_context->extradata, ctx.codec_context->extradata_size, &ctx.vhdr);
          ctx.vhdr_newflag = true;
        } else {
          if (ctx.codec_context->extradata[0] == 0 && 
              ctx.codec_context->extradata[1] == 0 && 
              ctx.codec_context->extradata[2] == 0 && 
              ctx.codec_context->extradata[3] == 1)
          {
            uint8_t *saved_extradata;
            unsigned int saved_extrasize;
            saved_extradata = ctx.codec_context->extradata;
            saved_extrasize = ctx.codec_context->extradata_size;

            // video content is from x264 or from bytestream h264 (AnnexB format)
            // NAL reformating to bitstream format needed
            ByteIOContext *pb;
            if (url_open_dyn_buf(&pb) < 0)
              return false;

            convert_bytestream = true;
            // create a valid avcC atom data from ffmpeg's extradata
            my_isom_write_avcc(pb, ctx.codec_context->extradata, ctx.codec_context->extradata_size);
            // unhook from ffmpeg's extradata
            ctx.codec_context->extradata = NULL;
            // extract the avcC atom data into extradata then write it into avcCData for VDADecoder
            ctx.codec_context->extradata_size = url_close_dyn_buf(pb, &ctx.codec_context->extradata);
            printf("convert to avcC atom data\n");
            dump_extradata(&ctx);
            
            h264_add_header(ctx.codec_context->extradata, ctx.codec_context->extradata_size, &ctx.vhdr);
            ctx.vhdr_newflag = true;
            // done with the converted extradata, we MUST free using av_free
            av_free(ctx.codec_context->extradata);
            //restore orignal contents
            ctx.codec_context->extradata = saved_extradata;
            ctx.codec_context->extradata_size = saved_extrasize;
          } else {
            printf("%s - invalid avcC atom data", __FUNCTION__);
            return false;
          }
        }
      }
    break;
    case CODEC_ID_MPEG4:
      printf("CODEC_ID_MPEG4\n");
      ctx.vpcodec.video_type = VFORMAT_MPEG4;
      /*
      if (ctx.codec_context->extradata_size) {
        ByteIOContext *pb;
        quicktime_esds_t *esds;
        uint8_t *saved_extradata;
        unsigned int saved_extrasize;
        saved_extradata = ctx.codec_context->extradata;
        saved_extrasize = ctx.codec_context->extradata_size;

        if (url_open_dyn_buf(&pb) < 0)
          return false;

        esds = quicktime_set_esds(ctx.codec_context->extradata, ctx.codec_context->extradata_size);
        quicktime_write_esds(pb, esds);

        // unhook from ffmpeg's extradata
        ctx.codec_context->extradata = NULL;
        // extract the esds atom decoderConfig from extradata
        ctx.codec_context->extradata_size = url_close_dyn_buf(pb, &ctx.codec_context->extradata);
        free(esds->decoderConfig);
        free(esds);

        ctx.fmt_desc = vtdec_create_format_description_from_codec_data(&ctx, 'esds');

        // done with the converted extradata, we MUST free using av_free
        av_free(ctx.codec_context->extradata);
        //restore orignal contents
        ctx.codec_context->extradata = saved_extradata;
        ctx.codec_context->extradata_size = saved_extrasize;
      } else {
        ctx.fmt_desc = vtdec_create_format_description(&ctx);
      }
      */
    break;
    case CODEC_ID_MPEG2VIDEO:
      printf("CODEC_ID_MPEG2VIDEO\n");
      ctx.vpcodec.video_type = VFORMAT_MPEG12;
      /*
      if (ctx.codec_context->extradata_size) {
        // mp2p
        // mp2t
        ctx.fmt_desc = vtdec_create_format_description_from_codec_data(&ctx, '1234');
      } else {
        ctx.fmt_desc = vtdec_create_format_description(&ctx);
      }
      */
    break;
    default:
      fprintf(stderr, "ERROR: Invalid FFmpegFileReader Codec Format (not h264/mpeg4) = %d\n",
        ctx.codec_context->codec_id);
      goto fail;
    break;
  }

	ret = codec_init(&ctx.vpcodec);
	if(ret != CODEC_ERROR_NONE)
	{
		printf("codec init failed, ret=-0x%x", -ret);
		return -1;
	}
	printf("video codec ok!\n");


	ret = codec_init_cntl(&ctx.vpcodec);
	if( ret != CODEC_ERROR_NONE )
	{
		printf("codec_init_cntl error\n");
		return -1;
	}
	//codec_set_cntl_avthresh(&ctx.vpcodec, AV_SYNC_THRESH);
	//codec_set_cntl_syncthresh(&ctx.vpcodec, ctx.vpcodec.has_audio);


  {
    int frame_count, byte_count, total = 0;
    uint64_t bgn, end;
    uint8_t* data;
    uint64_t dts, pts;

    ctx.demuxer->Read(&data, &byte_count, &dts, &pts);
    printf("byte_count(%d), dts(%llu), pts(%llu)\n", byte_count, dts, pts);
    if (!byte_count) {
      fprintf(stderr, "ERROR: Zero bytes read from input\n");
      //goto fail;
    }

    usleep(10000);
    frame_count = 0;
    bool done = false;
    while (!g_signal_abort && !done && (frame_count < 5000)) {
      int demuxer_bytes = byte_count;
      uint8_t *demuxer_content = data;
      codec_para_t *pcodec = &ctx.vpcodec;

      codec_checkin_pts(pcodec, pts * time_base_ratio);
      if (ctx.vhdr_newflag) {
        do {
          ret = codec_write(pcodec, ctx.vhdr.data, ctx.vhdr.size);
        } while(ret < 0);
        ctx.vhdr_newflag = false;
      }
      if (convert_bytestream) {
        // convert demuxer packet from bytestream (AnnexB) to bitstream
        ByteIOContext *pb;

        if(url_open_dyn_buf(&pb) < 0)
          goto fail;

        demuxer_bytes = my_avc_parse_nal_units(pb, data, byte_count);
        demuxer_bytes = url_close_dyn_buf(pb, &demuxer_content);
      }
      
      bgn = CurrentHostCounter() * 1000 / CurrentHostFrequency();

      do {
        ret = codec_write(pcodec, demuxer_content, demuxer_bytes);
      } while(ret < 0);

      end = CurrentHostCounter() * 1000 / CurrentHostFrequency();
      if (convert_bytestream)
        av_free(demuxer_content);

      free(data);
      fprintf(stdout, "decode time(%llu)\n", end-bgn);
      frame_count++;
      usleep(10000);

      ctx.demuxer->Read(&data, &byte_count, &dts, &pts);
      if (!byte_count) done = true;
      total += byte_count;
    }
  }

fail:
	codec_close(&ctx.apcodec);
	codec_close(&ctx.vpcodec);

  return 0;
}
