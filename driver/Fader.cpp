#include "Fader.h"

// FIXME: brightness shift needs attention, probably


#define FADESTEPTIME 0
#define NUMPIXELS (strip->numPixels())

Fader::Fader(Adafruit_NeoPixel *s)
{
  strip = s;
  targetColor = (uint8_t*)malloc(NUMPIXELS);
  fadingFlags = (uint8_t*)calloc(NUMPIXELS/8 + 1, 1); // +1 for rounding error
  fadeInOnly = false;
  brightnessShift = 0;
}

Fader::~Fader()
{
  free(targetColor);
}

void Fader::reset()
{
  for (int i=0; i<NUMPIXELS; i++) {
    stopFading(i);
  }
//  fadeEverythingOut();
}

void Fader::fadeEverythingOut()
{
  for (int i=0; i<NUMPIXELS; i++) {
    setFadeTarget(i, 0, 0, 0);
  }
}

void Fader::setFadeTarget(uint8_t pixelNum, 
          uint8_t r, uint8_t g, uint8_t b)
{
  // Could be fading in or out.
  targetColor[pixelNum] = reduceColorTo8bit(r, g, b);
  startFading(pixelNum);
}

void Fader::setFadeTarget(uint8_t pixelNum, uint32_t c)
{
  uint8_t r, g, b;
  r = (c >> 16) & 0xFF;
  g = (c >>  8) & 0xFF;
  b = (c      ) & 0xFF;
  setFadeTarget(pixelNum, r, g, b);
}

void Fader::stopFading(uint8_t pixelNum)
{
  // Get the current pixel color
  uint32_t c = strip->getPixelColor(pixelNum);
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >>  8) & 0xFF;
  uint8_t b = (c      ) & 0xFF;

  // Reduce that to an 8-bit value, and set it as our target
  targetColor[pixelNum] = reduceColorTo8bit(r, g, b);
  // Set the strip's LED to match the target exactly
  strip->setPixelColor(pixelNum, expandColorFrom8bit(targetColor[pixelNum]));

  fadingFlags[pixelNum/8] &= ~(1 << (pixelNum % 8));
}

void Fader::startFading(uint8_t pixelNum)
{
  fadingFlags[pixelNum/8] |= (1 << (pixelNum % 8));
}

void Fader::setBrightnessShift(int8_t shift)
{
  // If the caller is limiting brightness, then we need to know what the significant bits are of each 
  // color value - b/c we're also reducing color to 3 bits per channel (max). This tells us how many bits
  // to shift down.

  brightnessShift = shift;
}


void Fader::setFadeMode(bool fadeInOnly)
{
  fadeInOnly = fadeInOnly;
}

bool Fader::performFadeForOnePixel(uint8_t pixelNum)
{
  // What color is the pixel right now?
  uint32_t c = strip->getPixelColor(pixelNum);
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >>  8) & 0xFF;
  uint8_t b = (c      ) & 0xFF;

  // Find the target color
  uint32_t target = expandColorFrom8bit(targetColor[pixelNum]);

  // separate the target component values
  uint8_t tr, tg, tb;
  tr = (target >> 16) & 0xFF;
  tg = (target >> 8 ) & 0xFF;
  tb = (target      ) & 0xFF;

  if (r > tr) {
    r--;
  } else if (r < tr) {
    r++;
  }
  if (b > tb) {
    b--;
  } else if (b < tb) {
    b++;
  }
  if (g > tg) {
    g--;
  } else if (g < tg) {
    g++;
  }

  // Set the pixelData to the new value we want
  strip->setPixelColor(pixelNum, r, g, b);
}

// One step in the fade action, to be called regularly. 
// returns true if it updates any LEDs.
bool Fader::performFade()
{
  bool retval = false;
  numExtinguishedLastFade = 0;
  static unsigned long nextMillis = 0;

  if (millis() >= nextMillis) {
    for (uint8_t idx = 0; idx < NUMPIXELS; idx++) {
      if (isFading(idx)) {
	retval = true;
	performFadeForOnePixel(idx);
	
	uint32_t c = strip->getPixelColor(idx);
	uint32_t t = expandColorFrom8bit(targetColor[idx]);
	if (c == t) {
	  // We reached our target!
	  if (targetColor[idx] == 0) {
	    stopFading(idx);
	    numExtinguishedLastFade++;
	  } else {
	    // And it's not black, so we finished fading in
	    if (fadeInOnly) {
	      stopFading(idx);
	      numExtinguishedLastFade++; // where "Extinguished" seems to loosely mean "stoped fading"
	    } else {
	      // But we want to go back to black
	      setFadeTarget(idx, 0, 0, 0);
	    }
	  }
	}
      }
    }

    nextMillis = millis() + FADESTEPTIME;
  }
  return retval;
}

uint8_t Fader::howManyWentOut()
{
  return numExtinguishedLastFade;
}

/* Private */

bool Fader::isFading(uint8_t pixelNum)
{
  return (fadingFlags[pixelNum/8] & (1 << (pixelNum % 8)));
}


uint8_t Fader::reduceColorTo8bit(uint8_t r, uint8_t g, uint8_t b)
{
  // Reduce this to a 2-bit R/G/B
  // NOTE: could make it 3/3/2 R/G/B with a loss in the relative precision 
  // between the color channels; not sure if that would be palatable though.
  return (
    ((r >> (2-brightnessShift)) & 0x30) |
    ((g >> (4-brightnessShift)) & 0x0C) |
    ((b >> (6-brightnessShift)) & 0x03)
    );
}

uint32_t Fader::expandColorFrom8bit(uint8_t c)
{
  // Losing precision, return a color to a 32-bit form
  uint32_t r, g, b;

  r = (c & 0x30) << (2-brightnessShift);
  g = (c & 0x0C) << (4-brightnessShift);
  b = (c & 0x03) << (6-brightnessShift);

  return (r << 16) | (g << 8) | (b);
}
