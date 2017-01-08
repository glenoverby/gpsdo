/*
 * main.c - Proportional Control
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


#define X12MHZ 0
#define X16MHZ 1

#define REPORT_C

long count = 0;						// counter for 10mhz clock.  Managed by counter overflow interrupt handler
long countadd = 0x10000;			// value to add to count on overflow.
									// the only time this value is not 65536 is when the 1pps signal arrives.
									// it is then set to 65536 - captured count.
long capture = 0;					// captured count
char pps = 0;						// flag from 1pps handler.  Used if there is no 10mhz clock.
char blink = 2;						// blink LED in 1pps handler.
									// 1 = toggle green LED  2 = toggle red LED

// simple blocking character transmit.
void tx(char c)
{
	  while (!(IFG2&UCA0TXIFG));                // USCI_A0 TX buffer ready?
	  UCA0TXBUF = c;                            // TX -> character
}

// printf(%x) of 1 digit.
printfx4(int v)
{
	v &= 0xf;
	v += '0';
	if (v > '9')
		v += 7;
	tx((char)v);
}

// printf(%x) of 16-bit word.
void printfx16(int v)
{
	printfx4(v >> 12);
	printfx4(v >> 8 & 0xf);
	printfx4(v >> 4 & 0xf);
	printfx4(v & 0xf);
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

// newline (CR & LF)
void nl()
{
	tx('\r');
	tx('\n');
}

int main(void)
{
	unsigned int	pwm_duty_cycle = 32768;		// PWM duty cycle ~ voltage
	long sum=0;									// sum of captured counts during (counter) pulses
	int counter=-10;							// count of 1pps pulses before acting.

	long error;									// calculated error from 10mhz
	int adjust;									// adjustment of PWM duty cycle

	WDTCTL = WDTPW | WDTHOLD;					// Stop watchdog timer

    // Set processor clock speed
    // From TI's example program: msp430g2xx3_dco_calib
    //12Mhz
     if (CALBC1_12MHZ==0xFF)					// If calibration constant erased
     {
       return(1);                              	// do not load, trap CPU!!
     }
     DCOCTL = 0;                               	// Select lowest DCOx and MODx settings
#if X12MHZ
     BCSCTL1 = CALBC1_12MHZ;                   	// Set range
     DCOCTL = CALDCO_12MHZ;                    	// Set DCO step + modulation*/
#endif
#if X16MHZ
     BCSCTL1 = CALBC1_16MHZ;                   	// Set range
     DCOCTL = CALDCO_16MHZ;                    // Set DCO step + modulation*/
#endif

     // Configure Timer TA0 to count clock pulses on P1.0 with a capture input on P1.1.
     // From TI's example program: msp430g2xx3_ta_03.c (with modifications)
     TA0CTL = MC_2 + TAIE;           			// TACLK, continuous mode, interrupt
     P1SEL |= 0x01;								// Function: TA0.TACLK

     // Enable Capture/Compare register 0
     TA0CCTL0 =  CM1 | SCS | CAP | CCIE;		// Capture on CCIxA on falling edge, synchronous
     TA0CCTL1 = 0;
     TA0CCTL2 = 0;

     P1DIR &= ~0x02;							// input
     P1SEL |= 0x02;								// Function: CCI0A

     //P1DIR &= ~0x08;							// P1.3 (Button) as an input
     //P1REN |= 0x08;							// P1.3 pull-up resistor enable

     // PWM using timer 1 on P2.2-10
     // This goes through a low-pass filter and connects to the oscillator's voltage
     // control input.
     // From TI's example program: msp430g2xx3_ta_16.c (with modifications)
     P2DIR |= 0x04;                          	// P2.2 output
     P2SEL |= 0x04;                           	// P2.2 TA1 options
     TA1CCR0 = 65535;                          	// PWM Period

     TA1CCTL1 = OUTMOD_7;                  		// CCR1 reset/set
     TA1CCR1 = pwm_duty_cycle;
     TA1CTL = TASSEL_2 + MC_1;                 	// SMCLK, up mode - counts to TA1CCR0
     // These two configuration lines will use a signal on XIN for the PWM clock
     //TA1CTL = TASSEL_1 | MC_2 ;                 // ACLK, continuous up
     //TA1CTL = TASSEL_1 | MC_1 ;                 // ACLK, continuous up
     //BCSCTL3 = LFXT1S0 | LFXT1S1;				// sets LFXT1 to external clock on XIN

     //P1DIR |= 0x10;								// P1.4 output
     //P1SEL |= 0x10;								// P1.4 is SMCLK

     P1DIR |= 0xc0;								// P1.6 is LED2, P1.7 is LED1 (new)
     P1OUT &= ~0xc0;

     // Serial port from TI's example program: msp430g2xx3_uscia0_uart_01_9600 (modified)
     P1SEL |= BIT2 ;                           // P1.2=TXD
     P1SEL2 |= BIT2 ;                          // P1.2=TXD
     UCA0CTL1 |= UCSSEL_2;                     // Uart clock from SMCLK
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
     printfx16(CALBC1_12MHZ);
     tx(' ');
     printfx16(CALDCO_12MHZ);
     nl();

     //TA1CCR1 = 0x1d17;							// ~1.5v @ 16384
     //TA1CCR1 = 0x745d;							// ~1.5v @ 65536

     //_BIS_SR(LPM0_bits + GIE);                 // Enter LPM0 w/ interrupt
     _BIS_SR(GIE);                 				// Enable interrupt

     TA1CCR1 = pwm_duty_cycle;
     while(1) {
    	 if (capture != 0) {
    		 if (counter >= 0) {
    			 sum += capture;
    		 }
#ifdef REPORT_C
    		 tx('c');
    		 printfx16((capture >> 16) & 0xffff);
    		 printfx16(capture & 0xffff);
    		 tx(' ');
    		 printfx16((sum >> 16) & 0xffff);
    		 printfx16(sum & 0xffff);
    		 //nl();
    		 tx('\r');
#endif

    		 counter++;
    		 if (counter == 10) {
    			 tx('*');
        		 printfx16((sum >> 16) & 0xffff);
        		 printfx16(sum & 0xffff);
        		 tx(' ');

        		 error = 100000000 - sum;
        		 printfd(error);

        		 if (abs(error) >= 10) {
        			 blink = 2;
        			 P1OUT &= ~0x40;
        		 } else if (abs(error) < 2) {
        			 // should make the requirement for this state be that error < 2 for at least 5 samples.
        			 P1OUT |= 0x40;			// turn on Green LED
        			 P1OUT &= ~0x80;		// turn off Red LED
        			 blink = 0;
        		 } else {
        			 P1OUT &= ~0x80;
        			 blink = 1;
        		 }

        		 //adjust = error * 7;	// approximately 2.2 PWM steps per hertz for FOX801 10mhz oscillator
        		 if (abs(error) > 2) {			// Error > 5 for FOX801
        			 //adjust = error * 25;		// Error adjustment for FOX801 10mhz oscillator.
        			 adjust = error * 75;		// Error adjustment for Isotemp 134-10 10mhz oscillator.
        			 if (adjust > 10000) {
        				 adjust = 10000;
        			 } else if (adjust < -10000) {
        				 adjust = -10000;
        			 }
        		 } else if (abs(error) > 1) {
        			 //adjust = error * 3;		// Error adjustment for FOX801 10mhz oscillator.
        			 adjust = error * 10;		// Error adjustment for Isotemp 134-10 10mhz oscillator
        		 } else {
        			 adjust = error;
        		 }
        		 if (adjust > 0 && pwm_duty_cycle + adjust < pwm_duty_cycle) {
        			 // Overflow
        			 pwm_duty_cycle = 0xffff;
        		 } else if (adjust < 0 && pwm_duty_cycle + adjust > pwm_duty_cycle) {
        			 pwm_duty_cycle = 1;
        		 } else {
        			 pwm_duty_cycle += adjust;
        		 }
        		 tx(' ');
        		 printfx16(pwm_duty_cycle);
        		 tx(' ');
        		 printfd(adjust);
        		 tx(' ');tx(' ');tx(' ');tx(' ');
        		 nl();
    			 TA1CCR1 = pwm_duty_cycle;

    			 counter = -5;
    			 sum = 0;
    		 }
    		 capture = 0;
    		 pps = 0;
    	 } else if (capture > 15000000) {
    		 // 15 seconds of clocks from oscillator without a clock from GPS.
    		 // toggle the red LED to indicate functioning
    		 P1OUT ^= 0x80;					// toggle red LED
    		 capture = 0;
    	 } else if (pps > 4) {
    		 // 5 seconds of clocks from GPS but no 10mhz clock pulses
    		 // toggle the red LED to indicate the device is functioning
    		 P1OUT ^= 0x80;					// toggle red LED
    		 pps = 0;
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
 	case 10:							// counter overflow
 		 count += countadd;				// add remaining (or full) count
 		 countadd = 0x10000;			// set count to a full count.
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
	if (blink == 1) {
		P1OUT ^= 0x40;					// toggle green LED
	} else if (blink == 2) {
		P1OUT ^= 0x80;					// toggle red LED
	}
	c = TA0CCR0;						// get capture value
	if (TA0CCTL0 & COV) {				// If there has been an overflow, reset it
		TA0CCTL0 &= ~COV;
	}

	capture = count + c;
	countadd = 0x10000 - c;				// count value for next counter overflow is the remainder of this cycle's count
	count = 0;
	pps++;
}
