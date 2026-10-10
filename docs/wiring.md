# Wiring: ESP32-C3 to Flipper Zero

Both devices are 3.3 V native, so they connect directly with no level shifter. Four
pins are used, chosen so the C3 lines up with the Flipper header and plugs straight on.

```
  Flipper Zero           ESP32-C3 SuperMini
  pin 13  TX    -------->  GPIO1   (UART_RX_PIN)
  pin 14  RX    <-------   GPIO0   (UART_TX_PIN)
  pin  9  3V3   -------->  3V3     (power)
  pin 11  GND   -------->  GPIO3   (jumpered to the C3 GND)
  (or pin 18 = GND)
```

| Flipper Zero | ESP32-C3 pin | Direction / role |
|--------------|--------------|------------------|
| pin 13 (`TX`) | `GPIO1` = `UART_RX_PIN` | Flipper transmits to ESP32 |
| pin 14 (`RX`) | `GPIO0` = `UART_TX_PIN` | ESP32 transmits to Flipper |
| pin 9 (`3V3`) | `3V3` | standard power pin, the Flipper powers the C3 |
| pin 11 or 18 (`GND`) | `GPIO3`, jumpered to the C3 `GND` | ground |

## The ground pin

On this layout the Flipper GND lines up with the C3 `GPIO3` position when the board is
plugged onto the header. To use that position as ground, run a short jumper on the C3
from `GPIO3` to the C3 `GND` pad. The Flipper GND then reaches the C3 ground through
`GPIO3`. This is safe because `GPIO3` is not a strapping pin on the ESP32-C3 (the C3
strapping pins are `GPIO2`, `GPIO8` and `GPIO9`) and it boots as a hi-Z input; the sketch
never drives it, so tying it to ground does nothing but ground it. It is a mechanical
convenience; electrically it is a normal ground connection.

Use insulated wire for this jumper. On the SuperMini top row `0 1 2 3 4 3.3 G 5V`, the
`G`-to-`3` wire passes over the `3.3` (power) and `GPIO4` pads. A bare wire, or a solder
bridge onto `3.3`, would short the Flipper's 3V3 rail to ground, which can trip or damage
the Flipper's 3V3 regulator. Keep both joints clean.

If you are not plugging directly onto the header, skip the jumper and wire the Flipper
GND to the C3 `GND` pad in the usual way.

## Photos (ESP32-C3 SuperMini)

The board used here is an ESP32-C3 SuperMini. Its top row is labelled
`0  1  2  3  4  3.3  G  5V`. The jumper bridges `G` (GND) to `3` (GPIO3):

![ESP32-C3 SuperMini with a jumper from GND to GPIO3](../images/esp32c3-supermini-gnd-to-gpio3.jpg)

![ESP32-C3 SuperMini pinout](../images/esp32c3-supermini-pinout.jpg)

GPIO0 and GPIO1 (the UART pins) are at the far end of the same top row, and `3.3` is the
power pin next to the jumper, so all four contacts the relay needs sit on one edge.

## TX and RX must cross

Each side's transmit goes to the other side's receive. If the Flipper stays on "Waiting
reader", the usual cause is TX to TX and RX to RX by mistake.

## Flipper GPIO reference

Flipper Zero top GPIO header, USB-C at the bottom:

- pin 13: `PA7`, USART `TX`
- pin 14: `PB2`, USART `RX`
- pin 9: `3V3` out (powers a small ESP32-C3)
- pin 11 and pin 18: `GND`

## Powering

With the four-pin layout the C3 is powered from the Flipper's `3V3` (pin 9), so no
separate USB is needed in the field. During bring-up you can instead power the C3 from
its own USB, which also gives you the debug serial at 115200; in that case share only
GND, TX and RX with the Flipper and leave `3V3` disconnected.

The C3's Wi-Fi SoftAP and ESP-NOW draw transient current peaks (roughly 300-350 mA). The
Flipper's 3V3 rail is modest, so if the C3 resets or the link is flaky when Flipper-powered,
add a bulk capacitor (100-470 uF) across the C3 `3V3`/`GND`, or power the C3 from its own
USB. The external-antenna variant helps the RF link but not the current draw.

## Choosing a different ESP32

Change only the two defines at the top of `esp32-bridge/EvilRelayESP32.ino`:

| Board | `UART_RX_PIN` / `UART_TX_PIN` |
|-------|-------------------------------|
| ESP32-C3 (default) | `1` / `0` |
| Classic ESP32 | `16` / `17` |
| ESP32-S3 | any two free GPIOs |

Avoid strapping, USB, flash and UART0 console pins for your chip. Keep `UART_BAUD` equal
to the FAP `RELAY_UART_BAUD`, both `115200`.
