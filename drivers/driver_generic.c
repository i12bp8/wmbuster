#include "driver_registry.h"
#include "../protocol/wmbus_app_layer.h"

static bool match_any(uint16_t m, uint8_t v, uint8_t med) {
    (void)m; (void)v; (void)med; return true;
}

static size_t decode_generic(const uint8_t* a, size_t len, char* out, size_t cap) {
    return wmbus_app_render(a, len, out, cap);
}

const WmbusDriver g_driver_generic = {
    .name = "generic",
    .match = match_any,
    .decode = decode_generic,
};
