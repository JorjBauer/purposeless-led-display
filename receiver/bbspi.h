#include <Arduino.h>

class bbSPISettings {
public:
  // clock is in Hz
  bbSPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) : clock(clock){
    (void) bitOrder;
    (void) dataMode;
  };

private:
  uint32_t clock;

friend class BitBangedSPI;
};

class BitBangedSPI {
public:
  BitBangedSPI(byte sck, byte mosi, byte miso) : sck(sck), mosi(mosi), miso(miso)
  {}

  void begin() {
    digitalWrite(sck, LOW);
    digitalWrite(mosi, LOW);
    pinMode(sck, OUTPUT);
    pinMode(mosi, OUTPUT);
    pinMode(miso, INPUT);
  }

  void beginTransaction(bbSPISettings settings) {
    pulseWidth = (500000 + settings.clock - 1) / settings.clock;
    if (pulseWidth == 0)
      pulseWidth = 1;
  }

  void end() {
    pinMode(sck, INPUT);
    pinMode(mosi, INPUT);
    /* pinMode(miso, INPUT); -- it's already an input. */
  }

  uint8_t transfer (uint8_t b) {
    for (unsigned int i = 0; i < 8; ++i) {
      digitalWrite(mosi, (b & 0x80) ? HIGH : LOW);
      digitalWrite(sck, HIGH);
      delayMicroseconds(pulseWidth);
      b = (b << 1) | digitalRead(miso);
      digitalWrite(sck, LOW); // slow pulse
      delayMicroseconds(pulseWidth);
    }
    return b;
  }

private:
  unsigned long pulseWidth; // in microseconds
  byte sck, mosi, miso;
};
