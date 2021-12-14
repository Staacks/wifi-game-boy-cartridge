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

void main(void) {
    puts("");
    puts("there.oughta.be");
    puts("  /a/wifi-game-boy");
    puts("          -cartridge");
    puts("");
    puts("Serial test");
    puts("");

    while(1) {
        unsigned char buffer[256];
        readFromESP(buffer);
        if (buffer[0] != '\0') {
            puts("   --Receiving--");
            puts(buffer);
        }

        if (joypad() & J_A) {
            puts("    --Sending--");
            gets(buffer);
            sendToESP(buffer);
        }
        delay(100);
    }
}
