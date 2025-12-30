#include <Arduino.h>
#include "config.h"
#include "Buzzer.h"
#include "UiOled.h"
#include "Storage.h"

static uint32_t gateCounts[GATE_COUNT] = {0};

static Buzzer buzzer;
static UiOled ui;
static Storage storage;

static AppMode mode = AppMode::Run;
static uint8_t selectedGate = 0; // 0..9

// ------------------------------------------------------------
// ARM debounce (LOW = ON)
// ------------------------------------------------------------
static bool readArmDebounced() {
  static uint8_t stable = HIGH;
  static uint8_t last = HIGH;
  static uint32_t lastChange = 0;

  uint8_t r = digitalRead(SW_ARM);
  uint32_t now = millis();

  if (r != last) { last = r; lastChange = now; }
  if (now - lastChange > 30) stable = last;

  return (stable == LOW);
}

// ------------------------------------------------------------
// ToggleTracker: vyhodnotí sekvenci OFF->ON přepnutí
// - pokud 10x v okně => toggle DIAG
// - pokud v DIAG a jen 1x => next gate
// - pokud v RUN a 3x => reset
// ------------------------------------------------------------
struct ToggleTracker {
  uint32_t windowStart = 0;
  uint32_t lastEdgeMs = 0;
  uint8_t  count = 0;

  bool lastArmed = false;

  // po jak dlouhé pauze sekvenci "uzavřít"
  uint32_t gapEndMs = TOGGLE_GAP_END_MS;

  // update vrací true, když nastala OFF->ON hrana
  bool updateEdge(bool armed, uint32_t nowMs) {
    bool edge = (armed && !lastArmed);
    lastArmed = armed;
    if (!edge) return false;

    if (count == 0) windowStart = nowMs;
    count++;
    lastEdgeMs = nowMs;
    return true;
  }

  // volat každé loop; když je pauza nebo okno překročeno, sekvenci uzavře
  // vrací uzavřený počet toggle, jinak 0
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
// ------------------------------------------------------------
static int16_t diffNow = 0;
static int16_t diffPeak = 0;
static uint32_t peakUntil = 0;

static int16_t diffMin = 32767;
static int16_t diffMax = -32768;
static uint32_t noiseWindowStart = 0;

static void updateDiagMetrics(int16_t d, uint32_t nowMs) {
  diffNow = d;

  // peak hold 1500 ms
  if (d > diffPeak || nowMs > peakUntil) {
    diffPeak = d;
    peakUntil = nowMs + 1500;
  }

  // noise okno 600 ms (max-min)
  if (noiseWindowStart == 0) noiseWindowStart = nowMs;
  if (d < diffMin) diffMin = d;
  if (d > diffMax) diffMax = d;

  if ((nowMs - noiseWindowStart) >= 600) {
    // reset okna
    noiseWindowStart = nowMs;
    diffMin = d;
    diffMax = d;
  }
}

static int16_t getNoise() {
  int32_t n = (int32_t)diffMax - (int32_t)diffMin;
  if (n < 0) n = 0;
  if (n > 32767) n = 32767;
  return (int16_t)n;
}

// ------------------------------------------------------------
// Helper: přepnutí režimu RUN<->DIAG (signatura zvuk)
// ------------------------------------------------------------
static void toggleMode() {
  if (mode == AppMode::Run) {
    mode = AppMode::Diag;
    selectedGate = 0; // začni na B1
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
  pinMode(IR1_PIN, INPUT_ANALOG);
  pinMode(SW_ARM, INPUT_PULLUP);

  buzzer.begin(PZ_A, PZ_B);

  // Boot beep 2x
  buzzer.beepMs(2000, 60); delay(60); buzzer.beepMs(2000, 60);

  storage.begin();
  storage.loadCounts(gateCounts, GATE_COUNT);

  ui.begin();
}

// ------------------------------------------------------------
// Loop
// ------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // --- čtení přepínače ---
  bool armed = readArmDebounced();

  // --- zpracování togglů ---
  bool edge = toggles.updateEdge(armed, now);

  // v DIAG: každý *single* toggle = další brána, ale až po uzavření sekvence
  // v RUN: 3 = reset, 10 = DIAG
  uint8_t doneCount = toggles.finalizeIfReady(now);
  if (doneCount > 0) {
    if (doneCount >= DIAG_TOGGLES) {
      // 10x -> toggle režimu
      toggleMode();
    } else {
      // jiné počty
      if (mode == AppMode::Diag) {
        // single toggle -> další brána
        if (doneCount == 1) {
          selectedGate = (uint8_t)((selectedGate + 1) % GATE_COUNT);
          buzzer.click(); // potvrzení přepnutí brány
        }
        // 2..9 v DIAG ignoruj
      } else {
        // RUN
        if (doneCount == RESET_TOGGLES) {
          // reset všech počítadel
          for (uint8_t i = 0; i < GATE_COUNT; i++) gateCounts[i] = 0;
          storage.saveCountsIfNeeded(gateCounts, GATE_COUNT, true);

          // potvrzení resetu 3x
          buzzer.beepMs(2000, 80); delay(80);
          buzzer.beepMs(2000, 80); delay(80);
          buzzer.beepMs(2000, 80);
        }
        // 1..2 nebo 4..9 ignor
      }
    }
  }

  // ----------------------------------------------------------
  // Měření brány 1 (PA0) – používáme i pro DIAG zatím jako “zdroj diff”
  // ----------------------------------------------------------
  static uint16_t base1 = 0;
  static bool baseInit = false;
  if (!baseInit) { base1 = analogRead(IR1_PIN); baseInit = true; }

  static bool signal1 = false;  // true = emitor vidím (SIGNAL ON)

  uint16_t v1 = analogRead(IR1_PIN);

  // baseline adaptace
  if (!signal1) base1 = base1 + ((int32_t)v1 - (int32_t)base1) / (1 << BASE_SHIFT);
  else          base1 = base1 + ((int32_t)v1 - (int32_t)base1) / (1 << (BASE_SHIFT + 3));

  int16_t diff1 = DIFF_INVERT ? (int16_t)v1 - (int16_t)base1
                              : (int16_t)base1 - (int16_t)v1;

  // ----------------------------------------------------------
  // DIAG mód: pípání zrychluje + zobrazení metrik
  // ----------------------------------------------------------
  if (mode == AppMode::Diag) {
    updateDiagMetrics(diff1, now);

    // geiger zvuk z diff (zatím z gate1)
    buzzer.tickDiagMeter(now, diff1);

    UiState s;
    s.mode = AppMode::Diag;
    s.selectedGate = selectedGate;
    s.diff = diffNow;
    s.diffPeak = diffPeak;
    s.noise = getNoise();

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) {
      lastDraw = now;
      ui.draw(s, gateCounts, GATE_COUNT);
    }
    return;
  }

  // ----------------------------------------------------------
  // RUN mód: původní chování (ARM + eskalace) + počítání B1
  // ----------------------------------------------------------
  UiState s;
  s.mode = AppMode::Run;
  s.armed = armed;

  static uint32_t ignoreUntil = 0;
  static bool lastArmedForIgnore = false;

  // IGNORE okno po zapnutí ARM (jen RUN)
  if (armed && !lastArmedForIgnore) {
    base1 = analogRead(IR1_PIN);
    ignoreUntil = now + ARM_IGNORE_MS;
  }
  lastArmedForIgnore = armed;

  bool inIgnore = (armed && now < ignoreUntil);
  s.inIgnore = inIgnore;

  // per-run přerušení pro gate1
  static bool interrupted1 = false;
  static uint32_t interruptSince1 = 0;
  static bool countedThisInterrupt1 = false;

  if (!armed) {
    // když není ARM, ticho a UI
    signal1 = false;
    interrupted1 = false;
    countedThisInterrupt1 = false;
    buzzer.off();

    s.gate1Signal = false;
    s.stage = 0;

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 200) {
      lastDraw = now;
      ui.draw(s, gateCounts, GATE_COUNT);
    }
    delay(15);
    return;
  }

  if (inIgnore) {
    signal1 = false;
    interrupted1 = false;
    countedThisInterrupt1 = false;
    buzzer.off();

    s.gate1Signal = false;
    s.stage = 0;

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) {
      lastDraw = now;
      ui.draw(s, gateCounts, GATE_COUNT);
    }
    delay(8);
    return;
  }

  // hystereze signal1
  if (!signal1) { if (diff1 > (int16_t)DELTA_ON)  signal1 = true; }
  else          { if (diff1 < (int16_t)DELTA_OFF) signal1 = false; }

  s.gate1Signal = signal1;

  // Přerušení = signal OFF
  if (!signal1) {
    if (!interrupted1) {
      interrupted1 = true;
      interruptSince1 = now;
      countedThisInterrupt1 = false;
    }
  } else {
    interrupted1 = false;
    countedThisInterrupt1 = false;
  }

  uint32_t interruptedMs = interrupted1 ? (now - interruptSince1) : 0;
  s.interruptedMs = interruptedMs;

  SoundMode sm = SoundMode::Off;
  uint8_t stage = 0;

  if (interrupted1) {
    if (interruptedMs < STAGE1_MS)      { stage = 1; sm = SoundMode::GateInterruptedStage1; }
    else if (interruptedMs < STAGE2_MS) { stage = 2; sm = SoundMode::GateInterruptedStage2; }
    else if (interruptedMs < STAGE3_MS) { stage = 3; sm = SoundMode::GateInterruptedStage3; }
    else                                { stage = 4; sm = SoundMode::GateInterruptedSiren; }

    if (!countedThisInterrupt1 && interruptedMs >= COUNT_AT_MS) {
      gateCounts[0]++; // B1
      countedThisInterrupt1 = true;
    }
  }

  s.stage = stage;

  // zvuk
  buzzer.tick(sm, now);

  // save + UI
  storage.saveCountsIfNeeded(gateCounts, GATE_COUNT, false);

  static uint32_t lastDraw = 0;
  if (now - lastDraw >= 120) {
    lastDraw = now;
    ui.draw(s, gateCounts, GATE_COUNT);
  }
}
