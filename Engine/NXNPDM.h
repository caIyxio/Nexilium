#ifndef NEXILIUM_NXNPDM_H
#define NEXILIUM_NXNPDM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NX_NPDM_HEADER_SIZE 0x80u
#define NX_NPDM_ACID_HEADER_SIZE 0x240u
#define NX_NPDM_ACI0_HEADER_SIZE 0x38u

typedef struct {
    const uint8_t *image;
    size_t imageSize;
    uint64_t titleID;
    uint64_t titleIDRangeMin;
    uint64_t titleIDRangeMax;
    uint32_t mainStackSize;
    uint32_t version;
    uint8_t mainThreadPriority;
    uint8_t defaultCPU;
    uint8_t mmuFlags;
    uint8_t isRetail;
    uint32_t acidOffset;
    uint32_t acidSize;
    uint32_t aci0Offset;
    uint32_t aci0Size;
    uint32_t aci0KACOffset;
    uint32_t aci0KACSize;
    uint32_t aci0SACOffset;
    uint32_t aci0SACSize;
} NXNPDMView;

/* Opens and bounds-checks the NPDM META, ACID, and ACI0 structures. */
bool NXEngineOpenNPDM(const uint8_t *image, size_t imageSize,
                      NXNPDMView *view);

#endif
