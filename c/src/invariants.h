/* invariants.h — relationships the design PROMISES, asserted in code.
 *
 * A guarantee that is not encoded is a belief, and beliefs lose to numbers.
 * These abort the run rather than warn: a warning is a caveat, and caveats
 * make an unexamined number look examined.
 *
 * inv_disjoint  would have caught the NLU dev-leak (75.6% of dev in the index)
 * inv_superset  would have caught "twin-ternary < binary", which is impossible
 *               by construction (all-ones mask reduces ternary to binary)      */
#ifndef INVARIANTS_H
#define INVARIANTS_H
extern int INV_QUIET;   /* 1 = stay silent when an invariant PASSES; failures still abort */
#include <stddef.h>

void inv_disjoint(const char *what, char **a, int na, char **b, int nb);
void inv_superset(const char *sub, double sub_acc, const char *sup, double sup_acc, double tol);
void inv_similar_rate(const char *what, double a, double b, double tol_pts);
void inv_bounded(const char *what, long v, long lo, long hi);
int  inv_violations(void);
#endif
