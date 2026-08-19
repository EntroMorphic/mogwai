/* twin-ternary: two bit-planes per vector.
 *   m[] mask  — 1 if this dim carries evidence
 *   s[] sign  — 1 if above centre (only meaningful where m=1)
 * dot = popcount(ma&mb&~(sa^sb)) - popcount(ma&mb&(sa^sb))
 * Binary forces empty dims to -1, so sparse vectors agree on hundreds of dims
 * that carry no information. Ternary lets "no evidence" be its own state. */
#ifndef TERNARY_H
#define TERNARY_H
#include "router.h"
typedef struct { uint32_t m[RWORDS], s[RWORDS]; } tvec;
void t_encode(const router_t *r, const char *text, tvec *out);
int  t_dot(const tvec *a, const tvec *b);       /* raw agreement, [-RD,+RD] */
int  t_score(const tvec *q, const tvec *b);     /* normalised by b's support */
int  t_active(const tvec *v);
#endif
