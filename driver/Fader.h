#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

/*
 * This pixel-fading class is designed to use relatively little memory, at 
 * the expense of CPU time. It is bound to the size of a byte, so cannot 
 * handle more than 256 LEDs as coded (cf. numPixels).
 *
 * We have sacrificed the fadeTime and have to accept a fixed stepwise 
 * increment based on targetColor; we have sacrificed precision of the target
 * color; we have sacrificed CPU time to calculate the bitwise indexes; and 
 * we have sacrificed in the direction code size and complexity. But for a 
 * device that only has 1500 bytes of RAM, this buys us a significant chunk 
 * of RAM.
 *
 */

class Fader {

 public:
  Fader(Adafruit_NeoPixel *s);
  ~Fader();
  void reset();

  void fadeEverythingOut();

  void setFadeTarget(uint8_t pixelNum, uint8_t r, uint8_t g, uint8_t b);
  void setFadeTarget(uint8_t pixelNum, uint32_t c);

  void stopFading(uint8_t pixelNum);
  void startFading(uint8_t pixelNum);

  void setBrightnessShift(int8_t shift);

  void setFadeMode(bool fadeInOnly);
  void setFadeTime(uint16_t t);
  void setFadeSteps(uint8_t s);

  bool performFadeForOnePixel(uint8_t pixelNum);
  bool performFade();

  uint8_t howManyWentOut();

  bool isFading(uint8_t pixelNum);

 protected:
  uint8_t reduceColorTo8bit(uint8_t r, uint8_t g, uint8_t b);
  uint32_t expandColorFrom8bit(uint8_t c);


 private:
  // Private copy of strip, which holds data about the number of pixels...
  Adafruit_NeoPixel *strip;

  //   What is the target brightest value of this fade? - an array of TOTAL_LEDS
  //      (2 bits per R/G/B)
  uint8_t *targetColor;

  // bitwise flags for each pixel: is it fading in/out at all?
  uint8_t *fadingFlags; 

  uint8_t numExtinguishedLastFade;
  bool fadeInOnly;

  int8_t brightnessShift;
};
