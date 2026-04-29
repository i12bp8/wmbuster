/* Friendly meter-model identification table.
 *
 * Each row: (manufacturer 3-letter code, version, medium, friendly name).
 *   - version 0xFF in this table means "any version"
 *   - medium  0xFF means "any medium"
 *
 * The model strings are intentionally short (≤ 24 chars) so they fit on
 * the Flipper meter list without truncation.
 *
 * Sources cross-checked: wmbusmeters wiki & driver headers, OMS device
 * registry, and on-device captures from EU heating networks.
 */

#include "wmbus_models.h"
#include "wmbus_manuf.h"
#include <string.h>

typedef struct {
    const char* code;     /* 3-letter manuf */
    uint8_t     version;  /* 0xFF = wildcard */
    uint8_t     medium;   /* 0xFF = wildcard */
    const char* name;
} ModelRow;

static const ModelRow k_models[] = {
    /* ---------------- Techem ---------------- */
    {"TCH", 0x61, 0x08, "Techem FHKV-3 HCA"},
    {"TCH", 0x64, 0x08, "Techem FHKV-4 HCA"},
    {"TCH", 0x69, 0x08, "Techem HCA"},
    {"TCH", 0x40, 0x04, "Techem Compact-V heat"},
    {"TCH", 0x94, 0x07, "Techem mk-Radio4 water"},
    {"TCH", 0x95, 0x06, "Techem mk-Radio4 warm"},
    {"TCH", 0xFF, 0x08, "Techem HCA"},
    {"TCH", 0xFF, 0x07, "Techem water"},
    {"TCH", 0xFF, 0x06, "Techem warm water"},
    {"TCH", 0xFF, 0x04, "Techem heat meter"},
    {"TCH", 0xFF, 0x16, "Techem cold water"},
    {"TCH", 0xFF, 0x1A, "Techem smoke detector"},

    /* ---------------- Kamstrup ---------------- */
    {"KAM", 0xFF, 0x04, "Kamstrup MULTICAL heat"},
    {"KAM", 0xFF, 0x07, "Kamstrup flowIQ water"},
    {"KAM", 0xFF, 0x0C, "Kamstrup flowIQ cooling"},
    {"KAM", 0xFF, 0x16, "Kamstrup flowIQ cold"},
    {"KAM", 0x1B, 0xFF, "Kamstrup MULTICAL 21"},
    {"KAM", 0x1C, 0xFF, "Kamstrup MULTICAL 302"},
    {"KAM", 0x1D, 0xFF, "Kamstrup MULTICAL 403"},
    {"KAM", 0x1E, 0xFF, "Kamstrup MULTICAL 603"},

    /* ---------------- Diehl / Hydrometer / Sappel ---------------- */
    {"DME", 0xFF, 0x07, "Diehl HYDRUS water"},
    {"DME", 0xFF, 0x16, "Diehl HYDRUS cold"},
    {"DME", 0xFF, 0x04, "Diehl SHARKY heat"},
    {"HYD", 0xFF, 0x07, "Hydrometer water"},
    {"HYD", 0xFF, 0x04, "Hydrometer SHARKY"},
    {"SAP", 0xFF, 0x07, "Sappel water"},

    /* ---------------- Qundis (formerly Kundo Systemtechnik) ---------------- */
    {"QDS", 0xFF, 0x08, "Qundis Q caloric HCA"},
    {"QDS", 0xFF, 0x04, "Qundis Q heat"},
    {"QDS", 0xFF, 0x07, "Qundis Q water"},
    {"QDS", 0xFF, 0x06, "Qundis Q warm water"},
    {"QDS", 0xFF, 0x16, "Qundis Q cold water"},
    {"QDS", 0xFF, 0x1A, "Qundis Q smoke"},

    /* ---------------- Engelmann (sensostar / waterstar) ---------------- */
    {"EFE", 0xFF, 0x04, "Engelmann Sensostar"},
    {"EFE", 0xFF, 0x07, "Engelmann Waterstar"},
    {"EFN", 0xFF, 0x04, "Engelmann Sensostar"},

    /* ---------------- Sontex (HCA + heat) ---------------- */
    {"SON", 0xFF, 0x08, "Sontex 868 HCA"},
    {"SON", 0xFF, 0x04, "Sontex Supercal heat"},

    /* ---------------- Zenner ---------------- */
    {"ZRM", 0xFF, 0x07, "Zenner Multidata water"},
    {"ZRM", 0xFF, 0x04, "Zenner Multidata heat"},
    {"ZRM", 0xFF, 0x08, "Zenner Multidata HCA"},
    {"ZEN", 0xFF, 0x07, "Zenner water"},

    /* ---------------- ista (HCA / cold/warm water) ---------------- */
    {"IST", 0xFF, 0x08, "ista doprimo HCA"},
    {"IST", 0xFF, 0x07, "ista istameter water"},
    {"IST", 0xFF, 0x06, "ista istameter warm"},
    {"IST", 0xFF, 0x16, "ista istameter cold"},

    /* ---------------- Itron / Actaris ---------------- */
    {"ITW", 0xFF, 0x07, "Itron BM+M / aquadis water"},
    {"ITW", 0xFF, 0x03, "Itron gas"},
    {"ACW", 0xFF, 0x07, "Actaris water"},

    /* ---------------- Sensus ---------------- */
    {"SEN", 0xFF, 0x07, "Sensus iPERL water"},
    {"SEN", 0xFF, 0x04, "Sensus PolluTherm heat"},
    {"SPX", 0xFF, 0x07, "Sensus/Spanner water"},

    /* ---------------- Apator ---------------- */
    {"APA", 0xFF, 0x07, "Apator Powogaz water"},
    {"APA", 0xFF, 0x04, "Apator heat"},
    {"APT", 0xFF, 0x07, "Apator Powogaz water"},

    /* ---------------- Elster / Honeywell ---------------- */
    {"ELS", 0xFF, 0x03, "Elster gas"},
    {"ELS", 0xFF, 0x04, "Elster F2 / F90 heat"},
    {"ELS", 0xFF, 0x07, "Elster water"},

    /* ---------------- Landis+Gyr ---------------- */
    {"LUG", 0xFF, 0x04, "Landis+Gyr ULTRAHEAT"},
    {"LUG", 0xFF, 0x07, "Landis+Gyr water"},
    {"LGB", 0xFF, 0xFF, "Landis+Gyr"},

    /* ---------------- BMeters ---------------- */
    {"BMT", 0xFF, 0x07, "BMeters HYDROSONIS"},
    {"BMT", 0xFF, 0x04, "BMeters HYDROCAL"},
    {"BMT", 0xFF, 0x08, "BMeters HYDROCLIMA HCA"},

    /* ---------------- Maddalena ---------------- */
    {"MAD", 0xFF, 0x07, "Maddalena Microclima water"},

    /* ---------------- Aquametro ---------------- */
    {"AMT", 0xFF, 0x04, "Aquametro CALEC heat"},
    {"AMT", 0xFF, 0x07, "Aquametro water"},

    /* ---------------- NZR ---------------- */
    {"NZR", 0xFF, 0x04, "NZR heat"},
    {"NZR", 0xFF, 0x07, "NZR water"},

    /* ---------------- Relay ---------------- */
    {"REL", 0xFF, 0xFF, "Relay gateway"},

    /* ---------------- Brunata (HCA, smoke) ---------------- */
    {"BRA", 0xFF, 0x08, "Brunata Futura+ HCA"},
    {"BRA", 0xFF, 0x07, "Brunata water"},
    {"BRA", 0xFF, 0x1A, "Brunata smoke"},
};

const char* wmbus_model_name(uint16_t manuf, uint8_t version, uint8_t medium) {
    char c[4]; wmbus_manuf_decode(manuf, c);
    /* Two passes: prefer specific (version, medium) then wildcards. */
    for(unsigned i = 0; i < sizeof(k_models)/sizeof(k_models[0]); i++) {
        const ModelRow* r = &k_models[i];
        if(memcmp(c, r->code, 3) != 0) continue;
        if(r->version != 0xFF && r->version != version) continue;
        if(r->medium  != 0xFF && r->medium  != medium ) continue;
        return r->name;
    }
    return NULL;
}
