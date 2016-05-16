#include "Clock.h"

/* This is a simple clock object, handling time (not date).
 * 
 * 
 * This takes advantage of the Arduino platform's use of millis(), updated via 
 * interrupts. When you call setTime() this figures out the offset millis() needs
 * to calculate from midnight; and any time that it rolls over past 24 hours, it 
 * readjusts that offset. No additional interrupts or timers need be put in place;
 * requests for the current time are just a matter of simple math.
 * 
 * Assuming that nothing ever disables interrupts, this will only drift based on 
 * the temperature drift of the oscillator in this specific Atmel chip. If any code 
 * in the project ever disables interrupts, then millis() will lose ticks, and this 
 * code will drift further.
 *
 * Given that the clock won't be exact anyway, that's probably not a big deal.
 *
 * (c) 2016 Jorj Bauer <jorj@jorj.org>
 */

Clock::Clock()
{
  timeSetPoint = 0;
}

Clock::~Clock()
{
}


// returns the current hour and minute as (hour << 24) | (minute<<16) | (second)
uint32_t Clock::currentTime()
{
  uint32_t secondsSinceMidnight = (millis() - timeSetPoint) / 1000L;

  uint8_t hours   = secondsSinceMidnight / 3600L;
  uint8_t minutes = (secondsSinceMidnight % 3600L) / 60L;
  uint8_t seconds = (secondsSinceMidnight % 60L);

  while (hours >= 24) {
    timeSetPoint += (24L * 60L * 60L * 1000L); // advance the time set point one day
    hours -= 24;
  }

  return ((uint32_t)hours << 24) | ((uint32_t)minutes << 16) | (seconds);
  
}

// set the current time (24-hour).
void Clock::setTime(uint8_t hour, uint8_t minute, uint8_t second)
{
  uint32_t offset = 
    (uint32_t)hour * 60L * 60L * 1000L +
    (uint32_t) minute * 60L * 1000L +
    (uint32_t) second * 1000L;

  timeSetPoint = millis() - offset; // Set our set point to a point that's some number of milliseconds in the past, based on millis().
}
