"""Independently-authored evaluation set.

Written in a deliberately different register from corpus.py: different verbs,
different sentence shapes, more indirect. Independence is not assumed - it is
VERIFIED by nearest-neighbour similarity to the training corpus, and anything
too close is excluded from scoring.

DEV is used only for threshold selection. TEST is never used for tuning.
"""
DEV = [
 ("we could use more light in here","set_lights"),
 ("shut everything down in the hall","set_lights"),
 ("hold it at nineteen","set_thermostat"),
 ("stop heating the house","set_thermostat"),
 ("what is the reading on the probe","read_sensor"),
 ("give me the numbers","read_sensor"),
 ("assert line twelve","set_gpio"),
 ("float pin six","set_gpio"),
 ("did the parcel arrive",None),
 ("how is the traffic looking",None),
]
TEST = [
 # lights - indirect, novel verbs
 ("it is like a cave in the den","set_lights"),
 ("give me half brightness upstairs","set_lights"),
 ("the lamp in the study should be off","set_lights"),
 ("kill everything in the basement","set_lights"),
 ("I need it brighter to work","set_lights"),
 ("we are being blinded in the porch","set_lights"),
 # thermostat
 ("bump it up two degrees","set_thermostat"),
 ("I would like it warmer tonight","set_thermostat"),
 ("stop the air conditioning","set_thermostat"),
 ("take the edge off the cold","set_thermostat"),
 ("this place is arctic","set_thermostat"),
 # sensor
 ("how muggy is it","read_sensor"),
 ("check ambient conditions","read_sensor"),
 ("what do the instruments say","read_sensor"),
 ("give me a moisture figure","read_sensor"),
 # gpio
 ("bring output nine up","set_gpio"),
 ("ground the signal on three","set_gpio"),
 ("drop line twenty one","set_gpio"),
 ("energise channel four","set_gpio"),
 # off-topic
 ("what is for dinner",None),
 ("set an alarm for six",None),
 ("did I miss any calls",None),
 ("read me the headlines",None),
 ("how long until the bus",None),
]
