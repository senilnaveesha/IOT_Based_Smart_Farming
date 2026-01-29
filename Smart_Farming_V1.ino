/*
  IoT-Based Smart Farming System (ESP32) - 4 Zones
  Features:
  - 4 soil moisture sensors (analog)
  - 4 relays/valves/pump controls (digital)
  - Median filtering for stable readings
  - Hysteresis thresholds (ON/OFF)
  - Cooldown + min ON time
  - Optional watering time window
  - Sensor fault detection
  - Serial debug + simple calibration helper

  Author: Senil Jayamanna (Mahazon Studios / SNJ361)
*/

#include <Arduino.h>

// =========================
// USER CONFIG
// =========================

// How many zones?
static const uint8_t ZONES = 4;

// ----- Pin mapping (ESP32 ADC pins: use ADC1 pins for stability) -----
// Recommended ADC1 pins: 32-39. Avoid ADC2 when Wi-Fi is used.
const uint8_t MOISTURE_PINS[ZONES] = {34, 35, 32, 33};  // Analog inputs
const uint8_t RELAY_PINS[ZONES]    = {25, 26, 27, 14};  // Digital outputs

// Relay logic:
// Many relay modules are ACTIVE-LOW (LOW turns relay ON). Set true if yours is active-low.
static const bool RELAY_ACTIVE_LOW = true;

// ----- Moisture calibration (RAW ADC -> %)
// For each sensor i, set ADC value when sensor in DRY AIR and in WATER.
// You can start with rough values and adjust later using the calibration helper.
int DRY_ADC[ZONES]   = {3200, 3200, 3200, 3200};  // higher = drier (common)
int WET_ADC[ZONES]   = {1400, 1400, 1400, 1400};  // lower = wetter

// ----- Thresholds per zone (percentage) -----
// Water ON if moisture% < ON_THRESHOLD
// Water OFF when moisture% > OFF_THRESHOLD
// (OFF should be higher than ON)
uint8_t ON_THRESHOLD[ZONES]  = {35, 35, 35, 35};
uint8_t OFF_THRESHOLD[ZONES] = {45, 45, 45, 45};

// Watering behavior
uint32_t MIN_ON_TIME_MS      = 10UL * 1000;   // keep relay ON at least 10s
uint32_t MAX_ON_TIME_MS      = 30UL * 1000;   // safety stop per cycle (prevents flood)
uint32_t COOLDOWN_MS         = 2UL  * 60 * 1000; // 2 minutes between cycles per zone

// Sampling
uint16_t SAMPLE_INTERVAL_MS  = 1000;          // read every 1 sec
uint8_t  MEDIAN_SAMPLES      = 9;             // odd number (e.g., 5/7/9/11)

// Sensor fault detection
// If ADC is stuck near 0 or 4095 for too long, mark as faulty.
uint16_t FAULT_COUNT_LIMIT   = 30;            // ~30 seconds if interval is 1s
uint16_t ADC_NEAR_MIN        = 20;
uint16_t ADC_NEAR_MAX        = 4090;

// Optional watering time window (local "fake clock")
// If you have no RTC/NTP, you can disable this.
// Set to false to ignore time window.
static const bool USE_TIME_WINDOW = false;
// Allowed watering hours (0-23). Example: allow 5AM-9AM and 5PM-9PM.
// We'll simulate "hour" using millis() for demo unless you add RTC/NTP.
uint8_t ALLOWED_START_HOUR_1 = 5;
uint8_t ALLOWED_END_HOUR_1   = 9;
uint8_t ALLOWED_START_HOUR_2 = 17;
uint8_t ALLOWED_END_HOUR_2   = 21;

// =========================
// INTERNAL STATE
// =========================
struct ZoneState {
  bool watering = false;
  uint32_t lastSampleMs = 0;

  uint32_t wateringStartMs = 0;
  uint32_t lastWateringEndMs = 0;

  uint16_t faultMinCount = 0;
  uint16_t faultMaxCount = 0;
  bool sensorFault = false;

  uint8_t lastMoistPercent = 0;
  uint16_t lastAdc = 0;
};

ZoneState zone[ZONES];

// =========================
// UTILITIES
// =========================

// Relay write helper
void setRelay(uint8_t i, bool on) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_PINS[i], on ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_PINS[i], on ? HIGH : LOW);
  }
  zone[i].watering = on;
  if (on) zone[i].wateringStartMs = millis();
  else zone[i].lastWateringEndMs = millis();
}

// Simple sort (small array) for median
void sortArray(uint16_t *arr, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t j = i + 1; j < n; j++) {
      if (arr[j] < arr[i]) {
        uint16_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
      }
    }
  }
}

// Read median ADC value
uint16_t readMedianAdc(uint8_t pin, uint8_t samples) {
  if (samples < 3) samples = 3;
  if (samples % 2 == 0) samples++; // make odd

  // Limit to avoid huge stack
  if (samples > 15) samples = 15;

  uint16_t vals[15];
  for (uint8_t k = 0; k < samples; k++) {
    vals[k] = analogRead(pin);
    delay(10); // small settle
  }
  sortArray(vals, samples);
  return vals[samples / 2];
}

// Map ADC to moisture percentage with calibration
// Returns 0..100
uint8_t adcToPercent(uint16_t adc, int dry, int wet) {
  // Handle reversed calibrations safely
  if (dry == wet) return 0;

  // Many capacitive sensors: wet ADC < dry ADC
  // We'll map wet -> 100%, dry -> 0%
  long pct = map((long)adc, (long)dry, (long)wet, 0L, 100L);

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

// Cooldown check
bool cooldownPassed(uint8_t i) {
  uint32_t now = millis();
  if (zone[i].lastWateringEndMs == 0) return true; // never watered
  return (now - zone[i].lastWateringEndMs) >= COOLDOWN_MS;
}

// Optional time window check
bool wateringAllowedNow() {
  if (!USE_TIME_WINDOW) return true;

  // Demo clock: one "day" = 24 minutes (1 min per hour) to simulate time with millis
  // Replace this with RTC/NTP when you add real time.
  uint32_t mins = (millis() / 60000UL) % 24; // 0..23
  uint8_t hour = (uint8_t)mins;

  bool in1 = (hour >= ALLOWED_START_HOUR_1 && hour <= ALLOWED_END_HOUR_1);
  bool in2 = (hour >= ALLOWED_START_HOUR_2 && hour <= ALLOWED_END_HOUR_2);
  return in1 || in2;
}

// Fault detection update
void updateFault(uint8_t i, uint16_t adc) {
  if (adc <= ADC_NEAR_MIN) zone[i].faultMinCount++;
  else zone[i].faultMinCount = 0;

  if (adc >= ADC_NEAR_MAX) zone[i].faultMaxCount++;
  else zone[i].faultMaxCount = 0;

  // If either near-min or near-max persists, flag fault
  if (zone[i].faultMinCount >= FAULT_COUNT_LIMIT || zone[i].faultMaxCount >= FAULT_COUNT_LIMIT) {
    zone[i].sensorFault = true;
  }
}

// Serial commands (simple):
//  STATUS
//  CAL Z <zoneIndex 1-4> DRY <value>
//  CAL Z <zoneIndex 1-4> WET <value>
//  SHOWCAL
void handleSerial() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "STATUS") {
    Serial.println("\n=== STATUS ===");
    for (uint8_t i = 0; i < ZONES; i++) {
      Serial.print("Zone "); Serial.print(i + 1);
      Serial.print(" | ADC="); Serial.print(zone[i].lastAdc);
      Serial.print(" | Moist%="); Serial.print(zone[i].lastMoistPercent);
      Serial.print(" | Watering="); Serial.print(zone[i].watering ? "YES" : "NO");
      Serial.print(" | Fault="); Serial.println(zone[i].sensorFault ? "YES" : "NO");
    }
    Serial.println("==============\n");
    return;
  }

  if (cmd == "SHOWCAL") {
    Serial.println("\n=== CALIBRATION ===");
    for (uint8_t i = 0; i < ZONES; i++) {
      Serial.print("Zone "); Serial.print(i + 1);
      Serial.print(" | DRY_ADC="); Serial.print(DRY_ADC[i]);
      Serial.print(" | WET_ADC="); Serial.println(WET_ADC[i]);
    }
    Serial.println("===================\n");
    return;
  }

  // Parse CAL commands
  // Example: CAL Z 1 DRY 3200
  if (cmd.startsWith("CAL ")) {
    // very basic parsing
    int zPos = cmd.indexOf("Z ");
    if (zPos < 0) {
      Serial.println("CAL format error. Example: CAL Z 1 DRY 3200");
      return;
    }
    int afterZ = zPos + 2;
    int sp1 = cmd.indexOf(' ', afterZ);
    if (sp1 < 0) return;

    int zoneIndex = cmd.substring(afterZ, sp1).toInt(); // 1..4
    if (zoneIndex < 1 || zoneIndex > ZONES) {
      Serial.println("Zone index must be 1-4");
      return;
    }
    uint8_t i = (uint8_t)(zoneIndex - 1);

    bool isDry = cmd.indexOf(" DRY ") >= 0;
    bool isWet = cmd.indexOf(" WET ") >= 0;

    int lastSpace = cmd.lastIndexOf(' ');
    if (lastSpace < 0) return;
    int val = cmd.substring(lastSpace + 1).toInt();
    if (val <= 0) {
      Serial.println("Invalid value");
      return;
    }

    if (isDry) {
      DRY_ADC[i] = val;
      Serial.print("Set Zone "); Serial.print(zoneIndex);
      Serial.print(" DRY_ADC="); Serial.println(val);
    } else if (isWet) {
      WET_ADC[i] = val;
      Serial.print("Set Zone "); Serial.print(zoneIndex);
      Serial.print(" WET_ADC="); Serial.println(val);
    } else {
      Serial.println("Specify DRY or WET. Example: CAL Z 2 WET 1400");
    }
    return;
  }

  Serial.println("Unknown command. Try: STATUS | SHOWCAL | CAL Z 1 DRY 3200 | CAL Z 1 WET 1400");
}

// =========================
// MAIN CONTROL LOOP
// =========================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nESP32 Smart Farming (4 Zones) Booting...");

  // ESP32 ADC config
  analogReadResolution(12); // 0-4095
  // Optional: analogSetAttenuation(ADC_11db); // if needed for range

  // Relays
  for (uint8_t i = 0; i < ZONES; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    setRelay(i, false); // OFF initially
  }

  Serial.println("Ready. Type STATUS in Serial Monitor.");
}

void loop() {
  handleSerial();

  uint32_t now = millis();

  for (uint8_t i = 0; i < ZONES; i++) {
    if (now - zone[i].lastSampleMs < SAMPLE_INTERVAL_MS) continue;
    zone[i].lastSampleMs = now;

    // Read moisture
    uint16_t adc = readMedianAdc(MOISTURE_PINS[i], MEDIAN_SAMPLES);
    zone[i].lastAdc = adc;

    updateFault(i, adc);

    // If sensor fault, shut off watering for safety
    if (zone[i].sensorFault) {
      if (zone[i].watering) {
        Serial.print("Zone "); Serial.print(i + 1);
        Serial.println(" SENSOR FAULT -> STOP watering!");
        setRelay(i, false);
      }
      continue;
    }

    uint8_t moistPct = adcToPercent(adc, DRY_ADC[i], WET_ADC[i]);
    zone[i].lastMoistPercent = moistPct;

    // Safety: If watering too long, stop
    if (zone[i].watering) {
      uint32_t onTime = now - zone[i].wateringStartMs;
      if (onTime >= MAX_ON_TIME_MS) {
        Serial.print("Zone "); Serial.print(i + 1);
        Serial.println(" reached MAX_ON_TIME -> STOP");
        setRelay(i, false);
        continue;
      }
    }

    // Decision logic (hysteresis + min on time + cooldown + time window)
    if (!zone[i].watering) {
      // Consider turning ON
      if (moistPct < ON_THRESHOLD[i] && cooldownPassed(i) && wateringAllowedNow()) {
        Serial.print("Zone "); Serial.print(i + 1);
        Serial.print(" dry ("); Serial.print(moistPct);
        Serial.println("%) -> WATER ON");
        setRelay(i, true);
      }
    } else {
      // Consider turning OFF
      uint32_t onTime = now - zone[i].wateringStartMs;

      // Must respect min on-time
      if (onTime >= MIN_ON_TIME_MS) {
        if (moistPct > OFF_THRESHOLD[i]) {
          Serial.print("Zone "); Serial.print(i + 1);
          Serial.print(" recovered ("); Serial.print(moistPct);
          Serial.println("%) -> WATER OFF");
          setRelay(i, false);
        }
      }
    }
  }
}
