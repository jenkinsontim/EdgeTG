/*
 * test_evolution_experiment.c
 *
 * Minimal evolutionary search demo exercising EXISTING EdgeTG modules.
 * It measures ONE concrete claim:
 *
 *   "Canonical shape identity + interning means you never re-evaluate the
 *    same architecture twice, which makes evolutionary search dramatically
 *    cheaper."
 *
 * The measured evidence is a single number: the fraction of candidate
 * evaluations avoided because an identical canonical shape was already
 * interned (hash-consed) in a previous generation.
 *
 * This is evolutionary search over tree STRUCTURE only. It is not machine
 * learning and adds no new grammar glyphs. The interning mechanism itself
 * (ts_intern) is used unmodified.
 */
#include "ts_core.h"
#include "ts_core_ext.h"
#include "ts_boltzmann.h"
#include "ts_intern.h"
#include "ts_layers.h"
#include "ts_norm.h"
#include "ts_roles.h"
#include "ts_enum.h"
#include "ts_metric.h"
#include "device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define POP_SIZE       64
#define GENERATIONS    200
#define MAX_DEPTH      8
#define MAX_NODES      30
#define REPORT_EVERY   25

/* Cached scores for one intern ID (one canonical shape). */
typedef struct {
    DeviceScore s[3];           /* s[0]=A, s[1]=B, s[2]=C */
    bool        valid;
} ScoreCacheEntry;

typedef struct {
    TSNode      *tree;
    DeviceScore s[3];           /* current best-known scores for this individual's shape */
} Individual;

typedef struct {
    TSInternTable *intern;
    ScoreCacheEntry *cache;     /* indexed by intern ID */
    size_t          cache_cap;

    unsigned long long candidates_generated;
    unsigned long long intern_hits;      /* shapes already interned -> scores reused */
    unsigned long long intern_misses;    /* new unique shapes -> evaluated */
    unsigned long long evaluations_performed; /* equals intern_misses (X firmwares each) */
} Exp;

typedef struct {
    int64_t  id;
    double   scoreA, scoreB, scoreC;
} BestEntry;

static int ensure_cache(Exp *e, int64_t id) {
    size_t need = (size_t)id + 1;
    if (need <= e->cache_cap) return 0;
    size_t nc = e->cache_cap ? e->cache_cap * 2 : 256;
    while (nc < need) nc *= 2;
    ScoreCacheEntry *p = (ScoreCacheEntry *)realloc(e->cache, nc * sizeof(*p));
    if (!p) return -1;
    memset(p + e->cache_cap, 0, (nc - e->cache_cap) * sizeof(*p));
    e->cache = p;
    e->cache_cap = nc;
    return 0;
}

/* Consider one candidate shape. Either the interning cache already has its
 * scores (hit) or we evaluate it fresh under all three firmwares (miss). */
static void consider(Exp *e, Individual *ind) {
    e->candidates_generated++;

    int64_t id = ts_intern_lookup(e->intern, ind->tree);
    if (id >= 0) {
        e->intern_hits++;
        if (ensure_cache(e, id) != 0) { id = -1; }
        if (id >= 0 && e->cache[id].valid) {
            ind->s[0] = e->cache[id].s[0];
            ind->s[1] = e->cache[id].s[1];
            ind->s[2] = e->cache[id].s[2];
            return;
        }
        /* cached slot not yet populated (shouldn't normally happen, but be safe) */
        id = -1;
    }

    /* intern miss: evaluate fresh under A, B, C */
    e->intern_misses++;
    e->evaluations_performed++;

    ind->s[0] = device_evaluate(ind->tree, FW_A_ORDERED_PIPELINE);
    ind->s[1] = device_evaluate(ind->tree, FW_B_TASK_TREE);
    ind->s[2] = device_evaluate(ind->tree, FW_C_MODULAR_ROBOT);

    id = ts_intern_add(e->intern, ind->tree);
    if (id >= 0 && ensure_cache(e, id) == 0) {
        e->cache[id].s[0] = ind->s[0];
        e->cache[id].s[1] = ind->s[1];
        e->cache[id].s[2] = ind->s[2];
        e->cache[id].valid = true;
    }
}

static int cmp_ind_by_A(const void *A, const void *B) {
    const Individual *a = (const Individual *)A;
    const Individual *b = (const Individual *)B;
    if (a->s[0].score < b->s[0].score) return  1;
    if (a->s[0].score > b->s[0].score) return -1;
    return 0;
}

/* Insert a distinct architecture into the top-5 list (by intern ID), sorted
 * descending by Firmware A score. Already-present shapes are skipped so the
 * report shows five distinct architectures, not five copies of the elite. */
static void top5_insert(BestEntry *top5, size_t *top5_n, int64_t id,
                        double sa, double sb, double sc) {
    if (id < 0) return;
    for (size_t j = 0; j < *top5_n; j++) {
        if (top5[j].id == id) return;   /* already listed */
    }
    size_t insert = *top5_n;
    for (size_t j = 0; j < *top5_n; j++) {
        if (sa > top5[j].scoreA) { insert = j; break; }
    }
    if (*top5_n < 5) {
        for (size_t j = *top5_n; j > insert; j--) top5[j] = top5[j - 1];
        top5[insert].id = id;
        top5[insert].scoreA = sa;
        top5[insert].scoreB = sb;
        top5[insert].scoreC = sc;
        (*top5_n)++;
    } else if (insert < 5) {
        for (size_t j = 4; j > insert; j--) top5[j] = top5[j - 1];
        top5[insert].id = id;
        top5[insert].scoreA = sa;
        top5[insert].scoreB = sb;
        top5[insert].scoreC = sc;
    }
}

int main(void) {
    setbuf(stdout, NULL);
    ts_ext_srand(20260831);

    printf("EdgeTG -- STRUCTURE CHROMOSOME EXPERIMENT\n");
    printf("Population: %d | Generations: %d | Firmwares: A, B, C\n\n", POP_SIZE, GENERATIONS);

    Exp exp = {0};
    exp.intern = ts_intern_create(256);
    if (!exp.intern) { fprintf(stderr, "intern table alloc failed\n"); return 1; }

    TSBoltzmannParams params = ts_boltzmann_defaults();

    Individual pop[POP_SIZE];
    for (size_t i = 0; i < POP_SIZE; i++) {
        pop[i].tree = NULL;
        if (ts_random_tree_boltzmann(MAX_DEPTH, MAX_NODES, &params, &pop[i].tree) != TS_OK) {
            fprintf(stderr, "initial population generation failed at %zu\n", i);
            goto cleanup;
        }
        consider(&exp, &pop[i]);
    }

    BestEntry bestA = { -1, 0, 0, 0 };
    BestEntry bestB = { -1, 0, 0, 0 };
    BestEntry bestC = { -1, 0, 0, 0 };
    BestEntry top5[5] = {0};
    size_t top5_n = 0;

    /* capture bests from the initial population */
    for (size_t i = 0; i < POP_SIZE; i++) {
        int64_t id = ts_intern_lookup(exp.intern, pop[i].tree);
        if (id < 0) continue;
        const double sa = exp.cache[id].s[0].score;
        const double sb = exp.cache[id].s[1].score;
        const double sc = exp.cache[id].s[2].score;
        if (bestA.id < 0 || sa > bestA.scoreA) { bestA.id = id; bestA.scoreA = sa; bestA.scoreB = sb; bestA.scoreC = sc; }
        if (bestB.id < 0 || sb > bestB.scoreB) { bestB.id = id; bestB.scoreB = sb; bestB.scoreA = sa; bestB.scoreC = sc; }
        if (bestC.id < 0 || sc > bestC.scoreC) { bestC.id = id; bestC.scoreC = sc; bestC.scoreA = sa; bestC.scoreB = sb; }

        top5_insert(top5, &top5_n, id, sa, sb, sc);
    }

    for (size_t gen = 1; gen <= GENERATIONS; gen++) {
        /* selection: sort by A descending */
        qsort(pop, POP_SIZE, sizeof(Individual), cmp_ind_by_A);

        /* Replace bottom half with mutated clones of top-half parents. */
        for (size_t i = POP_SIZE / 2; i < POP_SIZE; i++) {
            size_t parent_idx = ts_ext_rand() % (POP_SIZE / 2);

            TSNode *child = NULL;
            if (ts_clone(pop[parent_idx].tree, &child) != TS_OK) child = NULL;
            if (child) {
                int op = (int)(ts_ext_rand() % 4);
                TSError mres = TS_OK;
                switch (op) {
                    case 0: mres = ts_mut_grow(child, MAX_DEPTH); break;
                    case 1: mres = ts_mut_shrink(child); break;
                    case 2: mres = ts_mut_swap(child); break;
                    default: mres = ts_mut_retarget(child, MAX_DEPTH); break;
                }
                if (mres != TS_OK) {
                    /* keep the (cloned) parent unmutated */
                    ts_free_tree(child); free(child);
                    child = NULL;
                }
            }
            if (!child) {
                if (ts_clone(pop[parent_idx].tree, &child) != TS_OK) child = NULL;
                if (!child) continue; /* leave old slot; do not free below */
            }

            ts_free_tree(pop[i].tree); free(pop[i].tree);
            pop[i].tree = child;
        }

        /* evaluate / reuse scores for every individual this generation */
        double sumA = 0.0;
        for (size_t i = 0; i < POP_SIZE; i++) {
            consider(&exp, &pop[i]);
            sumA += pop[i].s[0].score;
        }

        /* track bests */
        for (size_t i = 0; i < POP_SIZE; i++) {
            int64_t id = ts_intern_lookup(exp.intern, pop[i].tree);
            if (id < 0) continue;
            double sa = pop[i].s[0].score;
            double sb = pop[i].s[1].score;
            double sc = pop[i].s[2].score;
            if (bestA.id < 0 || sa > bestA.scoreA) { bestA.id = id; bestA.scoreA = sa; bestA.scoreB = sb; bestA.scoreC = sc; }
            if (bestB.id < 0 || sb > bestB.scoreB) { bestB.id = id; bestB.scoreB = sb; bestB.scoreA = sa; bestB.scoreC = sc; }
            if (bestC.id < 0 || sc > bestC.scoreC) { bestC.id = id; bestC.scoreC = sc; bestC.scoreA = sa; bestC.scoreB = sb; }

            top5_insert(top5, &top5_n, id, sa, sb, sc);
        }

        double avgA = sumA / (double)POP_SIZE;
        if (gen == 1 || gen % REPORT_EVERY == 0 || gen == GENERATIONS) {
            unsigned long long total_considered = exp.intern_hits + exp.intern_misses;
            double dedup = total_considered ? (100.0 * (double)exp.intern_hits / (double)total_considered) : 0.0;
            printf("Gen %4zu | bestA=%7.2f bestB=%7.2f bestC=%7.2f | avgA=%7.2f | "
                   "dedup=%5.1f%% (%llu hits / %llu misses)\n",
                   gen, bestA.scoreA, bestB.scoreB, bestC.scoreC, avgA,
                   dedup, exp.intern_hits, exp.intern_misses);
        }
    }

    unsigned long long total_considered = exp.intern_hits + exp.intern_misses;
    double dedup = total_considered ? (100.0 * (double)exp.intern_hits / (double)total_considered) : 0.0;

    printf("\n=== FINAL RESULTS ===\n");
    printf("Unique architectures discovered: %zu\n", ts_intern_count(exp.intern));
    printf("Total candidates considered:    %llu\n", total_considered);
    printf("Evaluations actually performed: %llu\n", exp.evaluations_performed);
    printf("Evaluations saved by interning: %llu (%.1f%%)\n", exp.intern_hits, dedup);
    printf("\n");
    printf("Without interning: %llu evaluations needed\n", total_considered);
    printf("With interning:    %llu evaluations needed\n", exp.evaluations_performed);
    printf("Compute saved:     %.1f%%\n", dedup);

    printf("\nBest under Firmware A (Ordered Pipeline): %s  score=%.2f\n",
           bestA.id >= 0 ? ts_intern_get(exp.intern, bestA.id) : "(none)", bestA.scoreA);
    printf("Best under Firmware B (Task Tree):        %s  score=%.2f\n",
           bestB.id >= 0 ? ts_intern_get(exp.intern, bestB.id) : "(none)", bestB.scoreB);
    printf("Best under Firmware C (Modular Robot):    %s  score=%.2f\n",
           bestC.id >= 0 ? ts_intern_get(exp.intern, bestC.id) : "(none)", bestC.scoreC);

    printf("\nTop-5 architectures by Firmware A score:\n");
    for (size_t j = 0; j < top5_n; j++) {
        if (top5[j].id < 0) continue;
        printf("  #%zu  A=%7.2f  B=%7.2f  C=%7.2f  %s\n",
               j + 1, top5[j].scoreA, top5[j].scoreB, top5[j].scoreC,
               ts_intern_get(exp.intern, top5[j].id));
    }

cleanup:
    for (size_t i = 0; i < POP_SIZE; i++) {
        if (pop[i].tree) { ts_free_tree(pop[i].tree); free(pop[i].tree); }
    }
    ts_intern_free(exp.intern);
    free(exp.cache);
    return 0;
}