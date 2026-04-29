#pragma once
/* Meter-model identification.
 *
 * Maps (manufacturer, version-byte, medium) to a short human-readable
 * model name suitable for the meter list. We don't claim to identify
 * every variant — we identify enough to make the list useful at a glance.
 *
 * Usage:
 *   const char* name = wmbus_model_name(M, V, MED);
 *   if(name) ... else fall back to "<MANUF> <medium>"
 */

#include <stdint.h>

const char* wmbus_model_name(uint16_t manuf, uint8_t version, uint8_t medium);
