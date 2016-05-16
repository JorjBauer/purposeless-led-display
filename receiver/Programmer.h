#include <Arduino.h>
#include "bbspi.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define FROMHEX(x) (((x) >= '0' && (x) <= '9' ? ((x)-'0') : \
( (x) >= 'A' && (x) <= 'Z' ? ((x) - 'A' + 10) : \
  ( (x) >= 'a' && (x) <= 'z' ? ((x) - 'a' + 10) : 0 )\
)))
#define ONEBYTE(l,r) ((FROMHEX(l) << 4) | FROMHEX(r))

enum ProgrammerStatus {
  PS_Invalid = -1,
  PS_OK = 0,
  PS_SyntaxError,
  PS_InvalidPacket,
  PS_FlashComplete
};

class Programmer {
 public:
  Programmer(byte rst, byte mosi, byte miso, byte sck);
  ~Programmer();

  ProgrammerStatus parseAndStoreDataFromRadio(uint8_t len, uint8_t *data);
  uint8_t getHighFuse();
  uint8_t getLowFuse();
  bool setHighFuse(uint8_t b);
  bool setLowFuse(uint8_t b);
  bool eraseTarget();

 protected:
  uint8_t spiTransaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
  void commit(int page);

  bool enterProgrammingMode();
  void leaveProgrammingMode();
  
 private:
  byte rst, mosi, miso, sck;

  BitBangedSPI *bbSPI;

  uint32_t lastProgrammedPage;
  bool programmingStarted;
};
