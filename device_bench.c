#include "device.h"
#include "ts_core.h"
#include "ts_core_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POP_SIZE  32
#define GENERATIONS 40
#define MAX_DEPTH  8
#define MAX_NODES  24

typedef struct {
    TSNode *tree;
    double fitness;   /* under Firmware A */
} Individual;

static void free_pop(Individual *pop, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (pop[i].tree) {
            ts_free_tree(pop[i].tree);
            free(pop[i].tree);
            pop[i].tree = NULL;
        }
    }
}

static int cmp_ind(const void *a, const void *b) {
    const Individual *x = a, *y = b;
    if (x->fitness > y->fitness) return -1;
    if (x->fitness < y->fitness) return 1;
    return 0;
}

int main(void) {
    setbuf(stdout, NULL);
    printf("EdgeTG â€” DEVICE FIRMWARE DEMO\n");
    printf("Evolving under Firmware A (Ordered pipeline), then scoring under B & C.\n\n");

    ts_ext_srand(20260831);

    Individual pop[POP_SIZE];
    memset(pop, 0, sizeof(pop));

    /* Seed population with random trees */
    for (size_t i = 0; i < POP_SIZE; i++) {
        if (ts_random_tree(MAX_DEPTH, MAX_NODES, &pop[i].tree) != TS_OK) {
            fprintf(stderr, "seed failed\n");
            free_pop(pop, i);
            return 1;
        }
        DeviceScore sc = device_evaluate(pop[i].tree, FW_A_ORDERED_PIPELINE);
        pop[i].fitness = sc.score;
    }

    DeviceStore store;
    device_store_init(&store);

    for (size_t gen = 0; gen < GENERATIONS; gen++) {
        /* Rank */
        qsort(pop, POP_SIZE, sizeof(Individual), cmp_ind);

        /* Store unique canonical forms of the top half */
        for (size_t i = 0; i < POP_SIZE / 2; i++)
            device_store_insert(&store, pop[i].tree, gen);

        /* Breed: keep top half, mutate copies into bottom half */
        for (size_t i = POP_SIZE / 2; i < POP_SIZE; i++) {
            size_t parent = ts_ext_rand() % (POP_SIZE / 2);
            TSNode *child = NULL;
            if (ts_clone(pop[parent].tree, &child) != TS_OK) continue;

            /* Apply a random mutation */
            unsigned m = ts_ext_rand() % 4;
            TSError e = TS_OK;
            switch (m) {
                case 0: e = ts_mut_grow(child, MAX_DEPTH); break;
                case 1: e = ts_mut_shrink(child); break;
                case 2: e = ts_mut_swap(child); break;
                case 3: e = ts_mut_retarget(child, MAX_DEPTH); break;
            }
            if (e != TS_OK && e != TS_EXT_ERR_EMPTY_ROOT) {
                /* keep parent on hard failure */
                ts_free_tree(child);
                free(child);
                if (ts_clone(pop[parent].tree, &child) != TS_OK) continue;
            }

            ts_free_tree(pop[i].tree);
            free(pop[i].tree);
            pop[i].tree = child;
            DeviceScore sc = device_evaluate(pop[i].tree, FW_A_ORDERED_PIPELINE);
            pop[i].fitness = sc.score;
        }

        if (gen % 10 == 0 || gen == GENERATIONS - 1) {
            DeviceScore best = device_evaluate(pop[0].tree, FW_A_ORDERED_PIPELINE);
            printf("Gen %2zu  bestA=%.1f  legalA=%s  nodes=%zu depth=%zu  store=%zu\n",
                   gen, best.score, best.legal ? "yes" : "no",
                   best.nodes, best.depth, device_store_count(&store));
        }
    }

    /* Final ranking under A */
    qsort(pop, POP_SIZE, sizeof(Individual), cmp_ind);
    for (size_t i = 0; i < POP_SIZE / 2; i++)
        device_store_insert(&store, pop[i].tree, GENERATIONS);

    size_t total = device_store_count(&store);
    size_t legal_B = 0, high_B = 0, legal_C = 0, high_C = 0;

    printf("\nCross-firmware evaluation of %zu unique topologies stored under A:\n", total);
    for (size_t i = 0; i < total; i++) {
        TSNode *t = NULL;
        if (ts_parse(store.entries[i].canonical, 64, &t) != TS_OK) continue;
        DeviceScore sb = device_evaluate(t, FW_B_TASK_TREE);
        DeviceScore sc = device_evaluate(t, FW_C_MODULAR_ROBOT);
        if (sb.legal) legal_B++;
        if (sb.score > 5.0) high_B++;
        if (sc.legal) legal_C++;
        if (sc.score > 5.0) high_C++;
        ts_free_tree(t);
        free(t);
    }

    printf("  Firmware B (Task tree)     : %zu legal, %zu high-scoring\n", legal_B, high_B);
    printf("  Firmware C (Modular robot) : %zu legal, %zu high-scoring\n", legal_C, high_C);

    /* Show top-3 under A with their B/C scores */
    printf("\nTop-3 under Firmware A and their scores on B & C:\n");
    for (size_t i = 0; i < 3 && i < POP_SIZE; i++) {
        DeviceScore sa = device_evaluate(pop[i].tree, FW_A_ORDERED_PIPELINE);
        DeviceScore sb = device_evaluate(pop[i].tree, FW_B_TASK_TREE);
        DeviceScore sc = device_evaluate(pop[i].tree, FW_C_MODULAR_ROBOT);
        char *s = NULL;
        ts_encode(pop[i].tree, &s, NULL);
        printf("  #%zu  A=%.1f(%s)  B=%.1f(%s)  C=%.1f(%s)  topo=%s\n",
               i + 1,
               sa.score, sa.legal ? "legal" : "illegal",
               sb.score, sb.legal ? "legal" : "illegal",
               sc.score, sc.legal ? "legal" : "illegal",
               s ? s : "?");
        free(s);
    }

    free_pop(pop, POP_SIZE);
    device_store_free(&store);
    printf("\nDemo complete â€” zero leaks expected under ASan.\n");
    return 0;
}
