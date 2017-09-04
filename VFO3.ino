#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DebouncedSwitch.h>
#include <RotaryEncoder.h>
#include <ClickDetector.h>
#include <Utils.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

#define PIN_D2 2
#define PIN_D3 3
#define PIN_D4 4

enum Mode { VFO, BFO };

Mode mode = VFO;

DebouncedSwitch db2(3L);
DebouncedSwitch db3(3L);
DebouncedSwitch db4(3L);

RotaryEncoder renc(&db2,&db3);
ClickDetector cd4(&db4);

unsigned long vfoFreq = 14000000;
unsigned long bfoFreq = 7000000;

unsigned long stepMenu[] = { 500, 100, 50, 1000 };
uint8_t stepIndex = 0;

unsigned long getMH(unsigned long f) {
  return f / 1000000L;
}

unsigned long getKH(unsigned long f) {
  return (f / 1000L) % 1000L;
}

unsigned long getH(unsigned long f) {
  return f % 1000L;
}

void updateDisplay() {
  
  int rowHeight = 16;
  int startX = 10;
  int y = 17;

  // Cursor
  int c0 = 0;
  int c1 = 0;
  if (mode == VFO) {
    c0 = 1;
  } else {
    c1 = 1;
  }  
  display.fillRect(0,16,2,rowHeight,c0);
  display.fillRect(0,32,2,rowHeight,c1);

  display.setTextSize(2);
  display.setTextColor(WHITE);
  char buf[4];

  // VFO
  display.fillRect(startX,y,display.width() - startX,rowHeight,0);

  display.setCursor(startX,y);
  display.print(getMH(vfoFreq)); 

  display.setCursor(startX + 30,y);
  sprintf(buf,"%03lu",getKH(vfoFreq));
  display.print(buf);

  display.setCursor(startX + 70,y);
  sprintf(buf,"%03lu",getH(vfoFreq));
  display.print(buf);

  // BFO
  y = 34;
  display.fillRect(startX,y,display.width() - startX,rowHeight,0);

  display.setCursor(startX,y);
  display.print(getMH(bfoFreq)); 

  display.setCursor(startX + 30,y);
  sprintf(buf,"%03lu",getKH(bfoFreq));
  display.print(buf);

  display.setCursor(startX + 70,y);
  sprintf(buf,"%03lu",getH(bfoFreq));
  display.print(buf);

  // Step
  display.fillRect(0,55,display.width(),8,0);
  display.setTextSize(0);
  display.setCursor(startX,55);
  display.print(stepMenu[stepIndex]);
}

void setup() {
  
  Serial.begin(9600);
  delay(500);
  Serial.println("KC1FSZ VFO 3");

  pinMode(PIN_D2,INPUT_PULLUP);
  pinMode(PIN_D3,INPUT_PULLUP);
  pinMode(PIN_D4,INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC,SSD1306_I2C_ADDRESS);

  display.clearDisplay();

  display.setTextSize(0);
  display.setTextColor(WHITE);
  display.println("KC1FSZ VFO 3");
  display.drawLine(0,15,display.width(),15,WHITE);

  // Pull values from EEPROM
  vfoFreq = Utils::eepromReadLong(0);
  bfoFreq = Utils::eepromReadLong(4);
  stepIndex = EEPROM.read(8);

  // Initial display render
  updateDisplay();

  display.display();
}

void loop() {
    
  int c2 = digitalRead(PIN_D2);
  int c3 = digitalRead(PIN_D3);

  db2.loadSample(c2 == 0);
  db3.loadSample(c3 == 0);
  db4.loadSample(digitalRead(PIN_D4) == 0);
  
  long mult = renc.getIncrement();
  long clickDuration = cd4.getClickDuration();
  boolean displayDirty = false;
  
  if (mult != 0) {
    long step = mult * stepMenu[stepIndex];
    if (mode == VFO) {
      vfoFreq += step;
    } else {
      bfoFreq += step;
    }
    displayDirty = true;
  }

  // Save frequencies in EEPROM
  if (clickDuration > 5000) {
    Utils::eepromWriteLong(0,vfoFreq);
    Utils::eepromWriteLong(4,bfoFreq);
    EEPROM.write(8,stepIndex);
  }
  else if (clickDuration > 750) {
    if (mode == VFO) {
      mode = BFO;
    } else {
      mode = VFO;
    } 
    displayDirty = true;
  } 
  else if (clickDuration > 0) {
    if (++stepIndex > 3) {
      stepIndex = 0;
    } 
    displayDirty = true;
  }

  if (displayDirty) {
    updateDisplay();
    display.display();
  } 
}

