#include "driver_registry.h"
#include "../protocol/wmbus_app_layer.h"
#include "../protocol/wmbus_manuf.h"
#include <string.h>

/* Diehl Metering ("DME"/"SAP"/"HYD"). Hydrus, Sharky, Izar, etc. */
static bool match_diehl(uint16_t m, uint8_t v, uint8_t med) {
    char c[4]; wmbus_manuf_decode(m, c);
    (void)v; (void)med;
    return memcmp(c, "DME", 3) == 0 ||
           memcmp(c, "SAP", 3) == 0 ||
           memcmp(c, "HYD", 3) == 0;
}

static size_t decode_diehl(const uint8_t* a, size_t l, char* o, size_t c) {
    return wmbus_app_render(a, l, o, c);
}

const WmbusDriver g_driver_diehl = { "diehl", match_diehl, decode_diehl };
