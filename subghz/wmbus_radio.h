#pragma once
/*
 * Radio-device selector. Wraps the Flipper `subghz_devices_*` plugin API
 * so we can transparently target the on-board CC1101 (`cc1101_int`) or a
 * GPIO-attached external CC1101 module (`cc1101_ext`). Lifted from
 * ProtoPirate's `radio_device_loader` and trimmed to the calls we need.
 *
 * Usage from app init/teardown:
 *   subghz_devices_init();           // once, at app start
 *   dev = wmbus_radio_select(NULL, WmbusModuleExternal);
 *   ... use `dev` with subghz_devices_*() ...
 *   wmbus_radio_release(dev);
 *   subghz_devices_deinit();         // once, at app teardown
 *
 * `wmbus_radio_select` falls back to the internal radio whenever the
 * external module isn't present, so callers never have to handle the
 * "no module connected" case explicitly. */

#include <lib/subghz/devices/devices.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WmbusModuleInternal = 0,
    WmbusModuleExternal = 1,
    WmbusModule_Count,
} WmbusModule;

/* Pick a radio device, powering the GPIO bus / OTG rail as needed.
 * Pass the previous device handle (or NULL on first call); the function
 * cleanly stops it before activating the new one. Always returns a valid
 * device — on error it falls back to the internal radio. */
const SubGhzDevice* wmbus_radio_select(
    const SubGhzDevice* current,
    WmbusModule module);

/* True when `dev` is the external CC1101 plugin. */
bool wmbus_radio_is_external(const SubGhzDevice* dev);

/* Stop a device that was previously activated by `wmbus_radio_select`.
 * Safe to call with the internal radio (no-op besides power-rail clean-
 * up). Always pair with the call that opened the device. */
void wmbus_radio_release(const SubGhzDevice* dev);

#ifdef __cplusplus
}
#endif
