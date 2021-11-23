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
  controller_update(hub, info, data, size, settings_button_masks(hub));
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

  for (;;) {
    hid_poll();
    controller_poll();
    settings_poll();
    // TODO: csync detect and settings_rapid_sync() call
  }
}
