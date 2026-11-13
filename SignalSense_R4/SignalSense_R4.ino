/*
  ===================================================================
   SignalSense – AI Smart Traffic Lab  ·  Arduino UNO R4 WiFi sketch
  ===================================================================
   Works with index.html (Web Bluetooth, Chrome / Edge).

   HARDWARE
   ------------------------------------------------------------------
   Traffic LEDs (each with a 220 Ω resistor to GND)
     RED    -> D2
     YELLOW -> D3
     GREEN  -> D4
   TM1637 4-digit display
     CLK -> D6     DIO -> D7     VCC -> 5V     GND -> GND
   Servos (signal wire)
     Servo 1 "Main gate"  -> D9   (open on GREEN/YELLOW, closed on RED)
     Servo 2 "Cross gate" -> D10  (opposite of servo 1 by default)
     Servo 3 "Side gate"  -> D11  (opposite or same, set in the app)
   Power the servos from an external 5 V supply (or a good USB supply).
   Connect the supply GND to Arduino GND.

   LIBRARIES (Arduino IDE -> Tools -> Manage Libraries)
     - ArduinoBLE           (by Arduino)
     - Servo                (by Arduino, built-in)
     - TM1637Display        (search "TM1637" by Avishay Orpaz)
   Board: "Arduino UNO R4 WiFi" (needs the Bluetooth radio; the
   UNO R4 Minima has no Bluetooth).

   COMMANDS FROM THE APP (text, one per write)
     CFG,green,yellow,red,max      light times in seconds, max = cap
     SRV,i,open,close,mode         i = 0..2, angles 0..180, mode 0 = same as main, 1 = opposite
     ADD,R,sec  / ADD,G,sec        add seconds to the current red / green
     GO,R / GO,Y / GO,G            jump to a colour now
     RUN,0 / RUN,1                 pause / resume
     TST,i,angle                   move a servo for 1.5 s to test it
     SAVE                          store settings in EEPROM (kept after power off)
     HELLO                         ask for status
   STATUS TO THE APP (notify):  S,<R|Y|G>,<seconds left>,<paused 0|1>
  ===================================================================
*/
#include <ArduinoBLE.h>
#include <Servo.h>
#include <TM1637Display.h>
#include <EEPROM.h>

/* ---------- pins ---------- */
const uint8_t PIN_RED = 2, PIN_YEL = 3, PIN_GRN = 4;
const uint8_t PIN_CLK = 6, PIN_DIO = 7;
const uint8_t SERVO_PINS[3] = {9, 10, 11};

/* ---------- Bluetooth (must match index.html) ---------- */
#define SVC_UUID  "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CMD_UUID  "19b10001-e8f2-537e-4f6c-d104768a1214"
#define STAT_UUID "19b10002-e8f2-537e-4f6c-d104768a1214"

BLEService svc(SVC_UUID);
BLEStringCharacteristic cmdChar(CMD_UUID, BLEWrite | BLEWriteWithoutResponse, 64);
BLEStringCharacteristic statChar(STAT_UUID, BLERead | BLENotify, 32);

TM1637Display display(PIN_CLK, PIN_DIO);
Servo servos[3];

/* ---------- types (kept at the top so the Arduino IDE prototypes compile) ---------- */
enum Phase : uint8_t { GREEN, YELLOW, RED };

/* ---------- settings (saved in EEPROM) ---------- */
struct Config {
  uint16_t magic;
  uint8_t green, yellow, red, maxPhase;
  uint8_t sOpen[3], sClose[3], sMode[3];   // sMode: 0 = same as main, 1 = opposite
};
const uint16_t MAGIC = 0x5A17;
Config cfg;

void defaultConfig() {
  cfg.magic = MAGIC; cfg.green = 15; cfg.yellow = 3; cfg.red = 15; cfg.maxPhase = 60;
  for (int i = 0; i < 3; i++) { cfg.sOpen[i] = 90; cfg.sClose[i] = 0; cfg.sMode[i] = (i == 0) ? 0 : 1; }
}

/* ---------- traffic state ---------- */
Phase phase = GREEN;
unsigned long phaseStart = 0, phaseEnd = 0;
bool paused = false;
unsigned long pausedLeft = 0;
unsigned long testUntil = 0;
unsigned long lastNotify = 0, lastShow = 0;
String lastStatus = "";
int lastShown = -1;

char phaseChar(Phase p) { return p == RED ? 'R' : (p == YELLOW ? 'Y' : 'G'); }
unsigned long phaseMs(Phase p) {
  uint8_t s = (p == GREEN) ? cfg.green : (p == YELLOW ? cfg.yellow : cfg.red);
  return (unsigned long)s * 1000UL;
}

/* lights + gates follow the current colour */
void applyOutputs() {
  digitalWrite(PIN_RED, phase == RED);
  digitalWrite(PIN_YEL, phase == YELLOW);
  digitalWrite(PIN_GRN, phase == GREEN);
  bool mainOpen = (phase != RED);              // main gate: open on green & yellow
  for (int i = 0; i < 3; i++) {
    bool open = (i == 0 || cfg.sMode[i] == 0) ? mainOpen : !mainOpen;
    servos[i].write(open ? cfg.sOpen[i] : cfg.sClose[i]);
  }
}

void enterPhase(Phase p) {
  phase = p;
  phaseStart = millis();
  phaseEnd = phaseStart + phaseMs(p);
  if (paused) pausedLeft = phaseMs(p);
  applyOutputs();
}

void nextPhase() {
  if (phase == GREEN) enterPhase(YELLOW);
  else if (phase == YELLOW) enterPhase(RED);
  else enterPhase(GREEN);
}

int secondsLeft() {
  long ms = paused ? (long)pausedLeft : (long)(phaseEnd - millis());
  if (ms < 0) ms = 0;
  return (int)((ms + 999) / 1000);
}

/* add time to the current light, never beyond the "max" cap */
void addTime(Phase p, int sec) {
  if (p != phase || sec <= 0) return;
  unsigned long add = (unsigned long)sec * 1000UL;
  if (paused) { pausedLeft += add; return; }
  unsigned long cap = phaseStart + (unsigned long)cfg.maxPhase * 1000UL;
  unsigned long target = phaseEnd + add;
  if ((long)(target - cap) > 0) target = cap;
  if ((long)(target - phaseEnd) > 0) phaseEnd = target;
}

void sendStatus(bool force) {
  String s = "S,";
  s += phaseChar(phase); s += ','; s += secondsLeft(); s += ','; s += (paused ? '1' : '0');
  if (force || s != lastStatus || millis() - lastNotify > 1000) {
    lastStatus = s; lastNotify = millis();
    if (BLE.connected()) statChar.writeValue(s);
  }
}

/* ---------- EEPROM ---------- */
void loadConfig() {
  EEPROM.get(0, cfg);
  if (cfg.magic != MAGIC || cfg.green == 0 || cfg.red == 0) defaultConfig();
}
void saveConfig() { EEPROM.put(0, cfg); }

/* ---------- command parser ---------- */
int splitInts(const String &s, int *out, int maxN) {   // reads numbers after the first comma
  int n = 0, start = s.indexOf(',') + 1;
  while (start > 0 && n < maxN) {
    int c = s.indexOf(',', start);
    String part = (c < 0) ? s.substring(start) : s.substring(start, c);
    out[n++] = part.toInt();
    if (c < 0) break;
    start = c + 1;
  }
  return n;
}

void handleCommand(String s) {
  s.trim();
  if (s.length() == 0) return;
  Serial.print("CMD: "); Serial.println(s);
  int v[6];

  if (s.startsWith("CFG")) {
    if (splitInts(s, v, 4) == 4) {
      cfg.green = constrain(v[0], 1, 250); cfg.yellow = constrain(v[1], 1, 250);
      cfg.red = constrain(v[2], 1, 250);   cfg.maxPhase = constrain(v[3], 5, 250);
    }
  } else if (s.startsWith("SRV")) {
    if (splitInts(s, v, 4) == 4 && v[0] >= 0 && v[0] < 3) {
      cfg.sOpen[v[0]] = constrain(v[1], 0, 180);
      cfg.sClose[v[0]] = constrain(v[2], 0, 180);
      cfg.sMode[v[0]] = (v[0] == 0) ? 0 : (v[3] ? 1 : 0);
      applyOutputs();
    }
  } else if (s.startsWith("ADD,")) {
    char c = s.charAt(4);
    int sec = s.substring(6).toInt();
    addTime(c == 'R' ? RED : (c == 'G' ? GREEN : YELLOW), sec);
  } else if (s.startsWith("GO,")) {
    char c = s.charAt(3);
    enterPhase(c == 'R' ? RED : (c == 'Y' ? YELLOW : GREEN));
  } else if (s.startsWith("RUN,")) {
    bool run = s.charAt(4) == '1';
    if (!run && !paused) { paused = true; pausedLeft = (long)(phaseEnd - millis()) > 0 ? phaseEnd - millis() : 0; }
    else if (run && paused) { paused = false; phaseEnd = millis() + pausedLeft; }
  } else if (s.startsWith("TST")) {
    if (splitInts(s, v, 2) == 2 && v[0] >= 0 && v[0] < 3) {
      servos[v[0]].write(constrain(v[1], 0, 180));
      testUntil = millis() + 1500;
    }
  } else if (s == "SAVE") {
    saveConfig();
  }
  sendStatus(true);
}

void onCmdWritten(BLEDevice central, BLECharacteristic characteristic) {
  handleCommand(cmdChar.value());
}
void onConnect(BLEDevice central) {
  Serial.print("Connected: "); Serial.println(central.address());
  sendStatus(true);
}
void onDisconnect(BLEDevice central) {
  Serial.println("Disconnected – still running on its own");
  BLE.advertise();
}

/* ---------- display ---------- */
const uint8_t SEG_SS[] = { 0x6D, 0x6D, 0x00, 0x00 };   // "SS" boot splash
void showTime() {
  int s = secondsLeft();
  if (s == lastShown && millis() - lastShow < 1000) return;
  lastShown = s; lastShow = millis();
  if (s > 9999) s = 9999;
  display.showNumberDec(s, false);                     // right-aligned seconds
}

/* ---------- setup / loop ---------- */
void setup() {
  Serial.begin(115200);
  pinMode(PIN_RED, OUTPUT); pinMode(PIN_YEL, OUTPUT); pinMode(PIN_GRN, OUTPUT);
  display.setBrightness(5);
  display.setSegments(SEG_SS);

  loadConfig();
  for (int i = 0; i < 3; i++) servos[i].attach(SERVO_PINS[i]);

  // LED self-test
  digitalWrite(PIN_RED, HIGH); delay(200); digitalWrite(PIN_YEL, HIGH); delay(200); digitalWrite(PIN_GRN, HIGH); delay(300);

  if (!BLE.begin()) {
    Serial.println("BLE failed to start! (Is this an UNO R4 WiFi?)");
  } else {
    BLE.setLocalName("SignalSense");
    BLE.setDeviceName("SignalSense");
    BLE.setAdvertisedService(svc);
    svc.addCharacteristic(cmdChar);
    svc.addCharacteristic(statChar);
    BLE.addService(svc);
    cmdChar.setEventHandler(BLEWritten, onCmdWritten);
    BLE.setEventHandler(BLEConnected, onConnect);
    BLE.setEventHandler(BLEDisconnected, onDisconnect);
    statChar.writeValue("S,G,0,0");
    BLE.advertise();
    Serial.println("SignalSense ready – open index.html and press Connect Arduino");
  }
  enterPhase(GREEN);
}

void loop() {
  BLE.poll();

  if (!paused && (long)(millis() - phaseEnd) >= 0) nextPhase();

  if (testUntil && (long)(millis() - testUntil) >= 0) { testUntil = 0; applyOutputs(); }

  showTime();
  if (millis() - lastNotify > 250) sendStatus(false);
}
