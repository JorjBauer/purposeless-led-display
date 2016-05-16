#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include <RFM69.h>         //get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>
#include <SPIFlash.h>      //get it here: https://www.github.com/lowpowerlab/spiflash
#include <avr/wdt.h>
#include <WirelessHEX69.h> //get it here: https://github.com/LowPowerLab/WirelessProgramming/tree/master/WirelessHEX69
#include <RingBuffer.h>
#include "Programmer.h"
#include "Clock.h"

// degrees C
#define MAXTEMP 60
#define MINTEMP 40

#define MAXLINE 85
char oneLine[MAXLINE];

#define PIN_RST 4
#define PIN_MOSI 15
#define PIN_MISO 16
#define PIN_SCK 17
#define PIN_FAN 5

bool sendingTemp = false;
unsigned long tempTimer = 0;

#define NODEID      3
#define NETWORKID   212
#define FREQUENCY RF69_433MHZ
#define IS_RFM69HW  //uncomment only for RFM69HW! Leave out if you have RFM69W!
#ifdef DEFAULTKEY
  #define ENCRYPTKEY DEFAULTKEY
#else
  #pragma message("Default encryption key not found; using compiled-in default")
  #define ENCRYPTKEY "sampleEncryptKey"
#endif

#define FLASH_SS 8

#define CTSPIN 7

RFM69 radio;
SPIFlash flash(FLASH_SS, 0xEF30); //EF30 for windbond 4mbit flash

// ring buffer that accepts data from wireless to send out serial
RingBuffer serialBuffer(128);

Clock clock;

enum timeModes {
  TM_off   = 0,
  TM_clock,
  TM_twinkle,
  TM_chase,
  TM_ring,
  TM_rainbow,
  TM_theater
};

timeModes nextTimeMode = TM_off;
unsigned long nextUpdate = 0;

void clearTextMode()
{
  // Send a command to the pro mini (via serial) to end text mode, if it's in text mode.
  addBufferData((uint8_t *)"\0\0", 2); // DUH can't add "\0" as a STRING you dolt
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

void randomColor(uint8_t *r, uint8_t *g, uint8_t *b)
{
  double v = ((double) random(0,75) / 100.0) + 0.25; // random (0.75) + 0.25
  // Set r, g, b with our random number, which we'll pick out of the HSV space.
  hsv_to_rgb(random(0, 360), 1.0, v, 
              r, g, b);
}

void setup() {
  randomSeed(analogRead(0));
  
#ifdef CTSPIN
  pinMode(CTSPIN, INPUT);
#endif
  pinMode(PIN_FAN, OUTPUT);
  analogWrite(PIN_FAN, 255); // Start by running the fan full-out in case of problems starting up

  ResetProMini();

  Serial.begin(115200);
  if (!radio.initialize(FREQUENCY,NODEID,NETWORKID)) {
    clearTextMode();
    addBufferString("tRADIO FAILURE");
  }
  radio.encrypt(ENCRYPTKEY); //OPTIONAL
#ifdef IS_RFM69HW
  radio.setHighPower(); //only for RFM69HW!
#endif

  if (!flash.initialize()) {
    clearTextMode();
    addBufferString("tFLASH FAILURE");
  }
  analogWrite(PIN_FAN, 255);
}

void addBufferByte(volatile uint8_t d)
{
  serialBuffer.addByte(d);
}

void addBufferData(volatile uint8_t *d, uint8_t dsize)
{
  while (dsize) {
    addBufferByte(*d);
    d++;
    dsize--;
  }
}

void addBufferString(const char *s)
{
  addBufferData((volatile uint8_t *)s, strlen(s));
}

void handleFuseRequest(Programmer *programmer, bool isHighFuseRequest)
{
  if (isHighFuseRequest) {
    sprintf(oneLine, "0x%.2X", programmer->getHighFuse());
  } else {
    // low fuses
    sprintf(oneLine, "0x%.2X", programmer->getLowFuse());
  }
  radio.sendACK(oneLine, strlen(oneLine));
}

void checkTemperature()
{
  if (millis() >= tempTimer) {
    uint8_t temperatureC =  radio.readTemperature(-1); // -1 = user cal factor, adjust for correct ambient later

    if (sendingTemp) {
      sprintf(oneLine, "T: %u ", radio.readTemperature(-1));
    }

    if (temperatureC >= MAXTEMP)
      temperatureC = MAXTEMP;

    if (temperatureC > MINTEMP) {
      uint8_t fanRate = map(temperatureC, MINTEMP, MAXTEMP, 0, 255); // scale the temperature value to a 0-255 fan rate
      analogWrite(PIN_FAN, fanRate);

      if (sendingTemp) {
        sprintf(&oneLine[strlen(oneLine)], "F: %u", fanRate);
      }
    } else {
      analogWrite(PIN_FAN, 0);
      if (sendingTemp) {
        strcat(oneLine, "F: 0");
      }
    }

    if (sendingTemp) {
        radio.send(1, oneLine, strlen(oneLine)); // no ACK
      
        addBufferData((uint8_t *)"\0\0    t", 7);
        addBufferString(oneLine);
  }
    
    tempTimer = millis() + 60000; // every 60 seconds
  }
}

void ResetProMini()
{
  digitalWrite(PIN_RST, LOW);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(100);
  pinMode(PIN_RST, INPUT); // let it float
  digitalWrite(PIN_RST, HIGH); // and end reset mode on the target
}

void CheckForMagicPackets()
{
  // Any packet that begins with '~~~' is a magic packet. We intercept it rather than sending it to the Pro Mini.
  
  if (radio.DATALEN > 3 && radio.DATA[0] == '~' && radio.DATA[1] == '~' && radio.DATA[2] == '~') {
    if (radio.DATALEN == 7 && !strcmp((char *)&radio.DATA[3], "Temp")) {
      sendingTemp = !sendingTemp;
      tempTimer = 0; // start right away, please
      nextTimeMode = TM_off;
      
      radio.DATALEN = 0; // Consume the radio data
    } else if (radio.DATALEN == 7 && !strcmp((char *)&radio.DATA[3], "Rset")) {
      // Reset the other controller
      ResetProMini();
      // And then reset ourself, too
      void(* resetFunc) (void) = 0; //declare reset function @ address 0
      resetFunc();
      // No need to consume the data; we're resetting
    } else if (radio.DATALEN == 8 && radio.DATA[3] == 'C' && radio.DATA[4] == 'k') {
      // Start or stop running the second microcontroller in a clock display. DATA[5..7] have the current time, encoded as
      // Hour (0..23) in DATA[5] and
      // Minute (0..59) in DATA[6] and
      // Second (0..59) in DATA[7]
      // We take that hour/minute/second and turn it in to an offset, that when added to millis(), will (with probable overflow) return the number of millis since midnight.
      clock.setTime(radio.DATA[5], radio.DATA[6], radio.DATA[7]);

      nextTimeMode = TM_clock;
      nextUpdate = millis();
      radio.DATALEN = 0; // Consume the radio data
    } else if (radio.DATALEN == 7 && radio.DATA[3] == 'F' && radio.DATA[4] == 'u' && radio.DATA[5] == 's') {
      Programmer *programmer = new Programmer(PIN_RST, PIN_MOSI, PIN_MISO, PIN_SCK);
      handleFuseRequest(programmer, radio.DATA[6] == '+'); // + for high fuses, - for low fuses
      delete programmer;
      radio.DATALEN = 0; // Consume the radio data
    } else if (radio.DATALEN == 8 && radio.DATA[3] == 'F' && radio.DATA[4] == 'u' && radio.DATA[5] == 'S' && radio.DATA[6] == '+') {
      Programmer *programmer = new Programmer( PIN_RST, PIN_MOSI, PIN_MISO, PIN_SCK);
      programmer->setHighFuse(radio.DATA[7]);
      sprintf(oneLine, "0x%.2X", programmer->getHighFuse());
      delete programmer;
      radio.sendACK(oneLine, strlen(oneLine));

      radio.DATALEN = 0; // Consume the radio data
    } else if (radio.DATALEN == 7 && !strcmp((char *)&radio.DATA[3], "Erse")) {
      Programmer *programmer = new Programmer( PIN_RST, PIN_MOSI, PIN_MISO, PIN_SCK);
      programmer->eraseTarget();
      sprintf(oneLine, "Erased");
      delete programmer;
      radio.sendACK(oneLine, strlen(oneLine));
      
      radio.DATALEN = 0;
    } else if (radio.DATALEN == 7 && !strcmp((char *)&radio.DATA[3], "Flsh")) {
      enterFlashMode();
      radio.DATALEN = 0; // Consume the radio data
    }
  }
}

void loop()
{
  if (nextTimeMode != TM_off) {
    unsigned long cur = millis();
    if (cur >= nextUpdate) {
        nextUpdate = cur + 30000;
        clearTextMode();
        uint8_t r, g, b;
        randomColor(&r, &g, &b);
        oneLine[0] = 0;
        switch (nextTimeMode) {
          case TM_clock:
            {
              uint32_t theTime = clock.currentTime();
              uint8_t hour = (uint32_t)(theTime >> 24) & 0xFF;
              uint8_t minute = (uint32_t)(theTime >> 16) & 0xFF;
              sprintf(oneLine, 
                      "t%.2d:%.2d",
                      hour,
                      minute);
              nextTimeMode = (timeModes) ((int)TM_twinkle + random(0,5)); // random from [0,5)
              nextUpdate = cur + 10000;
            }
            break;
          case TM_twinkle:
            sprintf(oneLine, "T");
            nextTimeMode = TM_clock;
            break;
          case TM_chase:
            sprintf(oneLine, "!%c%c%c%c", 127 /*repeat*/, r, g, b); // repeat, RGB
            addBufferData((uint8_t *)oneLine, 5);
            oneLine[0] = 0;
            nextTimeMode = TM_clock;
            break;
          case TM_ring:
            sprintf(oneLine, "R%c%c%c%c%c", 127, r, g, b, 1); // repeat, rgb, direction
            addBufferData((uint8_t *)oneLine, 6);
            oneLine[0] = 0;
            nextTimeMode = TM_clock;
            break;
          case TM_rainbow:
            sprintf(oneLine, "~");
            nextTimeMode = TM_clock;
            break;
          case TM_theater:
            sprintf(oneLine, "@");
            nextTimeMode = TM_clock;
            break;
          default:
            sprintf(oneLine, "t[err - %d]", (int)nextTimeMode);
            nextUpdate = cur + 10000;
            nextTimeMode = TM_clock;
            break;
        }
      if (oneLine[0]) {
        addBufferString(oneLine);
      }
    }
  }
  
  if (radio.receiveDone()) {
    // Check for ACK request & reply if so
    
    // Look for firmware updates via radio
    if (radio.DATALEN >= 4 && radio.DATA[0]=='F' && radio.DATA[1]=='L' && radio.DATA[2]=='X' && radio.DATA[3]=='?') {
      // going to enter programming mode inside CheckForWirelessHex; put the display in text mode to see debug output on serial
      clearTextMode();
      addBufferByte('t');

      // flush the buffered serial data, ignoring CTS
      while (serialBuffer.hasData()) {
        Serial.write(serialBuffer.consumeByte());
      }
      
      // and fire the fan up to max, since we'll be doing other work for a while
      analogWrite(PIN_FAN, 255);
    }
    CheckForWirelessHEX(radio, flash, 1, 9); // checks for the header 'FLX?', debug level 1, but use led on pin 9

    CheckForMagicPackets();

    // If we have data, dump it in to the serial buffer for sending...
    if (radio.DATALEN) {
      addBufferData(radio.DATA, radio.DATALEN);
    }

    // If we send the ACK sooner, we break CheckForWirelessHEX(). Not exactly sure why - dig in to the code and find out. :/
    if (radio.ACKRequested()) {
      radio.sendACK();
    }
  }

  // If there's serial data waiting to send, and the remote end asserts CTS, then send a character and remove it from the buffer
  if (serialBuffer.hasData() 
#ifdef CTSPIN
  && digitalRead(CTSPIN)
#endif
  ) {
    Serial.write(serialBuffer.consumeByte());
  }

  // If the remote end has sent us data, let's send it to the gateway. One byte at a time right now.
  if (Serial.available()) {
    oneLine[0] = Serial.read();
    radio.send(1, oneLine, 1);
  }

  // Update the fan speed based on temperature
  checkTemperature();
}

// expect packets to be line-based; copy them right in to oneLine and return success or 5-second timeout failure
ProgrammerStatus receiveLineFromRadioAndFlash(Programmer *programmer)
{
  uint32_t end = millis() + 5000; // 5-second timeout

  while (millis() < end) {
    if (radio.receiveDone()) {
      ProgrammerStatus ps = programmer->parseAndStoreDataFromRadio(radio.DATALEN, (uint8_t *)radio.DATA);

      if (radio.ACKRequested()) {
	radio.sendACK((const void*)radio.DATA, radio.DATALEN);
      }

      return ps;
    }
  }

  return PS_Invalid;
}

void fanDance(uint8_t times, uint16_t delayTime)
{
  for (int i=0; i<times; i++) {
    analogWrite(PIN_FAN, 0);
    delay(delayTime);
    analogWrite(PIN_FAN, 255);
    delay(delayTime);
  }
}


void enterFlashMode()
{
  Programmer *programmer = new Programmer( PIN_RST, PIN_MOSI, PIN_MISO, PIN_SCK);
  uint8_t oldHighFuse;

  fanDance(2, 150);
  
  // Set the Fan to full speed, since we'll be out of touch with the main loop and temperature sensing
  analogWrite(PIN_FAN, 255);

  // Send notice to the remote end that we're now in programming mode
  sprintf(oneLine, "Flsh!!");
  radio.sendACK(oneLine, strlen(oneLine));

  // Display on the LEDs what we're about to do
  Serial.write("\0\0tPgm", 6); // Can't buffer this: we don't expect to run the main loop for a while

  // Get all the data from the radio and flash it
  ProgrammerStatus ps;
  do {
    ps = receiveLineFromRadioAndFlash(programmer);
  } while (ps == PS_OK);

  if (ps != PS_FlashComplete) {
    // Any other value is an error
    addBufferData((uint8_t*)"\0\0tF1", 5); // try to display it if we can...

      if (ps == PS_SyntaxError) {
	fanDance(5, 500);
      } else if (ps == PS_InvalidPacket) {
      	fanDance(2, 1000);
      }

    goto error;
  }

  /* Perform the flash update. We must ensure that no bootloader run
   * (the low bit of the high fuse must be set) and we must erase the
   * target before we begin (because the 328P, when being programmed
   * via SPI, can change a bit from a 1 to a 0 but not the reverse; so
   * we erase it first, which sets all bits to 1, but also destroys
   * the bootloader, which is why we have to disable it).
   */

  oldHighFuse = programmer->getHighFuse();
  if (!(oldHighFuse & 0x01)) {
    // Make sure the bootloader is disabled
    programmer->setHighFuse(oldHighFuse | 0x01);
  }

 error:
  delete programmer;

  // Send to the remote end that we're leaving flash mode.
  sprintf(oneLine, "Flsh..");
  radio.send(1, oneLine, strlen(oneLine));
  
  // Set the fan back to 0 for half a second to signal that we're finished, and then back to full in case there's a problem resuming normal temperature sensing
  analogWrite(PIN_FAN, 0);
  delay(500);
  analogWrite(PIN_FAN, 255);
  ResetProMini();
}

