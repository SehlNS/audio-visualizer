# ESP32 audio oscilloscope firmware

This PlatformIO project is the ESP32 port. The Raylib application in
`../main.cpp` remains a desktop simulator; its source is not flashed to the
board.

## Hardware

- Freenove classic ESP32 development board (`esp32dev` target)
- Cogae 3.5-inch 480x320 SPI TFT, ILI9488 controller
- INMP441 I2S microphone
- Three B10K potentiometers, one gang per control
- USB power bank, USB current/voltage meter, USB-A extension, and USB-A-to-C
  cable
- Panel latching switch in series with the USB 5V conductor; it is a hardware
  switch, not an ESP32 GPIO input

## Pin map

GPIO assignments are defaults for this firmware. Check the labels on the
actual modules before wiring. ESP32 GPIO is 3.3V logic and is not 5V tolerant.

| Module signal | ESP32 connection |
| --- | --- |
| TFT SCK / CLK | GPIO 18 |
| TFT MOSI / SDI | GPIO 23 |
| TFT CS | GPIO 13 |
| TFT D/C / RS | GPIO 21 |
| TFT RESET | GPIO 22 |
| TFT SDO / MISO | Not connected; unused by this write-only display code |
| TFT VCC / LED | Follow the display board's documented supply/backlight wiring; do not connect LED directly to a GPIO unless the board manual explicitly allows it |
| INMP441 BCLK / SCK | GPIO 26 |
| INMP441 WS / LRCLK | GPIO 25 |
| INMP441 SD / DOUT | GPIO 34 |
| INMP441 L/R | GND for left channel |
| Warp potentiometer wiper | GPIO 32 (ADC1) |
| Wave type potentiometer wiper | GPIO 33 (ADC1) |
| Theme potentiometer wiper | GPIO 35 (ADC1, input only) |
| Potentiometer outer terminals | 3.3V and GND |

Share GND between the ESP32, TFT, microphone, and potentiometers. Use one
three-pin gang on each dual-gang pot. The firmware reads pots as 12-bit ADC
values. Potentiometer outer terminals may be swapped if you want to reverse
the knob direction.

Power path: power-bank USB-A output -> USB extension with the panel switch
interrupting 5V/VBUS only -> USB meter -> USB-A-to-C cable -> ESP32 USB-C
input. This places the meter near the load. Keep USB GND and data conductors
continuous. Verify the switch and cable ratings and check the voltage at the
ESP32 under load. Disconnect the power bank before connecting the ESP32 to a
computer for flashing.

The INMP441 needs an acoustic opening. Do not seal its microphone port inside
the junction box. Check display orientation and pin order on the actual board
silkscreen/manual; the Cogae listing may not match every generic ILI9488
breakout diagram.

## Controls

- Warp potentiometer: trace gain and woozy horizontal/vertical distortion
- Wave potentiometer: sine, square, or saw
- Theme potentiometer: cycles through all 15 themes
- `[` / `]` are simulator-only threshold keys; firmware currently uses a
  fixed 0.05 RMS threshold, with the level shown on screen

Themes included: Walnut, Moss, Copper, Sepia, Olive, Clay, Neon, Sunset,
Arcade, Neon Bars, Ember Bars, Pixel Spectrum, Pixel Cyber, Pixel Ice, and
Pixel Fire. Volume continuously blends each theme's phosphor toward its own
threshold color. Neon Bars and Ember Bars show 24 smoothed time-domain
loudness bars. The four Pixel themes show the FFT frequency spectrum in a
two-sided pixel style.

## Build and upload

Install PlatformIO Core and connect the ESP32 to the computer over USB-C. From
this directory run:

```sh
pio run
pio run --target upload
pio device monitor
```

The project uses Arduino_GFX for the ILI9488 and the classic ESP32 I2S driver
for the INMP441. If microphone initialization fails, a synthesized signal
keeps the display active and the serial monitor reports the failure.