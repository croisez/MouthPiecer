/**
 * LButton.h
 * @author Croisez LM
 * @version 1.0.0
 * @license LGPL
 */

#ifndef _LButton_h
#define _LButton_h

#include <Arduino.h>

//#define DEBUG_BUTTON_EVENTS
#define BUTTON_SCANNING_SPEED_MS 20
#define BUTTON_DBLCLICK_MAXDURATION 450
#define BUTTON_LONGPRESS_DURATION 30 * BUTTON_SCANNING_SPEED_MS

class LButton {
public:
  // Common functions.
  LButton(void);
  void Begin(uint8_t pin, int max_dblclick_duration_ms, bool pullup_enable = true, bool active_low = false);
  void Begin(uint8_t pin, int max_dblclick_duration_ms, int scanning_speed_ms, bool pullup_enable = true, bool active_low = false);
  void onClick(void (*cb)(void));     // Call a callback function when the button has been pressed and released.
  void onDblClick(void (*cb)(void));  // Call a callback function when the button has been pressed and released 2 times in a row
  //void onPressedFor(void (*cb)(void));
  void onPressedFor(void (*cb)(void), int duration);  // Call a callback function when the button has been held for at least the given number of milliseconds.
  void process_events(void);

protected:
  void (*_onClicked)(void);
  void (*_onDblClicked)(void);
  void (*_onPressedFor)(void);

  bool _active_low;     // Inverts button logic. If true, low = pressed else high = pressed.
  bool _current_state;  // Current button state, true = pressed.
  bool _last_state;     // Previous button state, true = pressed.
  byte btnCount;
  byte btnLongCnt;
  uint32_t lastProcessTimer;
  uint32_t lastFallingTimer;
  boolean isRising;
  boolean isFalling;
  uint8_t _pin;
  bool _pullup_enabled;	// Internal pullup resistor enabled.
  uint32_t _scanning_speed_ms;
  uint32_t _dblclick_max_duration;
  uint32_t _longpress_duration_ms;
  void ClickedEvent(void);
  void DblClickedEvent(void);
  void PressedForEvent(void);
  void RisingEvent(void);
  void FallingEvent(void);
  void IdleEvent(void);
  boolean get_button_state(void);
};

#endif
