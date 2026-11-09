# Roadmap

Ideas collected after reviewing related projects. **Not part of 1.0.1.** Nothing here is a promise.

Sources consulted (links only, nothing copied):
[bouni – UAP1 bus](https://blog.bouni.de/posts/2018/hoerrmann-uap1/) ·
[mariopenzendorfer/esphome-hoermann_supramatic_3](https://github.com/mariopenzendorfer/esphome-hoermann_supramatic_3) ·
[stephan192/hoermann_door](https://github.com/stephan192/hoermann_door) ·
[JSladen/hoermann_door_e2](https://github.com/JSladen/hoermann_door_e2) ·
[tsaG1337/SupraMatic-E3](https://github.com/tsaG1337/SupraMatic-E3) ·
[xuio/SupraMatic-E2](https://github.com/xuio/SupraMatic-E2) ·
[Tysonpower/HCPBridgeMqtt_tynet](https://github.com/Tysonpower/HCPBridgeMqtt_tynet) ·
[hgdo](https://github.com/search?q=hgdo&type=repositories) ·
official ESPHome component `hoermann_hcp` (series 4, different protocol)

## v1.1 – structure and testability

- Separate layers: protocol (frames, CRC), command state machine, ESPHome glue.
- Bus ring buffer with µs timestamps, filled outside the receive path, plus export for analysis.
- Bus simulator with these test cases: E3 open/close · P3 `0x0002 → 0x0000 → 0x0001` · stop during travel ·
  command not fetched · real UAP1 answers first · ESP reboot during travel · broadcast loss · second command
  during a running one · CRC errors / incomplete frames.
- CI builds against several ESPHome versions.
- Versioned releases.

## v1.2 – more insight (estimates, clearly marked)

- Detection of failed closing (door did not reach "closed").
- Travel history.
- Position estimate from travel times – shown as an estimate only, **no** percentage control.

## v2.0 – optional hardware

- Optional separate bus controller / watchdog.
- Galvanically isolated RS485.
- Ethernet.

## Deliberately not planned

- Own web interface.
- Automatic closing.
- RFID / keypad in the core component.
- Series 4 protocol (use the official `hoermann_hcp` component).
