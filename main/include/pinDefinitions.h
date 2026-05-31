#pragma once

// =======================
// SELECT TARGET BOARD
// =======================
// Uncomment ONE:

// #define BOARD_ESP32_WROOM
#define BOARD_ESP32_WROOM

// =======================
// ESP32 WROOM-32E
// =======================
#if defined(BOARD_ESP32_WROOM)

#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13

#define A_PIN 23
#define B_PIN 22
#define C_PIN 5
#define D_PIN 17
#define E_PIN 32

#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

#define ENCODER_CLK GPIO_NUM_18
#define ENCODER_DT GPIO_NUM_19
#define ENCODER_BTN GPIO_NUM_21

// =======================
// ESP32-S3 (HUB75 DMA)
// =======================
#elif defined(BOARD_ESP32_S3)

#define R1_PIN 42
#define G1_PIN 41
#define B1_PIN 40
#define R2_PIN 38
#define G2_PIN 39
#define B2_PIN 37

#define A_PIN 45
#define B_PIN 36
#define C_PIN 48
#define D_PIN 35
#define E_PIN 21

#define LAT_PIN 47
#define OE_PIN 14
#define CLK_PIN 2

// avoid USB pins 19/20
#define ENCODER_CLK GPIO_NUM_7
#define ENCODER_DT GPIO_NUM_15
#define ENCODER_BTN GPIO_NUM_16

#else
#error "No board defined! Define BOARD_ESP32_WROOM or BOARD_ESP32_S3"
#endif