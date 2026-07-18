// ===============================
// BOX A - TRANSMITTER (GatePulse)
// ===============================

#include <SPI.h>
#include <RF24.h>
#include <EEPROM.h>

#define CE_PIN           2
#define CSN_PIN          3
#define SWITCH_PIN       4

#define EEPROM_ADDR      0
#define EEPROM_MARKER    0x42

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "GATEP";

// --- Gate state ---
bool gateOpen = false;

// --- Debounce ---
bool          lastSwitchRaw  = false;
unsigned long debounceTimer  = 0;
const unsigned long DEBOUNCE = 500;

// --- Sending ---
bool          needToSend   = false;
char          queuedMsg[16] = {0};
bool          waitingAck   = false;
unsigned long lastSendTime = 0;
const unsigned long RETRY  = 1000;

// --- Heartbeat ---
unsigned long lastHeartbeat = 0;
const unsigned long HB_OPEN   = 30000;
const unsigned long HB_CLOSED = 30000;

// ==========================
// 🆕 NRF AUTO-RECOVERY VARS
// ==========================
int failCount = 0;
const int FAIL_THRESHOLD = 5;  // Number of failed sends before recovery

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  // Log reset cause
  byte rst = MCUSR;
  MCUSR = 0;
  if (rst & (1 << PORF))  Serial.println("Reset: Power on");
  if (rst & (1 << EXTRF)) Serial.println("Reset: External");
  if (rst & (1 << BORF))  Serial.println("Reset: BROWNOUT");
  if (rst & (1 << WDRF))  Serial.println("Reset: Watchdog");

  delay(100);

  if (!radio.begin()) {
    Serial.println("ERROR: Radio failed");
    while (1);
  }

  initRadio();  // 🆕 moved config into function

  // Read switch directly for initial state
  bool switchNow = (digitalRead(SWITCH_PIN) == LOW);
  lastSwitchRaw  = switchNow;
  debounceTimer  = millis();

  // EEPROM recovery
  if (EEPROM.read(EEPROM_ADDR + 1) == EEPROM_MARKER) {
    bool saved = (bool)EEPROM.read(EEPROM_ADDR);
    Serial.print("EEPROM: was ");
    Serial.println(saved ? "OPEN" : "CLOSED");

    gateOpen = saved;

    if (saved != switchNow) {
      gateOpen = switchNow;
      saveState(gateOpen);
      Serial.println("State changed during power loss - syncing");
    }

    queueMessage(gateOpen ? "Gate Open" : "Gate Closed");

  } else {
    gateOpen = switchNow;
    saveState(gateOpen);
    Serial.println("First boot");
    queueMessage(gateOpen ? "Gate Open" : "Gate Closed");
  }

  lastHeartbeat = millis();

  Serial.print("Gate: ");
  Serial.println(gateOpen ? "OPEN" : "CLOSED");
  Serial.println("Box A Ready");
}

// ---------------------------------------------------------------------------
void loop() {

  // --- Read and debounce switch ---
  bool switchRaw = (digitalRead(SWITCH_PIN) == LOW);

  if (switchRaw != lastSwitchRaw) {
    debounceTimer = millis();
    lastSwitchRaw = switchRaw;
  }

  if ((millis() - debounceTimer >= DEBOUNCE) && (switchRaw != gateOpen)) {
    gateOpen = switchRaw;
    saveState(gateOpen);
    lastHeartbeat = millis();

    if (gateOpen) {
      Serial.println("GATE OPENED");
      queueMessage("Gate Open");
    } else {
      Serial.println("GATE CLOSED");
      queueMessage("Gate Closed");
    }
  }

  // --- Send queued message or retry ---
  if (needToSend || (waitingAck && millis() - lastSendTime >= RETRY)) {
    sendQueued();
  }

  // --- Heartbeat ---
  unsigned long hbInterval = gateOpen ? HB_OPEN : HB_CLOSED;
  if (!needToSend && !waitingAck && (millis() - lastHeartbeat >= hbInterval)) {
    Serial.println(gateOpen ? "HB: open" : "HB: closed");
    queueMessage(gateOpen ? "Gate Open" : "Gate Closed");
    lastHeartbeat = millis();
  }
}

// ---------------------------------------------------------------------------
// 🆕 RADIO INIT (no logic change, just reusable)
// ---------------------------------------------------------------------------
void initRadio() {
  radio.openWritingPipe(address);
  radio.setAutoAck(true);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(108);
  radio.setRetries(3, 5);
  radio.stopListening();
}

// ---------------------------------------------------------------------------
// 🆕 NRF RECOVERY FUNCTION
// ---------------------------------------------------------------------------
void recoverRadio() {
  Serial.println("⚠️ NRF RECOVERY TRIGGERED");

  radio.powerDown();
  delay(100);
  radio.powerUp();
  delay(100);

  initRadio();

  failCount = 0;  // reset failure counter
}

// ---------------------------------------------------------------------------
void queueMessage(const char* msg) {
  strcpy(queuedMsg, msg);
  needToSend  = true;
  waitingAck  = false;
}

// ---------------------------------------------------------------------------
// 🔧 UPDATED: sendQueued WITH AUTO RECOVERY
// ---------------------------------------------------------------------------
void sendQueued() {
  needToSend  = false;
  lastSendTime = millis();
  lastHeartbeat = millis();

  bool ok = radio.write(queuedMsg, strlen(queuedMsg) + 1);

  if (ok) {
    Serial.println("  [ACK OK]");
    waitingAck = false;

    failCount = 0;   // 🆕 reset on success

  } else {
    Serial.println("  [No ACK - retry]");
    waitingAck = true;

    failCount++;     // 🆕 count failures

    // 🆕 trigger recovery if stuck
    if (failCount >= FAIL_THRESHOLD) {
      recoverRadio();
    }
  }
}

// ---------------------------------------------------------------------------
void saveState(bool state) {
  EEPROM.write(EEPROM_ADDR,     (byte)state);
  EEPROM.write(EEPROM_ADDR + 1, EEPROM_MARKER);
}
