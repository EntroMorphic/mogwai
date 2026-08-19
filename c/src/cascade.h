/* cascade.h — retrieval cascade. Stage 1 REORDERS, never rejects; the final
 * threshold remains the only refusal point. (A classification cascade whose
 * second stage could not abstain cost us 35 points earlier in this project.)
 *
 * stage 1: mask plane only, ~half the ops        12,083 -> top K
 * stage 2: rerank K against the stored TEXT — strictly more information than
 *          any hash width, and only ~600KB for the whole corpus. */
#ifndef CASCADE_H
#define CASCADE_H
#include "ternary.h"
#define CAS_K 100
int  c_mask_overlap(const tvec *a, const tvec *b);
/* integer text similarity: word-set Dice, scaled by 256 */
int  c_text_sim(const char *qa, const char *qb);
#endif
