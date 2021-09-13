A basic audio spectrum analyzer built around an Arduino, an LCD display and a crude OpAmp audio amplifier.

Compile using avr-gcc and upload to an atmega328p.
Must include:

#include <avr/io.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include "hd44780_settings.h"
#include "hd44780.h"
#include "util/delay.h"
#include "kiss_fft.h"
#include "_kiss_fft_guts.h"

The libraries not in the avr-gcc toolchain can be found in src/include/

To run, plug in an audio source (3.5mm jack, etc.). Power is provided from the Arduino's 5V.
The full circuit is specified in a Fritzing diagram in the report.