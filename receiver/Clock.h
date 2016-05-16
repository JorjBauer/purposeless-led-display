#ifndef __CLOCK_H
#define __CLOCK_H

#include <Arduino.h>

class Clock {
 public:
  Clock();
  ~Clock();

  // returns the current hour and minute as (hour << 24) | (minute << 16) | (second)
  uint32_t currentTime();

  // set the current time (24-hour).
  void setTime(uint8_t hour, uint8_t minute, uint8_t second);

 private:
  // timeSetPoint is a relative value that, when added to millis(),
  // results in the number of millis since midnight.
  uint32_t timeSetPoint;
  
};

#endif
