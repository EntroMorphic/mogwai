/* Exhaustive equivalence: the table must equal __builtin_popcount on every
 * one of the 2^32 possible words. Stronger than diffing benchmark output —
 * this leaves no input unchecked, so the LUT swap cannot change any result. */
#include <stdio.h>
#include <stdint.h>
#include "../src/ternary.h"
int t_dot_ref(const tvec *a, const tvec *b) {          /* pre-rewrite form */
    int agree = 0, disagree = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t both = a->m[i] & b->m[i], diff = a->s[i] ^ b->s[i];
        agree    += __builtin_popcount(both & ~diff);
        disagree += __builtin_popcount(both &  diff);
    }
    return agree - disagree;
}
extern int t_pc_probe(uint32_t x);
int main(void) {
    t_popcnt_init();
    uint32_t x = 0; uint64_t n = 0;
    do { if (t_pc_probe(x) != __builtin_popcount(x)) {
             printf("MISMATCH at %08x\n", x); return 1; }
         n++; } while (++x);
    printf("  popcount table: %llu/%llu words exact (all of 2^32)\n",
           (unsigned long long)n, 4294967296ULL);
    /* and the algebraic rewrite of t_dot must match the original form */
    uint64_t st = 88172645463325252ULL; int bad = 0;
    for (int t = 0; t < 2000000; t++) {
        tvec a, b;
        for (int i = 0; i < RWORDS; i++) {
            st^=st<<13; st^=st>>7; st^=st<<17; a.m[i]=(uint32_t)st;
            st^=st<<13; st^=st>>7; st^=st<<17; a.s[i]=(uint32_t)st;
            st^=st<<13; st^=st>>7; st^=st<<17; b.m[i]=(uint32_t)st;
            st^=st<<13; st^=st>>7; st^=st<<17; b.s[i]=(uint32_t)st;
        }
        if (t_dot(&a,&b) != t_dot_ref(&a,&b)) bad++;
    }
    printf("  t_dot rewrite  : %s (2M random vector pairs)\n", bad ? "MISMATCH" : "exact");
    return bad != 0;
}
