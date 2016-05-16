#include <Arduino.h>

typedef unsigned char uint8_t;

#define NUM_RINGS 8
#define LEDS_PER_RING 24

// bitwise packing macros for Life mode, so we're only using 1 bit per pixel...
#define getBit(u,y,x) (u[(y*LEDS_PER_RING+x)/8] & (1<<((y*LEDS_PER_RING+x)%8)))
#define setBit(u,y,x) (u[(y*LEDS_PER_RING+x)/8] |= (1<<((y*LEDS_PER_RING+x)%8)))
#define clearBit(u,y,x) (u[(y*LEDS_PER_RING+x)/8] &= ~(1<<((y*LEDS_PER_RING+x)%8)))

#define unpackBit(u, y, x, width) (u[(y*width+x)/8] & (1<<((y*width+x)%8)))

typedef void (*lightPixelFunc)(uint8_t, uint8_t, uint32_t); // y, x, callbackValue

class Life {
 public:
  Life();
  ~Life();
  
  void init();
  bool show(lightPixelFunc f, unsigned long v); // callback function and callback value
  uint8_t evolve();

  void addEntropy();

  uint8_t CRC8();

 private:
  uint8_t countNeighborsWithWraparound(uint8_t x,uint8_t y);
  void addGlider(uint8_t x, uint8_t y, uint8_t rotation);
  void addSwitch(uint8_t x, uint8_t y, uint8_t rotation);


  uint8_t universe[NUM_RINGS*LEDS_PER_RING/8]; // bits for display
  uint8_t newUniverse[NUM_RINGS*LEDS_PER_RING/8]; // bits for evolution
};
