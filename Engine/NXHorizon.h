#ifndef NEXILIUM_HORIZON_H
#define NEXILIUM_HORIZON_H

#include <stdbool.h>
#include <stdint.h>
#include "NXMachine.h"

/* Dispatch one Horizon SVC immediate. Returns false for an unimplemented SVC. */
bool NXHorizonDispatchSVC(NXEngine *engine, uint16_t serviceID);

#endif
