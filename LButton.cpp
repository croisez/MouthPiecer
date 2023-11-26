#include <stdint.h>
/**
 * LButton.cpp
 * @author Croisez LM
 * @version 1.0.0
 * @license LGPL
 */

#include "LButton.h"

LButton::LButton(void){};

void LButton::init(uint8_t pin, int dblclick_max_duration_ms) {
  LButton::init(pin, dblclick_max_duration_ms, BUTTON_SCANNING_SPEED_MS);
}

void LButton::init(uint8_t pin, int dblclick_max_duration_ms, int scanning_speed_ms) {
  _scanning_speed_ms = scanning_speed_ms;
  _dblclick_max_duration = dblclick_max_duration_ms;
  isFalling = false;
  isRising = false;
  lastFallingTimer = 0;
  btnLongCnt = 0;
  btnCount = 0;
  _last_state = false;
  _current_state = false;
  _active_low = false;
  _pin = pin;
}

void LButton::onClick(void (*cb)(void) = NULL) {
  _onClicked = cb;
}

void LButton::onDblClick(void (*cb)(void) = NULL) {
  _onDblClicked = cb;
}

/*void LButton::onPressedFor(void (*cb)(void) = NULL) {
  LButton::onPressedFor((void (*)(void))cb, BUTTON_LONGPRESS_DURATION);
}*/

void LButton::onPressedFor(void (*cb)(void) = NULL, int duration = BUTTON_LONGPRESS_DURATION) {
  _longpress_duration_ms = duration;
  _onPressedFor = cb;
}

void LButton::ClickedEvent(void) {
  if (_onClicked != NULL) _onClicked();
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("BTNCLK");
#endif
}

void LButton::DblClickedEvent(void) {
  if (_onDblClicked != NULL) _onDblClicked();
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("BTNDBLCLK");
#endif
}

void LButton::PressedForEvent(void) {
  if (_onPressedFor != NULL) _onPressedFor();
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("BTNLONG");
#endif
}

void LButton::RisingEvent(void) {
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("RISING");
#endif
  if (btnCount == 0) {
    lastFallingTimer = millis();
  }
}

void LButton::FallingEvent(void) {
  //Serial.print(btnCount); Serial.print(", "); Serial.print(millis() - lastFallingTimer);
  if (btnCount == 2) {
    if (millis() - lastFallingTimer < _dblclick_max_duration) {
      DblClickedEvent();
    }
    btnCount = 0;
  }
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("FALLING");
#endif
}

void LButton::IdleEvent(void) {
  if (btnCount == 1 && millis() - lastFallingTimer > _dblclick_max_duration) {
    ClickedEvent();
    btnCount = 0;
  }
}

bool LButton::get_button_state() {
  boolean bs = (!digitalRead(_pin));
  if (bs) {
    btnLongCnt += 1;
    //Serial.println(btnLongCnt);
  }
  return bs;
}

void LButton::process_events() {
  if (millis() - lastProcessTimer > _scanning_speed_ms) {  //Handles button rising edge event
    if (_last_state == false && _current_state == true) {
      isRising = true;
      isFalling = false;
      LButton::RisingEvent();
    } else if (_last_state == true && _current_state == false)  //Handles button falling edge event
    {
      if (btnLongCnt >= (_longpress_duration_ms / _scanning_speed_ms) + 1) {
        _last_state = false;
        _current_state = false;
      } else {
        btnCount += 1;
        isRising = false;
        isFalling = true;
        LButton::FallingEvent();
      }
      btnLongCnt = 0;
    } else {
      isRising = false;
      isFalling = false;
      LButton::IdleEvent();
    }

    if (btnLongCnt == _longpress_duration_ms / _scanning_speed_ms) {  //Handles button long-press event
      btnLongCnt += 1;
      LButton::PressedForEvent();
    }

    lastProcessTimer = millis();
    _last_state = _current_state;
    _current_state = LButton::get_button_state();
  }
}
