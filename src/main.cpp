#include <Arduino.h>
#include "config.h"
#include "Buzzer.h"
#include "UiOled.h"
#include "Storage.h"
#include "Gate.h"   // <-- NOVÉ


static uint32_t gateCounts[GATE_COUNT] = {0};

static Buzzer buzzer;
static UiOled ui;
static Storage storage;

static AppMode mode = AppMode::Run;
static uint8_t selectedGate = 0; // 0..9

// Zatím měříme jen jednu bránu fyzicky (PA0 = IR1_PIN), ale UI má 10
static Gate gate1;

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
// - 10x v okně => toggle DIAG
// - DIAG: 1x => next gate, 3x => set idle (kalibrace)
// - RUN: 3x => reset počítadel
// ------------------------------------------------------------
struct ToggleTracker {
  uint32_t windowStart = 0;
  uint32_t lastEdgeMs = 0;
  uint8_t  count = 0;

  bool lastArmed = false;

  uint32_t gapEndMs = TOGGLE_GAP_END_MS;

  bool updateEdge(bool armed, uint32_t nowMs) {
    bool edge = (armed && !lastArmed);
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
// Pozor: teď metriky děláme nad "strength", ne nad raw diff.
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
    selectedGate = 0; // začni na B1

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
  pinMode(IR1_PIN, INPUT_ANALOG);
  pinMode(SW_ARM, INPUT_PULLUP);

  buzzer.begin(PZ_A, PZ_B);

  // Boot beep 2x
  buzzer.beepMs(2000, 60); delay(60); buzzer.beepMs(2000, 60);

  storage.begin();
  storage.loadCounts(gateCounts, GATE_COUNT);

  ui.begin();

  // NOVÉ: Gate engine
  gate1.begin(IR1_PIN);
}

// ------------------------------------------------------------
// Loop
// ------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // --- čtení přepínače ---
  bool armed = readArmDebounced();

  // --- zpracování togglů ---
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
        } else if (doneCount == 3) {
          // Zatím fyzicky kalibrujeme jen gate1 (B1)
          // (Když později přidáš 10 bran, budeš kalibrovat gate[selectedGate])
          if (selectedGate == 0) {
            gate1.setIdle();
            resetDiagMetrics(now);

            // potvrzení kalibrace: 2x krátké píp
            buzzer.beepMs(2000, 70); delay(60);
            buzzer.beepMs(2000, 70);
          } else {
            // ostatní brány zatím nejsou fyzicky zapojené
            // krátké "ne" pípnutí
            buzzer.beepMs(1200, 80);
          }
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
  // Gate update (zatím jen gate1)
  // ----------------------------------------------------------
  gate1.update();

  // strength = |diff - idle|
  int16_t rawDiff1 = gate1.getDiff();
int16_t strength1 = gate1.getStrength();

  // ----------------------------------------------------------
  // DIAG mód: pípání zrychluje + zobrazení metrik
  // ----------------------------------------------------------
  if (mode == AppMode::Diag) {
    // metriky děláme z "strength"
    updateDiagMetrics(strength1, now);

    // DIAG zvuk: posíláme strength
    buzzer.tickDiagMeter(now, strength1);

    UiState s;
    s.mode = AppMode::Diag;
    s.selectedGate = selectedGate;

    // Pozn.: UI má pole diff/diffPeak/noise – použijeme je pro strength/peak/noise
    // (pokud chceš, později přejmenujeme v UI na STR/PEAK/NOISE)
    s.diff = metNow;
    s.diffPeak = metPeak;
    s.noise = getNoise();

    // Pokud chceš vidět i raw diff, musel bys rozšířit UiState + UiOled.
    // Tady ho zatím jen "držíme" v proměnné rawDiff1 pro případné logy.

    static uint32_t lastDraw = 0;
    if (now - lastDraw >= 120) {
      lastDraw = now;
      ui.draw(s, gateCounts, GATE_COUNT);
    }
    return;
  }

  // ----------------------------------------------------------
  // RUN mód: ARM + eskalace + počítání B1
  // Porušení = odchylka od idle > RUN_THR (tj. broken)
  // ----------------------------------------------------------
  UiState s;
  s.mode = AppMode::Run;
  s.armed = armed;

  static uint32_t ignoreUntil = 0;
  static bool lastArmedForIgnore = false;

  if (armed && !lastArmedForIgnore) {
    // po zapnutí ARM: krátké ignore okno
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

// Brána: detekce funguje i bez kalibrace (fallback = abs(diff)).
// Kalibrace (setIdle) pouze zlepší stabilitu v náročném prostředí.
bool broken1 = gate1.isBroken(RUN_THR);

// "Signal OK" = není broken (pro UI)
s.gate1Signal = !broken1;


  // Přerušení = broken
  if (broken1) {
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
    if      (interruptedMs < STAGE1_MS) { stage = 1; sm = SoundMode::GateInterruptedStage1; }
    else if (interruptedMs < STAGE2_MS) { stage = 2; sm = SoundMode::GateInterruptedStage2; }
    else if (interruptedMs < STAGE3_MS) { stage = 3; sm = SoundMode::GateInterruptedStage3; }
    else                                { stage = 4; sm = SoundMode::GateInterruptedSiren; }

    if (!countedThisInterrupt1 && interruptedMs >= COUNT_AT_MS) {
      gateCounts[0]++; // B1
      countedThisInterrupt1 = true;
    }
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
