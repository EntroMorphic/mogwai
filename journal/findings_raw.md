# RAW — what twenty experiments actually taught us

I have run about twenty comparisons. Almost none of them were significant. The
only difference whose confidence interval cleared zero was the 22MB teacher.
Everything else — three student widths, TriX signatures, three feature views,
two fusion schemes, two cascades — sat inside a noise floor set by 220 test
examples. I decomposed a five-point gap into "3.6 dimensionality, 1.4 capacity"
and presented it as mechanism. It was 1.6 standard errors. It was numerology.

What scares me. I just looked at per-class accuracy for the first time and the
84.5% mean is hiding iot_wemo_on at 30% and iot_hue_lighton at 33%. And then I
looked at *how* wemo_on fails: five of its ten test cases are predicted as
wemo_off. The router flips ON to OFF half the time. Not random error — a
systematic polarity inversion, on a device that switches mains sockets.

Then the error taxonomy, which nobody asked for until now. 52 wrong actuations
against 15 safe abstentions. The system is three and a half times more likely to
fire an incorrect command than to politely decline. Thirty-three of those fired
an IoT command on input that was not an IoT command at all — someone asks about
the news and the coffee machine starts.

What's probably wrong with my first instinct. My instinct all session was that
the interesting question was architecture. Router or encoder, d=64 or d=128,
signatures or nearest neighbour, fused or cascaded. I ran twenty experiments on
that axis. But the threshold — one scalar, tuned on dev to maximise accuracy —
moves wrong actuations from 107 to 9. That is a bigger effect than every
architectural choice I tested, combined, by an order of magnitude. And it is
free. It has been sitting there the whole time as a number I optimised for the
wrong objective without ever noticing it was a choice.

Worse: 0.47 is not even accuracy-optimal on test. 0.55 gives higher accuracy
(98.1 vs 97.9) AND fewer wrong actions (32 vs 50). The operating point we have
been using is strictly dominated. I tuned it on 518 dev examples and never
checked the curve.

The pattern I keep half-seeing. Everything that adds structure lost. Signatures
compress 769 utterances into 9 vectors: -8.2. Cascade imposes two stages: -35.
Fusion combines views: no gain. Everything that adds raw distinguishing capacity
won. Bigger hash: +2.7. More index data: +11.8. Bigger teacher: +8.2. This task
does not reward abstraction. It rewards remembering.

Open questions:
1. What is the actual cost ratio between a wrong actuation and an abstention?
2. Can char n-grams represent polarity at all, or is that a hard limit?
3. Is per-class accuracy on 3 test examples meaningful in any sense?
4. Why did I never plot an operating curve?
5. Is the retrieval-beats-abstraction pattern a property of this task, or of
   every task with a small closed command set and repetitive users?
