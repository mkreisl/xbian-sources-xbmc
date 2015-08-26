/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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

#ifndef WINDOW_EVENTS_LINUX_H
#define WINDOW_EVENTS_LINUX_H

#pragma once
#include "windowing/WinEvents.h"
#include "input/linux/LinuxInputDevices.h"

class CWinEventsLinux : public IWinEvents
{
public:
  CWinEventsLinux();
  bool MessagePump();
  size_t GetQueueSize();
  void RefreshDevices();
  void CloseDevices();
  void Notify(const Observable &obs, const ObservableMessage msg)
  {
    if (msg == ObservableMessagePeripheralsChanged)
      RefreshDevices();
  }
  static bool IsRemoteLowBattery();

private:
  static bool m_initialized;
  static CLinuxInputDevices m_devices;
#ifdef TARGET_RASPBERRY_PI
  bool LoadXML(const std::string strFileName);
  void *LoadImage(const std::string texturePath, int &width, int &height);
  int64_t m_last_mouse_move_time;
  struct
  {
    void *pixels;
    int width, height;
  } m_cursors[4];
  int m_mouse_state;
#endif
};

#endif
