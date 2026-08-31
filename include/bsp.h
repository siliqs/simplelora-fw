#pragma once

#define USE_LORA

// SQC485I v2 board profile — SX1262 pins only. This project only exercises the raw radio, so
// the board's RS485/BLE peripherals (used by its other, LoRaWAN-based firmware) aren't touched
// here.

#define LORA_DIO1 3
#define LORA_BUSY 4
#define LORA_NRST 5
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_NSS 8
#define LORA_SCK 10

// A typical AS923-1 channel plan (923.2 / 923.4 / ... / 924.6 MHz, multi-SF SF7-SF12 on each)
// puts 923.2 MHz as its lowest channel. 923.2 MHz / SF7 keeps airtime short for a ping-pong test.
#define SIMPLELORA_FREQUENCY_MHZ 923.2
#define SIMPLELORA_BANDWIDTH_KHZ 125.0
#define SIMPLELORA_SF 7
#define SIMPLELORA_CR 5
// concentratord's lorawan_public config option decides which LoRa sync word its demodulator is
// tuned to: false -> "private network" (0x12, used here), true -> "public network" (0x34). Must
// match the gateway's setting or its correlators never recognize our preamble at all.
#define SIMPLELORA_SYNC_WORD 0x12
#define SIMPLELORA_OUTPUT_POWER 22
#define SIMPLELORA_PREAMBLE_LENGTH 8
