// ===============================
// BOX B - RECEIVER (GatePulse)
// ===============================

#include <SPI.h>
#include <RF24.h>

#define LED_PIN    6   // Active-low LED driver - LOW = on, HIGH = off
#define BUZZER_PIN 4   // Piezo buzzer - HIGH = on, LOW = off
#define BUTTON_PIN 5   // Mode button - LOW when pressed (INPUT_PULLUP)

#define CE_PIN     2
#define CSN_PIN    3

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "GATEP";

// --- State ---
bool gateOpen  = false;
int  mode      = 1;    // 1 = normal (LED+buzzer), 2 = silent (LED only)

// --- LED ---
unsigned long ledTimer = 0;
bool          ledState = false;

// --- Buzzer state machine ---
// State 0: idle
// State 1: playing 3 initial beeps
// State 2: waiting 2 minutes
// State 3: playing 2 reminder beeps
int           buzzerState = 0;
unsigned long buzzerTimer = 0;
int           beepCount   = 0;
bool          buzzerOn    = false;

const unsigned long BEEP_ON  = 200;
const unsigned long BEEP_OFF = 250;
const unsigned long REMIND   = 120000;   // 2 minutes

// --- Button ---
bool          lastBtn       = HIGH;
unsigned long btnTimer      = 0;
bool          btnActed      = false;
const unsigned long BTN_DEBOUNCE = 50;

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN,    HIGH);  // LED off
  digitalWrite(BUZZER_PIN, LOW);   // Buzzer off

  delay(100);

  if (!radio.begin()) {
    Serial.println("ERROR: Radio failed");
    while (1) {
      digitalWrite(LED_PIN, LOW);  delay(100);
      digitalWrite(LED_PIN, HIGH); delay(100);
    }
  }

  radio.openReadingPipe(0, address);
  radio.setAutoAck(true);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(108);
  radio.startListening();

  // Startup: 3 LED blinks + 1 beep
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);  delay(300);
    digitalWrite(LED_PIN, HIGH); delay(300);
  }
  digitalWrite(BUZZER_PIN, HIGH); delay(100);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("Box B Ready - Mode 1 (Normal)");
}

// ---------------------------------------------------------------------------
void loop() {
  handleRadio();
  handleButton();
  handleLED();
  handleBuzzer();
}

// ---------------------------------------------------------------------------
void handleRadio() {
  if (!radio.available()) return;

  char msg[32] = {0};
  radio.read(msg, sizeof(msg));
  Serial.print("RX: ");
  Serial.println(msg);

  if (strcmp(msg, "Gate Open") == 0) {
    if (!gateOpen) {
      // Only trigger alarm on transition from closed to open
      gateOpen    = true;
      buzzerState = 0;   // Arm the beep sequence
      digitalWrite(LED_PIN, LOW);   // LED on immediately
      Serial.println("Gate OPEN");
    }
    // If already open: heartbeat received, do nothing - alarm already running
  }

  else if (strcmp(msg, "Gate Closed") == 0) {
    // Always clear alarm on any Gate Closed message, whether state change or heartbeat
    // This is what self-corrects any ghost Gate Open
    if (gateOpen) {
      Serial.println("Gate CLOSED");
    } else {
      Serial.println("Gate CLOSED (heartbeat - already closed)");
    }
    gateOpen    = false;
    buzzerState = 0;
    buzzerOn    = false;
    beepCount   = 0;
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN,    HIGH);
  }
}

// ---------------------------------------------------------------------------
void handleButton() {
  bool btn = digitalRead(BUTTON_PIN);

  if (btn != lastBtn) {
    btnTimer = millis();
    lastBtn  = btn;
  }

  if (millis() - btnTimer >= BTN_DEBOUNCE) {
    if (btn == LOW && !btnActed) {
      btnActed = true;
      mode = (mode == 1) ? 2 : 1;
      Serial.print("Mode: ");
      Serial.println(mode);

      if (mode == 2) {
        // Entering silent mode
        digitalWrite(BUZZER_PIN, LOW);
        buzzerOn  = false;
        beepCount = 0;
        // Two LED flashes as confirmation
        for (int i = 0; i < 2; i++) {
          digitalWrite(LED_PIN, LOW);  delay(120);
          digitalWrite(LED_PIN, HIGH); delay(150);
        }
        // Restore LED state
        if (gateOpen) digitalWrite(LED_PIN, ledState ? LOW : HIGH);
      } else {
        // Returning to normal mode
        if (gateOpen) {
          // Skip re-triggering 3-beep burst, go straight to reminder wait
          buzzerState = 2;
          buzzerTimer = millis();
          beepCount   = 0;
          buzzerOn    = false;
        }
        // Two beeps as confirmation
        for (int i = 0; i < 2; i++) {
          digitalWrite(BUZZER_PIN, HIGH); delay(70);
          digitalWrite(BUZZER_PIN, LOW);  delay(120);
        }
      }
    }
    if (btn == HIGH) btnActed = false;
  }
}

// ---------------------------------------------------------------------------
void handleLED() {
  if (!gateOpen) {
    digitalWrite(LED_PIN, HIGH);  // Gate closed - LED off
    return;
  }
  // Gate open - flash at 1Hz
  if (millis() - ledTimer >= 500) {
    ledTimer = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? LOW : HIGH);
  }
}

// ---------------------------------------------------------------------------
void handleBuzzer() {
  unsigned long now = millis();

  // Always idle and silent when gate is closed
  if (!gateOpen) {
    buzzerState = 0;
    buzzerOn    = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  // Silent mode - keep buzzer off, preserve state for when mode resumes
  if (mode == 2) {
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  switch (buzzerState) {

    case 0:
      // Entry - arm 3-beep sequence
      buzzerState = 1;
      beepCount   = 0;
      buzzerOn    = false;
      buzzerTimer = now;
      break;

    case 1: {
      // 3 initial beeps
      unsigned long interval = buzzerOn ? BEEP_ON : BEEP_OFF;
      if (now - buzzerTimer >= interval) {
        buzzerTimer = now;
        buzzerOn    = !buzzerOn;
        digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
        if (buzzerOn) {
          beepCount++;
        } else {
          // Check completion on OFF so last beep plays fully
          if (beepCount >= 3) {
            buzzerState = 2;
            buzzerTimer = now;
            beepCount   = 0;
            Serial.println("3 beeps done");
          }
        }
      }
      break;
    }

    case 2:
      // Wait 2 minutes
      if (now - buzzerTimer >= REMIND) {
        buzzerState = 3;
        buzzerTimer = now;
        beepCount   = 0;
        buzzerOn    = false;
        Serial.println("Reminder");
      }
      break;

    case 3: {
      // 2 reminder beeps
      unsigned long interval = buzzerOn ? BEEP_ON : BEEP_OFF;
      if (now - buzzerTimer >= interval) {
        buzzerTimer = now;
        buzzerOn    = !buzzerOn;
        digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
        if (buzzerOn) {
          beepCount++;
        } else {
          // Check completion on OFF so last beep plays fully
          if (beepCount >= 2) {
            buzzerState = 2;
            buzzerTimer = now;
            beepCount   = 0;
            Serial.println("Reminder done");
          }
        }
      }
      break;
    }
  }
}
