#include "Buzzer.h"
#include "config.h"

void Buzzer::begin(uint8_t pinA, uint8_t pinB) {
  _a = pinA; _b = pinB;
  pinMode(_a, OUTPUT);
  pinMode(_b, OUTPUT);
  off();
  _nextDiagBeepMs = 0;
}

void Buzzer::off() {
  digitalWrite(_a, LOW);
  digitalWrite(_b, LOW);
}

inline void Buzzer::toneStep(uint32_t halfPeriodUs) {
  digitalWrite(_a, HIGH);
  digitalWrite(_b, LOW);
  delayMicroseconds(halfPeriodUs);
  digitalWrite(_a, LOW);
  digitalWrite(_b, HIGH);
  delayMicroseconds(halfPeriodUs);
}

void Buzzer::toneChunk(uint16_t hz, uint16_t steps) {
  const uint32_t halfPeriodUs = 1000000UL / ((uint32_t)hz * 2UL);
  for (uint16_t i = 0; i < steps; i++) toneStep(halfPeriodUs);
}

void Buzzer::beepMs(uint16_t hz, uint16_t ms) {
  const uint32_t halfPeriodUs = 1000000UL / ((uint32_t)hz * 2UL);
  const uint32_t cycles = (uint32_t)ms * 1000UL / (halfPeriodUs * 2UL);
  for (uint32_t i = 0; i < cycles; i++) toneStep(halfPeriodUs);
  off();
}

void Buzzer::click() {
  beepMs(2400, 25);
}

uint16_t Buzzer::sirenFreq(uint32_t tMs) {
  const uint32_t period = (uint32_t)SIREN_SWEEP_MS * 2UL;
  uint32_t x = tMs % period;
  uint32_t up = (x <= SIREN_SWEEP_MS) ? x : (period - x);
  uint32_t span = (uint32_t)SIREN_HI_HZ - (uint32_t)SIREN_LO_HZ;
  return (uint16_t)((uint32_t)SIREN_LO_HZ + (span * up) / (uint32_t)SIREN_SWEEP_MS);
}

void Buzzer::tick(SoundMode mode, uint32_t nowMs) {
  switch (mode) {
    case SoundMode::Off:
      off();
      break;

    case SoundMode::GateInterruptedStage1:
      toneChunk(TONE_STAGE1_HZ);
      break;

    case SoundMode::GateInterruptedStage2: {
      uint32_t t = nowMs % BEEP1_PERIOD_MS;
      if (t < BEEP1_ON_MS) toneChunk(TONE_STAGE1_HZ);
      else { off(); delay(2); }
    } break;

    case SoundMode::GateInterruptedStage3: {
      uint32_t t = nowMs % BEEP2_PERIOD_MS;
      if (t < BEEP2_ON_MS) toneChunk(TONE_STAGE1_HZ);
      else { off(); delay(2); }
    } break;

    case SoundMode::GateInterruptedSiren:
      toneChunk(sirenFreq(nowMs));
      break;
  }
}

void Buzzer::tickDiagMeter(uint32_t nowMs, int16_t diff) {
  // pod prahem ticho
  if (diff < (int16_t)DELTA_ON) {
    off();
    // aby po návratu nezačal “okamžitě” v divné fázi
    if (_nextDiagBeepMs < nowMs) _nextDiagBeepMs = nowMs;
    return;
  }

  // mapování: diff nad DELTA_ON -> perioda 800..120 ms
  int32_t x = (int32_t)diff - (int32_t)DELTA_ON;
  if (x < 0) x = 0;

  // “perf” hranice: DELTA_ON + DIAG_DIFF_PERF
  int32_t xmax = (int32_t)DIAG_DIFF_PERF;
  if (x > xmax) x = xmax;

  int32_t period = (int32_t)DIAG_PERIOD_SLOW_MS
                 - ( (int32_t)(DIAG_PERIOD_SLOW_MS - DIAG_PERIOD_FAST_MS) * x ) / xmax;

  if (period < (int32_t)DIAG_PERIOD_FAST_MS) period = (int32_t)DIAG_PERIOD_FAST_MS;

  if (nowMs >= _nextDiagBeepMs) {
    beepMs(DIAG_BEEP_HZ, DIAG_BEEP_MS);
    _nextDiagBeepMs = nowMs + (uint32_t)period;
  } else {
    // nic – necháme loop běžet
    off();
  }
}
