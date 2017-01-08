/*
 * freq-measure: measure the clock frequency at various PWM settings.
 *
 * This can be used to find the maximum and minimum tuning values, and to check
 * PID factors.
 *
 * Copyright 2014-2017 Glen Overby
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * P1.0	TACLK (input)
 * P1.1 1PPS input
 * P1.2 Serial out
 * P1.3 Button (input)
 * P1.4 SMCLK (output)
 * P1.5
 * P1.6 LED 2
 * P1.7 LED 1 (moved)
 *
 * P2.2 PWM Output from timer 1
 */
#include <msp430.h> 
#include <stdio.h>

#define X12MHZ 1
#define X16MHZ 0

#define REPORT_C
#define AVERAGE_SIZE	8			// samples in average.  power of 2 for efficiency.
#define FRACTIONBITS	0

long count = 0;
long countadd = 0x10000;
long capture = 0;
int capflags = 0;
int cappins = 0;
int counth = 0, countl = 0;
int captureh = 0, capturel = 0, capturec = 0;

void tx(char c)
{
	  while (!(IFG2&UCA0TXIFG));                // USCI_A0 TX buffer ready?
	  UCA0TXBUF = c;                            // TX -> character
}

printfx4(int v)
{
	v &= 0xf;
	v += '0';
	if (v > '9')
		v += 7;
	tx((char)v);
}
void printfx16(int v)
{
	printfx4(v >> 12);
	printfx4(v >> 8 & 0xf);
	printfx4(v >> 4 & 0xf);
	printfx4(v & 0xf);
}

// printf(%x) of 32-bit word.
void printfx32(long v)
{
	printfx16(v>>16);
	printfx16(v&0xffff);
}

void printfld(long v)
{
	char output[16];
	char *c;
	sprintf(output, "%ld", v);
	for(c=output; *c; c++) {
		tx(*c);
	}
}

// printf(%d)
void printfd(int v)
{
	char output[16];
	char *c;
	sprintf(output, "%d", v);
	for(c=output; *c; c++) {
		tx(*c);
	}
}

void nl()
{
	tx('\r');
	tx('\n');
}

int main(void)
{
	unsigned int	pwm_duty_cycle = 65535;					// PWM duty cycle ~ voltage
	long sum=0, sum10s=0, sum30s=0;
	int counter=-1;
	unsigned int ti;		// table index
	static const unsigned int freqtable[] = {
#ifdef SMALLSTEPS
			32768,		32000,		31900,
			31800,		31700,		31600,		31500,
			31400,		31300,		31200,		31100,
			31090,		31080,		31070,		31060,
			31050,		31040,		31030,		31020,
			31010,		31005,		31000,
#endif
//			65534,		60000,		50000,		40000,
//			30000,		20000,		10000,		1,
#if 0
			1,			5041,		10082,		15123,
			20164,		25205,		30246,		35287,
			40328,		45369,		50410,		55451,
			60492,		65533,
#endif
//			0x7700,
//			31768,		32768,		33768,
//			16384,		32768,		49152,
			1,		16384,		32768,		49152,	65534,
			0
	};
	WDTCTL = WDTPW | WDTHOLD;	// Stop watchdog timer

    // Set processor clock speed
    // From TI's example program: msp430g2xx3_dco_calib
    //12Mhz
     if (CALBC1_12MHZ==0xFF)					// If calibration constant erased
     {
       return(1);                              // do not load, trap CPU!!
     }
     DCOCTL = 0;                               // Select lowest DCOx and MODx settings
#if X12MHZ
     BCSCTL1 = CALBC1_12MHZ;                   // Set range
     DCOCTL = CALDCO_12MHZ;                    // Set DCO step + modulation*/
#endif
#if X16MHZ
     BCSCTL1 = CALBC1_16MHZ;                   // Set range
     DCOCTL = CALDCO_16MHZ;                    // Set DCO step + modulation*/
#endif

     // Configure Timer TA0 to count clock pulses on P1.0 with a capture input on P1.1.
     // From TI's example program: msp430g2xx3_ta_03.c (with modifications)
     //TA0CTL = TASSEL_2 + MC_2 + TAIE;           // SMCLK, continuous mode, interrupt
     // Count on P1.0
     //TA0CTL = TASSEL_0 + MC_2 + TAIE;           // ?CLK, continuous mode, interrupt
     TA0CTL = MC_2 + TAIE;           			// TACLK, continuous mode, interrupt
     P1SEL |= 0x01;								// Function: TA0.TACLK

     // Enable Capture/Compare register 0
     //TA0CCTL0 = CM1 + CCIS0 + SCS + CAP;		// Capture on CCIxA
     //TA0CCTL0 = CM1 + SCS + CAP + CCIE;			// Capture on CCIxA on falling edge, synchronous
     TA0CCTL0 =  CM1 | SCS | CAP | CCIE;		// Capture on CCIxA on falling edge, synchronous
     TA0CCTL1 = 0;
     TA0CCTL2 = 0;

     P1DIR &= ~0x02;							// input
     P1SEL |= 0x02;								// Function: CCI0A

     P1DIR &= ~0x08;							// P1.3 (Button) as an input
     P1REN |= 0x08;								// P1.3 pull-up resistor enable

     // Enable Capture/Compare register 1
//     TA0CCTL1 = CM0 + SCS + CAP + CCIE;		// Capture on CCIxA
//    P1DIR &= ~0x04;							// input
//    P1SEL |= 0x04;								// Function: CCI1A

     // Enable Capture/Compare register 2.
     // table 12 indicates that TA0.2 connects to PinOsc
//     TA0CCTL2 = CM1 + CCIS0 + SCS + CAP + CCIE;		// Capture on CCIxA
//     P2SEL |= 0x40;

     // PWM using timer 1 on P2.2-10
     // This goes through a low-pass filter and connects to the oscillator's voltage
     // control input.
     // From TI's example program: msp430g2xx3_ta_16.c (with modifications)
     P2DIR |= 0x04;                            // P2.2 output
     P2SEL |= 0x04;                            // P2.2 TA1 options
     TA1CCR0 = 65535;                          // PWM Period
     TA1CCTL1 = OUTMOD_7;                  		// CCR1 reset/set
     //TA1CCR1 = 8192;                           // CCR1 PWM duty cycle
     TA1CCR1 = pwm_duty_cycle;
     TA1CTL = TASSEL_2 + MC_1;                 // SMCLK, up mode - counts to TA1CCR0
     //TA1CTL = TASSEL_2 + MC_2;                 // SMCLK, continuous up

     // These two configuration lines will use a signal on XIN for the PWM clock
     //TA1CTL = TASSEL_1 | MC_2 ;                 // ACLK, continuous up
     //BCSCTL3 = LFXT1S0 | LFXT1S1;				// sets LFXT1 to external clock on XIN

     P1DIR |= 0x10;								// P1.4 output
     P1SEL |= 0x10;								// P1.4 is SMCLK

     P1DIR |= 0xc0;								// P1.6 is LED2, P1.7 is LED1 (new)
     P1OUT &= ~0xc0;

     // Serial port from TI's example program: msp430g2xx3_uscia0_uart_01_9600 (modified)
     //P1SEL = BIT1 + BIT2 ;                     // P1.1 = RXD, P1.2=TXD
     //P1SEL2 = BIT1 + BIT2 ;                    // P1.1 = RXD, P1.2=TXD
     P1SEL |= BIT2 ;                           // P1.2=TXD
     P1SEL2 |= BIT2 ;                          // P1.2=TXD
     UCA0CTL1 |= UCSSEL_2;                     // Uart clock from SMCLK
     // 104 0 for 1mhz
#if X12MHZ
     UCA0BR0 = 0xe0;                           // 12MHz 9600
     UCA0BR1 = 4;                              // 12MHz 9600
#endif
#if X16MHZ
     UCA0BR0 = 0x80;                           // 16MHz 9600
     UCA0BR1 = 6;                              // 16MHz 9600
#endif
     UCA0MCTL = UCBRS0;                        // Modulation UCBRSx = 1
     UCA0CTL1 &= ~UCSWRST;                     // **Initialize USCI state machine**
     //IE2 |= UCA0RXIE;                          // Enable USCI_A0 RX interrupt

     UCA0TXBUF = '!';
     //printfx16(CALBC1_12MHZ);
     //tx(' ');
     //printfx16(CALDCO_12MHZ);
     nl();

     //_BIS_SR(LPM0_bits + GIE);                 // Enter LPM0 w/ interrupt
     _BIS_SR(GIE);                 				// Enable interrupt

     ti = 0;
     pwm_duty_cycle = freqtable[ti];
     TA1CCR1 = pwm_duty_cycle;
     counter = 2;
     while(counter) {						// first second is fractional
    	 if (capture) {
    		 counter--;
    		 capture = 0;
    	 }
     }
     capture = 0;
     counter = -1;
     while(1) {
    	 if (capture != 0) {
    		 if (counter >= 0) {
    			 sum += capture;
    			 sum10s += capture;
    			 sum30s += capture;
    		 }

    		 tx('1'); tx(' ');
    		 printfx32(capture);
    		 tx(' ');
    		 printfd(10000000-capture);
    		 tx(' ');
    		 //printfx32(sum10s);
    		 //tx(' ');
    		 //printfx32(sum30s);
    		 //tx(' ');
		     printfx16(pwm_duty_cycle);
    		 nl();

    		 counter++;
    		 if (counter == 10 || counter == 20 || counter == 30 || counter == 40 || counter == 50 || counter == 60) {
    			 tx('1');  tx('0'); tx(' ');
    			 printfx32(sum10s);
    			 tx(' ');
    			 printfd(100000000-sum10s);
        		 tx(' ');
        		 printfx16(pwm_duty_cycle);
        		 nl();
        		 sum10s = 0;
    		 }
    		 if (counter == 30  || counter == 60) {
    			 tx('3'); tx('0'); tx(' ');
    			 printfx32(sum30s);
    			 tx(' ');
    			 printfd(300000000-sum30s);
    			 tx(' ');
    		     printfx16(pwm_duty_cycle);
    			 nl();
    			 sum30s = 0;
    		 }
    		 if (counter >= 60) {
    			 tx('6'); tx('0'); tx(' ');
    			 printfx32(sum);
    			 tx(' ');
    			 printfd(600000000-sum);
    			 tx(' ');
    		     printfx16(pwm_duty_cycle);
    			 nl();
    			 sum = 0;

    			 ti++;
    			 if (freqtable[ti] == 0) {
    				 ti = 0;
    			 }
    			 pwm_duty_cycle = freqtable[ti];
    			 TA1CCR1 = pwm_duty_cycle;
    			 counter = -1;
    		 }
    		 capture = 0;
    		 capflags = 0;
    	 }
     }
}

// From TI's example program: msp430g2xx3_ta_03.c (with modifications)
// Timer_A3 Interrupt Vector (TA0IV) handler
#pragma vector=TIMER0_A1_VECTOR
__interrupt void Timer_A(void)
{
	switch( TA0IV ) {
	case  2: 		                     // CCR1 capture
 		 break;
 	case  4:
 		 break;                          // CCR2
 	case 10:
 		 //P1OUT ^= 0x80;                  // overflow
 		 count += countadd;
 		 countadd = 0x10000;
 		 counth++;
	   	 break;
	}
}

//
// Interrupt vector for capture/compare register 0
// 1PPS is to be connected here.
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A0(void)
{
	unsigned int c;
	capturec = TA0R;
	//TA0R = 0;
	//P1OUT ^= 0x40;
	//tx('s');
	c = TA0CCR0;
	capflags = TA0CCTL0;
	cappins = P1IN;
	if (TA0CCTL0 & COV) {
		TA0CCTL0 &= ~COV;
	}

	//if (TA0CCTL0 & CCIFG) {
	{
		captureh = counth;
		capturel = c;
		capture = count + c;
		countadd = 0x10000 - c;
		count = 0;
		counth = countl = 0;
	}
}
