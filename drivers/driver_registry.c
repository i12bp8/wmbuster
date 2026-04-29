#include "driver_registry.h"

extern const WmbusDriver g_driver_generic;
extern const WmbusDriver g_driver_kamstrup;
extern const WmbusDriver g_driver_diehl;
extern const WmbusDriver g_driver_techem;
extern const WmbusDriver g_driver_qundis;
extern const WmbusDriver g_driver_ista;
extern const WmbusDriver g_driver_brunata;

static const WmbusDriver* const k_drivers[] = {
    &g_driver_kamstrup,
    &g_driver_diehl,
    &g_driver_techem,
    &g_driver_qundis,
    &g_driver_ista,
    &g_driver_brunata,
    /* generic last */
};

const WmbusDriver* wmbus_driver_find(uint16_t m, uint8_t v, uint8_t med) {
    for(unsigned i = 0; i < sizeof(k_drivers)/sizeof(k_drivers[0]); i++) {
        if(k_drivers[i]->match && k_drivers[i]->match(m, v, med)) return k_drivers[i];
    }
    return &g_driver_generic;
}
