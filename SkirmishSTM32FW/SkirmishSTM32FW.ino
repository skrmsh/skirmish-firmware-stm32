#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <math.h>

#include <IRremote.hpp>

// Pin and other constant values
#define PIN_LED PA0
#define PIN_IR_IN PA3
#define PIN_ESP_IRQ PA7
#define LED_COUNT 8

// MCFG pins (used to set this controllers i2c address)
// The solder jumpers should be configured with one of
// this values:
#define MCFG_PHASER 0b000
#define MCFG_CHEST 0b001
#define MCFG_BACK 0b010
#define MCFG_SH_L 0b011
#define MCFG_SH_R 0b100
#define MCFG_HEAD 0b101
#define MCFG_HP 0b110
#define MCFG_UNDEF 0b111

#define PIN_MCFG0 PA4
#define PIN_MCFG1 PA5
#define PIN_MCFG2 PA6

// I2C Pins
#define PIN_I2C_SDA PA12
#define PIN_I2C_SCL PA11

#define I2C_ADDR_OFFSET 0x50  // | MCFG value

// I2C Commands
#define HP_CMD_SELECT_ANIM 0x01
#define HP_CMD_SET_ANIM_SPEED 0x02
#define HP_CMD_SET_COLOR 0x03
#define HP_CMD_TIMESYNC 0x04

// Animations
#define ANIM_SOLID 0
#define ANIM_BLINK 1
#define ANIM_ROTATE 2
#define ANIM_BREATHE 3

// Variables
uint8_t mcfg = 0x00;
uint8_t i2cAddr = 0x00;

uint8_t selectedCmd = 0;           // selected I2C Command
uint8_t paramIndex = 0;            // I2C parameter index
unsigned long lastI2CReceive = 0;  // Last time i2c data was received

uint8_t anim = ANIM_SOLID;  // selected animation
uint8_t speed = 1;          // animation speed
uint8_t r = 0;              // red color
uint8_t g = 0;              // green color
uint8_t b = 0;              // blue color

Adafruit_NeoPixel pixels(LED_COUNT, PIN_LED, NEO_GRB + NEO_KHZ800);

IRrecv irrecv(PIN_IR_IN);
decode_results results;

void setup() {
    // Set pin modes
    pinMode(PIN_IR_IN, INPUT);
    pinMode(PIN_MCFG0, INPUT_PULLUP);
    pinMode(PIN_MCFG1, INPUT_PULLUP);
    pinMode(PIN_MCFG2, INPUT_PULLUP);

    // Initialising WS2812 leds
    pixels.begin();
    for (int i = 0; i < LED_COUNT; i++) {  // For each pixel...
        pixels.setPixelColor(i, pixels.Color(1, 1, 1));
    }
    pixels.show();  // Send the updated pixel colors to the hardware.

    // Read Module Config from IO
    mcfg = (!digitalRead(PIN_MCFG2) << 2) | (!digitalRead(PIN_MCFG1) << 1) |
           !digitalRead(PIN_MCFG0);
    // Calculate the I2C address
    i2cAddr = I2C_ADDR_OFFSET | mcfg;

    // IRQ Pin mode depending on phaser / vest hitpoint
    if (mcfg == MCFG_PHASER) {
        pinMode(PIN_ESP_IRQ, OUTPUT);
        digitalWrite(PIN_ESP_IRQ, HIGH);
    } else {
        pinMode(PIN_ESP_IRQ, INPUT);  // Open-Collector mock
    }

    // Initing the Wire communication
    Wire.setSCL(PIN_I2C_SCL);
    Wire.setSDA(PIN_I2C_SDA);
    Wire.setClock(100000);
    Wire.begin(i2cAddr, true);
    Wire.onRequest(requestEvent);
    Wire.onReceive(receiveEvent);

    irrecv.enableIRIn();  // Start the receiver
}

void requestInterrupt() {
    // Phaser:
    //  Always set to high or low (irq on falling edge)
    // Other modules:
    //  Set to low or input (irq on falling edge, but requires an external
    //  pull-up resistor)
    if (mcfg == MCFG_PHASER) {
        digitalWrite(PIN_ESP_IRQ, LOW);
        delay(10);
        digitalWrite(PIN_ESP_IRQ, HIGH);
    } else {
        pinMode(PIN_ESP_IRQ, OUTPUT);
        digitalWrite(PIN_ESP_IRQ, LOW);
        delay(10);
        pinMode(PIN_ESP_IRQ, INPUT);
    }
}

unsigned long lastBlinkTime = 0;
uint8_t blinkState = 0;
uint8_t rotatePixel = 0;
float breatheFactor = 0;
float breatheDirection = 0.01;

uint32_t irRecvVal = 0;
uint8_t pid;
uint16_t sid;
uint8_t checksum;

/*
Calculate the CRC8 sum from a 24-Bit integer
*/
uint8_t calculateCRC8(uint32_t data) {
    uint8_t crc = 0xff;
    size_t i, j;
    for (i = 0; i < 3; i++) {
        crc ^= ((data >> (i * 8)) & 0xff);
        for (j = 0; j < 8; j++) {
            if ((crc & 0x80) != 0) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint32_t now;
int32_t timeOffset = 0;

void loop() {
    // Infrared receiver
    if (irrecv.decode(&results)) {
        irRecvVal = results.value;

        sid = irRecvVal & 0xffff;
        pid = (irRecvVal >> 16) & 0xff;
        checksum = (irRecvVal >> 24) & 0xff;

        if (checksum == calculateCRC8(pid << 16 | sid)) { requestInterrupt(); }
        else { irRecvVal = 0; }
    }
    irrecv.resume();

    now = millis() + timeOffset;

    // LED animations
    if (anim == ANIM_SOLID) {
        for (int i = 0; i < LED_COUNT; i++) {  // For each pixel...
            pixels.setPixelColor(i, pixels.Color(r, g, b));
        }
    }

    else if (anim == ANIM_BLINK) {
        if (now - lastBlinkTime > (speed * 10)) {
            blinkState = 1 - blinkState;
            lastBlinkTime = now;

            for (int i = 0; i < LED_COUNT; i++) {  // For each pixel...
                pixels.setPixelColor(
                    i, pixels.Color(r * blinkState, g * blinkState,
                                    b * blinkState));
            }
        }
    }

    else if (anim == ANIM_ROTATE) {
        if (now - lastBlinkTime > (speed * 10)) {
            lastBlinkTime = now;
            pixels.setPixelColor(rotatePixel, pixels.Color(0, 0, 0));
            rotatePixel = (rotatePixel + 1) % LED_COUNT;
            pixels.setPixelColor(rotatePixel, pixels.Color(r, g, b));
        }
    }

    else if (anim == ANIM_BREATHE) {
        if (now - lastBlinkTime > speed) {
            lastBlinkTime = now;
            breatheFactor += breatheDirection;
            if (breatheFactor >= 1) {
                breatheFactor = 1.0;
                breatheDirection *= -1;
            } else if (breatheFactor <= 0) {
                breatheFactor = 0.0;
                breatheDirection *= -1;
            }
            for (int i = 0; i < LED_COUNT; i++) {  // For each pixel...
                pixels.setPixelColor(
                    i, pixels.Color(r * breatheFactor, g * breatheFactor,
                                    b * breatheFactor));
            }
        }
    }

    if (irrecv.isIdle())
        pixels.show();  // Send the updated pixel colors to the hardware.
    delay(10);
}

uint8_t i2cPacket[4];

/**
 * This function writes the last received ir packet to the i2c main controller
 */
void requestEvent() {
    i2cPacket[3] = irRecvVal & 0xff;
    i2cPacket[2] = (irRecvVal >> 8) & 0xff;
    i2cPacket[1] = (irRecvVal >> 16) & 0xff;
    i2cPacket[0] = (irRecvVal >> 24) & 0xff;
    Wire.write(i2cPacket, 4);

    // Resetting received IR val -> next time the shot data
    // is requested it will return 0
    irRecvVal = 0;
}

// Temporary values
uint8_t _r = 0;  // red color
uint8_t _g = 0;  // green color
uint8_t _b = 0;  // blue color
uint32_t _syncTime = 0;

void receiveEvent(int bytes) {
    // This function is called when there is new I2C data

    uint8_t incomingByte = 0x00;  // buffer to store the current byte
    while (Wire.available()) {
        incomingByte = Wire.read();  // read i2c bus

        if (selectedCmd == 0) {          // when no command is selected
            selectedCmd = incomingByte;  // the incoming byte is the new command
            if (selectedCmd < 1 || selectedCmd > HP_CMD_TIMESYNC)
                selectedCmd = 0;
            paramIndex = 0;
        }

        else if (selectedCmd == HP_CMD_SELECT_ANIM) {
            /*
            Select animation command:
            Param: animation
            */
            if (paramIndex == 1) anim = incomingByte;
            if (paramIndex == 2) {
              selectedCmd = 0x00;
              blinkState = 0;
              rotatePixel = 0;
              breatheFactor = 0;
            }
        }

        else if (selectedCmd == HP_CMD_SET_ANIM_SPEED) {
            /*
            Set animation speed command
            Param: speed
            */
            if (paramIndex == 1) speed = incomingByte;
            if (paramIndex == 2) selectedCmd = 0x00;
        }

        else if (selectedCmd == HP_CMD_SET_COLOR) {
            /*
            Set color command:
            Params: r, g, b
            */
            if (paramIndex == 1) _r = incomingByte;
            if (paramIndex == 2) _g = incomingByte;
            if (paramIndex == 3) _b = incomingByte;
            if (paramIndex == 4) {
                r = _r;
                g = _g;
                b = _b;
                selectedCmd = 0x00;  // reset selected cmd
            }
        }

        else if (selectedCmd == HP_CMD_TIMESYNC) {
          /*
          Time synchro commad:
          Params b0, b1, b2, b3 (32-Bit integer milliseconds since start)
          */
          if (paramIndex >= 1 && paramIndex <= 4) _syncTime = (_syncTime << 8 | incomingByte);
          if (paramIndex == 4) {
            now = millis();

            timeOffset = _syncTime - now;

            // prevent duplicate execution of past events
            if (lastBlinkTime > (now+timeOffset)) {
              lastBlinkTime = (now+timeOffset) - 1;
            }

            selectedCmd = 0x00;
          }
        }

        paramIndex++;
    }
}
