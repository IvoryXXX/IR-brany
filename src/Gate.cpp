#include "Gate.h"
#include "config.h"

void Gate::begin(uint8_t adcPin) {
  _pin = adcPin;
  _base = analogRead(_pin);
  _idle = 0;
  _idleSet = false;
  _brokenLatch = false;
}

void Gate::update() {
  uint16_t v = analogRead(_pin);

  // diff: buď v-base, nebo base-v (kvůli zapojení)
#if DIFF_INVERT
  int16_t diffNow = (int16_t)v - (int16_t)_base;
#else
  int16_t diffNow = (int16_t)_base - (int16_t)v;
#endif

  // Hystereze pro "rozbitý" stav
  int16_t absDiff = abs(diffNow);
  if (_brokenLatch) {
    if (absDiff < (int16_t)DELTA_OFF) _brokenLatch = false;
  } else {
    if (absDiff > (int16_t)DELTA_ON) _brokenLatch = true;
  }

  // baseline adaptace:
  // - v klidu pomalu sleduj prostředí
  // - v "rozbitém" stavu baseline téměř neměň (jinak diff časem spadne na ~0)
  uint8_t shift = _brokenLatch ? (uint8_t)10 : (uint8_t)BASE_SHIFT; // 1/1024 vs 1/64 default
  _base = (uint16_t)((int32_t)_base + (((int32_t)v - (int32_t)_base) >> shift));

  // ulož aktuální diff
#if DIFF_INVERT
  _diff = (int16_t)v - (int16_t)_base;
#else
  _diff = (int16_t)_base - (int16_t)v;
#endif
}

void Gate::setIdle() {
  _idle = _diff;
  _idleSet = true;
}

int16_t Gate::getDiff() const {
  return _diff;
}

int16_t Gate::getStrength() const {
  // Když idle není nastavené, nechceme 0 (to zabíjí DIAG i RUN).
  // Fallback = abs(diff).
  if (!_idleSet) return abs(_diff);
  return abs(_diff - _idle);
}

bool Gate::isBroken(uint16_t thr) const {
  return getStrength() > (int16_t)thr;
}
