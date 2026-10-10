// ============================================================
//  VOLTAGE PROTECTOR 
//  Potentiometers stand in for the real sensors:
//    Voltage pot (A0): knob turns 0 - 300 V
//    Current pot (A1): knob turns 0 - 20 A
// ============================================================
#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);   // LCD pins: RS, E, D4, D5, D6, D7

// ---------- pins ----------
const int voltagePin = A0;
const int currentPin = A1;
const int relayPin   = 9;     // relay = switch to the appliance
const int greenLed   = 10;    // ON when power is connected
const int redLed     = 13;    // ON when there is a fault
const int menuButton  = 6;
const int upButton    = 7;
const int downButton  = 8;
const int resetButton = A2;

// ---------- settings (you can change these with the buttons) ----------
int   overVoltage     = 250;    // trip if voltage goes ABOVE this (V)
int   underVoltage    = 180;    // trip if voltage goes BELOW this (V)
float maxCurrent      = 10.0;   // trip if current goes ABOVE this (A)
int   reconnectSeconds = 5;     // wait this long (s) before turning power back on

// ---------- measured values ----------
float volts = 0;
float amps  = 0;

// ---------- what the device is doing now ----------
// state:  0 = WAITING (relay OFF, counting down)
//         1 = ON      (relay ON, appliance powered)
//         2 = TRIPPED (relay OFF, stays off until you press RESET)
int state = 0;

// faultCode: 0 = no fault, 1 = over-voltage, 2 = under-voltage, 3 = over-current
int faultCode = 0;

unsigned long waitStart = 0;     // time when the countdown started

// timers that measure "how long has the problem lasted?"
bool overVoltageTiming = false;   unsigned long overVoltageStart = 0;
bool underVoltageTiming = false;  unsigned long underVoltageStart = 0;
bool overCurrentTiming = false;   unsigned long overCurrentStart = 0;

// ---------- menu ----------
bool inMenu = false;      // true while you are changing settings
int  menuItem = 0;        // 0 = over-voltage, 1 = under-voltage, 2 = max current, 3 = reconnect time

// remember the last button state so we notice only the moment of pressing
bool menuWas = false, upWas = false, downWas = false, resetWas = false;

int lcdCounter = 0;

// ---------- PC communication variables ----------
String serialLine = "";          // letters received from the PC so far
bool streamOn = false;           // true = send a status line every second
unsigned long lastStream = 0;    // when the last status line was sent
int lastState = 0;               // used to notice when the state changes
int lastFault = 0;


// ============================================================
//  Is the grid good enough to switch the power back on?
// ============================================================
bool gridIsGood() {
  // voltage must be safely inside the limits (5 V margin) and current clearly low
  if (volts > underVoltage + 5 && volts < overVoltage - 5 && amps < 0.9 * maxCurrent) {
    return true;
  }
  return false;
}

// ============================================================
//  Switch the power OFF because of a problem
// ============================================================
void tripNow(int code) {
  faultCode = code;
  overVoltageTiming = false;
  underVoltageTiming = false;
  overCurrentTiming = false;

  if (code == 3) {
    state = 2;                 // over-current: stay off until RESET is pressed
  } else {
    state = 0;                 // voltage problem: wait, then reconnect by itself
    waitStart = millis();
  }
}

// ============================================================
//  The protection logic - runs every 20 ms
// ============================================================
void checkProtection() {

  // ----- WAITING: count down while the grid is good -----
  if (state == 0) {
    if (gridIsGood() == false) {
      waitStart = millis();                       // grid is bad -> start counting again
    } else if (millis() - waitStart >= reconnectSeconds * 1000UL) {
      state = 1;                                  // waited long enough -> power ON
      faultCode = 0;
    }
    return;
  }

  // ----- TRIPPED: do nothing, wait for the RESET button -----
  if (state == 2) {
    return;
  }

  // ----- ON: watch for problems -----

  // very big surge or short-circuit: switch off immediately
  if (volts >= overVoltage * 1.15) { tripNow(1); return; }
  if (amps >= maxCurrent * 3)      { tripNow(3); return; }

  // over-voltage: must last 20 ms (ignores tiny spikes)
  if (volts > overVoltage) {
    if (overVoltageTiming == false) {
      overVoltageTiming = true;
      overVoltageStart = millis();
    }
    if (millis() - overVoltageStart >= 20) { tripNow(1); return; }
  } else {
    overVoltageTiming = false;
  }

  // under-voltage: must last 0.5 s (short dips are normal, e.g. when a motor starts)
  if (volts < underVoltage) {
    if (underVoltageTiming == false) {
      underVoltageTiming = true;
      underVoltageStart = millis();
    }
    if (millis() - underVoltageStart >= 500) { tripNow(2); return; }
  } else {
    underVoltageTiming = false;
  }

  // over-current: must last 2 s
  if (amps > maxCurrent) {
    if (overCurrentTiming == false) {
      overCurrentTiming = true;
      overCurrentStart = millis();
    }
    if (millis() - overCurrentStart >= 2000) { tripNow(3); return; }
  } else {
    overCurrentTiming = false;
  }
}

// ============================================================
//  Buttons
// ============================================================
void checkButtons() {
  // a button is pressed when its pin reads LOW
  bool menuNow  = (digitalRead(menuButton)  == LOW);
  bool upNow    = (digitalRead(upButton)    == LOW);
  bool downNow  = (digitalRead(downButton)  == LOW);
  bool resetNow = (digitalRead(resetButton) == LOW);

  // "pressed" = it is down now, but it was not down last time
  bool menuPressed  = menuNow  && !menuWas;
  bool upPressed    = upNow    && !upWas;
  bool downPressed  = downNow  && !downWas;
  bool resetPressed = resetNow && !resetWas;

  menuWas = menuNow;  upWas = upNow;  downWas = downNow;  resetWas = resetNow;

  // RESET button: clears an over-current trip
  if (resetPressed && inMenu == false && state == 2) {
    state = 0;
    waitStart = millis();
  }

  // MENU button
  if (menuPressed) {
    if (inMenu == false) {
      inMenu = true;            // open the menu
      menuItem = 0;
    } else {
      menuItem = menuItem + 1;  // next setting
      if (menuItem > 3) {
        inMenu = false;         // after the last setting, leave the menu
      }
    }
  }

  // UP / DOWN buttons change the selected setting
  if (inMenu && (upPressed || downPressed)) {
    int direction = 1;
    if (downPressed) direction = -1;

    if (menuItem == 0) overVoltage = constrain(overVoltage + 5 * direction, underVoltage + 20, 300);
    if (menuItem == 1) underVoltage = constrain(underVoltage + 5 * direction, 100, overVoltage - 20);
    if (menuItem == 2) maxCurrent = constrain(maxCurrent + 0.5 * direction, 1.0, 20.0);
    if (menuItem == 3) reconnectSeconds = constrain(reconnectSeconds + direction, 1, 60);
  }
}

// ============================================================
//  LCD
// ============================================================
void clearRow(int row) {
  lcd.setCursor(0, row);
  lcd.print("                ");     // 16 spaces
  lcd.setCursor(0, row);
}

void printFaultName() {
  if (faultCode == 1) lcd.print("OV");
  if (faultCode == 2) lcd.print("UV");
  if (faultCode == 3) lcd.print("OC");
}

void updateLcd() {
  if (inMenu) {
    // ----- settings screen -----
    clearRow(0);
    lcd.print("SETTING ");
    lcd.print(menuItem + 1);
    lcd.print("/4");

    clearRow(1);
    if (menuItem == 0) { lcd.print("Over V: ");   lcd.print(overVoltage);      lcd.print(" V"); }
    if (menuItem == 1) { lcd.print("Under V: ");  lcd.print(underVoltage);     lcd.print(" V"); }
    if (menuItem == 2) { lcd.print("Max I: ");    lcd.print(maxCurrent, 1);    lcd.print(" A"); }
    if (menuItem == 3) { lcd.print("Wait: ");     lcd.print(reconnectSeconds); lcd.print(" s"); }
    return;
  }

  // ----- normal screen: top row = readings -----
  clearRow(0);
  lcd.print("V:");
  lcd.print(volts, 1);
  lcd.print(" I:");
  lcd.print(amps, 1);
  lcd.print("A");

  // ----- bottom row = status -----
  clearRow(1);
  if (state == 1) {
    lcd.print("RELAY ON   OK");
  }
  if (state == 0) {
    long passed = (millis() - waitStart) / 1000;
    long left = reconnectSeconds - passed;
    if (left < 0) left = 0;
    lcd.print("WAIT ");
    lcd.print(left);
    lcd.print("s ");
    if (faultCode == 0) lcd.print("BOOT");
    printFaultName();
  }
  if (state == 2) {
    lcd.print("TRIP:");
    printFaultName();
    lcd.print(" RST");
  }
}

// ============================================================
//  PC COMMUNICATION  
// ============================================================

// ----- send one status line to the PC -----
void printStatus(String prefix) {
  Serial.print(prefix);
  Serial.print(" state=");
  if (state == 0) Serial.print("WAITING");
  if (state == 1) Serial.print("ON");
  if (state == 2) Serial.print("TRIPPED");

  Serial.print(" fault=");
  if (faultCode == 0) Serial.print("NONE");
  if (faultCode == 1) Serial.print("OV");
  if (faultCode == 2) Serial.print("UV");
  if (faultCode == 3) Serial.print("OC");

  Serial.print(" V=");
  Serial.print(volts, 1);
  Serial.print(" I=");
  Serial.print(amps, 1);

  Serial.print(" relay=");
  if (state == 1) Serial.println("ON");
  else            Serial.println("OFF");
}

// ----- send the four settings to the PC -----
void printSettings() {
  Serial.print("SETTINGS OV=");
  Serial.print(overVoltage);
  Serial.print(" UV=");
  Serial.print(underVoltage);
  Serial.print(" OC=");
  Serial.print(maxCurrent, 1);
  Serial.print(" WAIT=");
  Serial.println(reconnectSeconds);
}

// ----- change one setting, e.g. text = "OV 240" -----
void setFromPc(String text) {
  text.trim();
  int space = text.indexOf(' ');
  if (space < 0) {
    Serial.println("ERROR use: SET OV 240");
    return;
  }
  String name = text.substring(0, space);          // the part before the space
  String valueText = text.substring(space + 1);    // the part after the space
  valueText.trim();
  float value = valueText.toFloat();               // text -> number

  if (name == "OV") {
    if (value >= 200 && value <= 300 && value >= underVoltage + 20) {
      overVoltage = (int)value;
      Serial.print("OK OV=");
      Serial.println(overVoltage);
    } else {
      Serial.println("ERROR OV must be 200-300 and at least UV+20");
    }
  }
  else if (name == "UV") {
    if (value >= 100 && value <= 220 && value <= overVoltage - 20) {
      underVoltage = (int)value;
      Serial.print("OK UV=");
      Serial.println(underVoltage);
    } else {
      Serial.println("ERROR UV must be 100-220 and at most OV-20");
    }
  }
  else if (name == "OC") {
    if (value >= 1 && value <= 20) {
      maxCurrent = value;
      Serial.print("OK OC=");
      Serial.println(maxCurrent, 1);
    } else {
      Serial.println("ERROR OC must be 1-20");
    }
  }
  else if (name == "WAIT") {
    if (value >= 1 && value <= 60) {
      reconnectSeconds = (int)value;
      Serial.print("OK WAIT=");
      Serial.println(reconnectSeconds);
    } else {
      Serial.println("ERROR WAIT must be 1-60");
    }
  }
  else {
    Serial.println("ERROR unknown setting (use OV, UV, OC or WAIT)");
  }
}

// ----- decide what to do with one complete command line -----
void handleCommand(String line) {
  line.trim();
  line.toUpperCase();                 // so "status" and "STATUS" both work
  if (line.length() == 0) return;

  if (line == "HELP") {
    Serial.println("Commands: STATUS | GET | SET OV 240 | SET UV 180 | SET OC 10 | SET WAIT 5 | RESET | STREAM ON | STREAM OFF");
  }
  else if (line == "STATUS") {
    printStatus("STATUS");
  }
  else if (line == "GET") {
    printSettings();
  }
  else if (line == "RESET") {
    if (state == 2) {
      state = 0;
      waitStart = millis();
      Serial.println("OK reset, waiting to reconnect");
    } else {
      Serial.println("ERROR nothing to reset (not tripped)");
    }
  }
  else if (line == "STREAM ON") {
    streamOn = true;
    Serial.println("OK streaming every 1 s");
  }
  else if (line == "STREAM OFF") {
    streamOn = false;
    Serial.println("OK streaming stopped");
  }
  else if (line.startsWith("SET ")) {
    setFromPc(line.substring(4));
  }
  else {
    Serial.println("ERROR unknown command. Type HELP");
  }
}

// ----- called from loop(): read the PC, send events and the stream -----
void pcCommunication() {
  // 1. read letters from the PC; a command is finished by the Enter key
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(serialLine);
      serialLine = "";
    } else if (c != '\r') {
      if (serialLine.length() < 40) serialLine += c;
    }
  }

  // 2. tell the PC immediately when the state or fault changes (trip, reconnect...)
  if (state != lastState || faultCode != lastFault) {
    lastState = state;
    lastFault = faultCode;
    printStatus("EVENT");
  }

  // 3. optional: status line every second
  if (streamOn && millis() - lastStream >= 1000) {
    lastStream = millis();
    printStatus("DATA");
  }
}

// ============================================================
//  setup() runs once at the start
// ============================================================
void setup() {
  digitalWrite(relayPin, LOW);     // relay OFF before anything else
  pinMode(relayPin, OUTPUT);
  pinMode(greenLed, OUTPUT);
  pinMode(redLed, OUTPUT);

  pinMode(menuButton,  INPUT_PULLUP);
  pinMode(upButton,    INPUT_PULLUP);
  pinMode(downButton,  INPUT_PULLUP);
  pinMode(resetButton, INPUT_PULLUP);

  lcd.begin(16, 2);
  lcd.print("Voltage Protector");

  Serial.begin(9600);                // start the connection to the PC
  Serial.println("Voltage Protector ready. Type HELP");

  state = 0;                       // always start by waiting, never connect instantly
  waitStart = millis();
}

// ============================================================
//  loop() repeats forever
// ============================================================
void loop() {
  // 1. read the two knobs and convert to volts and amps
  volts = analogRead(voltagePin) * (300.0 / 1023.0);
  amps  = analogRead(currentPin) * (20.0 / 1023.0);

  // 2. decide whether everything is OK
  checkProtection();

  // 3. switch the relay and LEDs
  if (state == 1) {
    digitalWrite(relayPin, HIGH);
    digitalWrite(greenLed, HIGH);
  } else {
    digitalWrite(relayPin, LOW);
    digitalWrite(greenLed, LOW);
  }
  if (state == 2 || (state == 0 && faultCode != 0)) {
    digitalWrite(redLed, HIGH);
  } else {
    digitalWrite(redLed, LOW);
  }

  // 4. buttons
  checkButtons();

  // 5. refresh the LCD about 5 times per second
  lcdCounter = lcdCounter + 1;
  if (lcdCounter >= 10) {
    lcdCounter = 0;
    updateLcd();
  }

  pcCommunication();             // talk to the PC

  delay(20);     // pause 20 ms, then repeat
}
