/*
 *      Copyright (C) 2005-2015 Team XBMC
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

#include "Resolution.h"
#include "GraphicContext.h"
#include "utils/log.h"
#include "utils/MathUtils.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include <cstdlib>
#include <float.h>

#include "Application.h"

#include "windowing/egl/vc_hdmi.h"

RESOLUTION_INFO::RESOLUTION_INFO(int width, int height, float aspect, const std::string &mode) :
  strMode(mode)
{
  iWidth = width;
  iHeight = height;
  iBlanking = 0;
  iScreenWidth = width;
  iScreenHeight = height;
  fPixelRatio = aspect ? ((float)width)/height / aspect : 1.0f;
  bFullScreen = true;
  fRefreshRate = 0;
  dwFlags = iSubtitles = iScreen = 0;
}

RESOLUTION_INFO::RESOLUTION_INFO(const RESOLUTION_INFO& res) :
  Overscan(res.Overscan),
  strMode(res.strMode),
  strOutput(res.strOutput),
  strId(res.strId)
{
  bFullScreen = res.bFullScreen;
  iScreen = res.iScreen; iWidth = res.iWidth; iHeight = res.iHeight;
  iScreenWidth = res.iScreenWidth; iScreenHeight = res.iScreenHeight;
  iSubtitles = res.iSubtitles; dwFlags = res.dwFlags;
  fPixelRatio = res.fPixelRatio; fRefreshRate = res.fRefreshRate;
  refresh_rate = res.refresh_rate; iBlanking = res.iBlanking;
}

float RESOLUTION_INFO::DisplayRatio() const
{
  return iWidth * fPixelRatio / iHeight;
}

RESOLUTION CResolutionUtils::ChooseBestResolution(float fps, int width, int height, bool is3D, bool isI)
{
  RESOLUTION res = g_graphicsContext.GetVideoResolution();
  float weight;
  if (!FindResolutionFromOverride(fps, width, height, is3D, isI, res, weight, false)) //find a refreshrate from overrides
  {
    if (!FindResolutionFromOverride(fps, width, height, is3D, isI, res, weight, true))//if that fails find it from a fallback
      FindResolutionFromFpsMatch(fps, width, height, is3D, isI, res, weight);//if that fails use automatic refreshrate selection
  }
  CLog::Log(LOGNOTICE, "Display resolution ADJUST : %s (%d) (weight: %.3f)",
            g_graphicsContext.GetResInfo(res).strMode.c_str(), res, weight);
  return res;
}

bool CResolutionUtils::FindResolutionFromOverride(float fps, int width, int height, bool is3D, bool isI, RESOLUTION &resolution, float& weight, bool fallback)
{
  RESOLUTION_INFO curr = g_graphicsContext.GetResInfo(resolution);

  //try to find a refreshrate from the override
  for (int i = 0; i < (int)g_advancedSettings.m_videoAdjustRefreshOverrides.size(); i++)
  {
    RefreshOverride& override = g_advancedSettings.m_videoAdjustRefreshOverrides[i];

    if (override.fallback != fallback)
      continue;

    //if we're checking for overrides, check if the fps matches
    if (!fallback && (fps < override.fpsmin || fps > override.fpsmax))
      continue;

    for (size_t j = (int)RES_DESKTOP; j < CDisplaySettings::GetInstance().ResolutionInfoSize(); j++)
    {
      RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)j);

      if (info.iScreenWidth  == curr.iScreenWidth &&
          info.iScreenHeight == curr.iScreenHeight &&
          (info.dwFlags & D3DPRESENTFLAG_MODEMASK) == (curr.dwFlags & D3DPRESENTFLAG_MODEMASK) &&
          info.iScreen == curr.iScreen)
      {
        if (info.fRefreshRate <= override.refreshmax &&
            info.fRefreshRate >= override.refreshmin)
        {
          resolution = (RESOLUTION)j;

          if (fallback)
          {
            CLog::Log(LOGDEBUG, "Found Resolution %s (%d) from fallback (refreshmin:%.3f refreshmax:%.3f)",
                      info.strMode.c_str(), resolution,
                      override.refreshmin, override.refreshmax);
          }
          else
          {
            CLog::Log(LOGDEBUG, "Found Resolution %s (%d) from override of fps %.3f (fpsmin:%.3f fpsmax:%.3f refreshmin:%.3f refreshmax:%.3f)",
                      info.strMode.c_str(), resolution, fps,
                      override.fpsmin, override.fpsmax, override.refreshmin, override.refreshmax);
          }

          weight = RefreshWeight(info.fRefreshRate, fps);

          return true; //fps and refresh match with this override, use this resolution
        }
      }
    }
  }

  return false; //no override found
}

void CResolutionUtils::FindResolutionFromFpsMatch(float fps, int width, int height, bool is3D, bool isI, RESOLUTION &resolution, float& weight)
{
  const float maxWeight = 0.0021f;
  RESOLUTION_INFO curr;

  resolution = FindClosestResolution(fps, width, height, is3D, isI, 1.0, resolution, weight);
  curr = g_graphicsContext.GetResInfo(resolution);

  if (weight >= maxWeight) //not a very good match, try a 2:3 cadence instead
  {
    CLog::Log(LOGDEBUG, "Resolution %s (%d) not a very good match for fps %.3f (weight: %.3f), trying 2:3 cadence",
        curr.strMode.c_str(), resolution, fps, weight);

    resolution = FindClosestResolution(fps, width, height, is3D, isI, 2.5, resolution, weight);
    curr = g_graphicsContext.GetResInfo(resolution);

    if (weight >= maxWeight) //2:3 cadence not a good match
    {
      CLog::Log(LOGDEBUG, "Resolution %s (%d) not a very good match for fps %.3f with 2:3 cadence (weight: %.3f), choosing 60 hertz",
          curr.strMode.c_str(), resolution, fps, weight);

      //get the resolution with the refreshrate closest to 60 hertz
      for (size_t i = (int)RES_DESKTOP; i < CDisplaySettings::GetInstance().ResolutionInfoSize(); i++)
      {
        RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)i);

        if (MathUtils::round_int(info.fRefreshRate) == 60
         && info.iScreenWidth  == curr.iScreenWidth
         && info.iScreenHeight == curr.iScreenHeight
         && (info.dwFlags & D3DPRESENTFLAG_MODEMASK) == (curr.dwFlags & D3DPRESENTFLAG_MODEMASK)
         && info.iScreen       == curr.iScreen)
        {
          if (fabs(info.fRefreshRate - 60.0) < fabs(curr.fRefreshRate - 60.0)) {
            resolution = (RESOLUTION)i;
            curr = info;
          }
        }
      }

      //60 hertz not available, get the highest refreshrate
      if (MathUtils::round_int(curr.fRefreshRate) != 60)
      {
        CLog::Log(LOGDEBUG, "60 hertz refreshrate not available, choosing highest");
        for (size_t i = (int)RES_DESKTOP; i < CDisplaySettings::GetInstance().ResolutionInfoSize(); i++)
        {
          RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)i);

          if (info.fRefreshRate  >  curr.fRefreshRate
           && info.iScreenWidth  == curr.iScreenWidth
           && info.iScreenHeight == curr.iScreenHeight
           && (info.dwFlags & D3DPRESENTFLAG_MODEMASK) == (curr.dwFlags & D3DPRESENTFLAG_MODEMASK)
           && info.iScreen       == curr.iScreen)
          {
            resolution = (RESOLUTION)i;
            curr = info;
          }
        }
      }

      weight = RefreshWeight(curr.fRefreshRate, fps);
    }
  }
}

RESOLUTION CResolutionUtils::FindClosestResolution(float fps, int width, int height, bool is3D, bool isI, float multiplier, RESOLUTION current, float& weight)
{
  if (CDisplaySettings::GetInstance().ResolutionInfoSize() < 2)
    return current;

  RESOLUTION_INFO curr = g_graphicsContext.GetResInfo(current);
  RESOLUTION orig_res  = CDisplaySettings::GetInstance().GetCurrentResolution();

  if (orig_res <= RES_DESKTOP)
    orig_res = RES_DESKTOP;

  RESOLUTION_INFO orig = g_graphicsContext.GetResInfo(orig_res);

  float fRefreshRate = fps;

  float last_diff = FLT_MAX;

  int curr_diff = std::abs(width - curr.iScreenWidth);
  int loop_diff = 0;

  // CHANGERESOLUTION
  if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTRESOLUTION) && !is3D)
  {
    bool i_found = false;

    // if interlaced mode
    if (isI && CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTRESOLUTIONINTERLACED))
      for (size_t i = (int)RES_DESKTOP; i < CDisplaySettings::GetInstance().ResolutionInfoSize(); i++)
      {
        const RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)i);
        if (!(info.dwFlags & D3DPRESENTFLAG_INTERLACED)
        ||  CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_MINIMUMVERTICAL) > info.iScreenHeight
        || ((int)CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTALLOWNONCEA) < GETFLAGS_GROUP(info.dwFlags)-1)
        ||    info.iScreenHeight != height
        ||    IS_3D(info.dwFlags))
          continue;

        current = (RESOLUTION)i;
        curr    = info;
        i_found = true;
      }

    if (!i_found)
      for (size_t i = (int)RES_DESKTOP; i < CDisplaySettings::GetInstance().ResolutionInfoSize(); i++)
      {
        const RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)i);
        if ((fabs(info.fRefreshRate - fRefreshRate) > 0.001 && fabs(info.fRefreshRate - 2*fRefreshRate) > 0.001)
        ||  IS_3D(info.dwFlags)
        ||  CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_MINIMUMVERTICAL) > info.iScreenHeight
        ||  (info.dwFlags & D3DPRESENTFLAG_INTERLACED && !isI)
        || (!CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTRESOLUTIONINTERLACED) && (info.dwFlags & D3DPRESENTFLAG_INTERLACED))
        || ((int)CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTALLOWNONCEA) < GETFLAGS_GROUP(info.dwFlags)-1)
        ||   width > info.iScreenWidth || height > info.iScreenHeight
        ||   pow(info.iScreenWidth*info.iScreenHeight - width*height, 2) > last_diff)
          continue;

        current = (RESOLUTION)i;
        curr    = info;
        i_found = true;
        last_diff = pow(curr.iScreenWidth*curr.iScreenHeight - width*height, 2);

        if (info.iScreenWidth == width && info.iScreenHeight == height)
          break;
      }

    last_diff = FLT_MAX;
    for (size_t i = (int)RES_DESKTOP; !i_found && i < CDisplaySettings::GetInstance().ResolutionInfoSize(); i++)
    {
      const RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)i);

      if (width > info.iScreenWidth || height > info.iScreenHeight
      ||  CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_MINIMUMVERTICAL) > info.iScreenHeight
      ||  pow(info.iScreenWidth*info.iScreenHeight - width*height, 2) > last_diff
      ||  info.iScreen != curr.iScreen
      || ((int)CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTALLOWNONCEA) < GETFLAGS_GROUP(info.dwFlags)-1)
      ||  (info.dwFlags & D3DPRESENTFLAG_MODEMASK) != (curr.dwFlags & D3DPRESENTFLAG_MODEMASK))
        {
        /*  CLog::Log(LOGDEBUG, "curr %.2f, trying %.2f, mode nr. %d, %dx%d msk %d, m_msk %d", info.fPixelRatio, curr.fPixelRatio, i,
               info.iScreenWidth, info.iScreenHeight, info.dwFlags & D3DPRESENTFLAG_MODEMASK,
                    m_iFlags & D3DPRESENTFLAG_MODEMASK); */
          continue;
        }

      current = (RESOLUTION)i;
      curr    = info;
      last_diff = pow(curr.iScreenWidth*curr.iScreenHeight - width*height, 2);
    }
  }

  // Find closest refresh rate
  for (size_t i = (int)RES_DESKTOP; i < CDisplaySettings::GetInstance().ResolutionInfoSize(); i++)
  {
    const RESOLUTION_INFO info = g_graphicsContext.GetResInfo((RESOLUTION)i);

    //discard resolutions that are not the same width and height (and interlaced/3D flags)
    //or have a too low refreshrate
    if ((!CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTRESOLUTION) &&
        (info.iScreenWidth != curr.iScreenWidth ||
         info.iScreenHeight != curr.iScreenHeight)) ||
        (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTRESOLUTION) &&
         CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_MINIMUMVERTICAL) > info.iScreenHeight))
      continue;

    if (info.iScreen != curr.iScreen ||
        ((int)CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTALLOWNONCEA) < GETFLAGS_GROUP(info.dwFlags)-1) ||
        (info.dwFlags & D3DPRESENTFLAG_MODEMASK) != (curr.dwFlags & D3DPRESENTFLAG_MODEMASK) ||
        info.fRefreshRate < (fRefreshRate * multiplier / 1.001) - 0.001)
    {
      // evaluate all higher modes and evalute them
      // concerning dimension and refreshrate weight
      // skip lower resolutions
      // don't change resolutions when 3D is wanted
      if ((width < orig.iScreenWidth) || // orig res large enough
         (info.iScreenWidth < orig.iScreenWidth) || // new res is smaller
         (info.iScreenHeight < orig.iScreenHeight) || // new height would be smaller
         ((int)CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_ADJUSTALLOWNONCEA) < GETFLAGS_GROUP(info.dwFlags)-1) ||
         (info.dwFlags & D3DPRESENTFLAG_MODEMASK) != (curr.dwFlags & D3DPRESENTFLAG_MODEMASK) || // don't switch to interlaced modes
         (info.iScreen != curr.iScreen) || // skip not current displays
         is3D) // skip res changing when doing 3D
      {
        continue;
      }
    }

    // Allow switching to larger resolution:
    // e.g. if m_sourceWidth == 3840 and we have a 3840 mode - use this one
    // if it has a matching fps mode, which is evaluated below

    loop_diff = std::abs(width - info.iScreenWidth);
    curr_diff = std::abs(width - curr.iScreenWidth);

    // For 3D choose the closest refresh rate
    if (is3D)
    {
      float diff = (info.fRefreshRate - fRefreshRate);
      if(diff < 0)
        diff *= -1.0f;

      if(diff < last_diff)
      {
        last_diff = diff;
        current = (RESOLUTION)i;
        curr = info;
      }
    }
    else
    {
      int c_weight = MathUtils::round_int(RefreshWeight(curr.fRefreshRate, fRefreshRate * multiplier) * 10000.0);
      int i_weight = MathUtils::round_int(RefreshWeight(info.fRefreshRate, fRefreshRate * multiplier) * 10000.0);

      RESOLUTION current_bak = current;
      RESOLUTION_INFO curr_bak = curr;

      // Closer the better, prefer higher refresh rate if the same
      if ((i_weight < c_weight) ||
          (i_weight == c_weight && info.fRefreshRate > curr.fRefreshRate))
      {
        current = (RESOLUTION)i;
        curr = info;
      }
      // use case 1080p50 vs 3840x2160@25 for 3840@25 content
      // prefer the higher resolution of 3840
      if (i_weight == c_weight && (loop_diff < curr_diff))
      {
        current = (RESOLUTION)i;
        curr = info;
      }
      // same as above but iterating with 3840@25 set and overwritten
      // by e.g. 1080@50 - restore backup in that case
      // to give priority to the better matching width
      if (i_weight == c_weight && (loop_diff > curr_diff))
      {
        current = current_bak;
        curr = curr_bak;
      }
    }
  }

  // For 3D overwrite weight
  if (is3D)
    weight = 0;
  else
    weight = RefreshWeight(curr.fRefreshRate, fRefreshRate * multiplier);

  return current;
}

//distance of refresh to the closest multiple of fps (multiple is 1 or higher), as a multiplier of fps
float CResolutionUtils::RefreshWeight(float refresh, float fps)
{
  float div   = refresh / fps;
  int   round = MathUtils::round_int(div);

  float weight = 0.0f;

  if (round < 1)
    weight = (fps - refresh) / fps;
  else
    weight = (float)fabs(div / round - 1.0);

  // punish higher refreshrates and prefer better matching
  // e.g. 30 fps content at 60 hz is better than
  // 30 fps at 120 hz - as we sometimes don't know if
  // the content is interlaced at the start, only
  // punish when refreshrate > 60 hz to not have to switch
  // twice for 30i content
  if (refresh > 60 && round > 1)
    weight += round / 10000.0;

  return weight;
}
