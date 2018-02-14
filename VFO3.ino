#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// The Etherkit library
#include <si5351.h>
#include <DebouncedSwitch.h>
#include <RotaryEncoder.h>
#include <ClickDetector.h>
#include <Utils.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

Si5351 si5351;

#define PIN_D2 2
#define PIN_D3 3
#define PIN_D4 4

enum Mode { VFO, VFO_OFFSET, BFO, CAL };
const char* modeTitles[] = { "VFO", "VFO+", "BFO", "CAL" };
Mode mode = VFO;

const unsigned long stepMenu[] = { 500, 100, 10, 1, 1000000, 100000, 10000, 1000 };
const char* stepMenuTitles[] = { "500 Hz", "100 Hz", "10 Hz", "1 Hz", "1 MHz", "100 kHz", "10 kHz", "1 kHz" };
const uint8_t maxStepIndex = 7;
uint8_t stepIndex = 0;

DebouncedSwitch db2(3L);
DebouncedSwitch db3(3L);
DebouncedSwitch db4(3L);
RotaryEncoder renc(&db2,&db3);
ClickDetector cd4(&db4);

unsigned long vfoFreq = 7000000;
long vfoOffsetFreq = 11998000;
unsigned long bfoFreq = 11998000;
long calPpm = 0;

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

  // Mode
  int modeX = 85;
  display.fillRect(modeX,0,45,8,0);
  display.setTextSize(0);
  display.setTextColor(WHITE);
  display.setCursor(modeX,0);
  display.print(modeTitles[(int)mode]);
    
  display.setTextSize(2);
  display.setTextColor(WHITE);
  char buf[4];

  // Clear frequency
  display.fillRect(startX,y,display.width() - startX,rowHeight,0);
  
  // Render frequency
  unsigned long f = 0;
  boolean neg = false;  
  if (mode == VFO) {
    f = vfoFreq;
  } else if (mode == VFO_OFFSET) {
    f = vfoOffsetFreq;
  } else if (mode == BFO) {
    f = bfoFreq;
  } else if (mode == CAL) {
    f = abs(calPpm);
    neg = (calPpm < 0);
  }

  // Sign
  if (neg) {
    display.drawLine(0,y+6,5,y+6,1);
  } else {
    display.drawLine(0,y+6,5,y+6,0);
  }

  // Number
  display.setCursor(startX,y);
  display.print(getMH(f)); 

  display.setCursor(startX + 30,y);
  sprintf(buf,"%03lu",getKH(f));
  display.print(buf);
  
  display.setCursor(startX + 70,y);
  sprintf(buf,"%03lu",getH(f));
  display.print(buf);
  
  // Step
  display.fillRect(0,55,display.width(),8,0);
  display.setTextSize(0);
  display.setCursor(startX,55);
  display.print(stepMenuTitles[stepIndex]);
}

/**
 * Called to load frequencies into the Si5351
 */
void updateVFOFreq() {
  long f = vfoOffsetFreq - vfoFreq;
  si5351.set_freq((unsigned long long)f * 100ULL,SI5351_CLK0);
}

void updateBFOFreq() {
  long f = bfoFreq;
  si5351.set_freq((unsigned long long)f * 100ULL,SI5351_CLK2);
}

void updateCal() {
  si5351.set_correction(calPpm,SI5351_PLL_INPUT_XO);
}

void setup() {
  
  Serial.begin(9600);
  delay(500);

  pinMode(PIN_D2,INPUT_PULLUP);
  pinMode(PIN_D3,INPUT_PULLUP);
  pinMode(PIN_D4,INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC,SSD1306_I2C_ADDRESS);

  // Si5351 initialization
  si5351.init(SI5351_CRYSTAL_LOAD_8PF,0,0);
  // Boost up drive strength
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_2MA);
  si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_2MA);
 
  /*
  // TEMP
  si5351.update_status();
  delay(500);
  si5351.update_status();
  delay(500);
  Serial.print("SYS_INIT: ");
  Serial.print(si5351.dev_status.SYS_INIT);
  Serial.print("  LOL_A: ");
  Serial.print(si5351.dev_status.LOL_A);
  Serial.print("  LOL_B: ");
  Serial.print(si5351.dev_status.LOL_B);
  Serial.print("  LOS: ");
  Serial.print(si5351.dev_status.LOS);
  Serial.print("  REVID: ");
  Serial.println(si5351.dev_status.REVID);
  */
  display.clearDisplay();

  display.setTextSize(0);
  display.setTextColor(WHITE);
  display.println("KC1FSZ VFO3");
  display.drawLine(0,15,display.width(),15,WHITE);

  // Pull values from EEPROM
  
  vfoFreq = Utils::eepromReadLong(0);
  vfoOffsetFreq = Utils::eepromReadLong(4);
  bfoFreq = Utils::eepromReadLong(8);
  calPpm = Utils::eepromReadLong(16);
  stepIndex = EEPROM.read(20);
  
  if (stepIndex > maxStepIndex) {
    stepIndex = 0;
  }

  // Initial update of Si5351
  updateVFOFreq();
  updateBFOFreq();
  updateCal();

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
      updateVFOFreq();
    } else if (mode == VFO_OFFSET) {
      vfoOffsetFreq += step;
      updateVFOFreq();
    } else if (mode == BFO) {
      bfoFreq += step;
      updateBFOFreq();
    } else if (mode == CAL) {
      calPpm += step;
      updateCal();
    }

    displayDirty = true;
  }

  // Save frequencies in EEPROM
  if (clickDuration > 5000) {
    Utils::eepromWriteLong(0,vfoFreq);
    Utils::eepromWriteLong(4,vfoOffsetFreq);
    Utils::eepromWriteLong(8,bfoFreq);
    Utils::eepromWriteLong(16,calPpm);
    EEPROM.write(20,stepIndex);
  }
  else if (clickDuration > 750) {
    if (mode == VFO) {
      mode = VFO_OFFSET;
    } else if (mode == VFO_OFFSET) {
      mode = BFO;
    } else if (mode == BFO) {
      mode = CAL;
    } else {
      mode = VFO;
    } 
    displayDirty = true;
  } 
  else if (clickDuration > 0) {
    if (++stepIndex > maxStepIndex) {
      stepIndex = 0;
    } 
    displayDirty = true;
  }

  if (displayDirty) {
    updateDisplay();
    display.display();
  } 
}

