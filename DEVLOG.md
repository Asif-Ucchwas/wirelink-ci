# DEVLOG

## 2026-10-06
- Created repo `wirelink-ci` (Portfolio Project 16).
- Plan: 4 stages, SCons build, raw Ethernet link to a Raspberry Pi, Jenkins CI, write-up.
- Experiment machine: Acer-Nitro (native Ubuntu 26.04). Legion is git-only.

## Stage 1 - SCons build and frame library (done)
- Frame format: dst MAC, src MAC, EtherType 0x88B5, seq, len, payload, CRC-32 (hand-written, covers every byte before it).
- 129 unit checks; known-answer CRC check ("123456789" gives 0xCBF43926).
- Padding note: the NIC pads short frames to 60 bytes, so the len field decides where the payload ends.
- `scons -Q test` is green (gcc 15.2, SCons 4.8.1).

## 2026-11-05 - Stage 2, part 1: sender, receiver and link tests (cable not tested yet)
- Raw AF_PACKET sender and receiver (EtherType 0x88B5). The receiver reports rx, CRC errors, gaps, lost, duplicates and bad payloads.
- Six pytest link tests. They pass on the loopback interface, and each fails when the code is broken on purpose.
- The sender counts a frame as dropped (instead of retrying forever) if the transmit queue stays full, so a cable pull shows up as a sequence gap.

## 2026-11-05 - Stage 2 blocked on hardware
- Acer built-in Ethernet jack is loose or faulty: NO-CARRIER on both ends.
- Added a carrier check to tests/test_link.py so a dead link fails loudly.
- Waiting on a USB-to-Ethernet adapter for the Acer.
