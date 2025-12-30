#pragma once
#include <Arduino.h>

// -------- Piny --------
static const uint8_t USE_PIEZO_PORT_B = 0;  // 1=PB8/PB9, 0=PA8/PA9
static const uint8_t DIFF_INVERT = 1;       // 1: diff=v-base, 0: diff=base-v

// Brána 1 (zatím jediná připojená)
static const uint8_t IR1_PIN = PA0;
static const uint8_t SW_ARM  = PA1;

// I2C OLED (BluePill I2C1)
static const uint8_t I2C_SCL = PB6;
static const uint8_t I2C_SDA = PB7;

// Piezo piny
#if USE_PIEZO_PORT_B
  static const uint8_t PZ_A = PB8;
  static const uint8_t PZ_B = PB9;
#else
  static const uint8_t PZ_A = PA8;
  static const uint8_t PZ_B = PA9;
#endif

// -------- Detekce signálu (Gate1) --------
static const uint16_t DELTA_ON  = 45;
static const uint16_t DELTA_OFF = 25;
static const uint8_t  BASE_SHIFT = 6;
static const uint16_t ARM_IGNORE_MS = 600;

// -------- Počítání / uložení --------
static const uint8_t  GATE_COUNT = 10;
static const uint16_t SAVE_EVERY_MS = 1000;

// -------- RUN: Reset sekvence (3x OFF->ON do 5s) --------
static const uint16_t RESET_WINDOW_MS = 5000;
static const uint8_t  RESET_TOGGLES   = 3;

// -------- DIAG/RUN přepínání režimu: 10x OFF->ON --------
static const uint16_t DIAG_WINDOW_MS = 9000;     // okno pro 10 toggle
static const uint8_t  DIAG_TOGGLES   = 10;
static const uint16_t TOGGLE_GAP_END_MS = 650;   // když takhle dlouho nikdo netoggluje => vyhodnoť sekvenci

// -------- Eskalace zvuku při PŘERUŠENÍ (signal OFF) – pro Gate1 (RUN) --------
static const uint16_t STAGE1_MS   = 1000;  // 0–1s táhlý tón
static const uint16_t STAGE2_MS   = 2000;  // 1–2s píp pomalejší
static const uint16_t STAGE3_MS   = 3000;  // 2–3s píp rychlejší
static const uint16_t COUNT_AT_MS = 3000;  // až při 3s přičti bod

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
static const uint16_t DIAG_BEEP_MS = 22;     // délka jednoho pípnutí

// Mapování diff -> perioda pípání (ms)
static const uint16_t DIAG_PERIOD_SLOW_MS = 800;  // když je to sotva OK
static const uint16_t DIAG_PERIOD_FAST_MS = 120;  // když je to perfektní

// pro mapování: diff nad DELTA_ON
static const uint16_t DIAG_DIFF_GOOD  = 60;
static const uint16_t DIAG_DIFF_PERF  = 120;

// OLED
static const uint8_t OLED_ADDR = 0x3C;   // když nic, zkus 0x3D

static const uint16_t GATE_THRESHOLD = 15;   // kdy začne pípání a běh času
static const uint16_t ALARM_TIME_MS  = 3000;

static const uint16_t RUN_THR = 20;   // prah pro "porušení" v RUN (doladíš podle reality)
