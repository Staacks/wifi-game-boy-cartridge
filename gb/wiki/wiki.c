#include <gb/gb.h>
#include <stdint.h>
#include <stdio.h>

unsigned char buffer[32];

const unsigned char * const fromESP = (unsigned char *)0x7ffe;
unsigned char * const toESP = (unsigned char *)0x7fff;

void readFromESP(unsigned char * result) {
    uint8_t i = 0;
    result[0] = *fromESP; //First read only triggers the read mode on the ESP. The result is garbage and will be overwriten in the first run of the loop.
    do {
        int burn_a_few_cycles = i*i*i;
        result[i] = *fromESP;
    } while (result[i++] != '\0');
}

void sendToESP(unsigned char * s) {
    unsigned char * i = s;
    do {
        uint8_t burn_a_few_cycles;
        //We need to send each character twice as the first write triggers the interrupt on the ESP, which will then wait and catch the second write
        *toESP = *i;
        for (burn_a_few_cycles = 0; burn_a_few_cycles < 1; burn_a_few_cycles++) {
            //NOOP - otherwise we will be too fast for the ESP to catch the second write
        }
        *toESP = *i;
        for (burn_a_few_cycles = 0; burn_a_few_cycles < 3; burn_a_few_cycles++) {
            //NOOP - a little longer as the ESP needs to store the previous value
        }
    } while (*(i++) != '\0');
}

//Helper - like puts but without the newline
void print(const char * s) {
    while (*s)
        putchar(*s++);
}

void main(void) {
    print("\n");
    print("there.oughta.be\n");
    print("  /a/wifi-game-boy\n");
    print("          -cartridge");
    print("\n");
    print("Welcome to\n");
    print("\n");
    print("\n");
    print("  == WIKIPEDIA ==\n");
    print("\n");
    print("\n");
    print("Press start to enter");
    print("a request.\n");
    print("\n");

    while(1) {
        unsigned char buffer[256];
        readFromESP(buffer);
        if (buffer[0] != '\0') {
            print(buffer);
        }

        uint8_t jp = joypad();
        if (jp & J_A)
            sendToESP("+");
        else if (jp & J_START) {
            print("\n\n\n");
            print("--------------------");
            gets(buffer);
            print("--------------------");
            sendToESP(buffer);
        }

        delay(100);
    }
}
