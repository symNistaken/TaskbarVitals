# RAWM protocol notes

RAWM mice (and the SADES Dagger, which shares the platform) talk to the RAWM HUB app over a
vendor HID collection, `FF00:0001`, with 64-byte unnumbered input and output reports (65 bytes
with the report ID). The receivers and mice use Nordic's vendor ID `1915`.

Source: the RAWM HUB web app, Hub Web 2.0 (<https://www.rawmtech.com/hub.html>), and its script
`https://hub.miracletek.net/hub/js/library.min.js` (obfuscated; the function and constant names below
are the script's own). The supported product IDs come from the device table the app downloads
(`https://www.miracletek.net/game/device.php`). Leviathan V4 GT and NYX60 use Hub Web 3.0 on VID
`373E`, which is a different protocol.

## Packets

Byte 0 of every report (after the report ID):

| Top bits | Meaning |
|---|---|
| `0x80` (`PACKET_TYPE_EVENT_DATA`) | the low 6 bits are a payload size; the payload continues the event stream |
| `0x40` (`PACKET_TYPE_BUFFER_SIZE`) | flow control (free buffer sizes); ignored here |
| `0xC0` (`PACKET_TYPE_EXTENDED`) | the low 6 bits are an ESB channel; the rest of the report is a packet for that channel. The mouse behind a receiver is channel 0 |

The payloads form a byte stream of events:

```
FF FF FF FF                      sync (device to host only)
cmd | ((len >> 4) & 0xF0), len   len includes these two bytes (12 bits)
body ...
```

## Reading the battery

The driver repeats what the app does when a receiver is connected (`send_event_query`, `send_event_ping`):

1. To the receiver: `00 8D 01 0D 03 00 00 <unix time, 8 bytes LE>` (`CMD_QUERY`, OS = PC), then
   pings `00 84 0E 04 00 00`. The device only sends while the host keeps writing, so the host pings.
   The receiver answers `CMD_QUERY_RESULT` (`02`) with JSON, e.g.
   `{"r":"2.0.2","rc":4,"dn":"RAWM HS Receiver","pi":9030,"vi":6421,...}`.
2. The same query and pings for channel 0, prefixed with `C0`. The mouse answers with a ~550-byte JSON object
   in 30-byte ESB chunks, e.g. `{"r":"1.2.0","dn":"LEVIATHAN V4","pi":9034,"vi":6421,"battery":100,"chr":0,...}`.
   `battery` is a percentage (RAWM HUB shows it as is), `chr` is 1 while charging.
3. Later changes arrive as `CMD_NOTIFY` (`0B`) type `17`: `[17, percent, charging]`.

The driver always asks the receiver first, as the app does (in one test, a query sent only to channel 0
got no reply). A sleeping mouse does not answer at all; the receiver still does.

The query carries the PC's clock because the app sends it that way; nothing else is written.
Mice report `"crc":1`, which tells the app to wrap later commands (configuration) in a CRC-16 frame;
the query and pings are accepted without one, as the app sends them before it knows.

Tested on a Leviathan V4 (firmware 1.2.0) with the RAWM HS Receiver `1915:2346` (firmware 2.0.2).
