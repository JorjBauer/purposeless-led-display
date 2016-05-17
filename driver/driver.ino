#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include <Adafruit_NeoPixel.h>

#include <SPI.h>

#include "Fader.h"
#include <RingBuffer.h>
#include "RingPixels.h"
#include "Life.h"

#define ENQ 5 // ASCII character 5, "Enquire"

#define LEDS_PER_RING 24
#define NUM_RINGS 8
#define TOTAL_LEDS (LEDS_PER_RING*NUM_RINGS)

#define WS2812PIN 6
#define CTSPIN 3
#define RAMPIN 10 // /SS on SPI RAM

// Serial SRAM commands
#define RAMREAD 0x03
#define RAMWRITE 0x02
#define RDMR 0x05
#define WRMR 0x01

#define MAX_TWINKLE_LIT ((1*TOTAL_LEDS)/3)
#define TWINKLE_LIGHT_RATE (3)

Adafruit_NeoPixel strip = Adafruit_NeoPixel(TOTAL_LEDS, WS2812PIN, NEO_GRB | NEO_KHZ800); // Also NEO_RGB | NEO_KHZ400
Fader *fader = NULL;
Life *lifeThing = new Life;

enum runmode {
  InvalidMode = -1,
  OffMode = 0,
  RawMode,
  TwinkleMode,
  WipeMode,
  ChaseMode,
  RingsMode,
  TextMode,
  MatrixMode,
  TheaterChaseMode,
  RainbowMode,
  TardisMode,
  TardisPillarMode,
  LifeMode,
  RotateMode,
  TestMode
};

runmode current_mode;
unsigned long next_millis = 0;

struct _ModeData {
  int8_t repeat;
  union _mode {
    struct _raw {
      uint32_t color;
      bool fade;
    } raw;
    struct _twinkle {
      uint8_t numLit;
    } twinkle;
    struct _wipe {
      uint32_t color;
      uint8_t pos;
    } wipe;
    struct _rings {
      uint32_t color;
      bool direction; // true = up; false = down
      int8_t nextRing;
    } rings;
    struct _matrix {
      int8_t matrix_state[LEDS_PER_RING];
      uint8_t newtext[5];
      uint32_t new_color;   // color to treat all of the pixels for the newtext
      uint32_t wipe_color;  // color of the matrix effect itself (0, 10, 4 is good)
    } matrix;
    struct _theaterChase {
      int8_t theater_state;
      uint8_t rainbow_state;
    } theaterChase;
    struct _rainbow {
      uint8_t state;
    } rainbow;
    struct _tardis {
      uint8_t state;
      uint8_t direction;
    } tardis;
    struct _tardisPillar {
      bool direction; // true = up; false = down
      int8_t nextRing;
    } tardisPillar;
    struct _life {
      uint8_t lastFingerprints[4];
    } life;
    struct _test {
      uint32_t testAddress;
      bool hasFailed;
    } test;
  } mode;
};

struct _ModeData modeData;

typedef bool (*modeCallback)();
typedef bool (*modeInitializer)();

typedef struct _modeDef {
  runmode mode;
  uint8_t trigger;
  uint8_t commandBytes;
  modeCallback callback;
  uint16_t callbackDelay;
  modeInitializer initializer;
} modeDef;

bool twinkle();
bool wipe();
bool rings();
bool text();
bool matrix();
bool theaterChase();
bool rainbow();
bool tardis();
bool tardisPillar();
bool life();
bool rotate();
bool test();

bool wipeModeInit();
bool chaseModeInit();
bool ringsModeInit();
bool textModeInit();
bool matrixModeInit();
bool rawFadeInit();
bool rawPixelInit();
bool brightnessInit();
bool dimtimeInit();
bool rawColorInit();
bool rawLedInit();
bool lifeInit();
bool testInit();

// Number of LEDs in a ring * 2 (for color info), +1 for the line number
#define RINGBYTES (LEDS_PER_RING * 2 + 1)

#define NUMMODES 21
const modeDef modes[NUMMODES] = { 
  /* Mode          trigger  bytes-reqd callback     delay     init 
   * ----             ---  ------     --------     -----     ----- */
  { OffMode,          '0', 0,         NULL,          0,      NULL },
  { RawMode,          'r', 0,         NULL,          0,      NULL },
  { TwinkleMode,      'T', 0,         twinkle,     125,      NULL },
  { WipeMode,         'W', 3,         wipe,          0,      wipeModeInit  },
  { ChaseMode,        '!', 4,         wipe,         30,      chaseModeInit },
  { RingsMode,        'R', 5,         rings,       500,      ringsModeInit },
  { TextMode,         't', 0,         text,        150,      textModeInit  },
  { MatrixMode,       'M', 5 + 3 + 3, matrix,       50,      matrixModeInit},
  { TheaterChaseMode, '@', 0,         theaterChase, 50,      NULL },
  { RainbowMode,      '~', 0,         rainbow,      50,      NULL },
  { InvalidMode,      'f', 1,         NULL,          0,      rawFadeInit  },
  { InvalidMode,      '1', 1,         NULL,          0,      rawPixelInit },
  { InvalidMode,      'b', 1,         NULL,          0,      brightnessInit },
  { InvalidMode,      'd', 2,         NULL,          0,      dimtimeInit  },
  { InvalidMode,      'c', 3,         NULL,          0,      rawColorInit },
  { InvalidMode,      'L', RINGBYTES, NULL,          0,      rawLedInit   },
  { TardisMode,       '|', 0,         tardis,        0,      NULL },
  { TardisPillarMode, '/', 0,         tardisPillar, 25,      NULL },
  { LifeMode,         'l', 0,         life,       1000,      lifeInit },
  { RotateMode,       '$', 0,         rotate,      150,      NULL },
  { TestMode,         '`', 0,         test,         10,      testInit },
};

#define MATRIX_INIT (-random(10) - 27)
#define MATRIX_BREAKPOINT 40
//#define MATRIX_BREAKPOINT2 60
#define MATRIX_STOPPOINT 80
#define MATRIX_HEIGHT 11

#define numberOfCustomCharacters 9
#define CHAR_WIDTH 5
const PROGMEM unsigned char charData[96 + numberOfCustomCharacters][CHAR_WIDTH] = {
#include "font_data.h"
};

// Offscreen pixel area that gets shifted onscreen (ring buffer)
#define BACKINGPIXELSIZE 24
RingPixels backingPixels(NUM_RINGS, BACKINGPIXELSIZE);

// Offscreen text and color ring buffers, still to be placed in the offscreen pixel area
#define BACKINGTEXTSIZE 30
RingBuffer backingText(BACKINGTEXTSIZE);
RingBuffer backingTextColor(BACKINGTEXTSIZE);
RingBuffer serialBuffer(RINGBYTES + 1); // RINGBYTES is our largest command, so we shouldn't need a buffer bigger than that.

#define MAX_BRIGHTSHIFT 8
byte brightnessShift = 0; // 0 = full bright; 8 = full dark. Shifts by 1 bit each time.

// One byte of command, two bytes of address, then one byte of potential data w/ retval
uint8_t ramTransaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
  SPI.transfer(a); // command
  SPI.transfer(b); // 3 bytes of address
  SPI.transfer(c);
  SPI.transfer(d);
  return SPI.transfer(e); // value and/or returned value
}

void initRam()
{
  SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE0));
  SPI.begin();
  pinMode(RAMPIN, OUTPUT);
  digitalWrite(RAMPIN, LOW); // 25nS setup time req'd

  SPI.transfer(WRMR);
  SPI.transfer(0x00); // "byte mode" per datasheet. 0x80 is page mode, 0x40 is sequential

  digitalWrite(RAMPIN, HIGH);
  pinMode(RAMPIN, INPUT);
  SPI.end();
  SPI.endTransaction();
}

void eraseRam()
{
  // Initialize the ram contents with zeroes
  for (uint32_t i=0; i<131072; i++) { // 128KB (1024 Kbit) device
    writeRam(i, 0);
  } 
}

uint8_t readRam(uint32_t a)
{
  SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE0));
  pinMode(RAMPIN, OUTPUT);
  digitalWrite(RAMPIN, LOW); // 25nS setup time req'd

  uint8_t d = ramTransaction(RAMREAD, (a>>16) & 0xFF, (a>>8) & 0xFF, a&0xFF, 0);

  digitalWrite(RAMPIN, HIGH);
  pinMode(RAMPIN, INPUT);
  SPI.endTransaction();

  return d;
}

void writeRam(uint32_t a, uint8_t d)
{
  SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE0));
  pinMode(RAMPIN, OUTPUT);
  digitalWrite(RAMPIN, LOW); // 25nS setup time req'd

  ramTransaction(RAMWRITE, (a>>16) & 0xFF, (a>>8) & 0xFF, a&0xFF, d);

  digitalWrite(RAMPIN, HIGH);
  pinMode(RAMPIN, INPUT);
  SPI.endTransaction();
}

uint32_t brightnessControlled(uint32_t color)
{
  if (brightnessShift) {
    uint16_t r =((color >> 16) & 0xFF);
    uint16_t g = ((color >>  8) & 0xFF);
    uint16_t b = ((color      ) & 0xFF);
    
    r >>= brightnessShift;
    g >>= brightnessShift;
    b >>= brightnessShift;
    
    color = r;
    color <<= 8;
    color |= g;
    color <<= 8;
    color |= b;
  }
  return color;
}

uint32_t brightnessControlled(uint16_t r, uint16_t g, uint16_t b)
{
  if (brightnessShift) {
    r >>= brightnessShift;
    g >>= brightnessShift;
    b >>= brightnessShift;
  }
  return strip.Color(r, g, b);
}

uint32_t colorFromSerialBuffer()
{
  return strip.Color(serialBuffer.consumeByte(),
		     serialBuffer.consumeByte(),
		     serialBuffer.consumeByte());
}

void setPixelColor(uint16_t pixelIdx, uint32_t color)
{
  strip.setPixelColor(pixelIdx, brightnessControlled(color));
}

void resetMode(runmode newMode)
{
  current_mode = newMode;
  
  // prepare to run loops at the next opportunity  
  next_millis = 0;

  // clear private union data
  memset(&modeData, 0, sizeof(modeData));


// FIXME undo all this stuff and put it in initializer functions
  // If we're going in to raw mode, let the fades finish as-was
  if (newMode != RawMode) {
    fader->reset();
    fader->setFadeMode(newMode == WipeMode);
  }

  // If we're going in to off mode, set all the pixels to fade out
  if (newMode == OffMode) {
    fader->fadeEverythingOut();
  }

  // For twinkle mode, clear the display before it starts
  if (newMode == TwinkleMode)
    strip.clear();
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

void setup() {
  pinMode(CTSPIN, OUTPUT);
  pinMode(RAMPIN, INPUT); // make sure this stays high-Z most of the time so we can still program the driver itself via pins 11-13
  
  strip.begin();

  strip.show(); // blank display

  initRam();

  fader = new Fader(&strip);
  fader->setBrightnessShift(brightnessShift);
  
  Serial.begin(115200);

// FIXME ... can print status to the display as we init! Just need to set backingText and go to text mode
  resetMode(TwinkleMode);

  digitalWrite(CTSPIN, HIGH);
}

int findRandomUnfadedPixel()
{
  // Try to find a random unfaded pixel 10 times. If we fail, then return -1.
  for (int i=0; i<10; i++) {
    uint8_t pixelNum = random(0, TOTAL_LEDS-1);
    if (fader->isFading(pixelNum) == false) {
      return pixelNum;
    }
  }
  return -1;
}

bool twinkle()
{
  bool didChangeAnything = false;
  
  for (int lightcount = 0; lightcount < TWINKLE_LIGHT_RATE; lightcount++) { // up to TWINKLE_LIGHT_RATE lights go on per iteration.
    if (modeData.mode.twinkle.numLit < MAX_TWINKLE_LIT) {
      // Light another if we can!
      int idx = findRandomUnfadedPixel();
      if (idx != -1) {
        didChangeAnything = true;
        if (random(0,2) == 0) {
          // fade to white
          fader->setFadeTarget(idx, brightnessControlled(255, 255, 196)); // a color mix that I liked as "white" with the pixels in 2014...
        } else {
          // fade to red
          fader->setFadeTarget(idx, brightnessControlled(255, 0, 0));
        }
        modeData.mode.twinkle.numLit++;
      }
    }
  }

  return didChangeAnything;
}

uint32_t un565(uint16_t c)
{
  uint8_t r = (c & 0xF800) >> 8;
  uint8_t g = (c & 0x07E0) >> 5;
  uint8_t b = (c & 0x001F);

  uint32_t col = r;
  col <<= 8;
  col |= g;
  col <<= 8;
  col |= b;

  return col;
}

// return -1 for error, or # of bytes we consumed otherwise
bool dimtimeInit()
{
//  fader->setFadeTime(serialBuffer.consumeByte());
//  fader->setFadeSteps(serialBuffer.consumeByte());
// FIXME: now useless

  return false; // we didn't change any pixels
}

bool rawColorInit()
{
  uint8_t r = serialBuffer.consumeByte();
  uint8_t g = serialBuffer.consumeByte();
  uint8_t b = serialBuffer.consumeByte();

  if (current_mode == RawMode) {
    modeData.mode.raw.color = strip.Color(r, g, b);
  }

  return false; // no pixels were changed
}

bool rawFadeInit()
{
  uint8_t b = serialBuffer.consumeByte();
  if (current_mode == RawMode) {
    modeData.mode.raw.fade = (b ? true : false);
  }

  return false; // no pixels were changed
}

bool rawPixelInit()
{
  uint8_t pixelIndex = serialBuffer.consumeByte();

  if (current_mode == RawMode) {
    if (modeData.mode.raw.fade) {
      fader->setFadeTarget(pixelIndex, brightnessControlled(modeData.mode.raw.color));
    } else {
      fader->stopFading(pixelIndex);
      setPixelColor(pixelIndex, modeData.mode.raw.color);
    }
    return true; // we updated a pixel
  }
  return false; // no pixels were updated
}

bool wipeModeInit()
{
  modeData.mode.wipe.color = colorFromSerialBuffer();

  modeData.mode.wipe.pos = 0;

  return false;
}

bool chaseModeInit()
{
  // Chase mode is a subclass of wipe, so we set things in the wipe union...
  modeData.repeat = serialBuffer.consumeByte();
  modeData.mode.wipe.color = colorFromSerialBuffer();
  modeData.mode.wipe.pos = 0;
  return false;
}

bool rawLedInit()
{
  int8_t linenum = serialBuffer.consumeByte() - '0'; // convert from ascii to a number (0-7) 
  uint8_t rb[RINGBYTES-1];
  for (int i=0; i<RINGBYTES-1; i++) {
    rb[i] = serialBuffer.consumeByte();
  }

  if (current_mode == RawMode) {
    if (linenum < 0) linenum = 0;
    if (linenum >= NUM_RINGS) linenum = NUM_RINGS-1;
           
    for (int i=0; i<LEDS_PER_RING; i++) {
      uint16_t pixelColor = (rb[i*2] << 8) | rb[i*2+1];
      uint8_t pixelIdx = linenum * LEDS_PER_RING + i;

      if (modeData.mode.raw.fade) {
	fader->setFadeTarget(pixelIdx, brightnessControlled(un565(pixelColor)));
      } else {
	fader->stopFading(pixelIdx);
	setPixelColor(pixelIdx, un565(pixelColor));
      }
    }
    return true; // we updated pixels
  }
  return false; // no pixels were updated
}

bool ringsModeInit()
{
  modeData.repeat = serialBuffer.consumeByte();
  modeData.mode.rings.color = colorFromSerialBuffer();
  modeData.mode.rings.direction = serialBuffer.consumeByte();
  if (modeData.mode.rings.direction) {
    modeData.mode.rings.nextRing = 0;
  } else {
    modeData.mode.rings.nextRing = NUM_RINGS - 1;
  }
  return false; // no pixels were updated
}

bool textModeInit()
{
  backingPixels.clear();
  backingText.clear();
  backingTextColor.clear();
  return false;
}

bool matrixModeInit()
{
  for (int i=0; i<LEDS_PER_RING; i++) {
    modeData.mode.matrix.matrix_state[i] = MATRIX_INIT;
  }
  for (int i=0; i<5; i++) {
    modeData.mode.matrix.newtext[i] = serialBuffer.consumeByte();
  }
  modeData.mode.matrix.new_color = colorFromSerialBuffer();
  modeData.mode.matrix.wipe_color = colorFromSerialBuffer();
  
  return false;
}

bool brightnessInit()
{
  brightnessShift = serialBuffer.consumeByte();
  if (brightnessShift >= MAX_BRIGHTSHIFT)
    brightnessShift = MAX_BRIGHTSHIFT;
  fader->setBrightnessShift(brightnessShift);
  return false;
}

// return true if any lights were modified
// read data from RingBuffer *serialBuffer.
bool handleSerialCommands(const modeDef *m)
{
  bool retval = false;

  // Start a mode transition
  if (m->mode != InvalidMode)
    resetMode(m->mode);

  // If there's a mode-specific callback, do that
  if (m->initializer) {
    retval |= m->initializer();
  }

  return retval;
}

bool wipe()
{
  fader->setFadeTarget(modeData.mode.wipe.pos, brightnessControlled(modeData.mode.wipe.color));
  if (modeData.mode.wipe.pos == TOTAL_LEDS-1) {
    if ((current_mode == ChaseMode) && (modeData.repeat > 0)) {
      modeData.repeat--;
      modeData.mode.wipe.pos = 0;
    } else {
      resetMode(RawMode);
    }
  } else {
    modeData.mode.wipe.pos++;
  }
  return true;
}

bool rings()
{
  for (int idx = 0; idx < LEDS_PER_RING; idx++) {
    fader->setFadeTarget((modeData.mode.rings.nextRing * LEDS_PER_RING) + idx, brightnessControlled(modeData.mode.rings.color));
  }
  if (modeData.mode.rings.direction) modeData.mode.rings.nextRing++;
  else modeData.mode.rings.nextRing--;

  if (modeData.mode.rings.nextRing == -1 ||
      modeData.mode.rings.nextRing >= NUM_RINGS) {
        if (modeData.repeat == 0)
          resetMode(RawMode);
        else {
          modeData.repeat--;
          if (modeData.mode.rings.direction) modeData.mode.rings.nextRing = 0;
          else modeData.mode.rings.nextRing = NUM_RINGS - 1;
        }
  }
  return true;
}

// The backing color store is 2/2/2 R/G/B. Turn that in to a 24-bit color.
uint32_t colorFromBackingColor(uint32_t backingColor)
{
  return ((backingColor & 0x30) << 18) | ((backingColor & 0x1C) << 12) | ((backingColor & 0x3) << 6);
}

uint8_t backingColorFromColor(uint32_t color)
{
  return ((color & 0xF00000) >> 18) | ((color & 0x00F000) >> 12) | ((color & 0x0000F0) >> 6);
}

void addColumnToBackingStore(uint8_t data, uint8_t backingColor)
{
  // Don't allow overflow; just drop the excess data
  if (backingPixels.isFull())
    return;
  
  // If there's no data in the backing pixel buffer, then we want to insert at index 0.
  byte storeData[NUM_RINGS];
  
  for (int y=0; y<NUM_RINGS; y++) {
    if (data & (1 << ((NUM_RINGS-1)-y))) {
      storeData[y] = backingColor;
    } else {
      storeData[y] = 0;
    }
  }

  backingPixels.addLine(storeData);
}

void addCharToBackingStore(char c, uint8_t backingColor)
{
  for (int i=0; i<CHAR_WIDTH; i++) {
    uint8_t d = pgm_read_byte(&(charData[c-' '][i]));
    addColumnToBackingStore(d, backingColor);
  }
  addColumnToBackingStore(0, 0);
}

bool text()
{
  // Shift the display left; Pixel 1 gets pixel 0's data.

  // Starting from the left edge, copy in whatever's to the right of that pixel (lower valued is right; 0,0 is bottom-right):
  for (int x=LEDS_PER_RING-1; x>0; x--) {
    for (int y=0; y<NUM_RINGS; y++) {
      uint32_t c = strip.getPixelColor(y*LEDS_PER_RING+x-1);
      // NOTE: no fading due to brightness here; we're copying
      strip.setPixelColor(y*LEDS_PER_RING+x, c);
    }
  }

  // Shift new data in to the display (in to pixel 0 on each row).
  byte column[NUM_RINGS];
  if (backingPixels.hasData()) {
    byte *p = backingPixels.consumeLine();
    memcpy(column, p, NUM_RINGS);
  } else {
    memset(column, 0, NUM_RINGS);
  }

  for (int y=0; y<NUM_RINGS; y++) {
    setPixelColor(y*LEDS_PER_RING+0, colorFromBackingColor(column[y]));
  }

  // If there is text to be placed in the backing pixels buffer, and there's room, do it
  if (backingText.hasData() && backingPixels.freeSpace() > CHAR_WIDTH+1) {
    addCharToBackingStore(backingText.consumeByte(), backingTextColor.consumeByte());
  }

  return true;
}
 
bool matrix()
{
  // Move the matrix effect down one pixel and count how many columns have finished
  int8_t count_done = 0;
  for (int8_t x=0; x<LEDS_PER_RING; x++) {
    if (modeData.mode.matrix.matrix_state[x] < MATRIX_STOPPOINT) {
      modeData.mode.matrix.matrix_state[x]++;
    } else {
      count_done++;
    }
  }

  if (count_done == LEDS_PER_RING) {
    // We shouldn't need to redraw anything; the pixels should all be right!
    resetMode(RawMode);
    return true;
  }
  
  for (int x=0; x<LEDS_PER_RING; x++) {
    for (int y=0; y<NUM_RINGS; y++) {
    	int ringnum = NUM_RINGS - y - 1;
      int8_t ms = modeData.mode.matrix.matrix_state[x];
      uint8_t pixelIdx = ringnum * LEDS_PER_RING + x;

      if (ms <= y) {
  	// wipe is still offscreen; leave the pixel alone
  	//	setPixelColor(pixelIdx, 0);
        } else if (ms > y && ms < y + MATRIX_HEIGHT) {
        	// during the wipe
        	setPixelColor(pixelIdx, modeData.mode.matrix.wipe_color);
//        } else if (ms > y + MATRIX_BREAKPOINT2 + MATRIX_HEIGHT) {
//        	setPixelColor(pixelIdx, 0);
//        } else if (ms > y + MATRIX_BREAKPOINT2) {
//  	setPixelColor(pixelIdx, modeData.mode.matrix.wipe_color);
//        } else if (ms > y + MATRIX_BREAKPOINT + MATRIX_HEIGHT) {
  	// ... not sure whatt this line did; FIXME check it out
  	// setPixelColor(pixelIdx, 0);
//        } else if (ms > y + MATRIX_BREAKPOINT) {
//  	setPixelColor(pixelIdx, modeData.mode.matrix.wipe_color);
      } else {
	// after the wipe; put new content in place for pixel x/y

	// Determine which character we're talking about in 
	// modeData.mode.matrix.newtext[]

	// With 5-pixel wide chars and 28 pixels wide, we can almost fit 5 characters -
	// we're two spaces shy. One space would be off to the right and we can ignore it.
	// The other isn't so good and we have to decide where to sacrifice it. Right 
	// now I'm going to sacrifice it off the right side of the rightmost char for 
	// simplicity - later it would be nice to pull out the space between the second 
	// and third chars (b/c of the colon and whatnot).

	int xpos = (LEDS_PER_RING - x - 1) / 6;
	int colpos = (LEDS_PER_RING - x - 1) % 6;
	char c = modeData.mode.matrix.newtext[xpos];
	// get the char data from program memory
  if (colpos != 5) {
  	uint8_t data = pgm_read_byte(&(charData[c-' '][colpos]));
  
  	// draw pixel[y] of that character's column (or clear it, if necessary)
  	if (data & ( 1 << y )) {
  	  setPixelColor(pixelIdx, modeData.mode.matrix.new_color);
  	} else {
  	  setPixelColor(pixelIdx, 0);
  	}
  } else {
    setPixelColor(pixelIdx, 0); // blank column between chars
  }
      }
    }
  }

  return true;
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

bool theaterChase()
{
  modeData.mode.theaterChase.theater_state++;
  modeData.mode.theaterChase.theater_state %= 3;
  modeData.mode.theaterChase.rainbow_state++; // will roll over
  for (int i=0; i<TOTAL_LEDS; i++) {
    if ((i % 3) == modeData.mode.theaterChase.theater_state) {
      setPixelColor(i, /*modeData.mode.theaterChase.color*/Wheel(modeData.mode.theaterChase.rainbow_state));
    } else {
      setPixelColor(i, 0);
    }
  }
  return true;
}

bool rainbow()
{
  modeData.mode.rainbow.state++; // will roll over

  for (int i=0; i<TOTAL_LEDS; i++) {
    uint32_t c = Wheel((modeData.mode.rainbow.state + i) & 0xFF);
    setPixelColor(i, c);
  }
  
  return true;
}

void
hsv_to_rgb (int h, double s, double v,
            uint8_t *r, uint8_t *g, uint8_t *b)
{
  double H, S, V, R, G, B;
  double p1, p2, p3;
  double f;
  int i;

  if (s < 0) s = 0;
  if (v < 0) v = 0;
  if (s > 1) s = 1;
  if (v > 1) v = 1;
  S = s; V = v;
  H = (h % 360) / 60.0;
  i = H;
  f = H - i;
  p1 = V * (1 - S);
  p2 = V * (1 - (S * f));
  p3 = V * (1 - (S * (1 - f)));
  if      (i == 0) { R = V;  G = p3; B = p1; }
  else if (i == 1) { R = p2; G = V;  B = p1; }
  else if (i == 2) { R = p1; G = V;  B = p3; }
  else if (i == 3) { R = p1; G = p2; B = V;  }
  else if (i == 4) { R = p3; G = p1; B = V;  }
  else             { R = V;  G = p1; B = p2; }
  *r = R * 255;
  *g = G * 255;
  *b = B * 255;
}

bool tardis()
{
  modeData.mode.tardis.state++;

  // Get the blue we want, at the brightness we want
  uint8_t r, g, b;
  uint8_t brightness;
  if (modeData.mode.tardis.state <= 127) {
    // fading in from [0-127]
    brightness = 2 * modeData.mode.tardis.state;
  } else {
    // fading out from [128-255]
    brightness = 2 * (255 - modeData.mode.tardis.state);
    
  }
  
  hsv_to_rgb(225, 1.0, (float)(brightness) / 256.0, &r, &g, &b);
  uint32_t c = strip.Color(r, g, b);

  for (int i=0; i<TOTAL_LEDS; i++) {
    setPixelColor(i, c);
  }
  return true;
}

bool tardisPillar()
{
  if (modeData.mode.tardisPillar.nextRing >= 0 &&
      modeData.mode.tardisPillar.nextRing <= NUM_RINGS-1) {
    for (int idx = 0; idx < LEDS_PER_RING; idx++) {
      fader->setFadeTarget((modeData.mode.tardisPillar.nextRing * LEDS_PER_RING) + idx, brightnessControlled(strip.Color(0, 0, 255)));
    }
  }
  if (modeData.mode.tardisPillar.direction) modeData.mode.tardisPillar.nextRing++;
  else modeData.mode.tardisPillar.nextRing--;

  // Allow the progression to go off for a while, and then come back.
  if (modeData.mode.tardisPillar.nextRing == -5 ||
      modeData.mode.tardisPillar.nextRing >= NUM_RINGS+4) {
        modeData.mode.tardisPillar.direction = !modeData.mode.tardisPillar.direction;
  }
  
  return true;
}

bool lifeInit()
{
  fader->fadeEverythingOut();
  lifeThing->init();
  return false; // no pixels were modified
}

void life_lightPixel(uint8_t y, uint8_t x, uint32_t v)
{
  fader->setFadeTarget(y * LEDS_PER_RING + x, brightnessControlled(v));
}

bool life()
{
  fader->setFadeMode(true); // fade in only (which also does "out")
  bool retval = lifeThing->show(life_lightPixel, 0xFF0000); // FIXME: color control could be better
  if (lifeThing->evolve() < 5) {
    lifeThing->addEntropy();
  }

  // See if we need to add some entropy. Keep track of the last few states' fingerprints, 
  // for which we're using CRC8.
  for (int8_t idx = sizeof(modeData.mode.life.lastFingerprints)-1; idx > 0; idx--) {
    modeData.mode.life.lastFingerprints[idx] = modeData.mode.life.lastFingerprints[idx-1];
  }
  modeData.mode.life.lastFingerprints[0] = lifeThing->CRC8();

  // Check to see if we've seen the current state (fingerprint) within the last few states.
  for (int8_t idx = 1; idx < sizeof(modeData.mode.life.lastFingerprints); idx++) {
    if (modeData.mode.life.lastFingerprints[0] == modeData.mode.life.lastFingerprints[idx])
      lifeThing->addEntropy();
  }
  return retval;
}

// Rotate all the pixels around the display, right-to-left (which makes sense for trying to read text).
bool rotate()
{
  for (int8_t y = 0; y < NUM_RINGS; y++) {
    uint32_t t = strip.getPixelColor((y+1)*LEDS_PER_RING - 1); // last pixel of row y, which is the pixel before row y+1

    for (int8_t x = LEDS_PER_RING-1; x>0; x--) {
      // Set the color based on the pixel next to the one we're looking at
      strip.setPixelColor(y*LEDS_PER_RING+x, 
			  strip.getPixelColor(y*LEDS_PER_RING+x-1));
    }
    // Finally, get pixel #0 from the pixel we saved @ the end
    strip.setPixelColor(y * LEDS_PER_RING, t);
  }

  return true;
}

bool testInit()
{
  modeData.mode.test.testAddress = 0;
  modeData.mode.test.hasFailed = false;

  for (int i=0; i<TOTAL_LEDS; i++) {
    strip.setPixelColor(i, 0);
  }
  
  fader->setFadeMode(false); // fade in and out
  return true;
}

bool test()
{
  if (modeData.mode.test.testAddress >= 0x01000000) {
    // Success: all passed
    for (int i=0; i<TOTAL_LEDS; i++) {
      fader->setFadeTarget(i, 0x00FF00);
    }
  } else {
    writeRam(modeData.mode.test.testAddress, modeData.mode.test.testAddress & 0xFF);
    uint8_t r = readRam(modeData.mode.test.testAddress);
    if (r != (modeData.mode.test.testAddress & 0xFF)) {
      modeData.mode.test.hasFailed = true;
    }
  
    // Light up the pixel either white (passed) or red (failed)
    uint8_t pixelNum = modeData.mode.test.testAddress % TOTAL_LEDS;
    fader->setFadeTarget(pixelNum, modeData.mode.test.hasFailed ? 0xFF0000 : 0x00FFFF);
  
    if (!modeData.mode.test.hasFailed) {
      modeData.mode.test.testAddress++;
    }
  }

  return true;
}

// return 'true' to stay in text mode and keep processing
bool textModeInputHandler(uint8_t c)
{
  static byte escapeMode = 0;
  static uint32_t color = strip.Color(255, 255, 255);

  if (c == '\0') {
    escapeMode++;
    if (escapeMode == 2) {
      escapeMode = 0;
      current_mode = RawMode;
      return false;
    }
    return true;
  } else if (c == ENQ) { // ENQ, chr(5), querying if we're alive - return text state
    Serial.write('T'); // "we're in text mode"
    return false; // no display update
  }

  if (escapeMode) {
    // Process a control character
    
    if (c == 'r') color = 0x0FF0000;
    else if (c == 'g') color = 0x00FF00;
    else if (c == 'b') color = 0x0000FF;
    else if (c == 'w') color = 0xFFFFFF;

    escapeMode = 0;
    return true;
  }

  // Not in an escape mode. Add the character to the backing text ring buffer.
  if (!backingText.isFull()) {
    backingText.addByte(c);
    backingTextColor.addByte(backingColorFromColor(color));
  }
  return true;
}

const modeDef *findMode(runmode r)
{
  for (byte i=0; i<NUMMODES; i++) {
    if (r == modes[i].mode) {
      return &modes[i];
    }
  }
  return NULL;
}

const modeDef *findModeByTrigger(uint8_t t)
{
  for (byte i=0; i<NUMMODES; i++) {
    if (t == modes[i].trigger) {
      return &modes[i];
    }
  }
  return NULL;
}

void loop()
{
  bool changes = false; // Did we change any lights?

  if (Serial.available() > 0) {
    static bool ledState = false;
    ledState = !ledState;
    byte b = Serial.read();
    if (current_mode == TextMode) {
      changes |= textModeInputHandler(b);
    } else {

      serialBuffer.addByte(b);
      const modeDef *d = findModeByTrigger(serialBuffer.peek(0));
      if (d) {
	byte moreNeeded = d->commandBytes;
	if (serialBuffer.count() > moreNeeded) { // '>' because of the command byte itself
	  serialBuffer.consumeByte();                 // drop the command byte
	  digitalWrite(CTSPIN, LOW);                  // don't accept commands right now
	  changes |= handleSerialCommands(d);         // go handle the command
	  digitalWrite(CTSPIN, HIGH);                 // enable command reception again
	}
      } else if (serialBuffer.peek(0) == '\5') {
	// ENQ, an enquiry packet. Respond that we're in "raw" mode.
	Serial.write('R'); // "we're in text mode"
	serialBuffer.consumeByte();
      } else { 
	// Can't find that mode, so we'll drop the data and keep reading
	serialBuffer.consumeByte();
      }
    }
  }

  // Perform automated routine updates based on what mode we're currently in
  const modeDef *m = findMode(current_mode);
  if (m && m->callback) {
    if (millis() >= next_millis) {
      changes |= m->callback();
      next_millis = millis() + m->callbackDelay;
    }
  }

  // Perform a fade update once per loop for anything that's fading
  changes |= fader->performFade();

  if (changes && current_mode == TwinkleMode) {
    // subtract the number that faded out last time 'round
    modeData.mode.twinkle.numLit -= fader->howManyWentOut();
  }

  if (changes) {
    strip.show();
  }
}

