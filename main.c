// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "ch559.h"
#include "hid.h"
#include "led.h"
#include "serial.h"

#include "controller.h"
#include "settings.h"

static void report(uint8_t hub,
                   const struct hub_info* info,
                   const uint8_t* data,
                   uint16_t size) {
  controller_update(hub, info, data, size, settings_rapid_mask(hub),
                    settings_button_masks(hub));
}

void main() {
  initialize();
  controller_init();
  settings_init();
  delay(30);

  struct hid hid;
  hid.report = report;
  hid_init(&hid);
  Serial.println("USB Host ready");

  pinMode(2, 0, INPUT);

  for (;;) {
    static uint8_t csync_value = 0;
    static uint8_t csync_lpf_count = 0;
    static uint8_t csync_lpf_value = 0;
    if (digitalRead(2, 0) == HIGH)
      csync_lpf_value++;
    csync_lpf_count++;
    if (csync_lpf_count == 6) {
      uint8_t csync_new_value = (csync_lpf_value < 3) ? 0 : 1;
      csync_lpf_value = 0;
      csync_lpf_count = 0;
      if (csync_value == 1 && csync_new_value == 0) {
        settings_rapid_sync();
        controller_map(0, settings_rapid_mask(0), settings_button_masks(0));
        controller_map(1, settings_rapid_mask(1), settings_button_masks(1));
      }
      csync_value = csync_new_value;
    }
    hid_poll();
    controller_poll();
    settings_poll();
  }
}
