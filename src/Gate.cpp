#include "Gate.h"

void Gate::begin(uint8_t adcPin) {
  _pin = adcPin;
  _base = analogRead(_pin);
  _idle = 0;
  _idleSet = false;
}

void Gate::update() {
  uint16_t v = analogRead(_pin);

  // baseline adaptace (pomalá)
  _base = _base + ((int32_t)v - (int32_t)_base) / 32;

  _diff = (int16_t)v - (int16_t)_base;
}

void Gate::setIdle() {
  _idle = _diff;
  _idleSet = true;
}

int16_t Gate::getDiff() const {
  return _diff;
}

int16_t Gate::getStrength() const {
  if (!_idleSet) return 0;
  return abs(_diff - _idle);
}

bool Gate::isBroken(uint16_t thr) const {
  if (!_idleSet) return false;
  return getStrength() > thr;
}
