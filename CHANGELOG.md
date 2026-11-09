# Changelog

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
