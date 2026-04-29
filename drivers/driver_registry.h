#pragma once
/* Manufacturer-specific driver registry.
 *
 * Each driver has a `match()` predicate over (manuf, version, medium) and
 * a `decode()` function that takes the *plaintext* APDU and renders a
 * human-readable string into the supplied buffer. If no driver matches the
 * generic decoder (DIF/VIF walk) is used.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef bool (*WmbusDriverMatch)(uint16_t manuf, uint8_t version, uint8_t medium);
typedef size_t (*WmbusDriverDecode)(
    const uint8_t* apdu, size_t len,
    char* out, size_t out_size);

typedef struct {
    const char*       name;
    WmbusDriverMatch  match;
    WmbusDriverDecode decode;
} WmbusDriver;

/* Find the best matching driver, or the generic one. Never returns NULL. */
const WmbusDriver* wmbus_driver_find(uint16_t m, uint8_t v, uint8_t med);
