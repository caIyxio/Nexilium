#ifndef NEXILIUM_HORIZON_H
#define NEXILIUM_HORIZON_H

#include <stdbool.h>
#include <stdint.h>
#include "NXMachine.h"

/* Dispatch one Horizon SVC immediate. Returns false for an unimplemented SVC. */
bool NXHorizonDispatchSVC(NXEngine *engine, uint16_t serviceID);
uint32_t NXHorizonConnectToNamedPort(NXEngine *engine, uint64_t nameAddress,
                                     uint32_t *outHandle);
uint32_t NXHorizonSendSyncRequest(NXEngine *engine, uint32_t handle);
void NXHorizonCloseHandle(NXEngine *engine, uint32_t handle);

#endif
