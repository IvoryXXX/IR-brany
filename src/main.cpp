#include <Arduino.h>
#include "config.h"
#include "Buzzer.h"
#include "UiOled.h"
#include "Storage.h"
#include "Gate.h"

static uint32_t gateCounts[GATE_COUNT] = {0};

static Buzzer buzzer;
static UiOled ui;
static Storage storage;

static AppMode mode = AppMode::Run;
static uint8_t selectedGate = 0; // 0..GATE_COUNT-1

// 8 bran
static Gate gates[GATE_COUNT];

// ------------------------------------------------------------
// ARM debounce (INPUT_PULLUP, active LOW) na BTN1
// ------------------------------------------------------------
static bool readArmDebounced() {
  static uint8_t stable = HIGH;
  static uint8_t last = HIGH;
  static uint32_t lastChange = 0;

  uint8_t r = digitalRead(BTN1_PIN);
  uint32_t now = millis();

  if (r != last) { last = r; lastChange = now; }
  if (now - lastChange > 30) stable = last;

  return (stable == LOW);
}

// ------------------------------------------------------------
// ToggleTracker: vyhodnotí sekvenci OFF->ON přepnutí ARM (BTN1)
// - 10x v okně => toggle DIAG
// - DIAG: 1x => next gate, 3x => set idle (kalibrace) vybrané brány
// - RUN: 3x => reset počítadel
// ------------------------------------------------------------
struct ToggleTracker {
  uint32_t windowStart = 0;
  uint32_t lastEdgeMs = 0;
  uint8_t  count = 0;

  bool lastArmed = false;
  uint32_t gapEndMs = TOGGLE_GAP_END_MS;

  bool updateEdge(bool armed, uint32_t nowMs) {
    bool edge = (armed && !lastArmed); // OFF->ON
    lastArmed = armed;
    if (!edge) return false;

    if (count == 0) windowStart = nowMs;
    count++;
    lastEdgeMs = nowMs;
    return true;
  }

  uint8_t finalizeIfReady(uint32_t nowMs) {
    if (count == 0) return 0;

    bool windowExpired = (nowMs - windowStart) > DIAG_WINDOW_MS;
    bool gapExpired = (nowMs - lastEdgeMs) > gapEndMs;

    if (windowExpired || gapExpired) {
      uint8_t out = count;
      count = 0;
      windowStart = 0;
      lastEdgeMs = 0;
      return out;
    }
    return 0;
  }

  void reset() {
    count = 0;
    windowStart = 0;
    lastEdgeMs = 0;
  }
};

static ToggleTracker toggles;

// ------------------------------------------------------------
// DIAG metriky: peak hold + noise (max-min v okně)
// Metriky děláme nad "strength" z vybrané brány.
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

  // noise okno 600 ms (max-min)
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
// Helper: přepnutí režimu RUN<->DIAG (signatura zvuk)
// ------------------------------------------------------------
static void toggleMode() {
  uint32_t now = millis();

  if (mode == AppMode::Run) {
    mode = AppMode::Diag;
    selectedGate = 0;
    resetDiagMetrics(now);

    // signature: 3 rychlé píp
    buzzer.beepMs(2400, 60); delay(40);
    buzzer.beepMs(2400, 60); delay(40);
    buzzer.beepMs(2400, 60);
  } else {
    mode = AppMode::Run;

    // signature: 1 dlouhé píp
    buzzer.beepMs(2000, 180);
  }
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  // Tlačítka
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  // Analog vstupy pro brány
  for (uint8_t i = 0; i < GATE_COUNT; i++) {
    pinMode(GATE_PINS[i], INPUT_ANALOG);
  }

  buzzer.begin(PZ_A, PZ_B);

  // Boot beep 2x
  buzzer.beepMs(2000, 60); delay(60); buzzer.beepMs(2000, 60);

  storage.begin();
  storage.loadCounts(gateCounts, GATE_COUNT);

  ui.begin();

  // Gate engine
  for (uint8_t i = 0; i < GATE_COUNT; i++) {
    gates[i].begin(GATE_PINS[i]);
  }
}

// ------------------------------------------------------------
// Loop
// ------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // --- čtení ARM (BTN1) ---
  bool armed = readArmDebounced();

  // --- zpracování togglů na ARM ---
  toggles.updateEdge(armed, now);

  uint8_t doneCount = toggles.finalizeIfReady(now);
  if (doneCount > 0) {
    if (doneCount >= DIAG_TOGGLES) {
      // 10x -> toggle režimu
      toggleMode();
    } else {
      if (mode == AppMode::Diag) {
        // DIAG: 1x next gate, 3x kalibrace idle
        if (doneCount == 1) {
          selectedGate = (uint8_t)((selectedGate + 1) % GATE_COUNT);
          buzzer.click();
          resetDiagMetrics(now);
        } else if (doneCount == 3) {
          gates[selectedGate].setIdle();
          resetDiagMetrics(now);

          // potvrzení kalibrace: 2x krátké píp
          buzzer.beepMs(2000, 70); delay(60);
          buzzer.beepMs(2000, 70);
        }
        // 2..9 ignor
      } else {
        // RUN: 3x reset počítadel
        if (doneCount == RESET_TOGGLES) {
          for (uint8_t i = 0; i < GATE_COUNT; i++) gateCounts[i] = 0;
          storage.saveCountsIfNeeded(gateCounts, GATE_COUNT, true);

          // potvrzení resetu 3x
          buzzer.beepMs(2000, 80); delay(80);
          buzzer.beepMs(2000, 80); delay(80);
          buzzer.beepMs(2000, 80);
        }
      }
    }
  }

  // ----------------------------------------------------------
  // Gate update (všechny brány)
  // ----------------------------------------------------------
  for (uint8_t i = 0; i < GATE_COUNT; i++) {
    gates[i].update();
  }

  // Vybraná brána pro DIAG metriky
  int16_t strengthSel = gates[selectedGate].getStrength();

  // ----------------------------------------------------------
  // DIAG mód
  // ----------------------------------------------------------
  if (mode == AppMode::Diag) {
    updateDiagMetrics(strengthSel, now);
    buzzer.tickDiagMeter(now, strengthSel);

    UiState s;
    s.mode = AppMode::Diag;
    s.selectedGate = selectedGate;

    s.diff = metNow;
    s.diffPeak = metPeak;
    s.noise = getNoise();

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) {
      lastDraw = now;
      ui.draw(s, gateCounts, GATE_COUNT);
    }
    return;
  }

  // ----------------------------------------------------------
  // RUN mód: ARM + eskalace + počítání (pro všechny brány)
  // ----------------------------------------------------------
  UiState s;
  s.mode = AppMode::Run;
  s.armed = armed;

  static uint32_t ignoreUntil = 0;
  static bool lastArmedForIgnore = false;

  if (armed && !lastArmedForIgnore) {
    ignoreUntil = now + ARM_IGNORE_MS;
  }
  lastArmedForIgnore = armed;

  bool inIgnore = (armed && now < ignoreUntil);
  s.inIgnore = inIgnore;

  // Per-gate interruption tracking
  static bool interrupted[GATE_COUNT] = {0};
  static uint32_t interruptSince[GATE_COUNT] = {0};
  static bool countedThisInterrupt[GATE_COUNT] = {0};

  if (!armed || inIgnore) {
    for (uint8_t i = 0; i < GATE_COUNT; i++) {
      interrupted[i] = false;
      countedThisInterrupt[i] = false;
    }
    buzzer.off();

    s.gate1Signal = false;
    s.stage = 0;
    s.interruptedMs = 0;

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) {
      lastDraw = now;
      ui.draw(s, gateCounts, GATE_COUNT);
    }
    delay(8);
    return;
  }

  // Najdi "nejhorší" přerušenou bránu pro zvuk + UI
  int bestGate = -1;
  uint32_t bestMs = 0;

  for (uint8_t i = 0; i < GATE_COUNT; i++) {
    bool broken = gates[i].isBroken(RUN_THR);

    if (broken) {
      if (!interrupted[i]) {
        interrupted[i] = true;
        interruptSince[i] = now;
        countedThisInterrupt[i] = false;
      }
    } else {
      interrupted[i] = false;
      countedThisInterrupt[i] = false;
    }

    if (interrupted[i]) {
      uint32_t ms = now - interruptSince[i];

      // Po 3s jednorázově přičíst bod
      if (!countedThisInterrupt[i] && ms >= COUNT_AT_MS) {
        gateCounts[i]++;
        countedThisInterrupt[i] = true;
      }

      if (bestGate < 0 || ms > bestMs) {
        bestGate = (int)i;
        bestMs = ms;
      }
    }
  }

  // UI: signal OK = žádná brána přerušená
  bool anyInterrupted = (bestGate >= 0);
  s.gate1Signal = !anyInterrupted;

  // Zvuk + stage podle nejhorší brány
  uint8_t stage = 0;
  SoundMode sm = SoundMode::Off;

  if (anyInterrupted) {
    uint32_t interruptedMs = bestMs;
    s.interruptedMs = interruptedMs;

    if      (interruptedMs < STAGE1_MS) { stage = 1; sm = SoundMode::GateInterruptedStage1; }
    else if (interruptedMs < STAGE2_MS) { stage = 2; sm = SoundMode::GateInterruptedStage2; }
    else if (interruptedMs < STAGE3_MS) { stage = 3; sm = SoundMode::GateInterruptedStage3; }
    else                                { stage = 4; sm = SoundMode::GateInterruptedSiren; }
  } else {
    s.interruptedMs = 0;
  }

  s.stage = stage;

  buzzer.tick(sm, now);

  storage.saveCountsIfNeeded(gateCounts, GATE_COUNT, false);

  static uint32_t lastDraw = 0;
  if (now - lastDraw >= 120) {
    lastDraw = now;
    ui.draw(s, gateCounts, GATE_COUNT);
  }
}
