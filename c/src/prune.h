/* Index pruning, shared by compare (measures it) and mkblob (ships it).
 * Deliberately one implementation: mkblob and compare already keep separate
 * copies of the index build, and the esp32 tree had drifted from c/src once.
 * If the harness and the exporter prune differently, on-device parity is a
 * measurement of nothing. */
#ifndef PRUNE_H
#define PRUNE_H
#include "router.h"
#include "ternary.h"
typedef struct {
    int dup;      /* drop identical code + identical label (provably free)   */
    int neg_k;    /* keep 1-in-K negatives; 0 or 1 = off (lossy)             */
    int cnn;      /* drop negatives that are nobody's nearest neighbour      */
    int neg_top;  /* keep the N negatives with the highest NN-coverage count */
    int neg_bound;/* keep the N negatives that most often sit on a POSITIVE's boundary */
} prune_opt;
/* Compacts every parallel array in place and returns the surviving count.
 * idx may be NULL (mkblob does not build the binary rvec index).
 * Must run AFTER encoding, so surviving codes are bit-identical to unpruned. */
int prune_index(char **U_t, char (*U_l)[RNAMELEN], router_t *R,
                tvec *TI, rvec *idx, int U_n, prune_opt o, int verbose);
int prune_parse(const char *arg, prune_opt *o);   /* returns 1 if consumed */
#endif
