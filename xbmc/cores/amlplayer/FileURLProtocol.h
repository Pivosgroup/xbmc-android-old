#pragma once
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

#include <stdint.h>
#include <stdio.h>

extern "C"
{
#include <libavformat/avio.h>
}

class CFileURLProtocol
{

public:
  static int Open (URLContext *h, const char *filename, int flags);
  static int Read (URLContext *h, unsigned char *buf, int size);
  static int Write(URLContext *h, unsigned char *buf, int size);
  static int64_t Seek(URLContext *h, int64_t pos, int whence);
  static int64_t SeekEx(URLContext *h, int64_t pos, int whence);
  static int Close(URLContext *h);
};
