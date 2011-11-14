#pragma once
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

#if defined(TARGET_AMLOGIC)

#include "DVDVideoCodec.h"
#include "cores/dvdplayer/DVDStreamInfo.h"
#include "threads/Thread.h"

typedef struct am_private_t am_private_t;

class CAmlogic : public CThread
{
public:
  CAmlogic();
  virtual ~CAmlogic();

  bool OpenDecoder(CDVDStreamInfo &hints);
  void CloseDecoder(void);
  void Reset(void);

  int  Decode(unsigned char *pData, size_t size, double dts, double pts);

  bool GetPicture(DVDVideoPicture* pDvdVideoPicture);
  void SetDropState(bool bDrop);
  int  GetDataSize(void);
  double GetTimeSize(void);

protected:
  virtual void Process();

private:
  am_private_t  *am_private;
  bool          m_started;
  int64_t       m_cur_pts;
  int64_t       m_cur_pictcnt;
  int64_t       m_old_pictcnt;
  CEvent        m_ready_event;
};

#endif
