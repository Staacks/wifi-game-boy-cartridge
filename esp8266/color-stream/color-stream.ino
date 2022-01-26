#include <ESP8266WiFi.h>

#include "secrets.h"
const char* ssid = SECRET_SSID;
const char* pass = SECRET_PASS;

WiFiServer server(4242);
WiFiClient client;

const uint16_t dataMask = 0xf00f;
const byte pinEspRd = 4;
const uint16_t pinEspRdMask = 1 << pinEspRd;
const byte pinEspClk = 5;
const uint16_t pinEspClkMask = 1 << pinEspClk;
const byte pinLED = 16;

uint8_t * outbufferFront;
uint8_t * outbufferBack;
volatile int outFrontIndex = 0;
volatile int outBackIndex = 0;
volatile bool outBackFilled = false;
const int bufferSizeDMG = 360*16;
const int bufferSizeCGB = 6184;

volatile uint8_t joypad = 0x00;
uint8_t lastJoypad = 0xff;

volatile bool cgb = false; //True if we have been notified that this is a Game Boy Color
volatile int nextBlockEnd = 16; //In CGB mode, we have varying block sizes. This keeps track of when the next one ends.

void setup() {
  outbufferFront = (uint8_t*)malloc(bufferSizeCGB);
  outbufferBack = (uint8_t*)malloc(bufferSizeCGB);
  for (int i = 0; i < bufferSizeCGB; i++)
    outbufferFront[i] = 0;

  //Setup pins
  GPEC = dataMask; //Disable outputs
  for (int i = 0; i < 16; i++) {
    if ((0x0001 << i) & dataMask) {
      GPF(i) = GPFFS(GPFFS_GPIO(i));//Set mode to GPIO
      GPC(i) = (GPC(i) & (0xF << GPCI)); //SOURCE(GPIO) | DRIVER(NORMAL!!!) | INT_TYPE(UNCHANGED) | WAKEUP_ENABLE(DISABLED)
      //Note: We are not using pinMode(i, INPUT) here, because this sets the open-drain drive mode. This is irrelevant for inputs, but as we need to switch to output mode with normal drive mode really fast, we do not want to another register for each pin.
    }
  }

  //Other pins with helper functions for readability.
  pinMode(pinEspRd, INPUT);
  pinMode(pinEspClk, INPUT);
  pinMode(pinLED, OUTPUT);


  //Interrupts
  //DO NOT TOUCH espRd INTERRUPTS!
  //Especially the logic of enabling the GPIO output in response to the interrupts
  //must match the behavior of the Game Boy program. The Game Boy may never try to
  //write to the ESP while the GPIO output is enabled or we will get bus contention
  //(i.e. a short circuit that might damage something)
  attachInterrupt(digitalPinToInterrupt(pinEspClk), espClkFall, FALLING);
  attachInterrupt(digitalPinToInterrupt(pinEspRd), espRdFall, FALLING);
}

//Get CPU cycle, based on https://sub.nanona.fi/esp8266/timing-and-ticks.html
static inline int32_t asm_ccount(void) {
  int32_t r;
  asm volatile ("rsr %0, ccount" : "=r"(r));
  return r;
}

//Variables controlling interrupt logic
volatile int32_t lastRd = 0;  //Number of clock cycles on the last ESPRD fall used to reject triggers of espClkFall while reading espRdFall is doing its thing.
volatile int32_t lastClk = 0; //Number of clock cycles on the last ESPCLK fall used to reject the trigger on the second event.
volatile boolean readingFromESP = false;

//DO NOT TOUCH INTERRUPTS unless you know what you are doing! (see above)
//When the ESPRD input rises, the Game Boy wants to read from the ESP
ICACHE_RAM_ATTR void espRdFall() {
  int32_t clk = asm_ccount();
  if (readingFromESP)
    return;
  if ((uint32_t)(clk - lastRd) < 1600) //Reject if (dt < 20µs), the ESP8266 has an 80MHz clock, so one cycle is 12.5ns
    //We don't want to trigger again on the second write
    return;
  lastRd = clk;
  
  readingFromESP = true;
  GPES = dataMask; //Enable all data pins

  do {
    //Wait for falling edge. (No need for a timeout. ESPRD only stays HIGH for a few 100ns.
    while (GPI & pinEspRdMask) {
      //NOOP
    }
    
    //Setnew value
    uint8_t data = outbufferFront[outFrontIndex++];
    uint32_t pinData = ((data & 0x00f0) << 8) | (data & 0x0f);
    GPOS = pinData & dataMask;
    GPOC = ~pinData & dataMask;
    
    //Wait for rising edge with a timeout, so this does not block forever if we miss it
    clk = asm_ccount();
    while ((uint32_t)(asm_ccount() - clk) < 1400) {
      if (GPI & pinEspRdMask)
        break;
    }
  } while(outFrontIndex < nextBlockEnd); //We have sent one tile if the index is a multiple of 16 again.

  GPEC = dataMask; //Disable all data pins

  //Update next block end
  if (cgb) {
    //We need to send
    //9x [20x tile attributes (1B), 20x tile data (16B) in pairs] = 9x 340B = 3060B
    //8x color palette (8B)                                                 =   64B
    //9x [20x tile attributes (1B), 20x tile data (16B) in pairs] = 9x 340B = 3060B
    if (outFrontIndex >= 3060 && outFrontIndex < 3060 + 64) {
      //8x color palette (8B)
      nextBlockEnd = outFrontIndex + 64;
    } else {
      int relIndex;
      //9x [20x tile attributes (1B), 20x tile data (16B)]
      if (outFrontIndex >= 3060 + 64)
        relIndex = (outFrontIndex - 3060 - 64) % 340;
      else
        relIndex = outFrontIndex % 340;
  
      if (relIndex >= 20)
        nextBlockEnd = outFrontIndex + 32;
      else
        nextBlockEnd = outFrontIndex + 20;
      
    }
  } else {
    nextBlockEnd = outFrontIndex + 16;
  }

  lastRd = asm_ccount();
  readingFromESP = false;
}

//DO NOT TOUCH INTERRUPTS unless you know what you are doing! (see above)
//The ESPCLK falls in the middle of a R/W operation. If the Game Boy is
//reading from the ESP, this is handled in espRdRise, which sets
//readingFromESP = true. In this case, this interrupt here will be ignored.
ICACHE_RAM_ATTR void espClkFall() {
  int32_t clk = asm_ccount();
  if (readingFromESP)
    return;
  if ((uint32_t)(clk - lastRd) < 3200) //Reject if (dt < 40µs), the ESP8266 has an 80MHz clock, so one cycle is 12.5ns
    //The thing is that even starting the interrupt routine can take several µs, so if this has been triggered together with the last call to espRdFall, an unwanted call might come in still a few µs after that
    return;
  if ((uint32_t)(clk - lastClk) < 3200) //Reject if (dt < 40µs), the ESP8266 has an 80MHz clock, so one cycle is 12.5ns
    //We don't want to trigger again on the second write
    return;

  //Wait for rising edge with a timeout, so this does not block forever if we miss it
  while ((uint32_t)(asm_ccount() - clk) < 1400) {
    if (GPI & pinEspClkMask)
      break;
  }
  //Wait for falling edge. (No need for a timeout. ESPRD only stays HIGH for a few 100ns.
  while (GPI & pinEspClkMask) {
    //NOOP
  }
  uint32_t gpi = GPI; //Catch the new value

  //Only accept new data if the buffer is not already filled
  joypad = ((gpi & 0xf000) >> 8) | (gpi & 0x0f);

  outFrontIndex = 0;

  if (cgb)
    nextBlockEnd = 20;
  else
    nextBlockEnd = 16;

  if (outBackFilled) {
    uint8_t * temp = outbufferBack;
    outbufferBack = outbufferFront;
    outbufferFront = temp;
    outBackFilled = false;
  }

  lastClk = clk;
}

void maintainWifi() {
  int status = WiFi.status();
  while (status != WL_CONNECTED) {
    digitalWrite(pinLED, LOW);

    status = WiFi.begin(ssid, pass);

    int pauses = 0;
    while (status != WL_CONNECTED && pauses < 20) {
      delay(500);
      pauses++;
      if (pauses % 2 == 0)
        digitalWrite(pinLED, LOW);
      else
        digitalWrite(pinLED, HIGH);
      status = WiFi.status();
    }

    digitalWrite(pinLED, LOW);

    server.begin();
    client = server.available();
  }
}

void handleDataFromGameBoy() {
  if (lastJoypad != joypad) {
    if (joypad == 0xf0) {
      cgb = true;
      nextBlockEnd = 20;
    }
    client.write(joypad);
    lastJoypad = joypad;
  }
}

void handleDataFromTcp() {
  if (outBackFilled)
    return;

  int bufferSize = cgb ? bufferSizeCGB : bufferSizeDMG;
  int minAvail = min(32, bufferSize - outBackIndex);
  while (client.available() >= minAvail) {
    client.read(outbufferBack + outBackIndex, minAvail);
    outBackIndex += minAvail;
    if (outBackIndex >= bufferSize) {
      outBackIndex = 0;
      outBackFilled = true;
      return;
    }
    minAvail = min(32, bufferSize - outBackIndex);
  }
}

void loop() {
  maintainWifi();

  if (!client) {
    client = server.available();
  } else if (!client.connected()) {
    client.stop();
    client = server.available();
  }

  handleDataFromTcp();
  handleDataFromGameBoy();
}
