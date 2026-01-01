#include <Arduino.h>
#include "config.h"
#include "Buzzer.h"
#include "UiOled.h"
#include "Storage.h"
#include "Gate.h"

// ------------------------------------------------------------
// Global
// ------------------------------------------------------------
static uint32_t gateCounts[GATE_COUNT] = {0};

static Buzzer  buzzer;
static UiOled  ui;
static Storage storage;

static AppMode mode = AppMode::Run;
static uint8_t selectedGate = 0; // 0..GATE_COUNT-1

static Gate gates[GATE_COUNT];

// ------------------------------------------------------------
// Debounce button (INPUT_PULLUP, active LOW)
// ------------------------------------------------------------
struct DebouncedButton {
  uint8_t pin = 0;
  uint8_t stable = HIGH;
  uint8_t last = HIGH;
  uint32_t lastChange = 0;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    stable = digitalRead(pin);
    last = stable;
    lastChange = millis();
  }

  uint8_t readStable(uint32_t now, uint32_t debounceMs = 25) {
    uint8_t r = digitalRead(pin);
    if (r != last) { last = r; lastChange = now; }
    if (now - lastChange > debounceMs) stable = last;
    return stable;
  }

  bool isPressed(uint32_t now) { return readStable(now) == LOW; }
};

static DebouncedButton btn1;
static DebouncedButton btn2;

// ------------------------------------------------------------
// Press-sequence tracker (counts PRESS events inside window)
// ------------------------------------------------------------
struct PressTracker {
  uint32_t windowStart = 0;
  uint32_t lastPressMs = 0;
  uint8_t  count = 0;

  bool lastPressed = false;

  bool updatePress(bool pressed, uint32_t nowMs) {
    bool pressEdge = (pressed && !lastPressed);
    lastPressed = pressed;
    if (!pressEdge) return false;

    if (count == 0) windowStart = nowMs;
    count++;
    lastPressMs = nowMs;
    return true;
  }

  uint8_t finalizeIfReady(uint32_t nowMs, uint32_t windowMs, uint32_t gapEndMs) {
    if (count == 0) return 0;

    bool windowExpired = (nowMs - windowStart) > windowMs;
    bool gapExpired    = (nowMs - lastPressMs) > gapEndMs;

    if (windowExpired || gapExpired) {
      uint8_t out = count;
      count = 0;
      windowStart = 0;
      lastPressMs = 0;
      return out;
    }
    return 0;
  }
};

static PressTracker btn1Seq;
static PressTracker btn2Seq;

// ------------------------------------------------------------
// DIAG metrics (per selected gate) based on getStrength()
// ------------------------------------------------------------
static int16_t metNow = 0;
static int16_t metPeak = 0;
static uint32_t peakUntil = 0;

static int16_t metMin = 32767;
static int16_t metMax = -32768;
static uint32_t noiseWindowStart = 0;

static void resetDiagMetrics(uint32_t nowMs) {
  metNow = 0;
  metPeak = 0;
  peakUntil = nowMs;

  metMin = 32767;
  metMax = -32768;
  noiseWindowStart = nowMs;
}

static void updateDiagMetrics(int16_t v, uint32_t nowMs) {
  metNow = v;

  // peak hold 1500 ms
  if (v > metPeak || nowMs > peakUntil) {
    metPeak = v;
    peakUntil = nowMs + 1500;
  }

  // noise okno 600 ms
  if (noiseWindowStart == 0) noiseWindowStart = nowMs;
  if (v < metMin) metMin = v;
  if (v > metMax) metMax = v;

  if ((nowMs - noiseWindowStart) >= 600) {
    noiseWindowStart = nowMs;
    metMin = v;
    metMax = v;
  }
}

static int16_t getNoise() {
  int32_t n = (int32_t)metMax - (int32_t)metMin;
  if (n < 0) n = 0;
  if (n > 32767) n = 32767;
  return (int16_t)n;
}

// ------------------------------------------------------------
// Helpers: mode toggle + signature sounds
// ------------------------------------------------------------
static void enterDiag() {
  uint32_t now = millis();
  mode = AppMode::Diag;
  selectedGate = 0;
  resetDiagMetrics(now);

  // 3 krátké (spec)
  buzzer.beepMs(2400, 60); delay(35);
  buzzer.beepMs(2400, 60); delay(35);
  buzzer.beepMs(2400, 60);
}

static void enterRun() {
  mode = AppMode::Run;
  buzzer.beepMs(1800, 240); // 1 dlouhé (spec)
}

static void toggleMode() {
  if (mode == AppMode::Run) enterDiag();
  else enterRun();
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  for (uint8_t i = 0; i < GATE_COUNT; i++) pinMode(GATE_PINS[i], INPUT_ANALOG);

  btn1.begin(BTN1_PIN);
  btn2.begin(BTN2_PIN);

  buzzer.begin(PZ_A, PZ_B);

  // Boot beep 2x (ověření)
  buzzer.beepMs(2000, 60); delay(60);
  buzzer.beepMs(2000, 60);

  storage.begin();
  storage.loadCounts(gateCounts, GATE_COUNT);

  ui.begin();

  for (uint8_t i = 0; i < GATE_COUNT; i++) gates[i].begin(GATE_PINS[i]);
}

// ------------------------------------------------------------
// RUN state per gate
// ------------------------------------------------------------
struct GateRunState {
  bool interrupted = false;
  uint32_t sinceMs = 0;
  bool counted = false;
};
static GateRunState runState[GATE_COUNT];

static void resetRunStates() {
  for (uint8_t i = 0; i < GATE_COUNT; i++) {
    runState[i].interrupted = false;
    runState[i].sinceMs = 0;
    runState[i].counted = false;
  }
}

// ------------------------------------------------------------
// Loop
// ------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // --- buttons stable ---
  bool b1 = btn1.isPressed(now);
  bool b2 = btn2.isPressed(now);

  // --- sequence counts ---
  btn1Seq.updatePress(b1, now);
  btn2Seq.updatePress(b2, now);

  // BTN1: 10x press => toggle RUN/DIAG
  uint8_t b1Done = btn1Seq.finalizeIfReady(now, DIAG_WINDOW_MS, TOGGLE_GAP_END_MS);
  if (b1Done >= DIAG_TOGGLES) toggleMode();

  // BTN2: in DIAG => 1x next gate, 3x setIdle(selected)
  if (mode == AppMode::Diag) {
    uint8_t b2Done = btn2Seq.finalizeIfReady(now, RESET_WINDOW_MS, TOGGLE_GAP_END_MS);
    if (b2Done == 1) {
      selectedGate = (uint8_t)((selectedGate + 1) % GATE_COUNT);
      buzzer.click();
      resetDiagMetrics(now);
    } else if (b2Done == 3) {
      gates[selectedGate].setIdle();
      resetDiagMetrics(now);
      buzzer.beepMs(2000, 70); delay(60);
      buzzer.beepMs(2000, 70);
    }
  }

  // BTN2: long press in RUN => reset counts
  static bool b2WasPressed = false;
  static uint32_t b2PressSince = 0;
  static bool b2LongDone = false;

  if (b2 && !b2WasPressed) { b2WasPressed = true; b2PressSince = now; b2LongDone = false; }
  else if (!b2 && b2WasPressed) { b2WasPressed = false; b2PressSince = 0; b2LongDone = false; }

  if (mode == AppMode::Run && b2WasPressed && !b2LongDone && (now - b2PressSince) >= BTN2_HOLD_RESET_MS) {
    b2LongDone = true;
    for (uint8_t i = 0; i < GATE_COUNT; i++) gateCounts[i] = 0;
    storage.saveCountsIfNeeded(gateCounts, GATE_COUNT, true);

    buzzer.beepMs(2000, 80); delay(80);
    buzzer.beepMs(2000, 80); delay(80);
    buzzer.beepMs(2000, 80);
  }

  // BTN1 short press => toggle ARM in RUN
  static bool armed = false;
  static bool b1WasPressed = false;
  static uint32_t ignoreUntil = 0;

  if (mode == AppMode::Run) {
    if (b1 && !b1WasPressed) {
      b1WasPressed = true;
      armed = !armed;

      if (armed) { ignoreUntil = now + ARM_IGNORE_MS; buzzer.click(); } // ARM ON = klik
      else { buzzer.off(); resetRunStates(); }                         // ARM OFF = ticho
    } else if (!b1 && b1WasPressed) b1WasPressed = false;
  } else {
    armed = false;
    b1WasPressed = b1;
  }

  // Update gates
  for (uint8_t i = 0; i < GATE_COUNT; i++) gates[i].update();

  // DIAG: selected gate meter + geiger
  if (mode == AppMode::Diag) {
    int16_t strength = gates[selectedGate].getStrength();
    updateDiagMetrics(strength, now);

    // zvuk DIAG nechávám na strength (zatím), spec percent doděláme později
    buzzer.tickDiagMeter(now, strength);

    UiState s;
    s.mode = AppMode::Diag;
    s.selectedGate = selectedGate;
    s.diff = metNow;
    s.diffPeak = metPeak;
    s.noise = getNoise();

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) { lastDraw = now; ui.draw(s, gateCounts, GATE_COUNT); }
    return;
  }

  // RUN: evaluate all gates, pick worst stage
  UiState s;
  s.mode = AppMode::Run;
  s.armed = armed;

  bool inIgnore = (armed && now < ignoreUntil);
  s.inIgnore = inIgnore;

  if (!armed || inIgnore) {
    buzzer.off();
    s.gate1Signal = !gates[0].isBroken(RUN_THR);
    s.stage = 0;
    s.interruptedMs = 0;

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) { lastDraw = now; ui.draw(s, gateCounts, GATE_COUNT); }
    delay(6);
    return;
  }

  uint8_t worstStage = 0;
  uint32_t longestInterruptedMs = 0;

  for (uint8_t i = 0; i < GATE_COUNT; i++) {
    bool broken = gates[i].isBroken(RUN_THR);

    if (broken) {
      if (!runState[i].interrupted) {
        runState[i].interrupted = true;
        runState[i].sinceMs = now;
        runState[i].counted = false;
      }
    } else {
      runState[i].interrupted = false;
      runState[i].counted = false;
      runState[i].sinceMs = 0;
    }

    if (runState[i].interrupted) {
      uint32_t ms = now - runState[i].sinceMs;
      if (ms > longestInterruptedMs) longestInterruptedMs = ms;

      uint8_t stage = 0;
      // SPEC: 0..1s ticho, 1..2s rychlé pípání, 2..3s táhlý tón, 3s+ alarm
      if      (ms < STAGE1_MS) stage = 0;
      else if (ms < STAGE2_MS) stage = 1;
      else if (ms < STAGE3_MS) stage = 2;
      else                     stage = 3;

      if (stage > worstStage) worstStage = stage;

      if (!runState[i].counted && ms >= COUNT_AT_MS) {
        gateCounts[i]++;
        runState[i].counted = true;
      }
    }
  }

  SoundMode sm = SoundMode::Off;
  // Mapujeme na existující režimy Buzzeru:
  //  - spec stage1 (1..2s) = rychlé pípání => GateInterruptedStage3
  //  - spec stage2 (2..3s) = táhlý tón     => GateInterruptedStage1
  //  - spec stage3 (3s+)   = siréna        => GateInterruptedSiren
  switch (worstStage) {
    default: sm = SoundMode::Off; break;
    case 0:  sm = SoundMode::Off; break;
    case 1:  sm = SoundMode::GateInterruptedStage3; break;
    case 2:  sm = SoundMode::GateInterruptedStage1; break;
    case 3:  sm = SoundMode::GateInterruptedSiren;  break;
  }

  // UI má jen gate1Signal => mapuju na B1
  s.gate1Signal = !gates[0].isBroken(RUN_THR);
  s.stage = worstStage;
  s.interruptedMs = longestInterruptedMs;

  buzzer.tick(sm, now);
  storage.saveCountsIfNeeded(gateCounts, GATE_COUNT, false);

  static uint32_t lastDraw = 0;
  if (now - lastDraw >= 120) { lastDraw = now; ui.draw(s, gateCounts, GATE_COUNT); }
}
