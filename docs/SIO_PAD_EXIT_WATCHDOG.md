# DualShock post-configuration watchdog

## Closed defect

Command `43h` disabled the controller watchdog when it exited configuration
mode. The pinned DualShock protocol explicitly states that entering
configuration mode activates an approximately one-second watchdog and that it
remains active after returning to normal mode. Games must continue polling the
pad during loading; otherwise hardware resets to digital mode, stops and locks
rumble, and turns the Analog LED off.

Exiting configuration mode now changes only the protocol mode. The existing
watchdog deadline remains armed and subsequent normal-mode communication
continues to refresh it through the same native SIO-cycle path.

Snapshot validation now accepts the reachable idle state where part of that
period has elapsed: remaining time must be positive and no greater than one
configured watchdog period. It still rejects expired, extended, sentinel, and
overflowed deadlines.

## Hostile boundary and replay proof

The adversarial test enters configuration mode, exits with the full nine-byte
packet, and snapshots the normal-mode pad with its watchdog still active. A
second pad restores that snapshot. Both are advanced to exactly one CPU cycle
before the deadline and must remain unlocked, then to the exact deadline and
must identically reset to locked digital state with the watchdog inactive.

## Remaining boundaries

The documented duration is approximate and remains represented by the pinned
33,868,800-cycle profile. Revision-specific oscillator tolerance and
sub-cycle controller timing require hardware traces.
