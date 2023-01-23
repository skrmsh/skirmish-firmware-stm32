#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <IRremote.hpp>

// Pin and other constant values
#define PIN_LED PA0
#define PIN_IR_IN PA3
#define PIN_ESP_IRQ PA7
#define LED_COUNT 8

#define PIN_MCFG0 PA4
#define PIN_MCFG1 PA5
#define PIN_MCFG2 PA6

#define PIN_I2C_SDA PA12
#define PIN_I2C_SCL PA11

#define I2C_ADDR_OFFSET 0x50 // | MCFG value

// I2C Commands
#define HP_CMD_SELECT_ANIM    0x01
#define HP_CMD_SET_ANIM_SPEED 0x02
#define HP_CMD_SET_COLOR      0x03

// Animations
#define ANIM_SOLID   0
#define ANIM_BLINK   1
#define ANIM_ROTATE  2
#define ANIM_BREATHE 3

// Variables
uint8_t mcfg = 0x00;
uint8_t i2cAddr = 0x00;

uint8_t selectedCmd = 0; // selected I2C Command
uint8_t paramIndex = 0; // I2C parameter index
unsigned long lastI2CReceive = 0; // Last time i2c data was received

uint8_t anim = ANIM_SOLID; // selected animation
uint8_t speed = 1; // animation speed
uint8_t r = 0; // red color
uint8_t g = 0; // green color
uint8_t b = 0; // blue color

Adafruit_NeoPixel pixels(LED_COUNT, PIN_LED, NEO_GRB + NEO_KHZ800);

IRrecv irrecv(PIN_IR_IN);
decode_results results;


void setup() {

  // Set pin modes
  pinMode(PIN_IR_IN, INPUT);
  pinMode(PIN_MCFG0, INPUT);
  pinMode(PIN_MCFG1, INPUT);
  pinMode(PIN_MCFG2, INPUT);
  pinMode(PIN_ESP_IRQ, OUTPUT);
  digitalWrite(PIN_ESP_IRQ, HIGH);

  // Initialising WS2812 leds
  pixels.begin();
  for(int i=0; i<LED_COUNT; i++) { // For each pixel...
    pixels.setPixelColor(i, pixels.Color(1, 1, 1));
  }
  pixels.show();   // Send the updated pixel colors to the hardware.

  // Read Module Config from IO
  mcfg = (digitalRead(PIN_MCFG0) << 2) | (digitalRead(PIN_MCFG1) << 1) | digitalRead(PIN_MCFG0);
  // Calculate the I2C address
  i2cAddr = I2C_ADDR_OFFSET | mcfg;

  // Initing the Wire communication
  Wire.setSCL(PIN_I2C_SCL);
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setClock(100000);
  Wire.begin(i2cAddr);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent);

  irrecv.enableIRIn(); // Start the receiver

}

void requestInterrupt() {
  digitalWrite(PIN_ESP_IRQ, LOW);
  delay(10);
  digitalWrite(PIN_ESP_IRQ, HIGH);
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

void loop() {

  // Infrared receiver
  if (irrecv.decode(&results)) {
    irRecvVal = results.value;

    sid = irRecvVal & 0xffff;
    pid = (irRecvVal >> 16) & 0xff;
    checksum = (irRecvVal >> 24) & 0xff;

    if (checksum == (pid << 16 | sid) % 0xff )
      requestInterrupt();

  }
  irrecv.resume();

  // LED animations
  if (anim == ANIM_SOLID) {
    for(int i=0; i<LED_COUNT; i++) { // For each pixel...
      pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
  }

  else if (anim == ANIM_BLINK) {
    if (millis() - lastBlinkTime > (speed * 10)) {
      blinkState = 1 - blinkState;
      lastBlinkTime = millis();

      for(int i=0; i<LED_COUNT; i++) { // For each pixel...
        pixels.setPixelColor(i, pixels.Color(r * blinkState, g * blinkState, b * blinkState));
      }
    }
  }

  else if (anim == ANIM_ROTATE) {
    if (millis() - lastBlinkTime > (speed * 10)) {
      lastBlinkTime = millis();
      pixels.setPixelColor(rotatePixel, pixels.Color(0, 0, 0));
      rotatePixel = (rotatePixel + 1) % LED_COUNT;
      pixels.setPixelColor(rotatePixel, pixels.Color(r, g, b));
    }
  }

  else if (anim == ANIM_BREATHE) {
    if (millis() - lastBlinkTime > speed) {
      lastBlinkTime = millis();
      breatheFactor += breatheDirection;
      if (breatheFactor >= 1) {
        breatheFactor = 1.0;
        breatheDirection *= -1;
      }
      else if (breatheFactor <= 0) {
        breatheFactor = 0.0;
        breatheDirection *= -1;
      }
      for(int i=0; i<LED_COUNT; i++) { // For each pixel...
        pixels.setPixelColor(i, pixels.Color(r * breatheFactor, g * breatheFactor, b * breatheFactor));
      }
    }
  }

  if (irrecv.isIdle()) pixels.show();   // Send the updated pixel colors to the hardware.
  delay(10);
}

uint8_t i2cPacket[4];

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
uint8_t _r = 0; // red color
uint8_t _g = 0; // green color
uint8_t _b = 0; // blue color

void receiveEvent(int bytes) {
  // This function is called when there is new I2C data
  
  uint8_t incomingByte = 0x00; // buffer to store the current byte
  while (Wire.available()) {
    incomingByte = Wire.read(); // read i2c bus

    if (selectedCmd == 0) { // when no command is selected
      selectedCmd = incomingByte; // the incoming byte is the new command
      if (selectedCmd < 1 || selectedCmd > HP_CMD_SET_COLOR) selectedCmd = 0;
      paramIndex = 0;
    }

    else if (selectedCmd == HP_CMD_SELECT_ANIM) {
      /*
      Select animation command:
      Param: animation
      */
      if (paramIndex == 1) anim = incomingByte;
      if (paramIndex == 2) selectedCmd = 0x00;
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
        r = _r; g = _g; b = _b;
        selectedCmd = 0x00; // reset selected cmd
      }
    }

    paramIndex ++;
  }
  
}