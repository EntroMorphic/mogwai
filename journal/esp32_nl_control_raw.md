# RAW — where the ESP32 NL-control project actually stands

Sharpen first. Cut once. We have been cutting a lot.

What do I think I know. We have a board that works, backed up, restorable. We
have a 0.62M encoder that runs on it in 23ms and matches the host to six
decimals. We have a deterministic router that scored 8/10 with no model at all.
We have LCVDB, which is 135KB of someone's good C, and a plan to port it. We
have Needle, which can't run on this chip and never will. We have a red team
that just took my headline number from 13/16 to 5/10 and found that 92.6% of
our trits collapse to zero, which means the encoder→LCVDB handoff is broken
right now and I said it wasn't.

What scares me. The red team didn't find bugs. It found that the evaluation is
a closed loop. I wrote the templates. I wrote the test queries. I picked the
threshold. I decided what counted as a hard case. Every number I have reported
in this session came out of a process I authored end to end. That's not a
measurement, that's a mirror. And I was pleased with it for hours.

What's probably wrong with my first instinct. My instinct is to write the
tokenizer next, because it's the obvious gap. But the tokenizer makes the
system take text, and we don't know what text. Nobody has told me what this
device controls. Lights, thermostat, sensors, GPIO — I invented that. I took it
from Needle's README examples and never questioned it. We may have spent the
whole session optimizing a command set that doesn't exist.

The thing I keep circling. The deterministic router got 8/10 on hard queries
with zero bytes of model. The encoder gets 5/10 on genuinely novel ones. Those
aren't the same test set so the comparison is invalid — but I never ran the
valid version. I have never put the 610KB neural path head to head against the
free one on identical queries with an identical protocol. If the free one wins,
everything downstream of it is dead weight, including the port I was about to
start.

And LCVDB. It benchmarks at 65K, 650K, 6.5M vectors. How many commands does a
light switch have? Two hundred? A thousand? We might be porting a database
three orders of magnitude past our need.

Open questions:
1. What does this device actually do, and who talks to it?
2. Does the neural encoder beat the deterministic router on a fair test?
3. Do we need a vector database at all at our N?
4. Is the classic ESP32 the target, or just what was plugged in?
5. What is "good enough" — accuracy, latency, power? Nobody has said.
