#include "Life.h"

static int random(int start, int endPlusOne)
{
  float ret = (float)rand() / (float)RAND_MAX;
  ret *= (endPlusOne - start);
  ret += start;
  return ret;
}

Life::Life()
{
  init();
}

Life::~Life()
{
}

void Life::init()
{
  for (char x=0; x<LEDS_PER_RING; x++) {
    for (char y=0; y<NUM_RINGS; y++) {
      if (random(0,10) >= 8) {
	setBit(universe, y, x);
      } else {
        clearBit(universe, y, x);
      }
    }
  }
}

bool Life::show(lightPixelFunc f, unsigned long v)
{
  bool changed = false;
  for (char y=0; y<NUM_RINGS; y++) {
    for (char x=0; x<LEDS_PER_RING; x++) {
      if (getBit(universe, y, x)) {
	f(y, x, v);
	changed = true;
      }
    }
  }

  return changed;
}

uint8_t Life::evolve()
{
  uint8_t changecount = 0;

  
  for (char y=0; y<NUM_RINGS; y++) {
    for (char x=0; x<LEDS_PER_RING; x++) {
      
      // Count the neighbors
      int n = countNeighborsWithWraparound(x,y);
      
      // implement the rules
      if (getBit(universe, y, x)) {
	// Any live cell with < 2 live neighbors dies.
	// Any live cell with 2-3 neighbors is good.
	// Any live cell with > 3 neighbors dies.
	if (n == 2 || n == 3) {
	  setBit(newUniverse, y, x);
	} else {
	  clearBit(newUniverse, y, x);
	}
      }
      // Any dead cell with == 3 neighbors becomes live.
      else if (n == 3) {
	setBit(newUniverse, y, x);
      } else {
	// (else it stays dead)
	clearBit(newUniverse, y, x);
      }
    }
  }
  
  // Put the new universe in place, counting the number
  // of changes
  for (char y=0; y<NUM_RINGS; y++) {
    for (char x=0; x<LEDS_PER_RING; x++) {
      uint8_t oldState = getBit(universe, y, x);
      if (getBit(newUniverse, y, x)) {
	if (!oldState)
	  changecount++;
        setBit(universe, y, x);
      } else {
	if (oldState)
	  changecount++;
        clearBit(universe, y, x);
      }
    }
  }
  
  return changecount;
}

void Life::addGlider(uint8_t x, uint8_t y, uint8_t rotation)
{
  // 3x3 array of glider, little-bittian
  uint8_t glider[] = { 0xAC, 0x01 };

  for (int8_t dx = 0; dx < 3; dx++) {
    for (int8_t dy = 0; dy < 3; dy++) {
      int fx;
      int fy;
      if (rotation % 2) {
	fx = (x + dx) % (LEDS_PER_RING);
      } else {
	fx = (x - dx);
	if (fx < 0) fx += LEDS_PER_RING;
      }
      if (rotation / 2) {
	fy = (y + dy) % (NUM_RINGS);
      } else {
	fy = (y - dy);
	if (fy < 0) fy += NUM_RINGS;
      }

      if (unpackBit(glider, dy, dx, 3)) {
	setBit(universe, fy, fx);
      } else {
	clearBit(universe, fy, fx);
      }
    }
  }
}

void Life::addSwitch(uint8_t x, uint8_t y, uint8_t rotation)
{
  // 5x5 array of a block-laying switch engine
  uint8_t blse[] = { 0x37, 0x60, 0x5B, 0x01 };
  for (int8_t dx = 0; dx < 5; dx++) {
    for (int8_t dy = 0; dy < 5; dy++) {
      int8_t fx;
      int8_t fy;
      if (rotation % 2) {
	fx = (x + dx) % (LEDS_PER_RING);
      } else {
	fx = (x - dx);
	if (fx < 0) fx += LEDS_PER_RING;
      }
      if (rotation / 2) {
	fy = (y + dy) % (NUM_RINGS);
      } else {
	fy = (y - dy);
	if (fy < 0) fy += NUM_RINGS;
      }

      if (unpackBit(blse, dy, dx, 5)) {
	setBit(universe, fy, fx);
      } else {
	clearBit(universe, fy, fx);
      }
    }
  }      
}


void Life::addEntropy()
{
  uint8_t x = random(0,LEDS_PER_RING);
  uint8_t y = random(0, NUM_RINGS);
  if (random(0,10) != 9) {
    // 90% chance we'll add a glider
    addGlider(x, y, random(0,4));
  }
  else {
    // Block-laying switch engine
    addSwitch(x, y, random(0,4));
  }
}

uint8_t Life::countNeighborsWithWraparound(uint8_t x, uint8_t y)
{
  uint8_t n = 0;
  for (char y1 = y-1; y1 <= y+1; y1++) {
    for (char x1 = x-1; x1 <= x + 1; x1++) {
      if (x1 != x || y1 != y) {
	if (getBit(universe,y1 % NUM_RINGS,x1 % LEDS_PER_RING)) n++;
      }
    }
  }
  return n;
}

uint8_t Life::CRC8()
{
  uint8_t crc = 0x00;
  uint8_t len = NUM_RINGS * LEDS_PER_RING / 8;
  uint8_t *p = universe;
  while (len--) {
    uint8_t b = *p++;
    for (uint8_t bit = 8; bit; bit--)
      {
	uint8_t sum = (crc ^ b) & 0x01;
	crc >>= 1;
	if (sum)
	  crc ^= 0x8C;
	b >>= 1;
      }
  }
  return crc;
}
