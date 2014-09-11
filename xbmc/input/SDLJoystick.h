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

#ifndef SDL_JOYSTICK_H
#define SDL_JOYSTICK_H

#include "system.h" // for HAS_SDL_JOYSTICK
#include "settings/lib/ISettingCallback.h"
#include <vector>
#include <string>
#include <map>

#define JACTIVE_BUTTON 0x00000001
#define JACTIVE_AXIS   0x00000002
#define JACTIVE_HAT    0x00000004
#define JACTIVE_NONE   0x00000000
#define JACTIVE_HAT_UP    0x01
#define JACTIVE_HAT_RIGHT 0x02
#define JACTIVE_HAT_DOWN  0x04
#define JACTIVE_HAT_LEFT  0x08

#ifdef HAS_SDL_JOYSTICK

#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_events.h>

#define MAX_AXES 64
#define MAX_AXISAMOUNT 32768


// Class to manage all connected joysticks
// Note: 'index' always refers to indices specific to this class,
//       whereas 'ids' always refer to SDL instance id's

class CJoystick : public ISettingCallback
{
public:
  CJoystick();

  virtual void OnSettingChanged(const CSetting *setting);

  void Initialize();
  void Reset();
  void ResetAxis(int axisIdx) { m_Amount[axisIdx] = m_RestState[axisIdx]; }
  void Update();
  void Update(SDL_Event& joyEvent);
  bool GetAxis(std::string& joyName, int& id) const;
  bool GetButton(std::string& joyName, int& id, bool consider_repeat = true);
  bool GetHat(std::string& joyName, int& id, int& position, bool consider_repeat = true);
  float GetAmount(std::string& joyName, int axisNum) const;
  bool IsEnabled() const { return m_joystickEnabled; }
  void SetEnabled(bool enabled = true);
  float SetDeadzone(float val);
  bool Reinitialize();
  void LoadTriggerMap(const std::map<std::string, std::vector<int> >& triggerMap);

private:
  bool IsButtonActive() const { return m_ButtonIdx != -1; }
  bool IsAxisActive() const { return m_AxisIdx != -1; }
  bool IsHatActive() const { return m_HatIdx != -1; }

  void AddJoystick(int id);
  void AddJoystick(SDL_Joystick* joy);
  void RemoveJoystick(int id);
  bool ReleaseJoysticks();

  int GetAxisWithMaxAmount() const;
  int JoystickIndex(std::string& joyName) const;
  int JoystickIndex(SDL_Joystick* joy) const;
  int MapAxis(SDL_Joystick* joy, int axisNum) const ; // <joy, axis> --> axisIdx
  void MapAxis(int axisIdx, SDL_Joystick*& joy, int& axisNum) const; // axisIdx --> <joy, axis>
  int MapButton(SDL_Joystick* joy, int buttonNum) const; // <joy, button> --> buttonIdx
  void MapButton(int buttonIdx, SDL_Joystick*& joy, int& buttonNum) const; // buttonIdx --> <joy, button>
  int MapHat(SDL_Joystick* joy, int hatNum) const; // <joy, hat> --> hatIdx
  void MapHat(int hatIdx, SDL_Joystick*& joy, int& hatNum) const; // hatIdx --> <joy, hat>

  int m_Amount[MAX_AXES];
  int m_RestState[MAX_AXES]; // axis value in rest state (0 for sticks, -32768 for triggers)
  bool m_IgnoreAxis[MAX_AXES]; // used to ignore triggers until SDL no longer reports 0
  int m_AxisIdx; // active axis
  int m_ButtonIdx; // active button
  uint8_t m_HatState; // state of active hat
  int m_HatIdx; // active hat

  int m_DeadzoneRange;
  bool m_joystickEnabled;
  uint32_t m_pressTicksButton;
  uint32_t m_pressTicksHat;
  std::vector<SDL_Joystick*> m_Joysticks;
  std::map<int, SDL_Joystick*> m_JoystickIds;
  std::map<std::string, std::vector<int> > m_TriggerMap;
};

extern CJoystick g_Joystick;

#endif

#endif
