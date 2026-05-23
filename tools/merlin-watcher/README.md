# merlin-watcher

User-space subscriber to the MERLIN-V multicast event channel
(`merlin_nl/events`).  See `docs/design/14-mvcp-multicast.md`
for the protocol specification.

## What it does

Joins the `merlin_nl` Generic Netlink family's `events`
multicast group, decodes every event the kernel emits, and
prints either a human-readable line or a JSON-Lines record
suitable for ingestion by a fleet observability pipeline.

## Status

The MERLIN-V kernel module does not yet implement the
`merlin_nl` family --- per the design doc, the in-kernel
implementation is deferred until the upstream merge of
`kernel/merlin/`.  For development this tool runs against a
**stub Genl producer** that ships under `test/`:

```
$ cd test
$ make            # builds stub_genl.ko
$ sudo insmod stub_genl.ko
$ cd ..
$ make
$ sudo ./merlin-watcher --json
[merlin-watcher] subscribed to 'merlin_nl/events'
{"ts":...,"ev":"prog_lifecycle","ns":0,"prog":1,"act":"loaded","tag":"deadbeef01020304"}
{"ts":...,"ev":"telemetry","ns":0,"prog":1,"run_cnt":1024,"run_time_ns":4567890}
...
```

Once the in-tree kernel module ships, the stub is dropped
and `merlin-watcher` works against the real producer with
no source changes.

## Build

Requires `libnl-3-dev` and `libnl-genl-3-dev`:

```bash
$ sudo apt install libnl-3-dev libnl-genl-3-dev
$ make
```

## Usage

```
merlin-watcher [--json] [--filter CLASS,CLASS,...]

  --json              Emit JSON-Lines instead of human-readable.
  --filter LIST       Comma-separated list of event classes to
                      receive.  One or more of:
                        prog_lifecycle map_update telemetry
                        namespace attestation reject ratelimit_drop
                      Default: all.
```

## Files

| File | Purpose |
|------|---------|
| `merlin-watcher.c` | main; libnl subscription + event dispatch |
| `decode.c`         | per-event attribute decoders |
| `Makefile`         | build with `pkg-config --libs libnl-genl-3.0` |
| `test/stub_genl.c` | out-of-tree kernel module that registers a `merlin_nl` family and emits synthetic events on a timer.  Lets us shake out the user-space code without waiting for the real in-kernel producer. |
| `test/Makefile`    | builds `stub_genl.ko` |

## Assisted-by

Copilot-CLI:Claude-Opus
