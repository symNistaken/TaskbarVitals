# YASB integration

Shows one icon per device type plus the battery percentage in [YASB](https://github.com/amnweb/yasb), for example:

```text
󰐍 75%   󰕾 30%
```

The widget needs no rebuild of YASB: a small PowerShell script turns the app's status into
the JSON that a YASB `CustomWidget` expects.

## How it works

The app writes a snapshot to `%LOCALAPPDATA%\WirelessBattery\status.json` after every poll
(**every 60 seconds**), and deletes it when it quits. The script reads that file when it is fresh
(less than three minutes old), so YASB does not poll the receivers a second time while the app is
running.

When the app is not running, the script runs `WirelessBattery.exe --json`, which polls the devices
and prints the same document. App versions without `--json` still work: the script falls back to
parsing `--list`.

The snapshot is written after every poll, so YASB's own interval only limits how quickly it picks
up a change. Keep `run_interval` at 60 seconds or higher; polling faster does not update the data
any faster.

## Requirements

- Wireless Battery with `--json` support (this version) for device types, last-known values and the
  snapshot. Older versions only work through the `--list` fallback.
- A Nerd Font in the YASB stylesheet, e.g. `JetBrainsMono NFP` (the glyphs are from Material
  Design Icons).

## Install

1. Copy `docs/wireless-battery-yasb.ps1` to `%USERPROFILE%\.config\yasb\`. The path must not
   contain spaces (see the YASB caveat below).
2. Add `"wireless_battery"` to `bars.<your-bar>.widgets.right`, just before `"volume"`.
3. Add the widget to `config.yaml`:

   ```yaml
   wireless_battery:
     type: "yasb.custom.CustomWidget"
     options:
       label: "{data[label]}"
       label_alt: "{data[short]}"
       class_name: "wb-widget"
       tooltip: true
       tooltip_label: "{data[tooltip]}"
       callbacks:
         on_left: "exec_custom"
         on_middle: "toggle_label"
         on_right: "exec cmd.exe /c start ms-settings:bluetooth"
       exec_options:
         # YASB 2.0.7 splits run_cmd on spaces: keep it unquoted and the path free of spaces.
         run_cmd: 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File %USERPROFILE%\.config\yasb\wireless-battery-yasb.ps1'
         run_interval: 60000
         return_format: "json"
         hide_empty: true
         use_shell: true
         encoding: "utf-8"
   ```

4. Add the styles to `styles.css`:

   ```css
   .wb-widget .label {
     padding: 0 2px;
   }
   .wb-widget .label.alt {
     color: var(--text-secondary);
   }
   ```

5. Reload YASB:

   ```powershell
   & "C:\Program Files\YASB\yasbc.exe" reload
   ```

## The JSON contract

`status.json` and `--json` print the same document:

```json
{
  "version": 1,
  "updated": "2026-11-17T21:24:50Z",
  "source": "snapshot",
  "devices": [
    {
      "key": "atknk:1216",
      "name": "ATK / VXE NK mouse",
      "kind": "mouse",
      "via": "2.4 GHz receiver",
      "percent": 75,
      "live": true,
      "charging": false,
      "asleep": false,
      "connected": true,
      "low": false,
      "levelText": "",
      "detail": "",
      "millivolts": 2400
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `version` | Document version, currently `1`. |
| `updated` | UTC time of the poll, ISO 8601. |
| `source` | `snapshot` for `status.json`, `cli` for `--json`. |
| `key` | Stable device id (the same value the app uses for hiding and ordering). |
| `kind` | `headset`, `mouse`, `keyboard`, `gamepad`, `phone` or `other`. |
| `percent` | The percentage the app would show: the live value, or the last known value while the device sleeps, or `-1` when nothing is known. |
| `live` | `true` when `percent` came from this poll, `false` when it is the last known value. |
| `low` | `true` below 15% and not charging. |
| `levelText` | Coarse level from devices that only report `Low`/`Full`/...; `percent` may be `-1`. |
| `detail` | Extra breakdown such as `L 80%, R 75%, case 40%`. |
| `millivolts` | Battery voltage when the device reports it, `0` otherwise. |
| `asleep`, `charging`, `connected` | Device state as shown in the app. |

The device list is already in the app's order, with devices hidden in the app
(`HiddenDevices`) and, when `HideLastKnownDevices` is on, sleeping devices removed first.

## Choosing what the label shows

At the top of the script:

```powershell
$LowPercent = 15
$Separator = '  '
$UseHtmlColors = $true
```

`-Mode` selects the device(s):

- `lowest` (default) — the device with the lowest known percentage;
- `all` — every visible device;
- `first` — the first device in the app's order.

Pass it through `run_cmd`: `... wireless-battery-yasb.ps1 -Mode all`.

Colours are inline HTML in the label: blue for charging (plus a bolt), gray for a sleeping device,
red at or below `$LowPercent`.

## Device icons

| `kind` | Glyph |
|---|---|
| `headset` | `U+F02CE` |
| `mouse` | `U+F037D` |
| `keyboard` | `U+F030C` |
| `gamepad` | `U+F0297` |
| `phone` | `U+F011C` |
| anything else | `U+F0FB0` |

They are set in `$KindIcons` at the top of the script; replace them with any other Nerd Font
glyphs to taste. Fluent Icons codepoints (`U+E7F6`, `U+E962`, ...) do not work here, because YASB
renders the bar with the stylesheet font.

## Troubleshooting

Run the script the same way YASB does:

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File %USERPROFILE%\.config\yasb\wireless-battery-yasb.ps1
```

It prints one JSON line. When no device is visible it prints an object with an empty `label`, so
`hide_empty: true` hides the widget. Warnings go to `%TEMP%\wireless-battery-yasb.log`; YASB's own
errors are in `%USERPROFILE%\.config\yasb\yasb.log`.

- **Empty label:** is the app running (snapshot fresh) or the exe reachable? Check
  `WirelessBattery.exe --json`.
- **Literal `{data[label]}` on the bar:** the script printed `null` (older versions did that when
  every device was asleep) or failed. Update `wireless-battery-yasb.ps1` and check the log.
- **Values stop updating:** the snapshot is more than three minutes old, so the script switched to
  `--json`; if that fails too, nothing is printed. Restart the app.
- **Squares instead of icons:** the stylesheet font is not a Nerd Font.

## Limitations

- The script starts PowerShell on every refresh (about a second). The snapshot avoids a second HID
  poll, not the PowerShell startup cost.
- The app only understands its own display modes; the widget is independent of the taskbar widget
  and tray icons, and works in both.
- `--json` and the snapshot are read-only: changing the order or hiding devices still happens in
  the app.
