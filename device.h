#ifndef DEVICE_H
#define DEVICE_H

#include "ts_core.h"
#include "ts_core_ext.h"
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    FW_A_ORDERED_PIPELINE = 0,  /* sequential stages, probe output */
    FW_B_TASK_TREE        = 1,  /* latency / width trade-off */
    FW_C_MODULAR_ROBOT    = 2   /* power budget / module count */
} FirmwareId;

typedef struct {
    double score;     /* higher is better */
    bool   legal;     /* hard constraints of this firmware */
    size_t nodes;
    size_t depth;
    size_t width;     /* max branching factor */
    double latency;   /* Firmware B metric */
    double power;     /* Firmware C metric */
} DeviceScore;

DeviceScore device_evaluate(const TSNode *tree, FirmwareId fw);

/* In-memory dedup store keyed by canonical topology string */
#define DEVICE_STORE_CAP 4096

typedef struct {
    char  *canonical;   /* owned */
    size_t gen;         /* generation first seen */
} DeviceEntry;

typedef struct {
    DeviceEntry entries[DEVICE_STORE_CAP];
    size_t count;
} DeviceStore;

void   device_store_init(DeviceStore *s);
void   device_store_free(DeviceStore *s);
bool   device_store_insert(DeviceStore *s, const TSNode *tree, size_t generation);
size_t device_store_count(const DeviceStore *s);

#endif
