# Inphic IN10 family, KP 8K receiver

The attached device, identified by its owner as IN10 MAX, enumerates as
`Inphic KP 8K`, `1D57:FA65`, release `0116`. Its status collection is
interface 2, usage `000A:0000`, input length 5 (including report ID 03).

Observed approximately every four seconds, without sending any request:

```text
03 73 41 01 64
```

The existing Xenta parser recognizes this format: model, event, battery state,
percentage. Two equal reports are required before displaying 100%; the first
report after linking may be a misleading full reading on related receivers.
This change preserves that safeguard and the existing charging/sleep behavior.

The official [Inphic HUB](https://hub.inphic.cn/) bundle `assets/index-BcHQx9B6.js`
maps model decimal 115 (`73`) to `IN10RMax`, and 116 (`74`) to `IN10Max`.
It uses battery state 1 for percentage, 2 for full and 3 for charging.
Use the firmware's model name rather than assigning `73` to IN10 MAX and
potentially mislabeling other devices sharing this receiver.

Before this change the app listed the attached mouse as `Wireless mouse`.
The Xenta transport already handled its battery frames; the change supplies
the missing model names. A fresh `--list` may show it as asleep because its five-second
initial observation can receive only one four-second report. The resident app
retains the listener and confirms the value on a later poll.

Hardware verified: passive reports, repeated 100% level and native driver model
identification. Charging transitions, low-battery boundaries, Bluetooth and cable
mode have not been verified. Model `74` is sourced from the official table and
has not been tested on separate hardware.
