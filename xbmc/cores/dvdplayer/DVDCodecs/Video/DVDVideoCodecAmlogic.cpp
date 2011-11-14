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

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#endif

#if defined(TARGET_AMLOGIC)
#include "Amlogic.h"
#include "settings/GUISettings.h"
#include "DVDClock.h"
#include "DVDStreamInfo.h"
#include "DVDVideoCodecAmlogic.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

#define __MODULE_NAME__ "DVDVideoCodecAmlogic"

CDVDVideoCodecAmlogic::CDVDVideoCodecAmlogic() :
  m_Codec(NULL),
  m_pFormatName("")
{
}

CDVDVideoCodecAmlogic::~CDVDVideoCodecAmlogic()
{
  Dispose();
}

bool CDVDVideoCodecAmlogic::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  m_Codec = new CAmlogic();
  if (!m_Codec)
  {
    CLog::Log(LOGERROR, "%s: Failed to create Amlogic Codec", __MODULE_NAME__);
    return false;
  }

  if (m_Codec && !m_Codec->OpenDecoder(hints))
  {
    CLog::Log(LOGERROR, "%s: Failed to open Amlogic Codec", __MODULE_NAME__);
    return false;
  }

  // allocate a dummy YV12 DVDVideoPicture buffer.
  // first make sure all properties are reset.
  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));

  m_videobuffer.dts = DVD_NOPTS_VALUE;
  m_videobuffer.pts = DVD_NOPTS_VALUE;
  m_videobuffer.format = DVDVideoPicture::FMT_AMLREF;
  m_videobuffer.color_range  = 0;
  m_videobuffer.color_matrix = 4;
  m_videobuffer.iFlags  = DVP_FLAG_ALLOCATED;
  m_videobuffer.iWidth  = hints.width;
  m_videobuffer.iHeight = hints.height;
  m_videobuffer.iDisplayWidth  = hints.width;
  m_videobuffer.iDisplayHeight = hints.height;
  m_videobuffer.amlcodec = m_Codec;

  CLog::Log(LOGINFO, "%s: Opened Amlogic Codec", __MODULE_NAME__);
  return true;
}

void CDVDVideoCodecAmlogic::Dispose(void)
{
  if (m_Codec)
  {
    m_Codec->CloseDecoder();
    m_Codec = NULL;
  }
  if (m_videobuffer.iFlags & DVP_FLAG_ALLOCATED)
  {
    m_videobuffer.iFlags = 0;
  }
}

int CDVDVideoCodecAmlogic::Decode(BYTE *pData, int iSize, double dts, double pts)
{
  // Handle Input, add demuxer packet to input queue, we must accept it or
  // it will be discarded as DVDPlayerVideo has no concept of "try again".
  return m_Codec->Decode(pData, iSize, dts, pts);
}

void CDVDVideoCodecAmlogic::Reset(void)
{
  m_Codec->Reset();
}

bool CDVDVideoCodecAmlogic::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  m_Codec->GetPicture(&m_videobuffer);
  *pDvdVideoPicture = m_videobuffer;

  return true;
}

void CDVDVideoCodecAmlogic::SetDropState(bool bDrop)
{
  m_Codec->SetDropState(bDrop);
}

int CDVDVideoCodecAmlogic::GetDataSize(void)
{
  return m_Codec->GetDataSize();
}

double CDVDVideoCodecAmlogic::GetTimeSize(void)
{
  return m_Codec->GetTimeSize();
}

#endif
