# Changelog

## 1.0.1 – bug fixes (unreleased, branch `v1.0.1`, to be tested at the door)

Each item can be tested separately; new safety logic can be switched off one by one. The time-critical
send/answer logic of `inject_v2` (when and in which order the board answers the status request) is **unchanged**;
all changes are in command acceptance, in the state/diagnostic logic outside the receive path and in `loop()`.

1. **Stop has priority** (`stop_priority`, default `true`, `false` = 1.0.0 behaviour): Stop replaces a command
   that is still waiting to be fetched. The replaced command is discarded (no retry, never sent again). A
   travel command never replaces a waiting Stop. Any accepted command ends the retries of the previous one
   (before, the legacy inject logic could re-send an old command after a new one was queued). Stop is accepted
   whenever one of our commands is waiting or being verified, also in an end position.
2. **Connection timeout** (`connection_timeout`, default `5s`, `0s` = off): timestamp of the last valid broadcast
   (length + CRC, as in the existing parser). Older than the timeout → `valid_broadcast` false (`Drive connected`
   off), state unknown (`Status` "Keine Verbindung", cover idle/intermediate, `derived_status` "unknown"), waiting
   commands dropped, new commands rejected. Recovers automatically. `valid_broadcast` is no longer set from any
   non-zero byte – only from valid broadcast frames. The state is `unknown` after boot until the first broadcast.
3. **P series light**: new `toggle_light_button` (button platform, "Toggle light"). With `p_series_mode` an
   existing `light` platform logs a warning (compile time and boot) and only toggles: every change of the light
   entity sends "toggle light" once, nothing on boot, no syncing from the (non-existent) light bit. E series
   unchanged.
4. **Unknown direction** in `derived_status`: a direction is only claimed if our own command was fetched ≤ 5 s
   before the end position was left in the matching direction, or if the drive reports direction bits (E series,
   now used directly). Otherwise "Moving (direction unknown)" (remote, wall button, real UAP1). Own command from
   an intermediate position on the P series: "Intermediate position (command sent, effect not observable)".
5. **Venting on the P series marked as estimate**: "Stopped between end positions (venting assumed)" /
   "Venting (estimate)".
6. **Command result** (`command_result` text sensor): `sent`, `confirmed`, `not observable`, `failed` (+ `rejected`,
   `replaced`). A missing confirmation of a command whose effect cannot be observed (P series light, impulse,
   stop, travel from an intermediate position) is **not** a failure and is never retried (unchanged: no retry).
7. **Limited retries / passive fallback** (`inject_max_failures`, default `5`, `0` = off; `inject_pause`, default
   `60s`): after this many failed commands in a row (not fetched within 3 s, or measurable effect missing after
   all attempts) no injects for the pause, log warning. Sensor `inject_failures` ("Inject failures in a row").
   Reset by the next confirmed command.
8. **Operating mode** `mode: monitor | inject | emulate`. `monitor` only listens and never sends (all commands
   rejected) – used in the example as safe start. `inject` = `parallel_uap1: true` + `inject_v2: true`.
   `emulate` = `parallel_uap1: false`, marked EXPERIMENTAL (warning). `parallel_uap1` / `inject_v2` are still
   accepted with their 1.0.0 meaning (deprecation warning); combining them with `mode` is an error.
9. **Diagnostics** (optional sensors): `valid_broadcasts`, `crc_errors` (approximate, sliding window),
   `seconds_since_broadcast`. Counting is one increment/assignment in the broadcast branch; publishing happens in
   the slow loop (counters every 30 s, seconds on change). Position tracking for `derived_status` moved from the
   receive path to the slow loop.
10. **Tests**: host test `tests/host/run.sh` (g++, ESPHome stubs, simulated frames, 15 scenarios). Both examples
    compile with ESPHome 2026.10.2.
11. **Docs**: README (options, upgrade notes, test plan, references: bouni blog, related projects, difference to
    the official `hoermann_hcp` component for series 4), [ROADMAP.md](ROADMAP.md).

## 1.0.0 – 2026-11-09

First release of this fork (base: mariopenzendorfer/esphome-hoermann_supramatic_3 @ 20f6509).

Changes in `components/uapbridge_esp` compared with upstream:

- **Inject mode** (`parallel_uap1`, default `true`): the real UAP1 stays on the bus; the board does not answer
  the slave scan and only answers one status request per pending command. Effect checked on the status
  broadcast, max. 3 attempts. `parallel_uap1: false` restores the upstream UAP1 emulation.
- **`inject_v2`**: every received byte is evaluated (before: only the last 5 bytes per loop), the answer is sent
  immediately and the main loop runs without its pause while a command is pending (high-frequency loop);
  3 s fetch timeout instead of a permanent block; retries only where the effect is measurable;
  stop allowed everywhere except in the end positions; "replaced by new command" handling.
- **`p_series_mode`** for SupraMatic P series 3 (status only `0x0002`/`0x0001`/`0x0000`): effect via end
  positions, light sent once, venting = closed left + 25 s without end position. Requires `inject_v2`.
- New text sensors `last_command`, `raw_status`, `derived_status` (event based, published from the slow loop,
  never from the receive path).
- Example uses `restore_mode: ALWAYS_OFF` for the light.

`components/uapbridge` is unchanged.

(Since 1.0.1 `components/uapbridge` has small additions: light toggle button, P series light handling, cover
unknown state, `is_light_state_reported()`.)
