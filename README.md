# simpleLoRa reference firmware

A minimal, deliberately **non-LoRaWAN** point-to-point LoRa example for SQC485I v2 hardware
(ESP32-C3 + SX1262). Two boards ping-pong a plain-text custom payload directly over the SX1262
radio — no LoRaWAN MAC layer, no join procedure, no Network Server involved.

## Why this exists

`chirpstack-concentratord` (the open-source LoRa gateway driver behind ChirpStack) is built for
the ChirpStack/LoRaWAN ecosystem, but it operates purely at the LoRa PHY layer — it demodulates
whatever bytes arrive on its configured channels and hands them upward as opaque `phyPayload`
bytes over its ZeroMQ event socket. It does not itself parse or require a valid LoRaWAN MAC
frame; that parsing happens further up the stack (ChirpStack's gateway-bridge / Network Server).

This firmware exists to generate a **real, on-air, non-LoRaWAN payload** so that claim can be
verified against real hardware, rather than taken on faith from architecture docs. It's the
transmit side of a small `simpleLoRa` project (a gateway-side ZMQ→MQTT forwarder pairs with it,
bridging `concentratord`'s uplink/downlink events to a plain custom MQTT payload with no
LoRaWAN/ChirpStack Network Server involved on that end either), and is meant to double as a
standalone reference example for anyone who wants to talk to an SX1262 directly without pulling
in a full LoRaWAN stack.

## Payload format

Deliberately simple, plain ASCII, pipe-delimited:

```
SIMPLELORA|<sender_node_id>|<sequence>
```

## Hardware

SQC485I **v2** only (ESP32-C3 + SX1262). Pin mapping in `include/bsp.h` — only the SX1262
SPI/reset/busy/DIO1 pins are used; this firmware doesn't touch the board's RS485/BLE
peripherals, since it only exercises the raw radio.

## Radio config

Defaults to 923.2 MHz / SF7 / BW125 / sync word `0x12` — the lowest channel of a typical AS923-1
channel plan. **The sync word matters**: `0x12` is the "private network" LoRa convention,
matching gateways configured with `lorawan_public=false`; a public-network gateway
(`lorawan_public=true`) needs `0x34` instead, or it will never even recognize the preamble.
Override in `include/bsp.h` to match your gateway.

## Build & flash

Two boards, two roles — only the first message needs a designated sender to avoid both boards
transmitting simultaneously at boot; after that, both sides just echo-and-increment. The
initiator also re-sends every 8s on its own if nothing comes back, so it generates traffic for a
gateway-side test even if the two boards never hear each other directly:

```bash
pio run -e node1 -t upload -t monitor   # initiator — sends first
pio run -e node2 -t upload -t monitor   # responder — waits, then echoes
```

If uploads fail with `esptool`'s stub reporting "No serial data received" right after "Stub
running..." (seen repeatedly on one dev machine, survived a real VBUS power-cycle, unrelated to
the firmware itself), `platformio.ini` already sets `upload_flags = --no-stub` as the default
workaround — the ROM bootloader path is slower but was reliable where the stub path wasn't.

## Status

**Bench-tested and verified end-to-end.** Both roles receive each other locally (RSSI ≈ -3 dBm,
SNR ≈ 13 dB at close range) and are received cleanly by a real `concentratord`-based gateway
(`rx_received_ok` == `rx_received`, no CRC failures) — the literal payload
(`SIMPLELORA|<id>|<seq>`) was observed unmodified all the way through to the gateway-side
forwarder's output, both uplink and downlink.

Two real bugs were found and fixed along the way, worth knowing if you're adapting this for your
own gateway and things go silent despite the transmitter clearly succeeding:
1. The `LoRaService` helper this firmware uses only arms the receiver as a side effect of a
   completed *transmit* — a role that never sends first (the responder) never actually starts
   listening unless you explicitly call its receive-arming function after `begin()`.
2. The LoRa sync word must match your gateway's `lorawan_public` setting (see "Radio config"
   above) — a mismatch causes total silence at the gateway with no error on either side, even
   at close range with a healthy link.
