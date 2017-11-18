#pragma once

/*
 *      Copyright (C) 2011-2013 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <string>
#include <vector>

#include <linux/fb.h>

#include <EGL/egl.h>
#include "EGLNativeType.h"

#define EDID_STRUCT_DISPLAY     0x14
#define EDID_MAXSIZE            512
#define EDID_HEADERSIZE         8

#define EDID_STRUCT_DISPLAY             0x14
#define EDID_DTM_START                  0x36
#define EDID_DTM_OFFSET_DIMENSION       0x0c

static const char EDID_HEADER[EDID_HEADERSIZE] = { 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0 };

class CEGLNativeTypeIMX : public CEGLNativeType
{
  struct dt_dim {
    uint8_t Width;
    uint8_t Height;
    uint8_t msbits;
  };

public:
  CEGLNativeTypeIMX();
  virtual ~CEGLNativeTypeIMX();
  virtual std::string GetNativeName() const { return "iMX"; }
  virtual bool  CheckCompatibility();
  virtual void  Initialize();
  virtual void  Destroy();
  virtual int   GetQuirks() { return EGL_QUIRK_RECREATE_DISPLAY_ON_CREATE_WINDOW; }

  virtual bool  CreateNativeDisplay();
  virtual bool  CreateNativeWindow();
  virtual bool  GetNativeDisplay(XBNativeDisplayType **nativeDisplay) const;
  virtual bool  GetNativeWindow(XBNativeWindowType **nativeWindow) const;

  virtual bool  DestroyNativeWindow();
  virtual bool  DestroyNativeDisplay();

  virtual bool  GetNativeResolution(RESOLUTION_INFO *res) const;
  virtual bool  SetNativeResolution(const RESOLUTION_INFO &res);
  virtual bool  ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions);
  virtual bool  GetPreferredResolution(RESOLUTION_INFO *res) const;

  virtual bool  ShowWindow(bool show = true);

#ifdef HAS_IMXVPU
protected:
  bool m_readonly;
  bool m_show;
  float m_sar;
  RESOLUTION_INFO m_init;
  bool ModeToResolution(std::string mode, RESOLUTION_INFO *res) const;
  bool FindMatchingResolution(const RESOLUTION_INFO &res, const std::vector<RESOLUTION_INFO> &resolutions);
  void GetMonitorSAR();
  float ValidateSAR(struct dt_dim *dtm, bool mb = false);

  EGLNativeDisplayType m_display;
  EGLNativeWindowType  m_window;
  uint8_t              m_edid[EDID_MAXSIZE];
#endif
};
