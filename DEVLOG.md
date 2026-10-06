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
