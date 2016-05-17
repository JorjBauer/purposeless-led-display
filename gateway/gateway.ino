/* Gateway node for purposeless LED display
 * 
 * (c) 2016 Jorj Bauer <jorj@jorj.org>
 */

#include <Arduino.h>

#include <RFM69.h>         //get it here: https://github.com/LowPowerLab/rfm69
#include <WirelessHEX69.h> //get it here: https://github.com/LowPowerLab/WirelessProgramming/tree/master/WirelessHEX69
#include <RingBuffer.h>    //get it here: https://github.com/JorjBauer/RingBuffer

// Standard RFM69 radio configuration
#define NODEID        1
#define NETWORKID     212
#define FREQUENCY     RF69_915MHZ
// ENCRYPTKEY must be 16 chars long. Or comment out for cleartext transmission.
#ifdef DEFAULTKEY
  #define ENCRYPTKEY DEFAULTKEY
#else
  #pragma message("Default encryption key not found; using compiled-in default")
  #define ENCRYPTKEY "sampleEncryptKey"
#endif
// enable this only if you're using an RFM69HW; enables high-power transmit.
//#define IS_RFM69HW  

// Constants for wireless programming
#define ACK_TIME    50                    // ACK time for Moteino programming
#define TIMEOUT     3000                  // Timeout waiting for Moteino programming
#define MAXLINE     64                    // Maximum serial line length to read.
#define MAX_PACKET_SIZE RF69_MAX_DATA_LEN // Maximum packet size on the RFM69.

// Pin definitions
#define LED         9

// Globals
RFM69 radio;

// Buffers
RingBuffer stashedSerialData(MAX_PACKET_SIZE+2); // ... +2 for a destination node and packet length
byte radioBuffer[MAX_PACKET_SIZE];               // Linear buffer, used to prepare packets for sending
int radioBufferPtr = 0;

void setup() {
  Serial.begin(115200);
  
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
#ifdef ENCRYPTKEY
  radio.encrypt(ENCRYPTKEY);
#endif
#ifdef IS_RFM69HW
  radio.setHighPower(); //only for RFM69HW!
#endif
  
  Serial.println("Hello.");
}

// Blocking read from the serial interface.
byte readSerial()
{
  while (Serial.available() <= 0) ;
  return Serial.read();
}

// Look for an enact the LowPowerLabs Moteino wireless programming mode.
void wirelessProgrammingMode()
{
  char input[MAXLINE + 1]; // Serial buffer while reading programming data. +1 for null terminator
  byte targetID=0;         // Programming targetID.

  // We started with 'TO'. If the next character is ':' then this is a programming request.
  byte b = readSerial();
  if (b == ':') {
    // Begin wireless programming mode - request node
    targetID = 0;
    b = readSerial();
    while (b != 10 && b != 13) {
      if (b >= '0' && b <= '9') {
        targetID *= 10;
        targetID += (b - '0');
      } else {
        Serial.println("aborting");
        return;
      }
      b = readSerial();
    }
    
    Serial.print("TO:");
    Serial.print(targetID);
    Serial.println(":OK");
    if (targetID <= 0) {
      // Abort.
      Serial.print(input);
      Serial.println(":INV");
      return;
    }

    /* From this point on, we're basically shuttling data back and
     * forth from the serial input to the wireless output, and vice
     * versa.
     */
    while (1) {
      byte inputLen = readSerialLine(input, 10, MAXLINE, 100);
      if (CheckForSerialHEX((byte*)input, inputLen, radio, targetID, TIMEOUT, ACK_TIME, true)) 
              return;
      
      if (radio.receiveDone())
      {
        for (byte i = 0; i < radio.DATALEN; i++)
          Serial.print((char)radio.DATA[i]);
        
        if (radio.ACK_REQUESTED)
          radio.sendACK();
        
        Serial.println();
      }
    }
  }
}

void loop() {
  // if data is incoming on the software serial side, then read it for later processing.
  if (Serial.available()) {
    byte b = Serial.read();
    stashedSerialData.addByte(b);

    /* If we are in the first two bytes and it's a "TO", then we don't
     * echo those immediately. That means that we can't address node
     * 84 ("T"). So, as a short cut, we won't echo the characters back
     * out the serial port if the data begins with a 'T'. Meh, small
     * price to pay...
     */

    if (stashedSerialData.peek(0) != 'T') {
      Serial.write(b);
    }
  }

  /* If there's data on the radio, then send that out the serial port
   * too. Yes, this means that if something is trying to send at the
   * same time that a reply comes in, there's a race condition.
   */

  if (radio.receiveDone()){
    for (int i=0; i<radio.DATALEN; i++) {
      Serial.write(radio.DATA[i]);
    }
  }
    
  /* If there's software serial data to send, then send it on the radio.
   * Serial data incoming is expected to be in a particular form:
   * 
   *   byte nodeID
   *   byte packetLength
   *   byte <packetLength bytes>
   */

  // See if we have a full packet ready to send out the radio:
  int count = stashedSerialData.count();
  if (count >= 2) {
    byte packetsize = stashedSerialData.peek(1);
    
    /* Special case: this might be a wireless programming request,
     * which begins with "TO:". Since the protocol expects two bytes
     * of header, we see if the two bytes we've received match those;
     * and if so, we hand off processing to the
     * wirelessProgrammingMode function. When we return we'll clear
     * any pending serial data before continuing.
     */
    if (stashedSerialData.peek(0) == 'T' && stashedSerialData.peek(1) == 'O') {
      wirelessProgrammingMode();
      stashedSerialData.clear();
      return;
    }

    if (packetsize > MAX_PACKET_SIZE) { // Overflow? flush the buffer.
      stashedSerialData.clear();
      Serial.println("overflow");
      return;
    }

    // Is the packet complete (have we read all the data for this packet)?
    if (count == packetsize + 2) { // +2 for header bytes
      // Once we've read the whole packet, send it as a single packet.
      radioBufferPtr = 0;
      byte destNode = stashedSerialData.consumeByte();
      stashedSerialData.consumeByte(); // packet size

      /* Collect the data to send in one distinct buffer (they are
       * packets, not necessarily bytestream data)
       */
      while (packetsize && stashedSerialData.hasData()) {
        byte c = stashedSerialData.consumeByte();
        radioBuffer[radioBufferPtr] = c;
        radioBufferPtr++;
        packetsize--;
      }

      if (radio.sendWithRetry(destNode, radioBuffer, radioBufferPtr, 10, 100)) { // 10 retry attempts, 100mS between
      	/* If we get an ACK, we'll return (to the caller, via the
	 * serial port) the data that was in the ACK:
	 *
	 * ACK<byte-length><ack-data>
	 */
        if (radio.DATALEN) {
          Serial.print("ACK");
          Serial.write(radio.DATALEN);
          for (int i=0; i<radio.DATALEN; i++) {
            Serial.write(radio.DATA[i]);
          }
        } else {
	  // There's no data in the ACK, so we return "ACK\0"
          Serial.print("ACK");
          Serial.write(0);
        }
      } else {
	// Failed to get an ACK in time. Tell the sender that we got nothing.
        Serial.print("NAK");
      }
    }
  }
}
