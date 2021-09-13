#define F_CPU 16000000UL

#include <avr/io.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include "hd44780_settings.h"
#include "hd44780.h"
#include "util/delay.h"
#include "kiss_fft.h"
#include "_kiss_fft_guts.h"

#define SQ(a) a*a

/*====================================
	LCD functions
====================================*/

//Custom character bitmaps
uint8_t cc[8][8] = {
	{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111 },
	{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111 },
	{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111111 },
	{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111 },
	{ 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111 },
	{ 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111 },
	{ 0b00000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111 },
	{ 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111 }
};

//Writes custom characters to LCD memory
void build_custom_chars() {
	int i, j;
	for (i = 0; i < 8; i++) {
		lcd_command(_BV(LCD_CGRAM)+i*8);
		for (j = 0; j < 8; j++) {
			lcd_putc(cc[i][j]);
		}
	}
	lcd_goto(0);
}

//Unused, tests custom characters by printing them on LCD
void initial_print() {
	int i;
	for (i = 0; i < 16; i++) {
		lcd_putc(' ');
	}
	lcd_goto(0x40);
	for (i = 0; i < 16; i++) {
		lcd_putc(0);
	}
}

//pos is 0-15, the x coord of the char on the LCD. val 0-15 is the value to be printed at that coordinate,
//since there are 2x8=16 vertical pixels on the LCD
void print_at_pos(uint8_t pos, int val) {
	if (val < 16) {
		lcd_goto(pos);
		if (val < 8) {
			lcd_putc(' ');
		} else {
			lcd_putc(val);
		}
		lcd_goto(pos + 0x40); //0x40 is the offset for writing chars to the 2nd line of the LCD
		if (val < 8) {
			lcd_putc(val);
		} else {
			lcd_putc(7);
		}
	}
}

//converts an integer to a string before printing it to the LCD
void lcd_putd(int i) {
	char buffer[8];
	itoa(i, buffer, 10);
	//lcd_goto(0x00);
	lcd_puts(buffer);
}

/*====================================
	ADC functions
====================================*/

static inline void initADC0(void) {
	ADMUX |= (1 << REFS0); //reference voltage on AVCC
	ADCSRA |= (1 << ADPS1) | (1 << ADPS0); //ADC clock prescaler /8
	ADCSRA |= (1 << ADEN); //enables the ADC
}

/*====================================
	Timer0 functions
====================================*/

void delay_us(unsigned long us) {
	unsigned int i = 1;
	for (i = 0; i < us; i++) {
		TCNT0 = 242; //255-16 = 239 theoretical, but 242 gives closest value with the oscilloscope
		TCCR0B = 0x01;
		while ((TIFR0 & (1 << TOV0)) == 0);
		TCCR0B = 0x00;
		TIFR0 = (1 << TOV0);
	}
}

void delay_ms(unsigned long ms) {
	int i;
	for (i = 0; i < ms; i++) {
		delay_us(1000);
	}
}

/*====================================
	Interrupt functions
====================================*/

int scale = 1;
int divs = 4;

//When button is pressed, change "volume" on LCD and display new volume
ISR(INT0_vect) {
	//first some software debouncing (delay then check if still pressed)
	delay_ms(300);
	if(!(PIND & (1 << 2))) {
		scale = (scale + 1) % divs;
		//lcd_goto(0x04);
		//lcd_putc(16+8);
		//lcd_putc(scale+ 'a');
		lcd_goto(0x05);
		lcd_puts("Volume: ");
		int vol = scale * (100/divs);
		lcd_putd(vol);
		lcd_putc('%');
		delay_ms(300);
	}
}

int main() {
	//init
	lcd_init();
	delay_ms(100);
	lcd_command(_BV(LCD_DISPLAYMODE) | _BV(LCD_DISPLAYMODE_ON)); //turn on LCD and backlight
	initADC0();
	
	//enable external interrupt for pin 2
	EICRA = 0x0A; // 0000 1010
	EIMSK |= (1 << INT0);
	sei();

	//setup pin 13 as output
	DDRB |= (1 << PINB5);
	DDRD |= (1 << PIND2);
	
	//N refers to the number of input and Fourier samples (N-point FFT)
	//must be power of 2 to allow for use of twiddle factors in FFT computation
	int N = 64;

	kiss_fft_cfg cfg = kiss_fft_alloc(N, 0, 0, 0); //config FFT
	kiss_fft_cpx fin[N]; //input buffer for audio samples
	kiss_fft_cpx fout[N]; //output buffer for Fourier samples
	
	build_custom_chars();
	
	int i, j, r_bin, i_bin, mag;
	int nb_bins = 16;
	int per_bin = (N/2) / nb_bins;
	int findex;
	
	while (1) {
		PORTB ^= (1 << PINB5);
		int k;
		for (k = 0; k < N; k++) {
			fin[k].r = (float)ADC / 1024; //read ADC value N times to sample audio signal into in buffer
			fin[k].i = 0.0f; //no imaginary part (i=0) for a mono audio signal
			ADCSRA |= (1 << ADSC); //start ADC conversion
		}
	
		kiss_fft(cfg, fin, fout); //compute FFT

		//Compute magnitude of frequencies, trim and scale, split 64 magnitudes into 16 bins to fill LCD
		for (i = 0; i < nb_bins; i++) {
			r_bin = 0;
			i_bin = 0;
			for (j = 0; j < per_bin; j++) {
				findex = N/2 + i * per_bin + j;
				r_bin += fout[findex].r;
				i_bin += fout[findex].i;
			}
			mag = (int)(sqrt(SQ(r_bin) + SQ(i_bin)));
			if (mag > 15) mag = 15;
			print_at_pos(nb_bins-1 - i, mag*(scale));
		}
	}
}