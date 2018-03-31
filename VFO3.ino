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

#define MAGIC_NUMBER 2727

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

Si5351 si5351;

#define PIN_D2 2
#define PIN_D3 3
#define PIN_D4 4
#define COMMAND_BUTTON1_PIN 5

enum Mode { VFO, VFO_OFFSET, BFO, CAL, VFO_POWER, BFO_POWER  };
const char* modeTitles[] = { "VFO", "VFO+", "BFO", "CAL", "VFOPwr", "BFOPwr" };
Mode mode = VFO;

const unsigned long stepMenu[] = { 500, 100, 10, 1, 1000000, 100000, 10000, 1000 };
const char* stepMenuTitles[] = { "500 Hz", "100 Hz", "10 Hz", "1 Hz", "1 MHz", "100 kHz", "10 kHz", "1 kHz" };
const uint8_t maxStepIndex = 7;

// 40m band limitations
const unsigned long minDisplayFreq = 7125000L;
const unsigned long maxDisplayFreq = 7300000L;

DebouncedSwitch db2(1L);
DebouncedSwitch db3(1L);
DebouncedSwitch db4(1L);
DebouncedSwitch commandButton1(5L);
RotaryEncoder renc(&db2,&db3);
ClickDetector cd4(&db4);

unsigned long vfoFreq = 7000000;
long vfoOffsetFreq = 11998000;
unsigned long bfoFreq = 11998000;
long calPpm = 0;
uint8_t stepIndex = 0;
uint8_t vfoPower = 0;
uint8_t bfoPower = 0;

// Scanning related.
// This controls the mode: 0 means not scanning, +1 means scan up, -1 means scan down
int scanMode = 0;
// This is the last time we made a scan jump
unsigned long lastScanStamp = 0;
// This controls how fast we scan
unsigned long scanDelayMs = 150;

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

  // Logo information and line
  display.setCursor(0,0);
  display.setTextSize(0);
  display.setTextColor(WHITE);
  display.println("KC1FSZ VFO3");
  display.drawLine(0,15,display.width(),15,WHITE);
  
  int startX = 10;
  int y = 17;

  // Mode
  int modeX = 85;
  display.setCursor(modeX,0);
  display.print(modeTitles[(int)mode]);

  display.setTextSize(2);
  display.setTextColor(WHITE);
  char buf[4];

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
  } else if (mode == VFO_POWER) {
    f = vfoPower;
  } else if (mode == BFO_POWER) {
    f = bfoPower;
  }

  // Sign
  if (neg) {
    display.drawLine(0,y+6,5,y+6,1);
  } else {
    display.drawLine(0,y+6,5,y+6,0);
  }

  // Number
  if (mode == VFO || mode == VFO_OFFSET || mode == BFO) {
    display.setCursor(startX,y);
    display.print(getMH(f)); 
  
    display.setCursor(startX + 30,y);
    sprintf(buf,"%03lu",getKH(f));
    display.print(buf);
    
    display.setCursor(startX + 70,y);
    sprintf(buf,"%03lu",getH(f));
    display.print(buf);
  } else {
    display.setCursor(startX,y);
    display.print(f); 
  }
  
  // Step
  if (mode == VFO || mode == VFO_OFFSET || mode == BFO || mode == CAL) {
    display.setTextSize(0);
    display.setCursor(startX,55);
    display.print(stepMenuTitles[stepIndex]);
  }
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

void updateVFOPower() {
  si5351.drive_strength(SI5351_CLK0,(si5351_drive)vfoPower);
}

void updateBFOPower() {
  si5351.drive_strength(SI5351_CLK2,(si5351_drive)bfoPower);
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
  pinMode(COMMAND_BUTTON1_PIN,INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC,SSD1306_I2C_ADDRESS);

  // Si5351 initialization
  si5351.init(SI5351_CRYSTAL_LOAD_8PF,0,0);
  // Boost up drive strength
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_2MA);
  si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_2MA);
 
  display.clearDisplay();

  // Pull values from EEPROM
  long magic = Utils::eepromReadLong(0);
  // Check to make sure that we have valid information in the EEPROM.  For instance,
  // if this is a new processor we might not have saved anything. 
  if (magic == MAGIC_NUMBER) {
    vfoFreq = Utils::eepromReadLong(4);
    vfoOffsetFreq = Utils::eepromReadLong(8);
    bfoFreq = Utils::eepromReadLong(12);
    calPpm = Utils::eepromReadLong(16);
    stepIndex = EEPROM.read(20);    
    vfoPower = EEPROM.read(21);
    bfoPower = EEPROM.read(22);
  } 
  
  if (stepIndex > maxStepIndex) {
    stepIndex = 0;
  }

  // Initial update of Si5351
  updateVFOFreq();
  updateBFOFreq();
  updateVFOPower();
  updateBFOPower();
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
  commandButton1.loadSample(digitalRead(COMMAND_BUTTON1_PIN) == 0);
  
  long mult = renc.getIncrement();
  long clickDuration = cd4.getClickDuration();
  
  boolean displayDirty = false;

  // Look for dial turning
  if (mult != 0) {
    // Immediately stop scanning
    scanMode = 0;
    // Handle dial
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
    } else if (mode == VFO_POWER) {
      vfoPower += 1;
      if (vfoPower > 3) {
        vfoPower = 0;
      }
      updateVFOPower();     
    } else if (mode == BFO_POWER) {
      bfoPower += 1;
      if (bfoPower > 3) {
        bfoPower = 0;
      }
      updateBFOPower();     
    }

    displayDirty = true;
  }
  // Save frequencies in EEPROM
  else if (clickDuration > 5000) {   
    Utils::eepromWriteLong(0,MAGIC_NUMBER);
    Utils::eepromWriteLong(4,vfoFreq);
    Utils::eepromWriteLong(8,vfoOffsetFreq);
    Utils::eepromWriteLong(12,bfoFreq);
    Utils::eepromWriteLong(16,calPpm);
    EEPROM.write(20,stepIndex);
    EEPROM.write(21,vfoPower);
    EEPROM.write(22,bfoPower);
  }
  else if (clickDuration > 500) {
    if (mode == VFO) {
      mode = VFO_OFFSET;
    } else if (mode == VFO_OFFSET) {
      mode = BFO;
    } else if (mode == BFO) {
      mode = CAL;
    } else if (mode == CAL) {
      mode = VFO_POWER;
    } else if (mode == VFO_POWER) {
      mode = BFO_POWER;
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
  } else if (commandButton1.getState()) {
    if (mode == VFO) {     
      if (scanMode == 0)
        scanMode = 1;
    }
  }

// Handle scanning.  If we are in VFO mode and scanning is enabled and the scan interval
  // has expired then step the VFO frequency.
  //  
  if (mode == VFO &&
      scanMode != 0 && 
      millis() > (lastScanStamp + scanDelayMs)) {
    // Record the time so that we can start another cycle
    lastScanStamp = millis();
    // Bump the frequency by the configured step
    long step = stepMenu[stepIndex];
    vfoFreq += step;
    // Look for wrap-around
    if (vfoFreq > maxDisplayFreq) {
      vfoFreq = minDisplayFreq;
    }
    updateVFOFreq();
    displayDirty = true;
  }

  if (displayDirty) {
    display.clearDisplay();
    updateDisplay();
    display.display();
  } 
}

