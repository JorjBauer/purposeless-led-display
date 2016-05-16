#include "Programmer.h"

/* Janky bit-banged Arduino programmer
 *  
 *  Assumptions:
 *    - Intel HEX file that's being parsed is contiguous (no gaps)
 *    - It's okay to blow away the bootloader
 *    - We're specifically programming an Arduino Pro Mini 5v 16MHz
 *      - signature 0x1E 0x95 0x0F  
 *      - timing is based on the datasheet for the 328P
 *      - currentPage and the commit mechanism are written from the 328P's spec
 *      - requires that target flash be erased before trying to program
 *    - HEX file ends cleanly with ":00000001FF"
 * 
 * Note that it's possible to use this to enable/disable and replace
 * the bootloader.
 *
 * Bit-bang on bbSPI-> Methodology partly comes from Atmel docs,
 * partly comes from ArduinoISP, partly comes from experimentation.
 * Look specifically at p.260 of the Atmel document doc7799.pdf
 * (ATMega32U2 datasheet) also
 * http://www.atmel.com/images/atmel-7810-automotive-microcontrollers-atmega328p_datasheet.pdf
 * page 256
 * and at ATMega25606-datasheet.pdf (2549P-AVR-10/2012) section 30.8.2 ("Serial Programming Algorithm").
 *  
 * To successfully program, you would do something like this:
 * 
 *   Programmer *programmer = new Programmer(PIN_RST, PIN_MOSI, PIN_MISO, PIN_SCK);
 *   bool running = true;
 *   while (running && receiveLineFromRadio()) {
 *     switch (programmer->parseAndStoreDataFromRadio(radio.DATALEN, radio.DATA)) {
 *      case PS_OK:
 *       // normal processing occurred; continue...
 *       break;
 *      case PS_FlashComplete:
 *       programmer->setHighFuse(0xDB);    // disable bootloader
 *       running = false;
 *       break;
 *      default:
 *       running = false;
 *       break;
 *     }
 *   }
 *   // If we get here and didn't perform the update, there was a problem with the transfer.
 *
 */

#define SPI_CLOCK     (1000000/6)

// Intel Hex datatype constants
#define IH_DATA 0
#define IH_EOF 1

#define SPI_MODE0 0x00

// Pages are 64 words on the 328P
#define currentPage(x) ((x) & 0xFFFFC0)

// Initialization params: 
//   - pins used for bit-banged rst, mosi, miso, and sck
// Note that there is no optimization using the hardware SPI. This is fairly slow.

Programmer::Programmer(byte rst, byte mosi, byte miso, byte sck) : rst(rst), sck(sck)
{
  bbSPI = new BitBangedSPI(sck, mosi, miso);
  bbSPI->begin();

  lastProgrammedPage = 0;
  programmingStarted = false;
}

Programmer::~Programmer()
{
  // Reset the SPI pins as inputs so they all float, and the target device can 
  // use SPI as it sees fit
  bbSPI->end();
}

uint8_t Programmer::spiTransaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  bbSPI->transfer(a);
  bbSPI->transfer(b);
  bbSPI->transfer(c);
  return bbSPI->transfer(d);
}

void Programmer::commit(int page)
{
    spiTransaction(0x4C, (page >> 8) & 0xFF, page & 0xFF, 0);
    delay(10); // delay of > 9mS, per datasheet for the 328P
}

bool Programmer::enterProgrammingMode()
{
  // reset the target
  digitalWrite(rst, LOW);
  pinMode(rst, OUTPUT);
  digitalWrite(rst, LOW);
  
  // put the target in programming mode
  bbSPI->begin();
  bbSPI->beginTransaction(bbSPISettings(SPI_CLOCK, MSBFIRST, SPI_MODE0));
  
  // Pull SCK low, then pulse the reset line
  digitalWrite(sck, LOW);
  delay(40); // let it discharge (datasheet says "at least 20ms")
  digitalWrite(rst, HIGH);
  delayMicroseconds(100); // ArduinoISP and docs agree – this must be > 2 target CPU clock cycles
  digitalWrite(rst, LOW);
  
  // Send the "enable programming" command
  delay(50); // Not sure this is required, but it doesn't hurt
  spiTransaction(0xAC, 0x53, 0x00, 0x00);
  
  // read the signature
  uint8_t high   = spiTransaction(0x30, 0x00, 0x00, 0x00); // Device ID: should be 0x1E
  uint8_t middle = spiTransaction(0x30, 0x00, 0x01, 0x00); // flash size: 0x95 on the 328P
  uint8_t low    = spiTransaction(0x30, 0x00, 0x02, 0x00); // device family: 0x0F for the 328P
  
  if (high != 0x1E || middle != 0x95 || low != 0x0F) {
    // ... If the signature is bad, bail; there's either a communication 
    // problem or the wrong device is on the other end.
    Serial.println("Bad signature");
    return false;
  }
  
  return true;
}

void Programmer::leaveProgrammingMode()
{
  // exit programming mode on the target
  bbSPI->end();
  pinMode(rst, INPUT); // let it float
  digitalWrite(rst, HIGH); // and end reset mode on the target

}

// Read each line, and return PS_FlashComplete when we receive the
// terminator; PS_OK for good lines; and PS_SyntaxError if there's a
// problem
ProgrammerStatus Programmer::parseAndStoreDataFromRadio(uint8_t len, uint8_t *data)
{
  ProgrammerStatus ret = PS_OK;
  
  if (!programmingStarted) {
    eraseTarget();

    enterProgrammingMode();
    programmingStarted = true;
    lastProgrammedPage = 0;
  }

  // The data received is binary, of the form
  //   (# bytes total after this one) (2-byte address, big-endian) (1-byte type) (remaining data)
  if (len < 3 || len != data[0] + 1) {
    // must have at least 3 bytes for address and type
    // must be whole
    return PS_InvalidPacket;
  }

  int8_t count = data[0] - 3; // how many bytes are we receiving for memory? (signed in case of underflow due to 2-byte data alignment)
  int address = (data[1] << 8) | data[2];
  uint32_t progmemAddress = address / 2; // program memory is addressed in 2-byte words
  byte type = data[3];

  data += 4; // move the data pointer past what we've consumed: size, address x 2, type

  if (type == IH_EOF) {
    // End of file!
    ret = PS_FlashComplete;
  }
  else if (type != IH_DATA) {
    ret = PS_SyntaxError;
  }

  while ((ret == PS_OK) && (count>0)) {
    byte b1 = *data;
    data++;
    byte b2 = *data;
    data++;

    if (currentPage(progmemAddress) != lastProgrammedPage) {
      commit(lastProgrammedPage);
      lastProgrammedPage = currentPage(progmemAddress);
    }

    spiTransaction(0x40, 0x00, progmemAddress & 0x3F, b1); // low byte first
    spiTransaction(0x48, 0x00, progmemAddress & 0x3F, b2);
    
    progmemAddress++; // advance one word for the two bytes
    count-=2;
  }

  if (ret == PS_FlashComplete) {
    // End of file!
    commit(lastProgrammedPage);

    leaveProgrammingMode();
    programmingStarted = 0;
  }

  return ret;
}

uint8_t Programmer::getHighFuse()
{
  if (!enterProgrammingMode())
    return 0;

  uint8_t highfuses = spiTransaction(0x58, 0x08, 0x00, 0x00);
  
  leaveProgrammingMode();
  return highfuses;
}

uint8_t Programmer::getLowFuse()
{
  if (!enterProgrammingMode())
    return 0;

  uint8_t lowfuses = spiTransaction(0x50, 0x00, 0x00, 0x00);

  leaveProgrammingMode();
  return lowfuses;
}

bool Programmer::setHighFuse(uint8_t b)
{
  if (!enterProgrammingMode())
    return false;

  // Defensive programming: ensure that bit 7 remains 1 (unprogrammed, "enable external reset")
  // and bit 5 remains 0 (programmed, "SPI programming enabled"), or we might wind up bricking 
  // the device
  b |= (1 << 7);  // Enable external reset, always
  b &= ~(1 << 5); // Enable SPI programming

  spiTransaction(0xAC, 0xA8, 0x00, b);
  
  leaveProgrammingMode();
  return true;
}

bool Programmer::setLowFuse(uint8_t b)
{
  if (!enterProgrammingMode())
    return false;

  spiTransaction(0xAC, 0xA0, 0x00, b);
  
  leaveProgrammingMode();
  return true;
}

bool Programmer::eraseTarget()
{
  if (!enterProgrammingMode())
    return false;
    
  spiTransaction(0xAC, 0x80, 0x00, 0x00);
  delay(18); // 9 should be sufficient, but had trouble; 18 (arbitrarily doubling it) seems fine

  leaveProgrammingMode();

  return true;
}
