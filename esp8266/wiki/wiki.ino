#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h" 
const char* ssid = SECRET_SSID;
const char* pass = SECRET_PASS;

const char * apiURL = "https://en.wikipedia.org/w/api.php";

const uint16_t dataMask = 0xf00f;
const byte pinEspRd = 4;
const uint16_t pinEspRdMask = 1 << pinEspRd;
const byte pinEspClk = 5;
const uint16_t pinEspClkMask = 1 << pinEspClk;
const byte pinLED = 16;

char outbuffer[256];
volatile byte outindex;
volatile bool outFilled;
char inbuffer[256];
volatile byte inindex;
volatile bool inFilled;

char resultData[4096];
char * resultPosition;
boolean resultPending = false;

char serialBuffer[256];
byte serialIndex = 0;

void setup() {  
  delay(500); //Just to avoid anything triggering during the somewhat "electrically turbulent" start.
  
  //Init buffers to terminal character
  outbuffer[0] = '\0';
  outindex = 0;
  outFilled = false;
  inbuffer[0] = '\0';
  inindex = 0;
  inFilled = false;
  
  //Setup pins
  for (int i = 0; i < 16; i++) {
    if ((0x0001 << i) & dataMask) {
      pinMode(i, INPUT_PULLUP);
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
//We are way to slow to respond to the first read request and the Game Boy
//will have to drop the first response as it will be undefined. However, we
//will use the read request as a trigger to output the next character and
//the Game Boy just needs to read slow enough so the ESP can keep up.
//When we set \0 as output, this is the last character of our data stream,
//but we disable the output when the rd line rises after that, because that
//is when the Game Boy has seen this \0 and also expects us to stop.
ICACHE_RAM_ATTR void espRdFall() {
  lastRd = asm_ccount();
  if (!readingFromESP) {
    //First read attempt. Enable output and start with first character
    readingFromESP = true;
    GPES = dataMask; //Enable all data pins
  }
  if (outFilled) {
    //Send current character and move to the next one
    char data = outbuffer[outindex++];
    uint32_t pinData = ((data & 0x00f0) << 8) | (data & 0x0f);
    GPOS = pinData & dataMask;
    GPOC = ~pinData & dataMask;
    if (data == '\0') {
      outFilled = false;
    }
  } else if (outindex == 0) {
    //There is no data to send, but we need to send \0 anyways, so the Game Boy knows that we are done.
    GPOC = dataMask;
    outindex++;
  } else {
    //The \0 has been sent last time. Let's turn off the output and end this transfer
    GPEC = dataMask; //Disable all data pins
    readingFromESP = false;
    outindex = 0;
    return;
  }
}

//DO NOT TOUCH INTERRUPTS unless you know what you are doing! (see above)
//The ESPCLK falls in the middle of a R/W operation. If the Game Boy is
//reading from the ESP, this is handled in espRdRise, which sets
//readingFromESP = true. In this case, this interrupt here will be ignored.
//If the Game Boy tries to write to the ESP, however, everything becomes a
//bit more tricky as the interrupt delay is a multiple of a single memory
//access cycle. Therefore, the Game Boy will do three consecutive write
//attempts at equidistant intervals. We will use the first two to figure out
//the timing of the write attempts (which will include our own interrupt
//delay), so we can try to exactly intercept the third attempt.
//We also need to ignore too fast interrupt triggers, because there is a
//short additional spike of ESPCLK at the end of each cycle (although I
//doubt that it can trigger the interrupt) and we will not try to directly
//catch subsequent write attempts as we can be lucky if this method is
//precise enough to capture the third write :)
ICACHE_RAM_ATTR void espClkFall() {
  int32_t clk = asm_ccount();
  if (readingFromESP)
    return;
  if ((uint32_t)(clk - lastRd) < 800) //Reject if (dt < 10µs), the ESP8266 has an 80MHz clock, so one cycle is 12.5ns
    //The thing is that even starting the interrupt routine can take several µs, so if this has been triggered together with the last call to espRdFall, an unwanted call might come in still a few µs after that
    return;
  if ((uint32_t)(clk - lastClk) < 2400) //Reject if (dt < 30µs), the ESP8266 has an 80MHz clock, so one cycle is 12.5ns
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
  
  if (!inFilled) {
    //Only accept new data if the buffer is not already filled
    char c = ((gpi & 0xf000) >> 8) | (gpi & 0x0f);
    inbuffer[inindex] = c;
    if (c == '\0') {
      if (inindex > 0)
        inFilled = true;
    } else
      inindex++;
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

    if (!outFilled && outindex == 0) {
      String ip = WiFi.localIP().toString();
      outbuffer[outindex++] = 'I';
      outbuffer[outindex++] = 'P';
      outbuffer[outindex++] = ':';
      outbuffer[outindex++] = ' ';
      for (uint8_t i = 0; i < ip.length(); i++) {
        outbuffer[outindex++] = ip[i];
      }
      outbuffer[outindex++] = '\0';
      outindex = 0;
      outFilled = true;
    }
  }
}

void handleDataFromGameBoy() {
  if (inFilled) {
    if (strcmp(inbuffer, "+") == 0) {
      if (resultPending) {
        strncpy(outbuffer, resultPosition, 241);
        if (outbuffer[240] != '\0') {
          resultPosition += 240;
          outbuffer[240] = '\0';
        } else
          resultPending = false;
        outindex = 0;
        outFilled = true;
      }
    } else {
      char url[256];

      //Replace spaces with underscores
      for (char * i = inbuffer; *i != '\0'; i++)
        if (*i == ' ')
          *i = '_';
          
      strcpy(url, apiURL);
      strcat(url, "?action=query&format=json&titles=");
      strcat(url, inbuffer);
      strcat(url, "&prop=extracts&exintro&explaintext");
  
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.useHTTP10(true);
      http.begin(client, url);
      int httpCode = http.GET();
      if (httpCode == 200) {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (error) {
          strcpy(outbuffer, "JSON error: ");
          strcat(outbuffer, error.c_str());
          outindex = 0;
          outFilled = true;
        } else {
          JsonObject pages = doc["query"]["pages"];
          JsonObject::iterator it = pages.begin();
          if (it->key() == "-1")
            strcpy(outbuffer, "Not found.");
          else {
            strncpy(resultData, it->value()["extract"], 4096);
            resultPending = true;
            resultPosition = resultData;
            strncpy(outbuffer,resultPosition, 241); //240+null, use multiple of 20 to align with screen width
            if (outbuffer[240] != '\0') {
              resultPosition += 240;
              outbuffer[240] = '\0';
            } else
              resultPending = false;
          }
          outindex = 0;
          outFilled = true;
        }
      } else {
        char code[4];
        itoa(httpCode, code, 10);
        strcpy(outbuffer, "Error: ");
        strcat(outbuffer, code);
        outindex = 0;
        outFilled = true;
      }
      http.end();
    }

    inindex = 0;
    inFilled = false;
  }
}

void loop() {
  maintainWifi();
  handleDataFromGameBoy();
}
