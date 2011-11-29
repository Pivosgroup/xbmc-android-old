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

#include "FileURLProtocol.h"
#include "filesystem/File.h"
#include "utils/log.h"
#include "FileItem.h"

//========================================================================
int CFileURLProtocol::Open(URLContext *h, const char *filename, int flags)
{
  if (flags != URL_RDONLY)
  {
    CLog::Log(LOGDEBUG, "CFileURLProtocol::Open: Only read-only is supported");
    return -EINVAL;
  }

  CStdString url = filename;
  if (url.Left(strlen("xb-http://")).Equals("xb-http://"))
  {
    url = url.Right(url.size() - strlen("xb-"));
  }
  CLog::Log(LOGDEBUG, "CFileURLProtocol::Open filename2(%s)", url.c_str());
  // open the file, always in read mode, calc bitrate
  unsigned int cflags = READ_BITRATE;
  XFILE::CFile *cfile = new XFILE::CFile();

  if (CFileItem(url, false).IsInternetStream())
    cflags |= READ_CACHED;

  // open file in binary mode
  if (!cfile->Open(url, cflags))
  {
    delete cfile;
    return -EIO;
  }

  h->priv_data = (void *)cfile;

  return 0;
}

//========================================================================
int CFileURLProtocol::Read(URLContext *h, unsigned char *buf, int size)
{
  XFILE::CFile *cfile = (XFILE::CFile*)h->priv_data;

  int readsize = cfile->Read(buf, size);
  //CLog::Log(LOGDEBUG, "CFileURLProtocol::Read size(%d), readsize(%d)", size, readsize);

  return readsize;
}

//========================================================================
int CFileURLProtocol::Write(URLContext *h, unsigned char *buf, int size)
{
  //CLog::Log(LOGDEBUG, "CFileURLProtocol::Write size(%d)", size);
  return 0;
}

//========================================================================
int64_t CFileURLProtocol::Seek(URLContext *h, int64_t pos, int whence)
{
  XFILE::CFile *cfile = (XFILE::CFile*)h->priv_data;

  // seek to the end of file
  if (whence == AVSEEK_SIZE)
    pos = cfile->GetLength();
  else
    pos = cfile->Seek(pos, whence & ~AVSEEK_FORCE);

  //CLog::Log(LOGDEBUG, "CFileURLProtocol::Seek2 pos(%lld), whence(%d)", pos, whence);

  return pos;
}

//========================================================================
int64_t CFileURLProtocol::SeekEx(URLContext *h, int64_t pos, int whence)
{
  XFILE::CFile *cfile = (XFILE::CFile*)h->priv_data;

  // seek to the end of file
  if (whence == AVSEEK_SIZE)
    pos = cfile->GetLength();
  else
    pos = cfile->Seek(pos, whence & ~AVSEEK_FORCE);

  //CLog::Log(LOGDEBUG, "CFileURLProtocol::Seek2 pos(%lld), whence(%d)", pos, whence);

  return pos;
}

//========================================================================
int CFileURLProtocol::Close(URLContext *h)
{
  //CLog::Log(LOGDEBUG, "CFileURLProtocol::Close");
  XFILE::CFile *cfile = (XFILE::CFile*)h->priv_data;
  cfile->Close();
  delete cfile;

  return 0;
}
