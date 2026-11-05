#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin configuration

#define RELAY_PIN     26
#define GREEN_LED     4
#define RED_LED       14
#define ACS712_PIN    35
#define GSM_RX        16
#define GSM_TX        17

#define RELAY_ON      LOW
#define RELAY_OFF     HIGH
#define LED_ON        HIGH
#define LED_OFF       LOW

// User configuration

String MASTER_NUMBER = "+91XXXXXXXXXX"; // Enter your mobile number here.

float DRY_RUN_THRESHOLD  = 0.05;
float OVERLOAD_THRESHOLD = 0.80;

#define STARTUP_IGNORE_MS   4000
#define MAX_RETRIES         3
#define RETRY_INTERVAL_MS   30000
#define ACS712_SENSITIVITY  0.100
#define CURRENT_SAMPLES     50

// GSM and LCD objects

HardwareSerial sim800l(2);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// System states

enum SystemState {
  STATE_IDLE,
  STATE_RUNNING,
  STATE_FAULT,
  STATE_LOCKED,
  STATE_INITIALIZING
};

enum FaultType {
  FAULT_NONE,
  FAULT_DRY_RUN,
  FAULT_OVERLOAD
};

// Global variables

SystemState systemState   = STATE_INITIALIZING;
FaultType   currentFault  = FAULT_NONE;

bool        isMotorOn       = false;
String      lastTrigger     = "NONE";
unsigned long motorStartTime  = 0;
unsigned long motorRunTime    = 0;

int           retryCount      = 0;
bool          pendingRetry    = false;
unsigned long retryTimer      = 0;

float         currentAmps     = 0.0;
float         zeroVoltage     = 1.65;
unsigned long lastCurrentRead = 0;

bool          isNetworkOK     = false;
unsigned long lastNetworkCheck = 0;

unsigned long lastLcdUpdate   = 0;
int           lcdAnimFrame    = 0;

unsigned long lastBlinkTime   = 0;
bool          blinkState      = false;

// Setup

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n[BOOT] Smart Pump Controller v3.1");

  sim800l.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  sim800l.setTimeout(500); // Read GSM responses line by line.

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);
  digitalWrite(GREEN_LED, LED_OFF);
  digitalWrite(RED_LED,   LED_OFF);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcdShowBoot("GSM MOTOR CTRL", "INITIALIZING...");
  delay(1200);

  lcdShowBoot("CALIBRATING...", "SENSOR ZERO");
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += analogRead(ACS712_PIN);
    delay(10);
  }
  zeroVoltage = (sum / 100.0) * (3.3 / 4095.0);

  lcdShowBoot("GSM CONNECTING", "PLEASE WAIT...");
  initGSM();

  systemState = STATE_IDLE;
  lcdShowBoot("SYSTEM  READY", "AWAITING CMD...");
  delay(1500);
}

// Main loop

void loop() {
  readGSM();
  monitorCurrent();
  checkFaults();
  handleAutoRetry();
  blinkRedLED();

  if (millis() - lastLcdUpdate >= 1200) {
    updateLCD();
    lastLcdUpdate = millis();
  }

  // Check network registration every 30 seconds.
  if (millis() - lastNetworkCheck >= 30000) {
    sim800l.println("AT+CREG?");
    lastNetworkCheck = millis();
  }
}

// GSM initialization

void initGSM() {
  while (sim800l.available()) sim800l.read();

  sendAT("AT", 500);
  sendAT("ATE0", 500);
  sendAT("AT+CMGF=1", 500);
  sendAT("AT+CNMI=2,2,0,0,0", 500);
  sendAT("AT+CLIP=1", 500);
  sendAT("AT+CMGDA=\"DEL ALL\"", 1000); // Delete old SMS messages.
  sendAT("AT+CREG=0", 500); // Check network registration in the main loop.

  // Initial network ping
  sim800l.println("AT+CREG?");
  delay(500);
}

// Read and process GSM messages

void readGSM() {
  while (sim800l.available()) {
    String line = sim800l.readStringUntil('\n');
    line.trim();
    line.toUpperCase();

    if (line.length() < 2) continue;
    Serial.println("[GSM] << " + line);

    // ─── NETWORK REGISTRATION ───────────────────
    if (line.indexOf("+CREG:") > -1) {
      // Accept both home and roaming network registration.
      bool ok = (line.indexOf("0,1") > -1 || line.indexOf("0,5") > -1 ||
                 line.indexOf("1,1") > -1 || line.indexOf("1,5") > -1);

      if (ok != isNetworkOK) {
        isNetworkOK = ok;
        Serial.println(ok ? "[NET] Connected" : "[NET] Lost connection");
      }
      continue;
    }

    // ─── INCOMING CALL (via +CLIP) ───────────────
    if (line.indexOf("+CLIP:") > -1) {
      sim800l.println("ATH");   // End the incoming call.
      delay(500);

      if (line.indexOf(MASTER_NUMBER) > -1) {
        handleCallToggle();
      } else {
        Serial.println("[SECURITY] Unauthorized call blocked.");
      }
      continue;
    }

    // ─── INCOMING SMS (via +CMT:) ────────────────
    if (line.indexOf("+CMT:") > -1) {
      if (line.indexOf(MASTER_NUMBER) == -1) {
        Serial.println("[SECURITY] Unauthorized SMS blocked.");
        continue;
      }

      delay(300); // Give the SMS body time to arrive.
      String body = sim800l.readStringUntil('\n');
      body.trim();
      body.toUpperCase();

      Serial.println("[SMS] Body: '" + body + "'");
      handleSMS(body);
      continue;
    }
  }
}

// SMS command handler

void handleSMS(String msg) {
  if (msg == "ON") {
    if (systemState == STATE_LOCKED) {
      sendSMS("SYSTEM LOCKED\nSend RESET to unlock");
      return;
    }
    if (isMotorOn) {
      sendSMS("Motor already ON\n" + buildStatus());
      return;
    }
    turnMotorOn("SMS");

  } else if (msg == "OFF") {
    if (!isMotorOn) {
      sendSMS("Motor already OFF\n" + buildStatus());
      return;
    }
    turnMotorOff("SMS");

  } else if (msg == "STATUS") {
    sendSMS(buildStatus());

  } else if (msg == "RESET") {
    if (systemState == STATE_LOCKED || systemState == STATE_FAULT) {
      fullSystemReset();
      sendSMS("SYSTEM RESET OK\n" + buildStatus());
    } else {
      sendSMS("No reset needed\n" + buildStatus());
    }
  } else {
    sendSMS("UNKNOWN COMMAND\nValid: ON OFF STATUS RESET");
  }
}

// Call handler

void handleCallToggle() {
  if (systemState == STATE_LOCKED) {
    sendSMS("SYSTEM LOCKED\nSend RESET to unlock");
    return;
  }

  if (isMotorOn) turnMotorOff("CALL");
  else turnMotorOn("CALL");
}

// Motor control

void turnMotorOn(String trigger) {
  if (isMotorOn) return;
  isMotorOn       = true;
  lastTrigger     = trigger;
  pendingRetry    = false;
  currentFault    = FAULT_NONE;
  systemState     = STATE_RUNNING;
  motorStartTime  = millis();
  motorRunTime    = 0;

  digitalWrite(RELAY_PIN, RELAY_ON);
  digitalWrite(GREEN_LED, LED_ON);
  digitalWrite(RED_LED,   LED_OFF);
  Serial.println("[MOTOR] ON via " + trigger);
  sendSMS("MOTOR ON\n" + buildStatus());
}

void turnMotorOff(String trigger) {
  if (!isMotorOn) return;
  motorRunTime = millis() - motorStartTime;
  isMotorOn    = false;
  lastTrigger  = trigger;
  pendingRetry = false;
  if (systemState == STATE_RUNNING) {
    systemState  = STATE_IDLE;
    currentFault = FAULT_NONE;
  }

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(GREEN_LED, LED_OFF);
  digitalWrite(RED_LED,   LED_OFF);

  Serial.println("[MOTOR] OFF via " + trigger);
  sendSMS("MOTOR OFF\n" + buildStatus());
}

// Status message

String buildStatus() {
  String msg = "";
  msg += "MOTOR: " + String(isMotorOn ? "ON" : "OFF") + "\n";
  msg += "CONDITION: " + String((systemState == STATE_FAULT || systemState == STATE_LOCKED) ? "FAULT" : "NORMAL") + "\n";

  String faultStr = "NO";
  if      (currentFault == FAULT_DRY_RUN)  faultStr = "DRY RUN";
  else if (currentFault == FAULT_OVERLOAD) faultStr = "OVERLOAD";
  msg += "FAULT: " + faultStr + "\n";

  msg += "VIA: " + lastTrigger + "\n";
  msg += "CURRENT: " + String(currentAmps, 2) + "A\n";

  if (isMotorOn) {
    unsigned long elapsed = millis() - motorStartTime;
    msg += "RUNNING: " + formatTime(elapsed) + "\n";
  } else if (motorRunTime > 0) {
    msg += "LAST RUN: " + formatTime(motorRunTime) + "\n";
  }

  if (systemState == STATE_FAULT || systemState == STATE_LOCKED) {
    msg += "RETRY: " + String(retryCount) + "/" + String(MAX_RETRIES) + "\n";
  }

  if (systemState == STATE_LOCKED) {
    msg += "STATUS: SYSTEM LOCKED\nSend RESET to unlock";
  }

  return msg;
}

String formatTime(unsigned long ms) {
  unsigned long totalSec = ms / 1000;
  unsigned long hrs  = totalSec / 3600;
  unsigned long mins = (totalSec % 3600) / 60;
  unsigned long secs = totalSec % 60;
  return String(hrs) + "h " + String(mins) + "m " + String(secs) + "s";
}

// Current monitoring

void monitorCurrent() {
  if (millis() - lastCurrentRead < 500) return;
  lastCurrentRead = millis();

  if (!isMotorOn) {
    currentAmps = 0.0;
    return;
  }

  long sum = 0;
  for (int i = 0; i < CURRENT_SAMPLES; i++) {
    sum += analogRead(ACS712_PIN);
    delayMicroseconds(200);
  }
  float avgRaw = sum / (float)CURRENT_SAMPLES;
  float voltage = (avgRaw / 4095.0) * 3.3;
  currentAmps   = abs((voltage - zeroVoltage) / ACS712_SENSITIVITY);

  if (currentAmps < 0.03) currentAmps = 0.0;
}

// Fault detection

void checkFaults() {
  if (!isMotorOn || systemState != STATE_RUNNING) return;
  if (millis() - motorStartTime < STARTUP_IGNORE_MS) return;

  bool faultDetected = false;
  if (currentAmps > 0.01 && currentAmps < DRY_RUN_THRESHOLD) {
    currentFault = FAULT_DRY_RUN;
    faultDetected = true;
  } else if (currentAmps > OVERLOAD_THRESHOLD) {
    currentFault = FAULT_OVERLOAD;
    faultDetected = true;
  }

  if (faultDetected) triggerFault();
}

void triggerFault() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(GREEN_LED, LED_OFF);

  isMotorOn    = false;
  motorRunTime = millis() - motorStartTime;
  systemState  = STATE_FAULT;
  retryCount++;

  if (retryCount <= MAX_RETRIES) {
    pendingRetry = true;
    retryTimer   = millis();
    String faultName = (currentFault == FAULT_DRY_RUN) ? "DRY RUN" : "OVERLOAD";
    String alertMsg  = "FAULT: " + faultName + "\n";
    alertMsg += buildStatus();
    alertMsg += "\nRetrying " + String(retryCount) + "/" + String(MAX_RETRIES) + " in 30s";
    sendSMS(alertMsg);
  } else {
    pendingRetry = false;
    systemState  = STATE_LOCKED;
    String lockMsg = "SYSTEM LOCKED\n";
    lockMsg += buildStatus();
    lockMsg += "\nSend RESET to unlock";
    sendSMS(lockMsg);
  }
}

// Automatic retry

void handleAutoRetry() {
  if (!pendingRetry) return;
  if (systemState != STATE_FAULT) return;
  if (millis() - retryTimer < RETRY_INTERVAL_MS) return;

  pendingRetry = false;
  isMotorOn       = true;
  systemState     = STATE_RUNNING;
  motorStartTime  = millis();
  lastTrigger     = "RETRY-" + String(retryCount);

  digitalWrite(RELAY_PIN, RELAY_ON);
  digitalWrite(GREEN_LED, LED_ON);

  sendSMS("RETRYING " + String(retryCount) + "/" + String(MAX_RETRIES) + "\n" + buildStatus());
}

// System reset

void fullSystemReset() {
  isMotorOn     = false;
  pendingRetry  = false;
  currentFault  = FAULT_NONE;
  retryCount    = 0;
  motorRunTime  = 0;
  lastTrigger   = "RESET";
  systemState   = STATE_IDLE;

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(GREEN_LED, LED_OFF);
  digitalWrite(RED_LED,   LED_OFF);
}

// LCD display

void lcdShowBoot(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(padRight(line1, 16));
  lcd.setCursor(0, 1);
  lcd.print(padRight(line2, 16));
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (systemState == STATE_LOCKED) {
    lcd.print("SYSTEM LOCKED!  ");
  } else {
    String row0 = "MOTOR:";
    row0 += isMotorOn ? "ON  " : "OFF ";
    row0 += isNetworkOK ? "NET:OK" : "NET:--";
    lcd.print(padRight(row0, 16));
  }

  lcd.setCursor(0, 1);
  if (systemState == STATE_LOCKED) {
    lcd.print("SEND RESET CMD  ");
  } else if (systemState == STATE_FAULT && pendingRetry) {
    String dots = "";
    for (int i = 0; i < (lcdAnimFrame % 4); i++) dots += ".";
    String row1 = "RETRY " + String(retryCount) + "/" + String(MAX_RETRIES) + dots;
    lcd.print(padRight(row1, 16));
    lcdAnimFrame++;
  } else if (systemState == STATE_FAULT) {
    String faultLabel = (currentFault == FAULT_DRY_RUN) ? "DRY RUN" : "OVERLOAD";
    lcd.print(padRight("FLT:" + faultLabel, 16));
  } else if (isMotorOn) {
    String row1 = String(currentAmps, 2) + "A ";
    unsigned long elapsed = (millis() - motorStartTime) / 1000;
    unsigned long m = elapsed / 60;
    unsigned long s = elapsed % 60;
    row1 += padTwo(m) + ":" + padTwo(s) + " RUN";
    lcd.print(padRight(row1, 16));
  } else {
    if (motorRunTime > 0) {
      String row1 = "LAST:" + formatTime(motorRunTime).substring(0, 11);
      lcd.print(padRight(row1, 16));
    } else {
      lcd.print("IDLE  0.00A     ");
    }
  }
}

// Red LED indicator

void blinkRedLED() {
  if (systemState == STATE_LOCKED) {
    if (millis() - lastBlinkTime >= 200) {
      blinkState = !blinkState;
      digitalWrite(RED_LED, blinkState ? LED_ON : LED_OFF);
      lastBlinkTime = millis();
    }
  } else if (systemState == STATE_FAULT) {
    if (millis() - lastBlinkTime >= 700) {
      blinkState = !blinkState;
      digitalWrite(RED_LED, blinkState ? LED_ON : LED_OFF);
      lastBlinkTime = millis();
    }
  } else {
    digitalWrite(RED_LED, LED_OFF);
    blinkState = false;
  }
}

// Send SMS

void sendSMS(String text) {
  while (sim800l.available()) sim800l.read();

  sim800l.println("AT+CMGF=1");
  delay(300);

  sim800l.print("AT+CMGS=\"");
  sim800l.print(MASTER_NUMBER);
  sim800l.println("\"");
  delay(800);

  sim800l.print(text);
  delay(200);
  sim800l.write(26);
  delay(3500);
}

// AT command helper

void sendAT(String cmd, int wait) {
  while (sim800l.available()) sim800l.read();
  sim800l.println(cmd);
  delay(wait);
}

// String utilities

String padRight(String s, int width) {
  while ((int)s.length() < width) s += ' ';
  if ((int)s.length() > width) s = s.substring(0, width);
  return s;
}

String padTwo(unsigned long n) {
  return (n < 10 ? "0" : "") + String(n);
}
