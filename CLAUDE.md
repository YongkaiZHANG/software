# Project Guidance

This is a C sensor-gateway project built with the local `Makefile`. Prefer small, targeted fixes over architectural rewrites.

## Working Style

- Start from the failing behavior or compiler/runtime error before editing.
- Read only the files directly tied to the issue.
- Preserve the existing process/thread model unless the bug clearly requires changing it.
- Avoid broad refactors in `main.c`, `connmgr.c`, `datamgr.c`, `sensor_db.c`, or `sbuffer.c` unless the user explicitly asks.
- After at most two failed edit-and-verify cycles, stop and explain the blocker instead of continuing to guess.

## Core Files

- `main.c`: program entry point, startup, runtime args, process/thread orchestration
- `connmgr.c` / `connmgr.h`: TCP listener, connection handling, sensor ingress
- `datamgr.c` / `datamgr.h`: sensor processing, logging, business logic
- `sensor_db.c` / `sensor_db.h`: database and persistence logic
- `sbuffer.c` / `sbuffer.h`: shared buffer and synchronization
- `config.h`: compile-time constants
- `sensor_nodes.c`: sender/client utility
- `Makefile`: authoritative build and run commands

## Commands

- Build:
  ```bash
  make
  ```
- Clean:
  ```bash
  make clean
  ```
- Run one gateway plus one sender:
  ```bash
  make run
  ```
- Run gateway plus multiple senders:
  ```bash
  make run-multi
  ```

Use the narrowest command that verifies the specific change. Prefer `make` before broader runtime experiments.

## Project Facts

- Logging uses `FIFO_LOG` / `gateway.log`.
- Sensor-room mapping comes from `room_sensor.map`.
- Local libraries live in `lib/`.
- The `Makefile` treats warnings as errors via `-Werror`, so initialize variables and keep types exact.

## Editing Constraints

- Keep fixes compatible with the current `Makefile`.
- Do not introduce new dependencies unless necessary.
- If changing behavior around FIFOs, sockets, threads, or process lifecycle, explain why before making the change.
