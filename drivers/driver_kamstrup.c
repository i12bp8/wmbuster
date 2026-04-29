#include "driver_registry.h"
#include "../protocol/wmbus_app_layer.h"
#include "../protocol/wmbus_manuf.h"
#include <string.h>

/* Kamstrup A/S — manuf code "KAM" = 0x2C2D. Multical 21/302/403/etc. */
static bool match_kam(uint16_t m, uint8_t v, uint8_t med) {
    char c[4]; wmbus_manuf_decode(m, c);
    (void)v; (void)med;
    return memcmp(c, "KAM", 3) == 0;
}

static size_t decode_kam(const uint8_t* a, size_t len, char* out, size_t cap) {
    /* For now use the generic DIF/VIF walk. Manufacturer-specific records
     * (DIF=0x0F/0x1F) can be interpreted here when their layouts are
     * known — see wmbusmeters drivers/multical*.cc for examples. */
    return wmbus_app_render(a, len, out, cap);
}

const WmbusDriver g_driver_kamstrup = { "kamstrup", match_kam, decode_kam };
