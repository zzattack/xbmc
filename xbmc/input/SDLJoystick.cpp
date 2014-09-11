/*
 *      Copyright (C) 2007-2013 Team XBMC
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

#include "system.h"
#include "SDLJoystick.h"
#include "ButtonTranslator.h"
#include "peripherals/devices/PeripheralImon.h"
#include "settings/AdvancedSettings.h"
#include "settings/lib/Setting.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#include <math.h>

#ifdef HAS_SDL_JOYSTICK
#include <SDL2/SDL.h>

using namespace std;

CJoystick::CJoystick()
{
  m_joystickEnabled = false;
  SetDeadzone(0);
  for (int i = 0; i < MAX_AXES; i++)
    m_RestState[i] = m_Amount[i] = 0;
  Reset();
}

void CJoystick::Reset()
{
  m_AxisIdx = -1;
  m_ButtonIdx = -1;
  m_HatIdx = -1;
  m_HatState = SDL_HAT_CENTERED;
  for (int i = 0; i < MAX_AXES; i++)
    m_Amount[i] = m_RestState[0];
}

void CJoystick::OnSettingChanged(const CSetting* setting)
{
  if (setting == NULL)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == "input.enablejoystick")
    SetEnabled(((CSettingBool*)setting)->GetValue() && PERIPHERALS::CPeripheralImon::GetCountOfImonsConflictWithDInput() == 0);
}

void CJoystick::Initialize()
{
  if (!IsEnabled())
    return;

  if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
  {
    CLog::Log(LOGERROR, "(Re)start joystick subsystem failed : %s",SDL_GetError());
    return;
  }

  Reset();

  // Set deadzone range
  SetDeadzone(g_advancedSettings.m_controllerDeadzone);

  // load joystick names and open all connected joysticks
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    AddJoystick(i);

  // disable joystick events, since we'll be polling them
  // SDL_JoystickEventState(SDL_DISABLE);
}

void CJoystick::AddJoystick(int id)
{
  SDL_Joystick* joy = SDL_JoystickOpen(id);
  if (!joy) 
    return;
  
  std::string joyName = std::string(SDL_JoystickName(joy));
#if defined(TARGET_DARWIN)
  // On OS X, the 360 controllers are handled externally, since the SDL code is
  // really buggy and doesn't handle disconnects.
  //
  if (joyName.find("360") != std::string::npos)
  {
    CLog::Log(LOGNOTICE, "Ignoring joystick: %s", joyName);
    continue;
  }  
#endif

  // Some (Microsoft) Keyboards are recognized as Joysticks by modern kernels
  // Don't enumerate them
  // https://bugs.launchpad.net/ubuntu/+source/linux/+bug/390959
  // NOTICE: Enabled Joystick: Microsoft Wired Keyboard 600
  // Details: Total Axis: 37 Total Hats: 0 Total Buttons: 57
  // NOTICE: Enabled Joystick: Microsoft Microsoft® 2.4GHz Transceiver v6.0
  // Details: Total Axis: 37 Total Hats: 0 Total Buttons: 57
  // also checks if we have at least 1 button, fixes
  // NOTICE: Enabled Joystick: ST LIS3LV02DL Accelerometer
  // Details: Total Axis: 3 Total Hats: 0 Total Buttons: 0
  int num_axis = SDL_JoystickNumAxes(joy);
  int num_buttons = SDL_JoystickNumButtons(joy);
  if ((num_axis > 20 && num_buttons > 50) || num_buttons == 0)
    CLog::Log(LOGNOTICE, "Ignoring Joystick %s Axis: %d Buttons: %d: invalid device properties",
      joyName.c_str(), num_axis, num_buttons);
  else
    AddJoystick(joy);
}

void CJoystick::AddJoystick(SDL_Joystick* joy)
{
  // get details
  std::string joyName = std::string(SDL_JoystickName(joy));
  int num_axis = SDL_JoystickNumAxes(joy);
  int num_buttons = SDL_JoystickNumButtons(joy);
  int num_hats = SDL_JoystickNumHats(joy);
  CLog::Log(LOGNOTICE, "Enabled Joystick: %s", joyName.c_str());
  CLog::Log(LOGNOTICE, "Details: Total Axis: %d Total Hats: %d Total Buttons: %d",
    num_axis, num_hats, num_buttons);
  
  // store joystick and its instance id for lookups
  m_Joysticks.push_back(joy);
  int id = SDL_JoystickInstanceID(joy);
  m_JoystickIds[id] = joy;

  // initialize rest states for axes that the keymap identified as triggers to -32768
  std::map<std::string, std::vector<int> >::const_iterator triggerAxes = m_TriggerMap.find(joyName);
  if (triggerAxes != m_TriggerMap.end())
  {
    for (std::vector<int>::const_iterator it = triggerAxes->second.begin(); it != triggerAxes->second.end(); ++it)
    {
      int axis = MapAxis(joy, *it);
      if (axis < MAX_AXES)
      {
        m_RestState[axis] = -32768;
        m_IgnoreAxis[axis] = true;
      }
    }
  }  
}

void CJoystick::RemoveJoystick(int id)
{
  CLog::Log(LOGDEBUG, "Joystick %d removed", id);
  
  // invalidate our current values
  SDL_Joystick* joy = m_JoystickIds[id];
  if (m_HatIdx >= MapHat(joy, 0)) m_HatIdx -= SDL_JoystickNumHats(joy);
  if (m_AxisIdx >= MapAxis(joy, 0)) m_AxisIdx -= SDL_JoystickNumAxes(joy);
  if (m_ButtonIdx >= MapButton(joy, 0)) m_ButtonIdx -= SDL_JoystickNumButtons(joy);

  SDL_JoystickClose(joy);
  m_Joysticks.erase(std::remove(m_Joysticks.begin(), m_Joysticks.end(), joy), m_Joysticks.end());
  m_JoystickIds.erase(id);
}

void CJoystick::Update()
{
  if (!IsEnabled())
    return;

  // update the state of all opened joysticks
  SDL_JoystickUpdate();
  SDL_Joystick* joy;
  int axisNum;
  int buttonIdx = -1, buttonNum;
  int hatIdx = -1, hatNum;

  // go through all joysticks
  for (size_t j = 0; j < m_Joysticks.size(); j++)
  {
    joy = m_Joysticks[j];
    int axisval, axisIdx;
    uint8_t hatval;

    for (int b = 0; b < SDL_JoystickNumButtons(joy); b++)
    {
      if (SDL_JoystickGetButton(joy, b))
      {
        buttonIdx = MapButton(joy, b);
        j = m_Joysticks.size();
        break;
      }
    }

    for (int h = 0; h < SDL_JoystickNumHats(joy); h++)
    {
      hatval = SDL_JoystickGetHat(joy, h);
      if (hatval != SDL_HAT_CENTERED)
      {
        hatIdx = MapHat(joy, h);
        m_HatState = hatval;
        j = m_Joysticks.size();
        break;
      }
    }

    // get axis states
    for (int a = 0; a < SDL_JoystickNumAxes(joy); a++)
    {
      axisval = SDL_JoystickGetAxis(joy, a);
      axisIdx = MapAxis(joy, a);
      m_Amount[axisIdx] = axisval;  //[-32768 to 32767]
      m_IgnoreAxis[axisIdx] &= axisval == 0; // sdl bug workaround
    }
  }

  m_AxisIdx = GetAxisWithMaxAmount();
  if (m_AxisIdx >= 0)
  {
    MapAxis(m_AxisIdx, joy, axisNum);
    CLog::Log(LOGDEBUG, "Joystick %d Axis %d Amount %d", JoystickIndex(joy), axisNum + 1, m_Amount[m_AxisIdx]);
  }

  if (hatIdx == -1 && m_HatIdx != -1)
  {
    // now centered
    MapHat(m_HatIdx, joy, hatNum);
    CLog::Log(LOGDEBUG, "Joystick %d hat %u hat centered", JoystickIndex(joy), hatNum + 1);
    m_pressTicksHat = 0;
  }
  else if (hatIdx != m_HatIdx)
  {
    MapHat(hatIdx, joy, hatNum);
    CLog::Log(LOGDEBUG, "Joystick %d hat %u value %d", JoystickIndex(joy), hatNum + 1, m_HatState);
    m_pressTicksHat = SDL_GetTicks();
  }
  m_HatIdx = hatIdx;

  if (buttonIdx == -1 && m_ButtonIdx != -1)
  {
    MapButton(m_ButtonIdx, joy, buttonNum);
    CLog::Log(LOGDEBUG, "Joystick %d button %d Up", -1, buttonNum + 1);
    m_pressTicksButton = 0;
    m_ButtonIdx = -1;
  }
  else if (buttonIdx != m_ButtonIdx)
  {
    MapButton(buttonIdx, joy, buttonNum);
    CLog::Log(LOGDEBUG, "Joystick %d button %d Down", JoystickIndex(joy), buttonNum + 1);
    m_pressTicksButton = SDL_GetTicks();
  }
  m_ButtonIdx = buttonIdx;
}

void CJoystick::Update(SDL_Event& joyEvent)
{
  if (!IsEnabled())
    return;

  SDL_Joystick* joy;
  int axisIdx;

  switch(joyEvent.type)
  {
  /*
  case SDL_JOYBUTTONDOWN:
    joy = m_Joysticks[joyEvent.jbutton.which];
    m_ButtonIdx = MapButton(joy, joyEvent.jbutton.button);
    m_pressTicksButton = SDL_GetTicks();
    CLog::Log(LOGDEBUG, "Joystick %d button %d Down", joyEvent.jbutton.which, joyEvent.jbutton.button + 1);
    break;

  case SDL_JOYAXISMOTION:
    joy = m_Joysticks[joyEvent.jaxis.which];
    axisIdx = MapAxis(joy, joyEvent.jaxis.axis);
    if (axisIdx >= 0)
    {
      m_Amount[axisIdx] = joyEvent.jaxis.value; //[-32768 to 32767]
      m_IgnoreAxis[axisIdx] &= joyEvent.jaxis.value == 0;
      m_AxisIdx = GetAxisWithMaxAmount();
      if (m_AxisIdx >= 0)
      {
        MapAxis(m_AxisIdx, joy, axisIdx);
        CLog::Log(LOGDEBUG, "Joystick %d Axis %d Amount %d", JoystickIndex(joy), joyEvent.jaxis.axis + 1, m_Amount[m_AxisIdx]);
      }
    }
    break;

  case SDL_JOYHATMOTION:
    joy = m_Joysticks[joyEvent.jhat.which];
    m_HatIdx = MapHat(joy, joyEvent.jhat.hat);
    m_HatState = joyEvent.jhat.value;
    m_pressTicksHat = SDL_GetTicks();
    CLog::Log(LOGDEBUG, "Joystick %d Hat %d Down with position %d", joyEvent.jhat.which, joyEvent.jhat.hat + 1, joyEvent.jhat.value);
    break;

  case SDL_JOYBUTTONUP:
    m_pressTicksButton = 0;
    m_ButtonIdx = -1;
    CLog::Log(LOGDEBUG, "Joystick %d button %d Up", joyEvent.jbutton.which, joyEvent.jbutton.button + 1);
  */
  case SDL_JOYDEVICEADDED:
    AddJoystick(joyEvent.jdevice.which);
    break;

  case SDL_JOYDEVICEREMOVED:
    RemoveJoystick(joyEvent.jdevice.which);
    break;

  default:
    break;
  }
}

bool CJoystick::GetHat(std::string& joyName, int& id, int& position, bool consider_repeat)
{
  if (!IsEnabled() || !IsHatActive())
    return false;

  SDL_Joystick* joy;
  MapHat(m_HatIdx, joy, id);
  joyName = std::string(SDL_JoystickName(joy));
  position = m_HatState;

  if (!consider_repeat)
    return true;

  static uint32_t lastPressTicksHat = 0;
  // allow it if it is the first press
  if (lastPressTicksHat < m_pressTicksHat)
  {
    lastPressTicksHat = m_pressTicksHat;
    return true;
  }
  uint32_t nowTicks = SDL_GetTicks();
  if (nowTicks - m_pressTicksHat < 500) // 500ms delay before we repeat
    return false;
  if (nowTicks - lastPressTicksHat < 100) // 100ms delay before successive repeats
    return false;
  lastPressTicksHat = nowTicks;

  return true;
}

bool CJoystick::GetButton(std::string& joyName, int& id, bool consider_repeat)
{
  if (!IsEnabled() || !IsButtonActive())
    return false;

  SDL_Joystick* joy;
  MapButton(m_ButtonIdx, joy, id);
  joyName = SDL_JoystickName(joy);

  if (!consider_repeat)
    return true;

  static uint32_t lastPressTicksButton = 0;
  // allow it if it is the first press
  if (lastPressTicksButton < m_pressTicksButton)
  {
    lastPressTicksButton = m_pressTicksButton;
    return true;
  }
  uint32_t nowTicks = SDL_GetTicks();
  if (nowTicks - m_pressTicksButton < 500) // 500ms delay before we repeat
    return false;
  if (nowTicks - lastPressTicksButton < 100) // 100ms delay before successive repeats
    return false;

  lastPressTicksButton = nowTicks;
  return true;
}

bool CJoystick::GetAxis(std::string& joyName, int& id) const
{
  if (!IsEnabled() || !IsAxisActive())
    return false;

  SDL_Joystick* joy;
  MapAxis(m_AxisIdx, joy, id);
  joyName = std::string(SDL_JoystickName(joy));

  return true;
}

int CJoystick::GetAxisWithMaxAmount() const
{
  int maxAmount= 0, axis = -1;
  for (int i = 0; i < MAX_AXES; i++)
  {
    int tempf = abs(m_Amount[i] - m_RestState[i]);
    int deadzone = m_RestState[i] == 0 ? m_DeadzoneRange : 0;
    if (tempf > deadzone && tempf > maxAmount && !m_IgnoreAxis[i])
    {
      maxAmount = tempf;
      axis = i;
    }
  }
  return axis;
}

float CJoystick::GetAmount(std::string& joyName, int axisNum) const
{
  int joyIdx = JoystickIndex(joyName);
  if (joyIdx == -1)
    return 0;

  int axisIdx = MapAxis(m_Joysticks[joyIdx], axisNum);
  if (m_IgnoreAxis[axisIdx])
    return 0;

  int amount = m_Amount[axisIdx] - m_RestState[axisIdx];
  int range = MAX_AXISAMOUNT - m_RestState[axisIdx];
  // deadzones on axes are undesirable
  int deadzone = m_RestState[axisIdx] == 0 ? m_DeadzoneRange : 0;
  if (amount > deadzone)
    return (float)(amount - deadzone) / (float)(range - deadzone);
  else if (amount < -deadzone)
    return (float)(amount + deadzone) / (float)(range - deadzone);

  return 0;
}

void CJoystick::SetEnabled(bool enabled /*=true*/)
{
  if (enabled && !m_joystickEnabled)
  {
    m_joystickEnabled = true;
    Initialize();
  }
  else if (!enabled && m_joystickEnabled)
  {
    ReleaseJoysticks();
    m_joystickEnabled = false;
  }
}

float CJoystick::SetDeadzone(float val)
{
  if (val < 0) val = 0;
  if (val > 1) val = 1;
  m_DeadzoneRange = (int)(val * MAX_AXISAMOUNT);
  return val;
}

bool CJoystick::ReleaseJoysticks()
{
  // close open joysticks
  for (std::vector<SDL_Joystick*>::iterator it = m_Joysticks.begin(); it != m_Joysticks.end(); ++it)
    SDL_JoystickClose(*it);
  m_Joysticks.clear();
  Reset();
  
  // Restart SDL joystick subsystem
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
  if (SDL_WasInit(SDL_INIT_JOYSTICK) != 0)
  {
    CLog::Log(LOGERROR, "Stop joystick subsystem failed");
    return false;
  }
  return true;
}

bool CJoystick::Reinitialize()
{
  if (!ReleaseJoysticks()) return false;
  Initialize();
  return true;
}

void CJoystick::LoadTriggerMap(const std::map<std::string, std::vector<int> >& triggerMap)
{
  m_TriggerMap.clear();
  m_TriggerMap.insert(triggerMap.begin(), triggerMap.end());
}

int CJoystick::JoystickIndex(std::string& joyName) const
{
  for (int i = 0; i < SDL_NumJoysticks(); ++i)
  {
    if (joyName == std::string(SDL_JoystickName(m_Joysticks[i])))
      return i;
  }
  return -1;
}

int CJoystick::JoystickIndex(SDL_Joystick* joy) const
{
  for (size_t i = 0; i < m_Joysticks.size(); ++i)
  {
    if (joy == m_Joysticks[i])
      return i;
  }
  return -1;
}
  
int CJoystick::MapAxis(SDL_Joystick* joy, int axisNum) const
{
  int idx = 0;
  std::vector<SDL_Joystick*>::const_iterator it;
  for (it = m_Joysticks.begin(); it < m_Joysticks.end() && *it != joy; ++it)
    idx += SDL_JoystickNumAxes(*it);
  if (it == m_Joysticks.end())
    return -1;
  else
    return idx + axisNum;
}

void CJoystick::MapAxis(int axisIdx, SDL_Joystick*& joy, int& axisNum) const
{
  int axisCount = 0;
  std::vector<SDL_Joystick*>::const_iterator it;
  for (it = m_Joysticks.begin(); it < m_Joysticks.end() && axisCount + SDL_JoystickNumAxes(*it) <= axisIdx; ++it)
    axisCount += SDL_JoystickNumAxes(*it);
  if (it != m_Joysticks.end())
  {
    joy = *it;
    axisNum = axisIdx - axisCount;
  }
  else
  {
    joy = NULL;
    axisNum = -1;
  }
}

int CJoystick::MapButton(SDL_Joystick* joy, int buttonNum) const
{
  int idx = 0;
  std::vector<SDL_Joystick*>::const_iterator it;
  for (it = m_Joysticks.begin(); it < m_Joysticks.end() && *it != joy; ++it)
    idx += SDL_JoystickNumButtons(*it);
  if (it == m_Joysticks.end())
    return -1;
  else
    return idx + buttonNum;
}

void CJoystick::MapButton(int buttonIdx, SDL_Joystick*& joy, int& buttonNum) const
{
  int buttonCount = 0;
  std::vector<SDL_Joystick*>::const_iterator it;
  for (it = m_Joysticks.begin(); it < m_Joysticks.end() && buttonCount + SDL_JoystickNumButtons(*it) <= buttonIdx; ++it)
    buttonCount += SDL_JoystickNumButtons(*it);
  if (it != m_Joysticks.end())
  {
    joy = *it;
    buttonNum = buttonIdx - buttonCount;
  }
  else
  {
    joy = NULL;
    buttonNum = -1;
  }
}

int CJoystick::MapHat(SDL_Joystick* joy, int hatNum) const
{
  int idx = 0;
  std::vector<SDL_Joystick*>::const_iterator it;
  for (it = m_Joysticks.begin(); it < m_Joysticks.end() && *it != joy; ++it)
    idx += SDL_JoystickNumHats(*it);
  if (it == m_Joysticks.end())
    return -1;
  else
    return idx + hatNum;
}

void CJoystick::MapHat(int hatIdx, SDL_Joystick*& joy, int& hatNum) const
{
  int hatCount = 0;
  std::vector<SDL_Joystick*>::const_iterator it;
  for (it = m_Joysticks.begin(); it < m_Joysticks.end() && hatCount + SDL_JoystickNumHats(*it) <= hatIdx; ++it)
    hatCount += SDL_JoystickNumHats(*it);
  if (it != m_Joysticks.end())
  {
    joy = *it;
    hatNum = hatIdx - hatCount;
  }
  else
  {
    joy = NULL;
    hatNum = -1;
  }
}

#endif
