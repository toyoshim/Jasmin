// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "controller.h"

#include "ch559.h"
#include "serial.h"

//#define _DBG_HID_REPORT_DUMP

static uint16_t raw_map[2] = {0, 0};
static uint8_t jvs_map[5] = {0, 0, 0, 0, 0};
static uint8_t mahjong[4] = {0, 0, 0, 0};

enum {
  MODE_NORMAL,
  MODE_MAHJONG,
};
static uint8_t mode = MODE_NORMAL;
static bool mode_sw = false;

static bool button_check(uint16_t index, const uint8_t* data) {
  if (index == 0xffff)
    return false;
  uint8_t byte = index >> 3;
  uint8_t bit = index & 7;
  return data[byte] & (1 << bit);
}

static int8_t axis_check(const struct hub_info* info,
                         const uint8_t* data,
                         uint8_t index) {
  if (info->axis[index] == 0xffff)
    return 0;
  if (info->axis_size[index] == 8) {
    uint8_t v = data[info->axis[index] >> 3];
    if (v < 0x60)
      return -1;
    if (v > 0xa0)
      return 1;
  } else if (info->axis_size[index] == 12) {
    uint8_t byte_index = info->axis[index] >> 3;
    uint16_t l = data[byte_index + 0];
    uint16_t h = data[byte_index + 1];
    uint16_t v = ((info->axis[index] & 7) == 0) ? (((h << 8) & 0x0f00) | l)
                                                : ((h << 4) | (l >> 4));
    if (info->axis_sign[index])
      v += 0x0800;
    if (info->axis_polarity[index])
      v = 0x0fff - v;
    if (v < 0x0600)
      return -1;
    if (v > 0x0a00)
      return 1;
  } else if (info->axis_size[index] == 16) {
    uint8_t byte = info->axis[index] >> 3;
    uint16_t v = data[byte] | ((uint16_t)data[byte + 1] << 8);
    if (info->axis_sign[index])
      v += 0x8000;
    if (info->axis_polarity[index])
      v = 0xffff - v;
    if (v < 0x6000)
      return -1;
    if (v > 0xa000)
      return 1;
  }
  return 0;
}

static void mahjong_update(const uint8_t* data) {
  uint8_t i;
  for (i = 0; i < 4; ++i)
    mahjong[i] = 0;
  if (data[0] & 0x11)  // Ctrl: Kan
    mahjong[0] |= 0x04;
  if (data[0] & 0x22)  // Shift: Reach
    mahjong[1] |= 0x04;
  if (data[0] & 0x44)  // Alt: Pon
    mahjong[3] |= 0x08;

  raw_map[0] = 0;
  for (i = 2; i < 8; ++i) {
    if (0x04 <= data[i] && data[i] <= 0x07)  // A-D
      mahjong[data[i] - 0x04] |= 0x80;
    else if (0x08 <= data[i] && data[i] <= 0x0b)  // E-H
      mahjong[data[i] - 0x08] |= 0x20;
    else if (0x0c <= data[i] && data[i] <= 0x0f)  // I-L
      mahjong[data[i] - 0x0c] |= 0x10;
    else if (0x10 <= data[i] && data[i] <= 0x11)  // M-N
      mahjong[data[i] - 0x10] |= 0x08;
    else if (data[i] == 0x2c)  // Space: Chi
      mahjong[2] |= 0x08;
    else if (data[i] == 0x1d)  // Z: Ron
      mahjong[2] |= 0x04;
    else if (data[i] == 0x1e)  // 1: Start
      mahjong[0] |= 0x02;
    else if (data[i] == 0x22)  // 5: Coin
      raw_map[0] = (1 << B_COIN);
  }
}

void controller_init() {
  pinMode(4, 2, INPUT_PULLUP);
  pinMode(4, 6, INPUT_PULLUP);

  digitalWrite(3, 6, LOW);  // SVC

  digitalWrite(4, 0, LOW);  // 1PC
  digitalWrite(3, 7, LOW);  // 1PS
  digitalWrite(3, 5, LOW);  // 1PU
  digitalWrite(3, 4, LOW);  // 1PD
  digitalWrite(3, 3, LOW);  // 1PL
  digitalWrite(3, 2, LOW);  // 1PR
  digitalWrite(3, 1, LOW);  // 1P1
  digitalWrite(3, 0, LOW);  // 1P2
  digitalWrite(4, 7, LOW);  // 1P3
  digitalWrite(1, 6, LOW);  // 1P4
  digitalWrite(4, 5, LOW);  // 1P5
  digitalWrite(4, 4, LOW);  // 1P6

  digitalWrite(4, 1, LOW);  // 2PC
  digitalWrite(1, 7, LOW);  // 2PS
  digitalWrite(1, 5, LOW);  // 2PU
  digitalWrite(1, 4, LOW);  // 2PD
  digitalWrite(1, 3, LOW);  // 2PL
  digitalWrite(1, 2, LOW);  // 2PR
  digitalWrite(1, 1, LOW);  // 2P1
  digitalWrite(1, 0, LOW);  // 2P2
  digitalWrite(0, 7, LOW);  // 2P3
  digitalWrite(0, 6, LOW);  // 2P4
  digitalWrite(0, 5, LOW);  // 2P5
  digitalWrite(0, 4, LOW);  // 2P6
}

void controller_update(uint8_t hub,
                       const struct hub_info* info,
                       const uint8_t* data,
                       uint16_t size,
                       uint16_t rapid_mask,
                       uint16_t* button_masks) {
#ifdef _DBG_HID_REPORT_DUMP
  static uint8_t old_data[256];
  bool modified = false;
  for (uint8_t i = 0; i < size; ++i) {
    if (old_data[i] == data[i])
      continue;
    modified = true;
    old_data[i] = data[i];
  }
  if (!modified)
    return;
  Serial.printf("Report %d Bytes: ", size);
  for (uint8_t i = 0; i < size; ++i)
    Serial.printf("%x,", data[i]);
  Serial.println("");
#endif  // _DBG_HID_REPORT_DUMP
  if (info->type == HID_TYPE_KEYBOARD) {
    if (size == 8) {
      mode = MODE_MAHJONG;
      mahjong_update(data);
    }
    return;
  } else if (mode == MODE_MAHJONG) {
    mode = MODE_NORMAL;
  }

  if (info->state != HID_STATE_READY) {
    jvs_map[1 + hub * 2 + 0] = 0;
    jvs_map[1 + hub * 2 + 1] = 0;
    return;
  }
  if (info->report_id) {
    if (info->report_id != data[0])
      return;
    data++;
  }
  uint8_t u = button_check(info->dpad[0], data) ? 1 : 0;
  uint8_t d = button_check(info->dpad[1], data) ? 1 : 0;
  uint8_t l = button_check(info->dpad[2], data) ? 1 : 0;
  uint8_t r = button_check(info->dpad[3], data) ? 1 : 0;
  int8_t x = axis_check(info, data, 0);
  if (x < 0)
    l = 1;
  else if (x > 0)
    r = 1;
  int8_t y = axis_check(info, data, 1);
  if (y < 0)
    u = 1;
  else if (y > 0)
    d = 1;
  if (info->hat != 0xffff) {
    uint8_t byte = info->hat >> 3;
    uint8_t bit = info->hat & 7;
    uint8_t hat = (data[byte] >> bit) & 0xf;
    switch (hat) {
      case 0:
        u = 1;
        break;
      case 1:
        u = 1;
        r = 1;
        break;
      case 2:
        r = 1;
        break;
      case 3:
        r = 1;
        d = 1;
        break;
      case 4:
        d = 1;
        break;
      case 5:
        d = 1;
        l = 1;
        break;
      case 6:
        l = 1;
        break;
      case 7:
        l = 1;
        u = 1;
        break;
    }
  }

  uint8_t service_map = hub ? 0 : (jvs_map[0] & 0x40);
  jvs_map[1 + hub * 2 + 0] = service_map | (u ? 0x20 : 0) | (d ? 0x10 : 0) |
                             (l ? 0x08 : 0) | (r ? 0x04 : 0);

  raw_map[hub] =
      (button_check(info->button[HID_BUTTON_SELECT], data) ? (1 << B_COIN)
                                                           : 0) |
      (button_check(info->button[HID_BUTTON_START], data) ? (1 << B_START)
                                                          : 0) |
      (button_check(info->button[HID_BUTTON_1], data) ? (1 << B_1) : 0) |
      (button_check(info->button[HID_BUTTON_2], data) ? (1 << B_2) : 0) |
      (button_check(info->button[HID_BUTTON_3], data) ? (1 << B_3) : 0) |
      (button_check(info->button[HID_BUTTON_4], data) ? (1 << B_4) : 0) |
      (button_check(info->button[HID_BUTTON_L1], data) ? (1 << B_5) : 0) |
      (button_check(info->button[HID_BUTTON_R1], data) ? (1 << B_6) : 0) |
      (button_check(info->button[HID_BUTTON_L2], data) ? (1 << B_7) : 0) |
      (button_check(info->button[HID_BUTTON_R2], data) ? (1 << B_8) : 0) |
      (button_check(info->button[HID_BUTTON_L3], data) ? (1 << B_9) : 0) |
      (button_check(info->button[HID_BUTTON_R3], data) ? (1 << B_10) : 0);

  bool current_mode_sw = button_check(info->button[HID_BUTTON_META], data);

  controller_map(hub, rapid_mask, button_masks);
}

void controller_map(uint8_t player,
                    uint16_t rapid_mask,
                    uint16_t* button_masks) {
  jvs_map[1 + player * 2 + 0] =
      (jvs_map[1 + player * 2 + 0] & 0x7c) |
      ((raw_map[player] & rapid_mask & button_masks[B_START]) ? 0x80 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_1]) ? 0x02 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_2]) ? 0x01 : 0);
  jvs_map[1 + player * 2 + 1] =
      ((raw_map[player] & rapid_mask & button_masks[B_3]) ? 0x80 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_4]) ? 0x40 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_5]) ? 0x20 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_6]) ? 0x10 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_7]) ? 0x08 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_8]) ? 0x04 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_9]) ? 0x02 : 0) |
      ((raw_map[player] & rapid_mask & button_masks[B_10]) ? 0x01 : 0);
}

void controller_poll() {
  if (controller_button(B_SERVICE))
    jvs_map[1] |= 0x40;
  else
    jvs_map[1] &= ~0x40;
  if (controller_button(B_TEST))
    jvs_map[0] |= 0x80;
  else
    jvs_map[0] &= ~0x80;

  pinMode(3, 6, controller_button(B_SERVICE) ? OUTPUT : INPUT);  // SVC

  bool mahjong_mode = digitalRead(1, 5) == LOW && digitalRead(1, 4) == LOW;

  if (mahjong_mode) {
    pinMode(0, 5, INPUT_PULLUP);  // M9
    pinMode(0, 6, INPUT_PULLUP);  // M10
    pinMode(0, 7, INPUT_PULLUP);  // M6
    pinMode(0, 4, INPUT_PULLUP);  // M2

    pinMode(4, 0, (raw_map[0] & (1 << B_COIN)) ? OUTPUT : INPUT);  // 1PC

    uint8_t matrix = 0;
    if (digitalRead(0, 6) == LOW)
      matrix = 1;
    else if (digitalRead(0, 7) == LOW)
      matrix = 2;
    else if (digitalRead(0, 4) == LOW)
      matrix = 3;

    pinMode(3, 1, (mahjong[matrix] & 0x80) ? OUTPUT : INPUT);  // M8
    pinMode(3, 2, (mahjong[matrix] & 0x20) ? OUTPUT : INPUT);  // M7
    pinMode(3, 3, (mahjong[matrix] & 0x10) ? OUTPUT : INPUT);  // M4
    pinMode(3, 4, (mahjong[matrix] & 0x08) ? OUTPUT : INPUT);  // M3
    pinMode(3, 5, (mahjong[matrix] & 0x04) ? OUTPUT : INPUT);  // M1
    pinMode(3, 7, (mahjong[matrix] & 0x02) ? OUTPUT : INPUT);  // M11
  } else {
    pinMode(4, 0, (raw_map[0] & (1 << B_COIN)) ? OUTPUT : INPUT);  // 1PC
    pinMode(3, 7, (jvs_map[1] & 0x80) ? OUTPUT : INPUT);           // 1PS
    pinMode(3, 5, (jvs_map[1] & 0x20) ? OUTPUT : INPUT);           // 1PU
    pinMode(3, 4, (jvs_map[1] & 0x10) ? OUTPUT : INPUT);           // 1PD
    pinMode(3, 3, (jvs_map[1] & 0x08) ? OUTPUT : INPUT);           // 1PL
    pinMode(3, 2, (jvs_map[1] & 0x04) ? OUTPUT : INPUT);           // 1PR
    pinMode(3, 1, (jvs_map[1] & 0x02) ? OUTPUT : INPUT);           // 1P1
    pinMode(3, 0, (jvs_map[1] & 0x01) ? OUTPUT : INPUT);           // 1P2
    pinMode(4, 7, (jvs_map[2] & 0x80) ? OUTPUT : INPUT);           // 1P3
    pinMode(1, 6, (jvs_map[2] & 0x40) ? OUTPUT : INPUT);           // 1P4
    pinMode(4, 5, (jvs_map[2] & 0x20) ? OUTPUT : INPUT);           // 1P5
    pinMode(4, 4, (jvs_map[2] & 0x10) ? OUTPUT : INPUT);           // 1P6
    pinMode(4, 1, (raw_map[1] & (1 << B_COIN)) ? OUTPUT : INPUT);  // 2PC
    pinMode(1, 7, (jvs_map[3] & 0x80) ? OUTPUT : INPUT);           // 2PS
    pinMode(1, 5, (jvs_map[3] & 0x20) ? OUTPUT : INPUT);           // 2PU
    pinMode(1, 4, (jvs_map[3] & 0x10) ? OUTPUT : INPUT);           // 2PD
    pinMode(1, 3, (jvs_map[3] & 0x08) ? OUTPUT : INPUT);           // 2PL
    pinMode(1, 2, (jvs_map[3] & 0x04) ? OUTPUT : INPUT);           // 2PR
    pinMode(1, 1, (jvs_map[3] & 0x02) ? OUTPUT : INPUT);           // 2P1
    pinMode(1, 0, (jvs_map[3] & 0x01) ? OUTPUT : INPUT);           // 2P2
    pinMode(0, 7, (jvs_map[4] & 0x80) ? OUTPUT : INPUT);           // 2P3
    pinMode(0, 6, (jvs_map[4] & 0x40) ? OUTPUT : INPUT);           // 2P4
    pinMode(0, 5, (jvs_map[4] & 0x20) ? OUTPUT : INPUT);           // 2P5
    pinMode(0, 4, (jvs_map[4] & 0x10) ? OUTPUT : INPUT);           // 2P6
  }
}

uint16_t controller_raw(uint8_t player) {
  return raw_map[player];
}

uint8_t controller_jvs(uint8_t index, uint8_t gpout) {
  if (mode != MODE_MAHJONG || index == 0)
    return jvs_map[index];
  if (index != 1)
    return 0;
  uint8_t service = jvs_map[1] & 0x40;
  if (gpout == 0x40)
    return mahjong[0] | service;
  if (gpout == 0x20)
    return mahjong[1] | service;
  if (gpout == 0x10)
    return mahjong[2] | service;
  if (gpout == 0x80)
    return mahjong[3] | service;
  return service;
}

bool controller_button(uint8_t button) {
  switch (button) {
    case B_TEST:
      return digitalRead(4, 2) == LOW;
    case B_SERVICE:
      return digitalRead(4, 6) == LOW;
  }
  return false;
}