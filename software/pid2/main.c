/*
 * Proportional + Integral Control for a GPS Disciplined Oscillator
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
 */

#include <msp430.h>
#include <stdio.h>
#include <string.h>

/*
 * Hardware Map
 * Timer 0
 *
 * P1.0 TACLK (input)
 * P1.1 1PPS input
 * P1.2 Serial out
 * P1.3 Button (input)
 * P1.4 SMCLK (output)
 * P1.5 LED 3: Red (new)
 * P1.6 LED 2: Green
 * P1.7 LED 1: Yellow (moved)
 *
 * P2.0 input - OSC Good - high when oscillator is cold
 * P2.1 input - GPS Lock
 * P2.2 PWM Output from timer 1
 * P2.5 output LED4: Blue - OSC Good
 *
 * GPSlock: PC0 low when data is good
 *          PC1 high when data is good
 *
 * LED Statuses:
 *  Red     Power
 *  Yellow  Lock Status
 *  Green   Lock Status
 *  Blue    OSC Cold
 *
 *  Blue is on when OSC is cold
 *  Blue blinks if there is no 10mhz clock from the oscillator
 *
 *  Yellow blinks slow if no GPS Lock or No GPS PPS
 *  Yellow blinks when fast algorithm is seeking
 *  Yellow on when fast algorithm has abs(error) < 1
 *
 *  Green blinks for 1 minute when an adjustment has been made
 *  Green on when no adjustment has been made for 1 minute
 */

//#define DEBUG_SECOND
#define DEBUG_SEC_SHORT
//#define DEBUG
#define DEBUG_PID

/* Controller constants */

/*
 * P_FACTOR_FAST was calculated by using a binary search to find frequencies.
 * Slow is 1/20th of that.
 */

/* Isotemp */
#if 1
#define P_FACTOR_FAST   2500 //5000
#define	P_ERRORBAND_FAST 1
#define P_MAX_ERROR		393		// maximum error value (per second) before counter overflow  (32768/P_FACTOR_FAST*SEC)
#define P_FACTOR_SLOW	50
#define P_ERRORBAND_SLOW 1
#define I_FACTOR_SLOW   25
	// tried: 50 - may have set up an oscillation
#define I_ERRORBAND_SLOW 1
#define HAVE_GPSLOCK 0
#define HAVE_OSCCOLD 1
#endif
#if 0
/* Fox 801 */
#define P_FACTOR_FAST   284		// calculated 284
#define	P_ERRORBAND_FAST 1
#define P_MAX_ERROR		920		// maximum error value (per second) before counter overflow  (32768/P_FACTOR_FAST*SEC)
#define P_FACTOR_SLOW	5
#define P_ERRORBAND_SLOW 10
#define I_FACTOR_SLOW   1
#define I_ERRORBAND_SLOW 1
#define HAVE_GPSLOCK 	0
#define HAVE_OSCCOLD 	0
#endif

#define USELED	0
#define SAMPLE_SECONDS  8
#define SAMPLE_MINUTE	60

//#define DEBUG_SECOND  1

#define X12MHZ 0
#define X16MHZ 1

/* Hardware Port definitions */
#define P1Button    0x80
#define P1LED3      0x20        // Red LED
#define P1LED2      0x40        // Green LED
#define P1LED1      0x80        // Yellow LED

#define P2OSC       0x01        // OSC Good input - high when cold
#define P2GPSLOCK   0x02        // GPS Lock - high when no lock
#define P2LED4      0x20        // Blue LED

// State Machine
#define START       0
#define CHECKERRORS 1
#define OSCCOLD     2
#define NOOSCCLOCK  3
#define NOGPSPPS    4
#define NOGPSLOCK   5
#define GOOD        6
// Below states run only on 1PPS clock
#define FASTINIT    7
#define FASTWAIT    8
#define FAST        9
#define SLOWINIT    10
#define SLOW        11


//
//  Data for interrupt handlers
//
volatile long count = 0;                 // counter for 10mhz clock.  Managed by counter overflow interrupt handler
volatile long countadd = 0x10000;        // value to add to count on overflow.
                                // the only time this value is not 65536 is when the 1pps signal arrives.
                                // it is then set to 65536 - captured count.
volatile long capture = 0;               // captured count
volatile char pps = 0;                   // counter from 1pps handler.  Used to detect no 10mhz clock.
char blinkcounter = 0;       // interrupt blink counter
char blink_blue = 0;
char blink_green = 0;
char blink_yellow = 0;
char bcg = 0;
char bcb = 0;
char bcy = 0;

//
// Basic Output: character, string, decimal, hex (4, 16, and 32 bits)
// printf for basic format types.  Calls to printf are expensive, these are shortcuts.
//

// blocking character transmit.
void
tx (char c)
{
    while (!(IFG2 & UCA0TXIFG));        // USCI_A0 TX buffer ready?
    UCA0TXBUF = c;              // TX -> character
}

// printf(%x) of 1 digit.
void
printfx4 (int v)
{
    v &= 0xf;
    v += '0';
    if (v > '9')
        v += 7;
    tx ((char) v);
}

// printf(%x) of 16-bit word.
void
printfx16 (int v)
{
    printfx4 (v >> 12);
    printfx4 (v >> 8 & 0xf);
    printfx4 (v >> 4 & 0xf);
    printfx4 (v & 0xf);
}

// printf(%x) of 32-bit word.
void
printfx32 (long v)
{
    printfx16 (v >> 16);
    printfx16 (v & 0xffff);
}

// printf(%d)
void
printfd (int v)
{
    char output[16];
    void printfs (char *c);
    sprintf (output, "%d", v);
    printfs (output);
}

// printf(%s)
void
printfs (char *c)
{
    for (; *c; c++) {
        if (*c == '\n')
            tx ('\r');
        tx (*c);
    }
}

// newline (CR & LF)
void
nl ()
{
    tx ('\r');
    tx ('\n');
}

// 
// Set blink state
//
void
ledstate(int blue, int green, int yellow)
{
    switch(blue) {
    case 0:     P2OUT &= ~0x20;         // Blue LED off
        break;
    case 10:
    case -1:    P2OUT |= 0x20;          // Blue LED on
        break;
    }
    blink_blue = blue;

#if USELED
    switch(green) {
    case 0:
    	P1OUT &= ~0x40;         // Green LED off
    	bcg = 0;
        break;
    case -1:
    	P1OUT |= 0x40;          // Green LED on
    	bcg = 0;
        break;
    default:
    	bcg = green;
    }
    blink_green = green;

    switch(yellow) {
    case 0:
    	P1OUT &= ~0x80;         // Yellow LED off
    	bcy = 0;
        break;
    case -1:
    	P1OUT |= 0x80;          // Yellow LED on
    	bcy = 0;
        break;
    default:
    	bcy = yellow;
    }
    blink_yellow = yellow;
#endif /* USELED */
}

//
// Configure the microcontroller ports.
//
int
config(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // Stop watchdog timer

    // Set processor clock speed
    // From TI's example program: msp430g2xx3_dco_calib
    //12Mhz
    if (CALBC1_12MHZ == 0xFF)   // If calibration constant erased
    {
        return (1);             // do not load, trap CPU!!
    }
    DCOCTL = 0;                 // Select lowest DCOx and MODx settings
#if X12MHZ
    BCSCTL1 = CALBC1_12MHZ;     // Set range
    DCOCTL = CALDCO_12MHZ;      // Set DCO step + modulation*/
#endif
#if X16MHZ
    BCSCTL1 = CALBC1_16MHZ;     // Set range
    DCOCTL = CALDCO_16MHZ;      // Set DCO step + modulation*/
#endif

    // Configure Timer TA0 to count clock pulses on P1.0 with a capture input on P1.1.
    // From TI's example program: msp430g2xx3_ta_03.c (with modifications)
    TA0CTL = MC_2 + TAIE;       // TACLK, continuous mode, interrupt
    P1SEL |= 0x01;              // Function: TA0.TACLK

    // Enable Capture/Compare register 0
    TA0CCTL0 = CM1 | SCS | CAP | CCIE;  // Capture on CCIxA on falling edge, synchronous
    TA0CCTL1 = 0;
    TA0CCTL2 = 0;

    P1DIR &= ~0x02;             // input
    P1SEL |= 0x02;              // Function: CCI0A

    //P1DIR &= ~0x08;                                                   // P1.3 (Button) as an input
    //P1REN |= 0x08;                                                    // P1.3 pull-up resistor enable

    // PWM using timer 1 on P2.2-10
    // This goes through a low-pass filter and connects to the oscillator's voltage
    // control input.
    // From TI's example program: msp430g2xx3_ta_16.c (with modifications)
    P2DIR |= 0x04;              // P2.2 output
    P2SEL |= 0x04;              // P2.2 TA1 options
    TA1CCR0 = 65535;            // PWM Period


    TA1CCTL1 = OUTMOD_7;        // CCR1 reset/set
    TA1CCR1 = 1;
    TA1CTL = TASSEL_2 + MC_1;   // SMCLK, up mode - counts to TA1CCR0

    P1DIR |= 0xf0;              // P1.6 is LED2, P1.7 is LED1 (new), P1.5 is POWER
    P1OUT &= ~0xf0;
    P2DIR &= ~0x01;             // P2.0 input - OSC Good
    P2DIR &= ~0x02;             // P2.1 input - GPS Lock
    P2DIR |= 0x20;              // P2.5 output - OSC Good (Blue)
    P2OUT &= ~0x20;             // turn Blue LED off

    // Serial port from TI's example program: msp430g2xx3_uscia0_uart_01_9600 (modified)
    P1SEL |= BIT2;              // P1.2=TXD
    P1SEL2 |= BIT2;             // P1.2=TXD
    UCA0CTL1 |= UCSSEL_2;       // Uart clock from SMCLK
#if X12MHZ
    UCA0BR0 = 0xe0;             // 12MHz 9600
    UCA0BR1 = 4;                // 12MHz 9600
#endif
#if X16MHZ
    UCA0BR0 = 0x80;             // 16MHz 9600
    UCA0BR1 = 6;                // 16MHz 9600
#endif
    UCA0MCTL = UCBRS0;          // Modulation UCBRSx = 1
    UCA0CTL1 &= ~UCSWRST;       // **Initialize USCI state machine**
    //IE2 |= UCA0RXIE;                          // Enable USCI_A0 RX interrupt

    UCA0TXBUF = '!';
    nl ();

    P1OUT |= 0x20;              // turn on power/status LED

    //_BIS_SR(LPM0_bits + GIE);                 // Enter LPM0 w/ interrupt
    _BIS_SR (GIE);              // Enable interrupt
    return 0;
}

int
main (void)
{
    unsigned int pwm_duty_cycle = 1;    // PWM duty cycle ~ voltage
    long sum = 0;               // sum of captured counts during (counter) pulses
    long wlc = 0;               // loop counter - experimental counter.  0x40961 iterations per second
    int counter = -10;          // count of 1pps pulses before acting.
    char lockcount = 0;         // iterations that had lock.

    //  initialized to a value not used by the state check
    char state = 0;             // state machine
    char oldstate = 0;          //   previous state, for reporting changes

    long error;                 // calculated error from 10mhz
    int adjust;                 // adjustment of PWM duty cycle
    int P, I;			// PID adjustment values
    int Ihist;                  // I history

    char slowlock = 0;		// number of minutes with no adjustment

    config();

    printfs("PID2-reorg-0703"); nl();

    TA1CCR1 = pwm_duty_cycle;
    ledstate(0,0,0);

    capture = 0;
    counter = -1;
    while (1) {
        wlc++;
        // Report when state has changed
        if (state != oldstate) {
            printfs("> state: ");
            printfd(oldstate);
            printfs(" -> ");
            printfd(state);
            nl();

            oldstate = state;
        }

        //
        // States that are run repeatedly
        //
        switch (state) {

        case START:
                /* FALL THROUGH */
        case CHECKERRORS:   // a start-up state that checks for errors before
                            // continuing
                state = GOOD;   // If no error checks fail, enter GOOD state
                /* FALL THROUGH */
        default:
            // Check for errors

            // Check if Oscillator is cold
#if HAVE_OSCCOLD
            if (P2IN & P2OSC) {
                ledstate(-1, 0, 0);     // Blue LED on
                state = OSCCOLD;
                break;
            }
#endif
            // Check for GPS Lock Lost
#if HAVE_GPSLOCK
            if (P2IN & P2GPSLOCK) {
                ledstate(0, 0, 5);      // slow blink yellow
                state = NOGPSLOCK;
                break;
            }
#endif
            // Check for missing 1PPS signal
            if (count > 150000000) {     // If no PPS clocks for 15 seconds
                ledstate(0, 0, 3);      // Slow blink yellow
                state = NOGPSPPS;
            }

#if 0
// >> Test this
            // Check for missing 1PPS signal
            // loop count measured at 264545/second.
            if (wlc > 2645450) {        // If no PPS clocks for 10 seconds
                ledstate(0, 0, 3);      // Slow blink yellow
                state = NOGPSPPS;
                printfs("loop count PPS check failed\n");
            }
#endif

            // Check for missing oscillator signal
            if (pps > 14) {
                // 15 seconds of clocks from GPS but no 10mhz clock pulses
                // toggle the red LED to indicate the device is functioning
                ledstate(1, 0, 0);      // blink blue
                state = NOOSCCLOCK;
                pps = 0;
            }
            break;

        case OSCCOLD:   // Oscillator is cold
            // check if oscillator is still cold
            if ((P2IN & P2OSC) == 0) {
                ledstate(0, 0, 0);      // blue (all) off
                state = CHECKERRORS;
            }
            break;

        case NOOSCCLOCK:
            // check if there has been any clocks from the oscillator.  
            // The 'pps' counter is tracked for this purpose: it is set by
            // the 1pps interrupt, and is reset when there is a non-zero
            // capture count.
            if (pps) {
                ledstate(0, 0, 0);
                state = CHECKERRORS;
            }
            break;

        case NOGPSPPS:
            // check if there has been any GPS 1PPS signals
            //if (capture > 10 || pps) {   // yes, I've had a capture
             // above line toggled states back and forth between CHECKERRORS and NOGPSPPS
            if (pps) {   // I've had a capture
                ledstate(0, 0, 0);
                state = CHECKERRORS;
            }
            break;

        case NOGPSLOCK:
            if ((P2IN & P2GPSLOCK) == 0) {
                ledstate(0, 0, 0);
                state = CHECKERRORS;
            }
            break;

        case GOOD:
            // State between one of the error states and one of the
            // operational states.  State is set to GOOD by CHECKERRORS
            // as the "no errors" state.
            // Hand off to 1PPS based actions
            state = FASTINIT;
            break;
        }

        // Look for a 1PPS signal
        if (capture != 0) { // && state > GOOD
            pps = 0;                        // reset pps counter.
                                            // used as oscillator loss check

            if (counter >= 0) {
                // sum clock counts only when positive.
                // negative count allows for stabilization after changing
                // the oscillator
                sum += capture;
            }

            // 1 second report: Letter Count Error
#ifdef DEBUG_SECOND
            error = 10000000 - capture;     // Error relative to a 10mhz clock rate
            tx ('A' + counter);
            tx (' ');
            printfx32 (capture);
            tx (' ');
            printfd (error);
            tx (' ');
            //printfx32 (wlc);     // while(1) loop counter.  see about using it
                                // as a fault detection counter.
            printfx32(sum);
            nl ();
#endif /* DEBUG_SECOND */
#ifdef DEBUG_SEC_SHORT
            error = 10000000 - capture;     // Error relative to a 10mhz clock rate
            printfd (error);
            tx (' ');
#endif

            capture = 0;
            wlc = 0;

            //
            // States that occur on a 1PPS clock
            //
            switch (state) {

            // I could put the GOOD state here

            case FASTINIT:      // initialize for fast wait
                counter = 5;
                state = FASTWAIT;
                ledstate(0, 0, 1);  // set LED to yellow blink
                lockcount = 0;
                break;

            case FASTWAIT:      // wait for counters to stabilize
                // switching from one of the error states to here is not 
                // synchronized with 1PPS, so the count in capture may not be
                // good. Wait for the partial second, and for good measure 
                // wait one more second.
                if (--counter <= 0) {
                    // yellow LED continues to blink
                    state = FAST;
                    sum = 0;
                }
                break;

            case FAST:
                // FAST Synchronize to GPS 1PPS. This uses a Proportional controller and
                // a factor that is estimated to be a full step.
                // Count for several seconds before acting to minimize GPS jitter.
                adjust = 0;
                counter++;
                if (counter >= SAMPLE_SECONDS) {
#ifdef DEBUG_SEC_SHORT
                    nl();
#endif
                    counter = 0;

                    error = (SAMPLE_SECONDS * 10000000) - sum;

                    //
                    // Determine LED status
                    //
                    // If error > 10 (1 in 10,000,000) : blink Yellow LED
                    // If error < 10 && > 2 (1 in ?) : slow blink Yellow LED
                    // If error <= 1 : solid on Yellow LED
                    //
                    if (abs (error) < 2) {
                        ledstate(0, 0, -1); // turn on Yellow LED
                    } else {
                        ledstate(0, 0, 1);  // blink Yellow LED
                    }

                    // an "error band" of +- 1
                    if (abs (error) <= P_ERRORBAND_FAST) {
                        lockcount++;
                        if (lockcount > 5) {
                            // After no ajustments are needed for 5 seconds, switch to the
                            // slow control program.
                            state = SLOWINIT;
                        }
                    } else {
                        lockcount = 0;
                        if (error > P_MAX_ERROR)
                        	error = P_MAX_ERROR;
                        if (error < 0-P_MAX_ERROR)
                        	error = 0-P_MAX_ERROR;
                        // Make an adjustment.
                        // The proportional factor is tuned for 1 second samples
                        // so divide by seconds
                        adjust = (P_FACTOR_FAST / SAMPLE_SECONDS) * error;
                    }

                    // Try to prevent underflow or overflow of the PWM duty cycle.
                    // First, by limiting the adjustment value
                    if (adjust > 32000) {
                        adjust = 32001;
                    } else if (adjust < -32000) {
                        adjust = -32000;
                    }

                    // second, by trying to detect overflow / underflow
                    if (adjust > 0
                        && pwm_duty_cycle + adjust < pwm_duty_cycle) {
                        // Overflow
                        pwm_duty_cycle = 0x8001;
                    } else if (adjust < 0
                               && pwm_duty_cycle + adjust > pwm_duty_cycle) {
                        pwm_duty_cycle = 0x8000;
                    } else {
                        // No overflow or underflow.  Make the adjustment.
                        pwm_duty_cycle += adjust;
                    }

                    // status message
                    printfs("== ");
                    printfx16(pwm_duty_cycle);
                    tx(' ');
                    printfd(error);
                    tx(' ');
                    printfd(adjust);
                    nl();

                    if (adjust) {
                        TA1CCR1 = pwm_duty_cycle;
                        // If an adjustment was made, skip the current second's count
                        counter = -1;
                    }
                    sum = 0;
                }
                break;

            case SLOWINIT:
                // initialize for slow control program.
                slowlock = 0;
                sum = 0;
                Ihist = 0;
                ledstate(0, 1, 0);
                state = SLOW;
                counter = -2;

                /* FALL THROUGH */
            case SLOW:
                // Slow tracking of GPS.  Measures offset from GPS over a minute
                // and makes small adjustments.
                // The P factor is typically 5% of the full step between frequencies.
                counter++;
            	if (counter >= SAMPLE_MINUTE) {
#ifdef DEBUG_SEC_SHORT
                    nl();
#endif
                    error = (60 * 10000000) - sum;
#ifdef DEBUG
                    printfs("S ");
                    printfx32 (sum);
                    tx(' ');
                    printfd(error);
                    nl();
#endif /* DEBUG */

                    counter = 0;
                    sum = 0;

                    if (abs(error) > 128) {  // glitch or something. 
                        // may need to lower this to catch drift problems that
                        // should cause switching back to FAST
                        state = FASTINIT;
                        printfs("** ERROR ");
                        printfx32 (error);
                        nl();
                    } else {
                    	adjust = 0;
                    	P = I = 0;
                        // Proportional control, based on the 1 minute error.
                    	if (abs(error) > P_ERRORBAND_SLOW) {
                    		P = error * P_FACTOR_SLOW;
                    	}

                        // Integral control
                        // Look at the last several minutes of error data
                        // for a trend + or -. When there is a trend of
                        // errors in one direction, make an adjustment.

                        // Integral history & calculation:
                        // When the error is positive or negative, and the I history
                        // matches (pos/neg), then increase the count in the direction
                        // of the error.
                        // If it flips from +1 to -1 (or -1 to +1), this require an extra
                        // cycle before counting.
                        if (error < 0 && Ihist <= 0) {
                            Ihist--;
                        } else if (error > 0 && Ihist >= 0) {
                            Ihist++;
                        } else {
                            Ihist = 0;
                        }

                        if (abs(Ihist) > I_ERRORBAND_SLOW) {
                            I = I_FACTOR_SLOW * Ihist;
                        }

                        adjust = P + I;
                        if (adjust) {
                            slowlock = 0;
                            ledstate(0, 1, 0);
                        } else {
                            // no adjustment - move to slower blinking green
                            adjust = 0;
                            slowlock++;
                            //ledstate(0, slowlock > 4 ? -1 : slowlock, 0);
                            ledstate(0, -1, 0);
                        }

                        if (adjust) {
                            pwm_duty_cycle += adjust;
                            TA1CCR1 = pwm_duty_cycle;
                            counter = -1;
                        }

#ifdef DEBUG_PID
                        printfs("** ");
                        printfd(error);
                        tx(' ');
                        printfd(P);
                        tx(' ');
                        printfd(I);
                        tx(' ');
                        printfd(Ihist);
                        tx(' ');
                        printfd(adjust);
                        tx(' ');
                        printfx16(pwm_duty_cycle);
                        nl();
#endif

                        // status message
                        printfs("== ");
                        printfx16(pwm_duty_cycle);
                        tx(' ');
                        printfd(error);
                        tx(' ');
                        printfd(adjust);
                        nl();
                    }
                }
                break;
            }
        }
    }
}

// From TI's example program: msp430g2xx3_ta_03.c (with modifications)
// Timer_A3 Interrupt Vector (TA0IV) handler
// The 10mhz clock is to be connected to it's count input.
#pragma vector=TIMER0_A1_VECTOR
__interrupt void
Timer_A (void)
{
    switch (TA0IV) {
    case 2:                     // CCR1 capture
        break;
    case 4:
        break;                  // CCR2
    case 10:                    // counter overflow
        count += countadd;      // add remaining (or full) count
        countadd = 0x10000;     // set count to a full count.
        break;
    }
}

// Interrupt vector for capture/compare register 0
// 1PPS is to be connected here.
#pragma vector=TIMER0_A0_VECTOR
__interrupt void
Timer_A0 (void)
{
    unsigned int c;
    c = TA0CCR0;                // get capture value

    if (TA0CCTL0 & COV) {       // If there has been an overflow, reset it
        TA0CCTL0 &= ~COV;
    }

    capture = count + c;
    countadd = 0x10000 - c;     // count value for next counter overflow is the remainder of this cycle's count

    count = 0;
    pps++;                      // 1pps counter

#if USELED
    if (bcg && (--bcg == 0)) {
        P1OUT ^= 0x40;          // toggle green LED
	if (P1OUT & 0x40) {     // off time is 1 second
    	    bcg = blink_green;
        } else {
            bcg = 1;
        }
    }

    if (bcy && --bcy == 0) {
        P1OUT ^= 0x80;          // toggle yellow LED
        if(P1OUT & 0x80) {
            bcy = blink_yellow;
        } else {
            bcy = 1;
        }
    }
#endif

}
// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4 

