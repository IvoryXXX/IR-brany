#pragma once
#include <Arduino.h>

// -------- Piny --------
// POZOR: musí to být #define, protože se používá v #if (preprocesor)
#define USE_PIEZO_PORT_B 1   // 1=PB8/PB9, 0=PA8/PA9
#define DIFF_INVERT 1        // 1: diff=v-base, 0: diff=base-v

// I2C OLED (BluePill I2C1)
static const uint8_t I2C_SCL = PB6;
static const uint8_t I2C_SDA = PB7;

// Tlačítka (INPUT_PULLUP, active LOW)
static const uint8_t BTN1_PIN = PB10;
static const uint8_t BTN2_PIN = PB11;

// Dlouhý stisk BTN2: reset počítadel (ms)
static const uint16_t BTN2_HOLD_RESET_MS = 10000;

// Piezo piny
#if USE_PIEZO_PORT_B
  static const uint8_t PZ_A = PB8;
  static const uint8_t PZ_B = PB9;
#else
  static const uint8_t PZ_A = PA8;
  static const uint8_t PZ_B = PA9;
#endif

// IR brány (PA0..PA7)
static const uint8_t  GATE_COUNT = 8;
static const uint8_t  GATE_PINS[GATE_COUNT] = { PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7 };

// -------- Detekce signálu (Gate) --------
static const uint16_t DELTA_ON  = 45;
static const uint16_t DELTA_OFF = 25;
static const uint8_t  BASE_SHIFT = 6;
static const uint16_t ARM_IGNORE_MS = 600;

// -------- Počítání / uložení --------
static const uint16_t SAVE_EVERY_MS = 1000;

// -------- RUN: Reset sekvence / okna --------
static const uint16_t RESET_WINDOW_MS = 5000;
static const uint8_t  RESET_TOGGLES   = 3;

// -------- DIAG/RUN přepínání režimu: 10x stisk BTN1 --------
static const uint16_t DIAG_WINDOW_MS = 9000;
static const uint8_t  DIAG_TOGGLES   = 10;
static const uint16_t TOGGLE_GAP_END_MS = 650;

// -------- Eskalace zvuku při PŘERUŠENÍ (SPEC) --------
// 0..1s: ticho
// 1..2s: rychlé pípání
// 2..3s: táhlý tón
// 3s+: siréna + počítání bodu
static const uint16_t STAGE1_MS   = 1000;
static const uint16_t STAGE2_MS   = 2000;
static const uint16_t STAGE3_MS   = 3000;
static const uint16_t COUNT_AT_MS = 3000;

static const uint16_t TONE_STAGE1_HZ = 1800;

static const uint16_t BEEP1_PERIOD_MS = 300;
static const uint16_t BEEP1_ON_MS     = 150;

static const uint16_t BEEP2_PERIOD_MS = 140;
static const uint16_t BEEP2_ON_MS     = 70;

static const uint16_t SIREN_LO_HZ = 800;
static const uint16_t SIREN_HI_HZ = 2400;
static const uint16_t SIREN_SWEEP_MS = 900;

// -------- DIAG: Geiger pípání podle síly diff --------
static const uint16_t DIAG_BEEP_HZ = 2200;
static const uint16_t DIAG_BEEP_MS = 22;

static const uint16_t DIAG_PERIOD_SLOW_MS = 800;
static const uint16_t DIAG_PERIOD_FAST_MS = 120;

static const uint16_t DIAG_DIFF_GOOD  = 60;
static const uint16_t DIAG_DIFF_PERF  = 120;

// OLED
static const uint8_t OLED_ADDR = 0x3C;

// RUN prah pro "porušení"
static const uint16_t RUN_THR = 20;
