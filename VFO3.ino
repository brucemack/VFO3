// Control script for the Peppermint II SSB Tranceiver
// Bruce MacKinnon KC1FSZ
//
#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// The Etherkit library
#include <si5351.h>
// Utilities used to manage switches, ecoders, etc.
#include <DebouncedSwitch2.h>
#include <RotaryEncoder.h>
#include <ClickDetector.h>
#include <Utils.h>

#define MAGIC_NUMBER 2728

Adafruit_SSD1306 display(128,64);
Si5351 si5351;

// Rotary encoder pins
#define PIN_D2 2
#define PIN_D3 3
#define PIN_D4 4
// Red command buttons
#define COMMAND_BUTTON1_PIN 5
#define COMMAND_BUTTON2_PIN 6
// PTT controls 
#define PTT_CONTROL_PIN 7
#define PTT_BUTTON_PIN 8
#define PTT_AUX_PIN 9
// Analog sample input
#define DETECTOR_INPUT_PIN A0

// ----------------------------------------------------------------------------------------
// Menu choices
enum Mode { VFO, MODE, BPF_TOP, BPF_WIDTH, CAL, LO_POWER, BFO_POWER, METER1, LO_SIDE };
const char* ModeTitles[] = { "VFO", "Mode", "BPFTop", "BPFWide", "CAL", "LOPwr", "BFOPwr", "Meter1", "LOSide" };
const int ModeChoices = 9;

const unsigned long Step[] = { 500, 100, 10, 1, 1000000, 100000, 10000, 1000 };
const char* StepTitles[] = { "500 Hz", "100 Hz", "10 Hz", "1 Hz", "1 MHz", "100 kHz", "10 kHz", "1 kHz" };
const int StepChoices = 8;

enum Mode2 { LSB, USB };
const char* Mode2Titles[] = { "LSB", "USB" };
const int Mode2Choices = 2;

enum LOSide { LOWSIDE_INJECTION, HIGHSIDE_INJECTION, DIRECT_CONVERSION };
const char* LOSideTitles[] = { "Low Side", "High Side", "Direct" };
const int LOSideChoices = 3;

const int LOPowerChoices = 4;
const int BFOPowerChoices = 4;

// 40m band limitations used for scanning (phone portion only)
const unsigned long MinDisplayFreq = 7125000L;
const unsigned long MaxDisplayFreq = 7300000L;

// The actual settings for the rig
Mode ModeSetting = VFO;
int StepSetting = 0;
Mode2 Mode2Setting = LSB;
LOSide LOSideSetting = LOWSIDE_INJECTION;
uint8_t BFOPowerSetting = 0;
uint8_t LOPowerSetting = 0;
unsigned long DisplayFreq = 7000000L;
unsigned long BPFTopFreq = 11998000l;
unsigned long BPFWidth = 4000L; 
long CalPpm = 0;
int AnalogSample1 = 0;

DebouncedSwitch2 db2(5L);
DebouncedSwitch2 db3(5L);
DebouncedSwitch2 db4(10L);
DebouncedSwitch2 commandButton1(10L);
DebouncedSwitch2 commandButton2(10L);
DebouncedSwitch2 pttButton(20L);
RotaryEncoder renc(&db2,&db3,100L);
ClickDetector cd4(&db4);

// Scanning related
bool scanMode = false;
// This is the last time we made a scan jump
unsigned long lastScanStamp = 0;
// This controls how fast we scan
unsigned long scanDelayMs = 150;

int Fault = 0;

unsigned long getMH(unsigned long f) {
  return f / 1000000L;
}

unsigned long getKH(unsigned long f) {
  return (f / 1000L) % 1000L;
}

unsigned long getH(unsigned long f) {
  return f % 1000L;
}

void updateDisplay1() {

  int startX = 10;
  int y = 17;
  char buf[4];
  
  // Render frequency
  unsigned long f = 0;
  boolean neg = false;  

  display.setTextSize(2);
  display.setTextColor(WHITE);
  
  if (ModeSetting == VFO) {
    f = DisplayFreq;
    display.setCursor(startX,y);
    display.print(getMH(f)); 
    display.setCursor(startX + 30,y);
    sprintf(buf,"%03lu",getKH(f));
    display.print(buf);
    display.setCursor(startX + 70,y);
    sprintf(buf,"%03lu",getH(f));
    display.print(buf);
    // Step
    display.setTextSize(0);
    display.setCursor(startX,55);
    display.print(StepTitles[StepSetting]);
  } else if (ModeSetting == BPF_TOP) {
    f = BPFTopFreq;
    display.setCursor(startX,y);
    display.print(getMH(f)); 
    display.setCursor(startX + 30,y);
    sprintf(buf,"%03lu",getKH(f));
    display.print(buf);
    display.setCursor(startX + 70,y);
    sprintf(buf,"%03lu",getH(f));
    display.print(buf);
    // Step
    display.setTextSize(0);
    display.setCursor(startX,55);
    display.print(StepTitles[StepSetting]);
  } else if (ModeSetting == BPF_WIDTH) {
    f = BPFWidth;
    display.setCursor(startX,y);
    display.print(f); 
    // Step
    display.setTextSize(0);
    display.setCursor(startX,55);
    display.print(StepTitles[StepSetting]);
  } else if (ModeSetting == CAL) {
    f = abs(CalPpm);
    neg = (CalPpm < 0);
    // Sign
    if (neg) {
      display.drawLine(0,y+6,5,y+6,1);
    }
    display.setCursor(startX,y);
    display.print(f); 
    // Step
    display.setTextSize(0);
    display.setCursor(startX,55);
    display.print(StepTitles[StepSetting]);
  } else if (ModeSetting == LO_POWER) {
    f = LOPowerSetting;
    display.setCursor(startX,y);
    display.print(f); 
  } else if (ModeSetting == BFO_POWER) {
    f = BFOPowerSetting;
    display.setCursor(startX,y);
    display.print(f); 
  } else if (ModeSetting == METER1) {
    f = AnalogSample1;
    display.setCursor(startX,y);
    display.print(f); 
  } else if (ModeSetting == MODE) {
    display.setCursor(startX,y);
    display.print(Mode2Titles[Mode2Setting]); 
  } else if (ModeSetting == LO_SIDE) {
    display.setCursor(startX,y);
    display.print(LOSideTitles[LOSideSetting]); 
  }
}

void updateDisplay() {

  // Logo information and line
  display.setCursor(0,0);
  display.setTextSize(0);
  display.setTextColor(WHITE);
  display.println("KC1FSZ VFO3.1");
  display.drawLine(0,15,display.width(),15,WHITE);

  if (Fault) {
    display.setTextSize(2);
    display.setCursor(0,10);
    display.print(Fault);
  } else {
    // Mode at the top of the screen
    int modeX = 85;
    display.setCursor(modeX,0);
    display.print(ModeTitles[(int)ModeSetting]);
    // Mode specific information 
    updateDisplay1();
  }
}

// Called to load the LO frequency into the Si5351 (CLK0)
void updateLOFreq() {
  long f = 0;
  if (LOSideSetting == LOWSIDE_INJECTION) {
    if (Mode2Setting == LSB) {
      f = BPFTopFreq - DisplayFreq;
    } else if (Mode2Setting == USB) {
      f = (BPFTopFreq - DisplayFreq) - BPFWidth;
    }
  } else if (LOSideSetting == HIGHSIDE_INJECTION) {
    if (Mode2Setting == USB) {
      f = BPFTopFreq + DisplayFreq;
    } else if (Mode2Setting == LSB) {
      f = (BPFTopFreq + DisplayFreq) - BPFWidth;
    }
  } else if (LOSideSetting == DIRECT_CONVERSION) {
    f = DisplayFreq;
  } else {
    Fault = 1;
  }
  si5351.set_freq((unsigned long long)f * 100ULL,SI5351_CLK0);    
}

// Called to load the BFO frequency into the Si5351 (CLK2)
void updateBFOFreq() {
  long f = 0;
  if (LOSideSetting == LOWSIDE_INJECTION) {
    if (Mode2Setting == LSB) {
      f = BPFTopFreq;
    } else if (Mode2Setting == USB) {
      f = BPFTopFreq - BPFWidth;
    }
  } else if (LOSideSetting == HIGHSIDE_INJECTION) {
    if (Mode2Setting == USB) {
      f = BPFTopFreq;
    } else if (Mode2Setting == LSB) {
      f = BPFTopFreq - BPFWidth;
    }
  } else if (LOSideSetting == DIRECT_CONVERSION) {
    f = DisplayFreq;
  } else {
    Fault = 1;
  }
  si5351.set_freq((unsigned long long)f * 100ULL,SI5351_CLK2);
}

void updateLOPower() {
  si5351.drive_strength(SI5351_CLK0,(si5351_drive)LOPowerSetting);
}

void updateBFOPower() {
  si5351.drive_strength(SI5351_CLK2,(si5351_drive)BFOPowerSetting);
}

void updateCal() {
  si5351.set_correction(CalPpm,SI5351_PLL_INPUT_XO);
}

void loadState() {
  // Pull values from EEPROM
  long magic = Utils::eepromReadLong(0);
  // Check to make sure that we have valid information in the EEPROM.  For instance,
  // if this is a new processor we might not have saved anything. 
  if (magic != MAGIC_NUMBER) {
    return;
  }
  DisplayFreq = Utils::eepromReadLong(4);
  BPFTopFreq = Utils::eepromReadLong(8);
  BPFWidth = Utils::eepromReadLong(12);
  CalPpm = Utils::eepromReadLong(16);
  StepSetting = EEPROM.read(20);   
  Mode2Setting = (Mode2)EEPROM.read(21);
  LOSideSetting =  (LOSide)EEPROM.read(22);
  LOPowerSetting = EEPROM.read(23);
  BFOPowerSetting = EEPROM.read(24);
}

void saveState() {
  Utils::eepromWriteLong(0,MAGIC_NUMBER);
  Utils::eepromWriteLong(4,DisplayFreq);
  Utils::eepromWriteLong(8,BPFTopFreq);
  Utils::eepromWriteLong(12,BPFWidth);
  Utils::eepromWriteLong(16,CalPpm);
  EEPROM.write(20,StepSetting);
  EEPROM.write(21,Mode2Setting);
  EEPROM.write(22,LOSideSetting);
  EEPROM.write(23,LOPowerSetting);  
  EEPROM.write(24,BFOPowerSetting);  
}

void setup() {

  //Serial.begin(9600);
  delay(500);

  pinMode(PIN_D2,INPUT_PULLUP);
  pinMode(PIN_D3,INPUT_PULLUP);
  pinMode(PIN_D4,INPUT_PULLUP);
  pinMode(COMMAND_BUTTON1_PIN,INPUT_PULLUP);
  pinMode(COMMAND_BUTTON2_PIN,INPUT_PULLUP);
  pinMode(PTT_BUTTON_PIN,INPUT_PULLUP);
  pinMode(PTT_AUX_PIN,INPUT_PULLUP);
  pinMode(PTT_CONTROL_PIN,OUTPUT);
  pinMode(13,OUTPUT);

  digitalWrite(PTT_CONTROL_PIN,0);

  // Diagnostic signal
  delay(250);
  digitalWrite(13,1);
  delay(250);
  digitalWrite(13,0);
  delay(250);
  digitalWrite(13,1);
  delay(250);
  digitalWrite(13,0);

  display.begin(SSD1306_SWITCHCAPVCC,0x3c,true,true);

  // Si5351 initialization
  si5351.init(SI5351_CRYSTAL_LOAD_8PF,0,0);
  // Boost up drive strength
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_2MA);
  si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_2MA);
 
  display.clearDisplay();

  // Pull values from EEPROM
  loadState();
  
  // Initial update of Si5351
  updateLOFreq();
  updateBFOFreq();
  updateLOPower();
  updateBFOPower();
  updateCal();

  // Initial display render
  updateDisplay();
  
  display.display();

  /*
  for (int i = 0; i < 16; i++) {
    float rad = 2.0f * 3.14159f * ((float)i / 16.0f);
    float a = sin(rad);
    float b = a + 1.0f;
    float c = (b / 2.0f) * 1024.0f;
    outTable[i] = (int)c;
  }
  // Audio cycle 
  myTimer.begin(audioCycle,200);
  */
}

int count = 0;

void loop() {
    
  boolean displayDirty = false;
  
  if (Fault) {
    displayDirty = true;
  }
  
  int c2 = digitalRead(PIN_D2);
  int c3 = digitalRead(PIN_D3);
  db2.loadSample(c2 == 0);
  db3.loadSample(c3 == 0);
  db4.loadSample(digitalRead(PIN_D4) == 0);
  commandButton1.loadSample(digitalRead(COMMAND_BUTTON1_PIN) == 0);
  commandButton2.loadSample(digitalRead(COMMAND_BUTTON2_PIN) == 0);
  pttButton.loadSample(digitalRead(PTT_BUTTON_PIN) == 0);
  int pttAux = digitalRead(PTT_AUX_PIN);
  long mult = renc.getIncrement();
  long clickDuration = cd4.getClickDuration();

  // Sample analog for meter 1
  int sample = analogRead(DETECTOR_INPUT_PIN);
  if (sample != AnalogSample1 && ModeSetting == METER1) {
    displayDirty = true;
  }
  AnalogSample1 = sample;

  // Look for dial turning action
  if (mult != 0) {
    // Immediately stop scanning
    scanMode = false;
    // Handle dial
    long step = mult * Step[StepSetting];
    if (ModeSetting == VFO) {
      DisplayFreq += step;
      updateLOFreq();
    } else if (ModeSetting == BPF_TOP) {
      BPFTopFreq += step;
      updateLOFreq();
      updateBFOFreq();
    } else if (ModeSetting == BPF_WIDTH) {
      BPFWidth += step;
      updateLOFreq();
      updateBFOFreq();
    } else if (ModeSetting == CAL) {
      CalPpm += step;
      updateCal();
    } else if (ModeSetting == LO_POWER) {
      LOPowerSetting = (LOPowerSetting == LOPowerChoices - 1) ? 0 : LOPowerSetting + 1;
      updateLOPower();     
    } else if (ModeSetting == BFO_POWER) {
      BFOPowerSetting = (BFOPowerSetting == BFOPowerChoices - 1) ? 0 : BFOPowerSetting + 1;
      updateBFOPower();     
    } else if (ModeSetting == MODE) {
      Mode2Setting = (Mode2Setting == Mode2Choices - 1) ? (Mode2)0 : (Mode2)(Mode2Setting + 1);
      updateLOFreq();
      updateBFOFreq();      
    } else if (ModeSetting == LO_SIDE) {
      LOSideSetting = (LOSideSetting == LOSideChoices - 1) ? (LOSide)0 : (LOSide)(LOSideSetting + 1);
      updateLOFreq();
      updateBFOFreq();
    }
    displayDirty = true;
  }
  // Save frequencies in EEPROM
  else if (clickDuration > 5000) {   
    saveState();
  } else if (clickDuration > 0) {
    StepSetting = (StepSetting == StepChoices - 1) ? 0 : StepSetting + 1;
    displayDirty = true;
  } else if (commandButton1.getState() && commandButton1.isEdge()) {
    if (ModeSetting == VFO) {
      scanMode = !scanMode;     
    }
    displayDirty = true;
  } else if (commandButton2.getState() && commandButton2.isEdge()) {
    ModeSetting = (ModeSetting == ModeChoices - 1) ? (Mode)0 : (Mode)(ModeSetting + 1);
    displayDirty = true;
  }

  // Deal with the PPT button
  if (pttButton.getState() || pttAux == 0) {
    digitalWrite(13,1);
    digitalWrite(PTT_CONTROL_PIN,1);
  } else {
    digitalWrite(13,0);
    digitalWrite(PTT_CONTROL_PIN,0);    
  }

  // Handle scanning.  If we are in VFO mode and scanning is enabled and the scan interval
  // has expired then step the VFO frequency.
  if (ModeSetting == VFO && 
      scanMode && 
      millis() > (lastScanStamp + scanDelayMs)) {
    // Record the time so that we can start another cycle
    lastScanStamp = millis();
    // Bump the frequency by the configured step
    long step = Step[StepSetting];
    DisplayFreq += step;
    // Look for wrap-around
    if (DisplayFreq > MaxDisplayFreq) {
      DisplayFreq = MinDisplayFreq;
    }
    updateLOFreq();
    displayDirty = true;
  }

  if (displayDirty) {
    display.clearDisplay();
    updateDisplay();
    display.display();
  } 
}

