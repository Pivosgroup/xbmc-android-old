/*
 *      Copyright (C) 2011 Team XBMC
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

#if defined (TARGET_AMLOGIC)
#include "AMLPlayer.h"
#include "Application.h"
#include "FileItem.h"
#include "FileURLProtocol.h"
#include "GUIInfoManager.h"
#include "Util.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/URIUtils.h"

// amlogic libplayer
extern "C"
{
#include <player.h>
}

static int set_video_axis(int x, int y, int width, int height)
{
  int fd;
  const char *path = "/sys/class/video/axis" ;
  char  bcmd[32];
  fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
  if (fd >= 0) {
    sprintf(bcmd, "%d %d %d %d", x, y, width, height);
    write(fd, bcmd, strlen(bcmd));
    close(fd);
    return 0;
  }
  return -1;
}

static int set_sysfs_int(const char *path, int val)
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

static int update_player_info(int pid,player_info_t * info)
{
  //printf("update_player_info: pid:%d, status:%x, current pos:%d, total:%d, errcode:%x\n",
  //  pid, info->status,info->current_time, info->full_time, ~(info->error_no));
  return 0;
}

static int media_info_dump(media_info_t* minfo)
{
  int i = 0;
  printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  printf("======||file size:%lld\n",                          minfo->stream_info.file_size);
  printf("======||file type:%d\n",                            minfo->stream_info.type);
  printf("======||duration:%d\n",                             minfo->stream_info.duration);
  printf("======||has video track?:%s\n",                     minfo->stream_info.has_video>0?"YES!":"NO!");
  printf("======||has audio track?:%s\n",                     minfo->stream_info.has_audio>0?"YES!":"NO!");
  printf("======||has internal subtitle?:%s\n",               minfo->stream_info.has_sub>0?"YES!":"NO!");
  printf("======||internal subtile counts:%d\n",              minfo->stream_info.total_sub_num);
  if (minfo->stream_info.has_video && minfo->stream_info.total_video_num > 0)
  {
    printf("======||video index:%d\n",                        minfo->stream_info.cur_video_index);
    printf("======||video counts:%d\n",                       minfo->stream_info.total_video_num);
    printf("======||video width :%d\n",                       minfo->video_info[0]->width);
    printf("======||video height:%d\n",                       minfo->video_info[0]->height);
    printf("======||video ratio :%d:%d\n",                    minfo->video_info[0]->aspect_ratio_num,minfo->video_info[0]->aspect_ratio_den);
    printf("======||frame_rate  :%.2f\n",                     (float)minfo->video_info[0]->frame_rate_num/minfo->video_info[0]->frame_rate_den);
    printf("======||video bitrate:%d\n",                      minfo->video_info[0]->bit_rate);
    printf("======||video format:%d\n",                       minfo->video_info[0]->format);
    printf("======||video duration:%d\n",                     minfo->video_info[0]->duartion);
  }
  if (minfo->stream_info.has_audio && minfo->stream_info.total_audio_num > 0)
  {
    printf("======||audio index:%d\n",                        minfo->stream_info.cur_audio_index);
    printf("======||audio counts:%d\n",                       minfo->stream_info.total_audio_num);
    for (i = 0; i < minfo->stream_info.total_audio_num; i++)
    {
      printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
      printf("======||audio track(%d) codec type:%d\n",       i, minfo->audio_info[i]->aformat);
      printf("======||audio track(%d) audio_channel:%d\n",    i, minfo->audio_info[i]->channel);
      printf("======||audio track(%d) bit_rate:%d\n",         i, minfo->audio_info[i]->bit_rate);
      printf("======||audio track(%d) audio_samplerate:%d\n", i, minfo->audio_info[i]->sample_rate);
      printf("======||audio track(%d) duration:%d\n",         i, minfo->audio_info[i]->duration);
      printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
      if (NULL != minfo->audio_info[i]->audio_tag)
      {
        printf("======||audio track title:%s",                minfo->audio_info[i]->audio_tag->title!=NULL?minfo->audio_info[i]->audio_tag->title:"unknown");
        printf("\n======||audio track album:%s",              minfo->audio_info[i]->audio_tag->album!=NULL?minfo->audio_info[i]->audio_tag->album:"unknown");
        printf("\n======||audio track author:%s\n",           minfo->audio_info[i]->audio_tag->author!=NULL?minfo->audio_info[i]->audio_tag->author:"unknown");
        printf("\n======||audio track year:%s\n",             minfo->audio_info[i]->audio_tag->year!=NULL?minfo->audio_info[i]->audio_tag->year:"unknown");
        printf("\n======||audio track comment:%s\n",          minfo->audio_info[i]->audio_tag->comment!=NULL?minfo->audio_info[i]->audio_tag->comment:"unknown");
        printf("\n======||audio track genre:%s\n",            minfo->audio_info[i]->audio_tag->genre!=NULL?minfo->audio_info[i]->audio_tag->genre:"unknown");
        printf("\n======||audio track copyright:%s\n",        minfo->audio_info[i]->audio_tag->copyright!=NULL?minfo->audio_info[i]->audio_tag->copyright:"unknown");
        printf("\n======||audio track track:%d\n",            minfo->audio_info[i]->audio_tag->track);
      }
    }
  }
  if (minfo->stream_info.has_sub && minfo->stream_info.total_sub_num > 0)
  {
    printf("======||subtitle index:%d\n",                     minfo->stream_info.cur_sub_index);
    printf("======||subtitle counts:%d\n",                    minfo->stream_info.total_sub_num);
    for (i = 0; i < minfo->stream_info.total_sub_num; i++)
    {
      if (0 == minfo->sub_info[i]->internal_external){
        printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
        printf("======||internal subtitle(%d) pid:%d\n",      i, minfo->sub_info[i]->id);
        printf("======||internal subtitle(%d) language:%s\n", i, minfo->sub_info[i]->sub_language?minfo->sub_info[i]->sub_language:"unknown");
        printf("======||internal subtitle(%d) width:%d\n",    i, minfo->sub_info[i]->width);
        printf("======||internal subtitle(%d) height:%d\n",   i, minfo->sub_info[i]->height);
        printf("======||internal subtitle(%d) resolution:%d\n", i, minfo->sub_info[i]->resolution);
        printf("======||internal subtitle(%d) subtitle size:%lld\n", i, minfo->sub_info[i]->subtitle_size);
        printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
      }
    }
  }
  printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  return 0;
}

bool CAMLPlayer::m_aml_init = false;

CAMLPlayer::CAMLPlayer(IPlayerCallback &callback)
  : IPlayer(callback),
  CThread(),
  m_ready(true)
{
  m_pid = -1;
  m_speed = 0;
  m_paused = false;
  m_StopPlaying = false;
  if (!m_aml_init)
  {
    //player_init();
    //printf("player init......\n");
    m_aml_init = true;
  }
}

CAMLPlayer::~CAMLPlayer()
{
  CloseFile();
}

bool CAMLPlayer::OpenFile(const CFileItem &file, const CPlayerOptions &options)
{
  try
  {
    CLog::Log(LOGNOTICE, "CAMLPlayer: Opening: %s", file.GetPath().c_str());
    // if playing a file close it first
    // this has to be changed so we won't have to close it.
    if(ThreadHandle())
      CloseFile();

    m_item = file;
    m_options = options;
    m_elapsed_ms  =  0;
    m_duration_ms =  0;

    m_audio_index = -1;
    m_audio_count =  0;
    m_audio_info  = "none";
    m_audio_delay = g_settings.m_currentVideoSettings.m_AudioDelay;

    m_video_index = -1;
    m_video_count =  0;
    m_video_info  = "none";
    m_video_width    =  0;
    m_video_height   =  0;
    m_video_fps_numerator = 25;
    m_video_fps_denominator = 1;

    m_subtitle_index = -1;
    m_subtitle_count =  0;
    m_subtitle_delay =  0;

    m_chapter_index  =  0;
    m_chapter_count  =  0;

    m_show_mainvideo = -1;
    m_dst_rect.SetRect(0, 0, 0, 0);

    static URLProtocol vfs_protocol = {
      "vfs",
      CFileURLProtocol::Open,
      CFileURLProtocol::Read,
      CFileURLProtocol::Write,
      CFileURLProtocol::Seek,
      CFileURLProtocol::Seek, // url_exseek, an amlogic extension.
      CFileURLProtocol::Close,
    };

    CStdString url = m_item.GetPath();
    if (url.Left(6).Equals("smb://"))
    {
      // the name string needs to persist 
      static const char *smb_name = "smb";
      vfs_protocol.name = smb_name;
    }
    else if (url.Left(6).Equals("afp://"))
    {
      // the name string needs to persist 
      static const char *smb_name = "afp";
      vfs_protocol.name = smb_name;
    }
    else if (url.Left(6).Equals("nfs://"))
    {
      // the name string needs to persist 
      static const char *smb_name = "nfs";
      vfs_protocol.name = smb_name;
    }
    else if (url.Left(7).Equals("http://"))
    {
      // strip user agent that we append
      int pos = url.Find('|');
      if (pos != -1)
        url = url.erase(pos-1, url.size());
    }
    printf("CAMLPlayer::OpenFile: URL=%s\n", url.c_str());

    player_init();
    printf("player init......\n");

    // must be after player_init
    av_register_protocol2(&vfs_protocol, sizeof(vfs_protocol));

    static play_control_t  play_control;
    memset(&play_control, 0, sizeof(play_control_t));
    // if we do not register a callback,
    // then the libamplayer will free run checking status.
    player_register_update_callback(&play_control.callback_fn, &update_player_info, 1000);
    // leak file_name for now.
    play_control.file_name = (char*)strdup(url.c_str());
    //play_control->nosound   = 1; // if disable audio...,must call this api
    play_control.video_index = -1; //MUST
    play_control.audio_index = -1; //MUST
    play_control.sub_index   = -1; //MUST
    play_control.hassub      =  1; //enable subtitle
    play_control.t_pos       = -1;
    play_control.need_start  =  1; // if 0,you can omit player_start_play API.
                                   // just play video/audio immediately.
                                   // if 1,then need call "player_start_play" API;
    m_pid = player_start(&play_control, 0);
    if (m_pid < 0)
    {
      printf("player start failed! error = %d\n", m_pid);
      return false;
    }

    // setupo to spin the busy dialog until we are playing
    m_ready.Reset();

    g_renderManager.PreInit();

    // create the playing thread
    m_StopPlaying = false;
    Create();
    if (!m_ready.WaitMSec(100))
    {
      CGUIDialogBusy *dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
      dialog->Show();
      while (!m_ready.WaitMSec(1))
        g_windowManager.Process(false);
      dialog->Close();
    }

    // Playback might have been stopped due to some error.
    if (m_bStop || m_StopPlaying)
      return false;

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown on open", __FUNCTION__);
    return false;
  }
}

bool CAMLPlayer::CloseFile()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::CloseFile");
  m_StopPlaying = true;

  CLog::Log(LOGDEBUG, "CAMLPlayer: waiting for threads to exit");
  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();

  CLog::Log(LOGDEBUG, "CAMLPlayer: finished waiting");
  g_renderManager.UnInit();

  return true;
}

bool CAMLPlayer::IsPlaying() const
{
  return !m_bStop;
}

void CAMLPlayer::Pause()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::Pause");
  CSingleLock lock(m_aml_csection);

  if ((m_pid < 0) && m_StopPlaying)
    return;

  if (m_paused)
    player_resume(m_pid);
  else
    player_pause(m_pid);

  m_paused = !m_paused;
}

bool CAMLPlayer::IsPaused() const
{
  return m_paused;
}

bool CAMLPlayer::HasVideo() const
{
  return m_video_count > 0;
}

bool CAMLPlayer::HasAudio() const
{
  return m_audio_count > 0;
}

void CAMLPlayer::ToggleFrameDrop()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::ToggleFrameDrop");
}

bool CAMLPlayer::CanSeek()
{
  return GetTotalTime() > 0;
}

void CAMLPlayer::Seek(bool bPlus, bool bLargeStep)
{
  CSingleLock lock(m_aml_csection);

  // try chapter seeking first, chapter_index is ones based.
  int chapter_index = GetChapter();
  if (bLargeStep)
  {
    // seek to next chapter
    if (bPlus && (chapter_index < GetChapterCount()))
    {
      SeekChapter(chapter_index + 1);
      return;
    }
    // seek to previous chapter
    if (!bPlus && chapter_index > 1)
    {
      SeekChapter(chapter_index - 1);
      return;
    }
  }

  // force updated to m_elapsed_ms, m_duration_ms.
  GetTotalTime();

  int64_t seek_ms;
  if (g_advancedSettings.m_videoUseTimeSeeking)
  {
    if (bLargeStep && (GetTotalTime() > (2 * g_advancedSettings.m_videoTimeSeekForwardBig)))
      seek_ms = bPlus ? g_advancedSettings.m_videoTimeSeekForwardBig : g_advancedSettings.m_videoTimeSeekBackwardBig;
    else
      seek_ms = bPlus ? g_advancedSettings.m_videoTimeSeekForward    : g_advancedSettings.m_videoTimeSeekBackward;
    // convert to milliseconds
    seek_ms *= 1000;
    seek_ms += m_elapsed_ms;
  }
  else
  {
    float percent;
    if (bLargeStep)
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForwardBig : g_advancedSettings.m_videoPercentSeekBackwardBig;
    else
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForward    : g_advancedSettings.m_videoPercentSeekBackward;
    percent /= 100.0f;
    percent += (float)m_elapsed_ms/(float)m_duration_ms;
    // convert to milliseconds
    seek_ms = m_duration_ms * percent;
  }

  // handle stacked videos, dvdplayer does it so we do it too.
  if (g_application.CurrentFileItem().IsStack() &&
    (seek_ms > m_duration_ms || seek_ms < 0))
  {
    CLog::Log(LOGDEBUG, "CAMLPlayer::Seek: In mystery code, what did I do");
    g_application.SeekTime((seek_ms - m_elapsed_ms) * 0.001 + g_application.GetTime());
    // warning, don't access any object variables here as
    // the object may have been destroyed
    return;
  }

  if (seek_ms <= 0)
    seek_ms = 100;

  if (seek_ms > m_duration_ms)
    seek_ms = m_duration_ms;

  // do seek here
  if (check_pid_valid(m_pid))
    player_timesearch(m_pid, seek_ms/1000);
}

bool CAMLPlayer::SeekScene(bool bPlus)
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::SeekScene");
  return false;
}

void CAMLPlayer::SeekPercentage(float fPercent)
{
  CSingleLock lock(m_aml_csection);

  // do seek here
}

float CAMLPlayer::GetPercentage()
{
  GetTotalTime();
  if (m_duration_ms)
    return 100.0f * (float)m_elapsed_ms/(float)m_duration_ms;
  else
    return 0.0f;
}

void CAMLPlayer::SetVolume(long nVolume)
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::SetVolume(%ld)", nVolume);
  CSingleLock lock(m_aml_csection);
  // nVolume is a milliBels from -6000 (-60dB or mute) to 0 (0dB or full volume)
  // 0db is represented by Volume = 0x10000000
  // bit shifts adjust by 6db.
  // Maximum gain is 0xFFFFFFFF ~=24db
  //uint32_t volume = (1.0f + (nVolume / 6000.0f)) * (float)0x10000000;
  if (check_pid_valid(m_pid))
  {
    //int min, max;
    //float volume =
    // int audio_set_mute(m_pid, int mute_on);
    //audio_get_volume_range(m_pid, &min, &max)
    //audio_set_volume(m_pid, volume);
  }
}

void CAMLPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetAudioInfo");
  if (check_pid_valid(m_pid))
  {
    //m_audio_info.Format("Audio stream (%d) [%s] of type %s",
  }
  //strAudioInfo = m_audio_info;
}

void CAMLPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetVideoInfo");
  if (check_pid_valid(m_pid))
  {
    //m_video_info.Format("Video stream (%d) [%s] of type %s",
  }
  //strVideoInfo = m_video_info;
}

int CAMLPlayer::GetAudioStreamCount()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetAudioStreamCount");
  return m_audio_count;
}

int CAMLPlayer::GetAudioStream()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetAudioStream");
  return m_audio_index;
}

void CAMLPlayer::GetAudioStreamName(int iStream, CStdString &strStreamName)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetAudioStreamName");
  CSingleLock lock(m_aml_csection);

  // strStreamName.Format("%s", res.value.streamInfo.name);
  strStreamName.Format("Undefined");
}
 
void CAMLPlayer::SetAudioStream(int SetAudioStream)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::SetAudioStream");
  CSingleLock lock(m_aml_csection);
}

void CAMLPlayer::SetSubTitleDelay(float fValue = 0.0f)
{
  if (GetSubtitleCount())
  {
    CSingleLock lock(m_aml_csection);
    m_subtitle_delay = fValue * 1000.0;
  }
}

float CAMLPlayer::GetSubTitleDelay()
{
  return (float)m_subtitle_delay / 1000.0;
}

int CAMLPlayer::GetSubtitleCount()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetSubtitleCount");
  return m_subtitle_count;
}

int CAMLPlayer::GetSubtitle()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetSubtitle");
  return m_subtitle_index;
}

void CAMLPlayer::GetSubtitleName(int iStream, CStdString &strStreamName)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetSubtitleName");
  CSingleLock lock(m_aml_csection);
  strStreamName.Format("GetSubtitleName_%d", iStream);
  //strStreamName.Format("%s", res.value.streamInfo.name);
}
 
void CAMLPlayer::SetSubtitle(int iStream)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::SetSubtitle");
  CSingleLock lock(m_aml_csection);
  if (check_pid_valid(m_pid) && iStream > m_subtitle_count)
    player_sid(m_pid, iStream);
  m_subtitle_index = iStream;
}

bool CAMLPlayer::GetSubtitleVisible()
{
  return m_subtitle_show;
}

void CAMLPlayer::SetSubtitleVisible(bool bVisible)
{
  CSingleLock lock(m_aml_csection);

  //if (bVisible)
  //else
  m_subtitle_show = bVisible;
}

int CAMLPlayer::AddSubtitle(const CStdString& strSubPath)
{
  // this waits until we can stop/restart video stream.
  return -1;
}

void CAMLPlayer::Update(bool bPauseDrawing)
{
  g_renderManager.Update(bPauseDrawing);
}

void CAMLPlayer::GetVideoRect(CRect& SrcRect, CRect& DestRect)
{
  g_renderManager.GetVideoRect(SrcRect, DestRect);
}

void CAMLPlayer::SetVideoRect(const CRect &SrcRect, const CRect &DestRect)
{
  // check if destination rect or video view mode has changed
  if ((m_dst_rect != DestRect) || (m_view_mode != g_settings.m_currentVideoSettings.m_ViewMode))
  {
    m_dst_rect  = DestRect;
    m_view_mode = g_settings.m_currentVideoSettings.m_ViewMode;
  }
  else
  {
    // mainvideo 'should' be showing already if we get here, make sure.
    ShowMainVideo(true);
    return;
  }

  CRect gui;
  RESOLUTION res = g_graphicsContext.GetVideoResolution();
  gui.SetRect(0, 0, g_settings.m_ResInfo[res].iWidth, g_settings.m_ResInfo[res].iHeight);
  if (!gui.PtInRect(CPoint(m_dst_rect.x1, m_dst_rect.y1)))
    return;
  if (!gui.PtInRect(CPoint(m_dst_rect.x2, m_dst_rect.y2)))
    return;

  ShowMainVideo(false);

  // some odd scaling going on, we do not quite get what we expect
  //set_video_axis(m_dst_rect.x1, m_dst_rect.y1, m_dst_rect.Width(), m_dst_rect.Height());
  set_video_axis(0, 0, 0, 0);

  // we only get called once gui has changed to something
  // that would show video playback, so show it.
  ShowMainVideo(true);
}

void CAMLPlayer::GetVideoAspectRatio(float &fAR)
{
  fAR = g_renderManager.GetAspectRatio();
}

int CAMLPlayer::GetChapterCount()
{
  return m_chapter_count;
}

int CAMLPlayer::GetChapter()
{
  // returns a one based value.
  int chapter_index = m_chapter_index + 1;
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetChapter:chapter_index(%d)", chapter_index);
  return chapter_index;
}

void CAMLPlayer::GetChapterName(CStdString& strChapterName)
{
  if (m_chapter_count)
    strChapterName = m_chapters[m_chapter_index].name;
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetChapterName:strChapterName(%s)", strChapterName.c_str());
}

int CAMLPlayer::SeekChapter(int chapter_index)
{
  CSingleLock lock(m_aml_csection);

  // chapter_index is a one based value.
  int chapter_count = GetChapterCount();
  if (chapter_count > 0)
  {
    if (chapter_index < 1)
      chapter_index = 1;
    if (chapter_index > chapter_count)
      chapter_index = chapter_count;

    // time units are seconds,
    // so we add 1000ms to get into the chapter.
    int64_t seek_ms = m_chapters[chapter_index - 1].seekto_ms + 1000;

    // bugfix: dcchd takes forever to seek to 0 and play
    //  seek to 1 second and play is immediate.
    if (seek_ms <= 0)
      seek_ms = 1000;

    // seek to chapter here

  }
  else
  {
    // we do not have a chapter list so do a regular big jump.
    if (chapter_index > GetChapter())
      Seek(true,  true);
    else
      Seek(false, true);
  }
  return 0;
}

float CAMLPlayer::GetActualFPS()
{
  float video_fps = m_video_fps_numerator / m_video_fps_denominator;
  if (check_pid_valid(m_pid))
  {
    //video_fps = m_video_fps_numerator / m_video_fps_denominator;
  }
  CLog::Log(LOGDEBUG, "CAMLPlayer::GetActualFPS:m_video_fps(%f)", video_fps);
  return video_fps;
}

void CAMLPlayer::SeekTime(__int64 seek_ms)
{
  CSingleLock lock(m_aml_csection);
  // bugfix, dcchd takes forever to seek to 0 and play
  //  seek to 1 second and play is immediate.
  if (seek_ms <= 0)
    seek_ms = 100;

  // seek here
  if (check_pid_valid(m_pid))
    player_timesearch(m_pid, seek_ms/1000);
}

__int64 CAMLPlayer::GetTime()
{
  return m_elapsed_ms;
}

int CAMLPlayer::GetTotalTime()
{
  return m_duration_ms / 1000;
}

int CAMLPlayer::GetAudioBitrate()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::GetAudioBitrate");
  return 0;
}
int CAMLPlayer::GetVideoBitrate()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::GetVideoBitrate");
  return 0;
}

int CAMLPlayer::GetSourceBitrate()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::GetSourceBitrate");
  return 0;
}

int CAMLPlayer::GetChannels()
{
  // returns number of audio channels (ie 5.1 = 6)
  if (check_pid_valid(m_pid))
  {
    //m_audio_channels  = ;
  }

  return m_audio_channels;
}

int CAMLPlayer::GetBitsPerSample()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::GetBitsPerSample");
  return 0;
}

int CAMLPlayer::GetSampleRate()
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::GetSampleRate");
  return 0;
}

CStdString CAMLPlayer::GetAudioCodecName()
{
  CStdString strAudioCodec;
  //if (check_pid_valid(m_pid))
  //  strAudioCodec = ;
  return strAudioCodec;
}

CStdString CAMLPlayer::GetVideoCodecName()
{
  CStdString strVideoCodec;
  //if (check_pid_valid(m_pid))
  //  strVideoCodec = ;
  return strVideoCodec;
}

int CAMLPlayer::GetPictureWidth()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetPictureWidth(%d)", m_video_width);
  return m_video_width;
}

int CAMLPlayer::GetPictureHeight()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetPictureHeight(%)", m_video_height);
  return m_video_height;
}

bool CAMLPlayer::GetStreamDetails(CStreamDetails &details)
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetStreamDetails");
  return false;
}

void CAMLPlayer::ToFFRW(int iSpeed)
{
  CLog::Log(LOGDEBUG, "CAMLPlayer::ToFFRW: iSpeed(%d), m_speed(%d)", iSpeed, m_speed);
  CSingleLock lock(m_aml_csection);

  if (!check_pid_valid(m_pid) && m_StopPlaying)
    return;

  if (m_speed != iSpeed)
  {
    // recover power of two value
    int ipower = 0;
    int ispeed = abs(iSpeed);
    while (ispeed >>= 1) ipower++;

    switch(ipower)
    {
      // regular playback
      case  0:
        player_forward(m_pid, 0);
        break;
      default:
        // N x fast forward/rewind (I-frames)
        // speed playback 2,4,8
        if (iSpeed > 0)
          player_forward(m_pid,   iSpeed);
        else
          player_backward(m_pid, -iSpeed);
        break;
    }

    m_speed = iSpeed;
  }
}

void CAMLPlayer::OnStartup()
{
  //m_CurrentVideo.Clear();
  //m_CurrentAudio.Clear();
  //m_CurrentSubtitle.Clear();

  //CThread::SetName("CAMLPlayer");
}

void CAMLPlayer::OnExit()
{
  //CLog::Log(LOGNOTICE, "CAMLPlayer::OnExit()");
  Sleep(100);
  m_bStop = true;
  // if we didn't stop playing, advance to the next item in xbmc's playlist
  if(m_options.identify == false)
  {
    if (m_StopPlaying)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }
}

void CAMLPlayer::Process()
{
  CLog::Log(LOGNOTICE, "CAMLPlayer::Process");
  try
  {
    // wait for media to open with 30 second timeout.
    if (WaitForOpenMedia(30000))
    {
      // start the playback.
      int res = player_start_play(m_pid);
      if (res != PLAYER_SUCCESS)
        throw "CAMLPlayer::Process:player_start_play() failed";
    }
    else
    {
      throw "CAMLPlayer::Process:WaitForOpenMedia timeout";
    }

    // hide the mainvideo layer so we can get stream info
    // and setup/transition to gui video playback
    // without having video playback blended into it.
    if (m_item.IsVideo())
      ShowMainVideo(false);

    // wait for playback to start with 20 second timeout
    if (WaitForPlaying(20000))
    {
      m_speed = 1;
      m_callback.OnPlayBackSpeedChanged(m_speed);

      // get our initial status.
      GetStatus();

      // restore system volume setting.
      SetVolume(g_settings.m_nVolumeLevel);

      // starttime has units of seconds
      if (m_options.starttime > 0)
      {
        SeekTime(m_options.starttime * 1000);
        WaitForPlaying(1000);
      }

      // wait until video or audio format is valid
      if (!WaitForFormatValid(2000))
        throw "CAMLPlayer::Process: WaitForFormatValid failed";

      // drop CGUIDialogBusy dialog and release the hold in OpenFile.
      m_ready.Set();

      // we are playing but hidden and all stream fields are valid.
      // check for video in media content
      if (GetVideoStreamCount() > 0)
      {
        // turn on/off subs
        SetSubtitleVisible(g_settings.m_currentVideoSettings.m_SubtitleOn);
        SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

        // setup renderer for bypass. This tell renderer to get out of the way as
        // hw decoder will be doing the actual video rendering in a video plane
        // that is under the GUI layer.
        int width  = GetPictureWidth();
        int height = GetPictureHeight();
        double fFrameRate = GetActualFPS();
        unsigned int flags = 0;

        flags |= CONF_FLAGS_FORMAT_BYPASS;
        flags |= CONF_FLAGS_FULLSCREEN;
        CStdString formatstr = "BYPASS";
        CLog::Log(LOGDEBUG,"%s - change configuration. %dx%d. framerate: %4.2f. format: %s",
          __FUNCTION__, width, height, fFrameRate, formatstr.c_str());
        g_renderManager.IsConfigured();
        if(!g_renderManager.Configure(width, height, width, height, fFrameRate, flags, 0))
        {
          CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
        }
        if (!g_renderManager.IsStarted())
        {
          CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
        }
      }

      if (m_options.identify == false)
        m_callback.OnPlayBackStarted();

      while (!m_bStop && !m_StopPlaying)
      {
        player_status pstatus = player_get_state(m_pid);
        switch(pstatus)
        {
          case PLAYER_INITING:
          case PLAYER_TYPE_REDY:
          case PLAYER_INITOK:
            printf("CAMLPlayer::Process: %s\n", player_status2str(pstatus));
            // player is parsing file, decoder not running
            break;
          case PLAYER_RUNNING:
            GetStatus();
            // playback status, decoder is running
            break;

          case PLAYER_START:
          case PLAYER_BUFFERING:
          case PLAYER_PAUSE:
          case PLAYER_SEARCHING:
          case PLAYER_SEARCHOK:
          case PLAYER_FF_END:
          case PLAYER_FB_END:
          case PLAYER_PLAY_NEXT:
          case PLAYER_BUFFER_OK:
            printf("CAMLPlayer::Process: %s\n", player_status2str(pstatus));
            break;

          case PLAYER_PLAYEND:
            GetStatus();
            printf("CAMLPlayer::Process: %s\n", player_status2str(pstatus));
            break;

          case PLAYER_ERROR:
          case PLAYER_STOPED:
          case PLAYER_EXIT:
            printf("CAMLPlayer::Process PLAYER_STOPED\n");
            printf("CAMLPlayer::Process: %s\n", player_status2str(pstatus));
            m_StopPlaying = true;
            break;
        }
        Sleep(500);
      }
    }
  }
  catch(char* error)
  {
    CLog::Log(LOGERROR, "%s", error);
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "CAMLPlayer::Process Exception thrown");
  }

  printf("CAMLPlayer::Process stopped\n");
  if (check_pid_valid(m_pid))
  {
    player_stop(m_pid);
    player_exit(m_pid);
    m_pid = -1;
  }

  // we are done, hide the mainvideo layer.
  ShowMainVideo(false);
  m_ready.Set();
}

int CAMLPlayer::GetVideoStreamCount()
{
  //CLog::Log(LOGDEBUG, "CAMLPlayer::GetVideoStreamCount(%d)", m_video_count);
  return m_video_count;
}

void CAMLPlayer::ShowMainVideo(bool show)
{
  if (m_show_mainvideo == show)
    return;

  set_sysfs_int("/sys/class/video/disable_video", show ? 0:1);

  m_show_mainvideo = show;
}

bool CAMLPlayer::WaitForStopped(int timeout_ms)
{
  while (!m_bStop && (timeout_ms > 0))
  {
    player_status pstatus = player_get_state(m_pid);
    printf("CAMLPlayer::WaitForStopped: %s\n", player_status2str(pstatus));
    switch(pstatus)
    {
      default:
        Sleep(100);
        timeout_ms -= 100;
        break;
      case PLAYER_PLAYEND:
      case PLAYER_STOPED:
      case PLAYER_EXIT:
        return true;
        break;
    }
  }

  return false;
}

bool CAMLPlayer::WaitForPlaying(int timeout_ms)
{
  while (!m_bStop && (timeout_ms > 0))
  {
    player_status pstatus = player_get_state(m_pid);
    printf("CAMLPlayer::WaitForPlaying: %s\n", player_status2str(pstatus));
    switch(pstatus)
    {
      default:
        Sleep(500);
        timeout_ms -= 500;
        break;
      case PLAYER_ERROR:
      case PLAYER_EXIT:
        return false;
        break;
      case PLAYER_RUNNING:
        return true;
        break;
    }
  }

  return false;
}

bool CAMLPlayer::WaitForOpenMedia(int timeout_ms)
{
  while (!m_bStop && (timeout_ms > 0))
  {
    player_status pstatus = player_get_state(m_pid);
    printf("CAMLPlayer::WaitForOpenMedia: %s\n", player_status2str(pstatus));
    switch(pstatus)
      {
        default:
          Sleep(500);
          timeout_ms -= 500;
          break;
      case PLAYER_ERROR:
      case PLAYER_EXIT:
        return false;
        break;
      case PLAYER_INITOK:
        return true;
        break;
    }
  }

  return false;
}

bool CAMLPlayer::WaitForFormatValid(int timeout_ms)
{
  while (!m_bStop && (timeout_ms > 0))
  {
    player_status pstatus = player_get_state(m_pid);
    printf("CAMLPlayer::WaitForFormatValid: %s\n", player_status2str(pstatus));
    switch(pstatus)
    {
      default:
        Sleep(500);
        timeout_ms -= 500;
        break;
      case PLAYER_ERROR:
      case PLAYER_EXIT:
        return false;
        break;
      case PLAYER_RUNNING:
        media_info_t media_info;
        int res = player_get_media_info(m_pid, &media_info);
        if (res != PLAYER_SUCCESS)
          return false;

        media_info_dump(&media_info);

        // m_video_index, m_audio_index, m_subtitle_index might be -1 eventhough
        // total_video_xxx is > 0, not sure why, they should be set to zero or
        // some other sensible value.
        printf("CAMLPlayer::WaitForFormatValid: m_video_index(%d), m_audio_index(%d), m_subtitle_index(%d)\n",
          media_info.stream_info.cur_video_index,
          media_info.stream_info.cur_audio_index,
          media_info.stream_info.cur_sub_index);
        if (media_info.stream_info.has_video && media_info.stream_info.total_video_num > 0)
        {
          m_video_index	= media_info.stream_info.cur_video_index;
          m_video_count	= media_info.stream_info.total_video_num;
          if (m_video_index != 0)
            m_video_index = 0;
          m_video_width	= media_info.video_info[m_video_index]->width;
          m_video_height= media_info.video_info[m_video_index]->height;
          m_video_fps_numerator	= media_info.video_info[m_video_index]->frame_rate_num;
          m_video_fps_denominator = media_info.video_info[m_video_index]->frame_rate_den;

          // bail if we do not get a valid width/height
          if (m_video_width == 0 || m_video_height == 0)
            return false;
        }

        if (media_info.stream_info.has_audio && media_info.stream_info.total_audio_num > 0)
        {
          m_audio_index	= media_info.stream_info.cur_audio_index;
          if (m_audio_index != 0)
            m_audio_index = 0;
          m_audio_count	= media_info.stream_info.total_audio_num;
        }

        if (media_info.stream_info.has_sub && media_info.stream_info.total_sub_num > 0)
        {
          m_subtitle_index = media_info.stream_info.cur_sub_index;
          if (m_subtitle_index != 0)
            m_subtitle_index = 0;
          m_subtitle_count = media_info.stream_info.total_sub_num;
        }
        return true;
        break;
    }
  }

  return false;
}

bool CAMLPlayer::GetStatus()
{
  CSingleLock lock(m_aml_csection);

  if (!check_pid_valid(m_pid))
    return false;

  player_info_t player_info;
  int res = player_get_play_info(m_pid, &player_info);
  if (res != PLAYER_SUCCESS)
    return false;

  m_elapsed_ms  = player_info.current_ms;
  m_duration_ms = 1000 * player_info.full_time;
  //printf("CAMLPlayer::GetStatus: audio_bufferlevel(%f), video_bufferlevel(%f), bufed_time(%d), bufed_pos(%lld)\n",
  //  player_info.audio_bufferlevel, player_info.video_bufferlevel, player_info.bufed_time, player_info.bufed_pos);

  return true;
}

#endif
