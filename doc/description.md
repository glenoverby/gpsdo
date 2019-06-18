A GPS Disciplined Oscillator

Glen Overby, KC0IYT

gpoverby@gmail.com

# Introduction

When operating on the microwave bands, I've often needed to have
oscillators that are both stable and accurate.
These don't always come as one package.
Combining the N5AC apolLO synthesized oscillator
(also sold by Downeast Microwave as the A32),
with a good ovenized oscillator,
has given me good stability when operating on bands up to 10ghz.
I desired an oscillator that is accurate as well as stable.
One approach for adding stability is to synchronize an oscillator with the
timing signal output from a GPS recieiver.
This paper presents my experience of using a microcontroller to do the
synchronization.


This project started out as a derivative of the article by VE2ZAZ in the
October 2006 QEX.
The author has a web site describing his project at:
[http://ve2zaz.net/GPS_Std/GPS_Std.htm](http://ve2zaz.net/GPS_Std/GPS_Std.htm)

During the adventure of assembling the hardware and writing the software,
I've arrived at a somewhat different device.


# Theory of Operation

GPS disciplinging of oscillators is frequently done using a phase-locked loop
(PLL).
This project replaces the PLL with a micrcocontroller and software.

At the center of this project is a microcontroller which count clocks from the
10mhz oscillator, checking the count against the expected 10 million clocks
against a GPS' 1 pulse-per-second output.
The microcontroller calculates the error and adjusts
the oscillator's voltage control input to synchronize it with the GPS signal.

I discovered that the simple approach of adjusting every 1PPS signal had a
problem:
the GPS signal (or the GPS receiver)
has some jitter -- the GPS' 1PPS signal is not accurate to nanoseconds on
every clock pulse.
I saw this this the first first time I put this project into operation:
the software would see the oscillator clock count being short one second, and
long the next second.
I experimented with longer sample times, but still saw similar results.
In investigating how other GPS disciplined oscillators work,
I discovered that commerical oscillators evaluate the GPS signal over
several minutes to several hours.
I have followed that approach, and the software on this device now operates on
several seconds of data, or a minute of data, depending on what mode it is in.

In addition, I found other conditions which need to be handled: 

* When an ovenized oscillator is warming up,
its frequency drifts enough that it isn't beneficial to try controlling it.
Many of those oscillators will have an output that can be monitored to show
when it has warmed up.

* The GPS receiver's 1PPS signal may not be generated when the GPS receiver
doesn't have lock (or before the first lock), and is likely to be inaccurate
when the GPS receiver does not have a 3D lock.


# Hardware

Shortly before starting this project, I purchased a TI MSP430 LaunchPad
board and was looking for a project to use it for.
The MSP430 microcontroller turned out to be an excellent choice because
it's peripherals have features that are ideally suited for this project.
The microcontroller on this board, the MSP430G2553, has two counters which can
count an external clock at the processor's clock speed.
The processor can run from an internal clock at up to 16mhz, thus allowing
it to count the 10mhz oscillator directly.
The counter includes a sample-and-hold register, which is triggered by an
external signal.
This is the feature that really made this chip ideal for this project.
Thus, the 10mhz clock is connected to the counter clock input
and the GPS 1PPS signal to the counter's sample and hold trigger.

A second counter in the microcontroller is used to control the oscillator's
frequency voltage control.
The counter is configured to produce a pulse-width modulated (PWM)
output signal, which is filtered to produce an analog voltage.
This is a common way to generate an analog output from a microcontroller.

The chip's architects left a complication for me: the counter's
sample-and hold has two sample inputs, but both of them use the same chip pins
as the serial port!
So I had to choose between serial input and serial output.
I would like to monitor the GPS' serial stream to see when it has 3D lock,
and thus the most accurate time, but the ability to debug software using the
serial output was more important.
Instead, I use a separate microcontroller to monitor the GPS.

## Circuit Board

The first board required several changes to make it useful,
including the addition of a 'dead bug' chip. The schematic and PCB
shown here incorporate all of the changes made to the first board.
Note that I haven't had this second board made.

I designed the board using through-hole components, except in a few places
where there was no through-hole component available.

## Board Inputs and Outputs

The circuit board has several headers for input and output:

* Oscillator Input
* Oscillator buffered output
* Oscillator digital output
* GPS
* GPS Serial access
* GPS Lock Detect
* Microcontroller Serial
* Power

### Serial Ports
 The serial ports all use 6-pin headers and the pinout used by Arduino
boards.
This allows plugging in USB-to-serial adapters made for Arduinos.

### Oscillator
* 10MHZ Input
* VFO Tune Output
* VFO Tune Voltage Input

### GPS
 This connects to a GPS module.

* GPS Serial input and output
* GPS 1PPS input
* +5V power for the GPS

### GPS Serial Access
* Connects to the GPS serial port

### GPS Lock Detect
 This header is for connecting to a second microcontroller that monitors the
GPS serial data for lock.

* GPS Serial Output
* Lock Detect Input
* +5V power

### Debug
* Microcontroller Serial Output

### Misc
* Power (6.5v - 12v)

## Circuit Description

This description references part numbers on the schematic.

### Overview

The 10mhz oscillator signal is amplified, filtered and buffered before
connecting to the counter input pins on the microcontroller.
The GPS' 1PPS signal is buffered and connected to the counter's
sample-and-hold trigger input.
The output from a second counter is is filtered and connected to the
oscillator's voltage control input.
Signal inputs for the Oscillator heater and GPS Lock are buffered and connected
to regular input pins.

Buffering is done using a combination of TTL/CMOS buffer chips and transistors. 
The MSP430 microcontroller is a 3.3V device, 
but some of the inputs (such as the GPS) are 5V or higher.
This project uses 74HCT type logic chips to do voltage level conversion.
These chips are tollerant of 5V input signals when powered at 3.3V and
when powered at 5V will "see" a 3.3V high signal as a "1" input.

The oscillator heater input can be a higher voltage, so it is buffered using
a 2N2222 transistor.


### Inputs

The 10mhz signal enters the board and is amplified by a MAR-5 (or ERA-1) MMIC,
U1.
This is to isolate the oscillator from the on board circuitry and to amplify it.
The output of the MMIC goes through a 5 pole low-pass filter to eliminate
high-frequency harmonics.

A second MMIC and filter is available to drive external circuitry.
I always use a buffer on the output of oscillators, as many of them are
load sensitive.  That is, their frequency varies depending on the output
load, requring some settling time to come back on frequency after connecting
or powering on the devices using their signal.

The 10mhz analog signal goes through a variable resistor to a 74HCT125 TTL
(digital) buffer.
The resistors pull the signal up to a level that the TTL buffer will detect it
as 1s and 0s.

The GPS' 1 Pulse Per second (1PPS) signal enters the board and goes through
a 74HCT125 gate for logic level conversion.

The oscillator clock and 1PPS signals go to one of the microcontroller's
counters, which will be used to measure the clock error.

An ovenized (heated) oscillator will likely have an output to indicate when
the heater has heated up to it's operating temperature.
The voltage of this output was not known for certain at the time this board
was built, and may vary from oscillator to  oscillator so it drives a 2N2222
transistor (Q2).
This input can be selected with JP6, or shorted to ground.

An input to indicate GPS lock, supplied by another microcontroller,
goes through another 74HCT125 gate for voltage conversion.

### Outputs

A Pulse-Width-Modulated output, or PWM, is used to provide an analog
output signal.  An As described earlier, a
a counter is configured to produce a square wave whose "on" pulse
width is adjusted to be long for a higher voltage or short for a
lower voltage.

This is done by configuring the counter to use two registers:
one for the maximum count
and the other for the length of the "on" pulse.
The counter starts at zero and sets the PWM output pin.
It counts to the value specified in the on pulse length register,
at which time the output pin is set to 0.
The counter continues counting to the value in the maximum count register
at which time the counter value is reset to zero and the cycle starts again.

This PWM signal is passed through a pair of 1hz low-pass filters which smooth
out the pulse to a constant voltage (U3).

The micocontroller serial output port connects to a header.
I have several of the SparkFun Electronics DEV-09873 - FTDI Basic Breakout
boards for connecting to USB ports.

There are three headers available for GPS interfacing: one to the GPS,
one to connect to the GPS serial signals, and one to connect to a board that
monitors the GPS serial output for it's lock state.

The GPS header can supply +5V power to the GPS, and has the input for the 1PPS
signal.
The second header uses the Arduino pinout and connects to the GPS serial lines.
This provides off-board access to the GPS.
The third header is intended to connect to a board, such as an Arduino,
which monitor the GPS serial streem to determine the lock state.
This header has the serial data from the GPS and a GPS Lock signal returning.

Several LEDs provide status output:

* Red LED is heartbeat of the software
* Yellow is the status of the fast lock
* Green is the status of the slow lock
* A Red/Green LED from the GPS lock board indicates if there is lock or not.

# Software

The software is written in "C" and is built using TI's MPLAB development
environment.
I created the project as a CCS project and listed the microcontroller.

The software as written as a state machine, and operates one of two 
Proportional, Integral, Differential (PID) controllers.
A PID controller is a control loop feedback mechanism used for process
control.
It can be mechanical, electrical or software.
Those who are not familiar with PID controlers,
which I was not at the start of this project,
Wikipedia had a good introduction to them.

I found it was useful to have two control domains: a fast 
mode that will quickly bring the oscillator close to the desired
frequency, using short time samples from the GPS,
and a slow mode that refines and maintains the frequeny using GPS samples
from a longer time period.

## States
The states are:

+ Check For Errors
+ Oscillator is cold
+ No GPS lock
+ No 1PPS from the GPS
+ No oscillator clock
+ Fast Lock (PID #1)
+ Slow Lock (PID #2)

### Check for Errors state

This is a state that looks for problems:

 + oscillator is cold
 + no GPS lock
 + no 1PPS signal from the GPS
 + no 10mhz clock from the oscillator

This is the state the controller starts out in,
and these values are checked constantly when in one of the PID controller
states.

### Error states

These states wait for their respective errors to clear.
When the error clears, the state machine goes back to Check For Errors state.

### Fast Lock
 This is used for fast synchronization to GPS 1PPS.
This uses a Proportional controller.
A Proportional controller looks at the error (how
far off of 10mhz the oscillator is) and changes the PWM's
output by a constant times the error.
This is to bring the oscillator close to 10mhz as quickly as possible.
Determining this constant requires measurements be done with the oscillator
using a separate program, described later.

This state counts the oscillator clock for 8 seconds before acting to
minimize jitter from the GPS 1PPS signal.
The error value is calculated by subtracting the actual clock pulse count from
what was expected (8,000,000).
The P value is used calculated from the error times a constant, which is
how much to change the PWM counter for a 1hz change.
It also uses what is called an Error Band, where if the count is off by
a small ammount (currently 1 clock count, or 1/8,000,000) no adjustment
is made.

After the error is within +/- 1 count of 10mhz for 5
samples (that is, 5 * 8 seconds) it switches to Slow Lock state.

### Slow Lock
 Slow tracking of GPS.
The slow lock uses a Proportional, Integral controller.
The proportional (P) controller operates once every 60 seconds,
while the Integral (I) controller looks at data over several minutes.
The goal of slow lock mode is to keep adjustments small, preferably being
made by the integral controller.

The proportional controller operates like the fast lock: the error is
multiplied by a constant and applied to the voltage control output.
Every minute the the error value is saved in a circular list for use
by the Integral controller.

The integral controller runs on the minute mark, 
and uses the sum of the errors from the past 10 minutes to determine if
it should apply an additional adjustment.
Currently, this adjustment is done only if there are more than 4 errors in
10 minutes.

If, during any one minute time, a large error (greater than 128 clocks)
is detected the state goes back to Fast Lock.


# Configuration

Configuring the software requires choosing parameters for the PID controllers:

+ P_FACTOR_FAST
+ P_FACTOR_SLOW
+ I_FACTOR_SLOW

These are the parameters for the proportional (P) and integral (I) parts
of the controller (the software is a P or PI controller, not a full PID).

Many books have been written about tuning PID controllers, and it can be
an involved process.
This is the technique that I came up with by trial and error.

I use a test program (freq-find)
that does a binary search on the VFO counter value to
hunt for the counter values for 9,999,999, 10,000,000, and 10,000,001 MHz.
The counter value difference between adjacent values and pick the lower of
them, this is the P_FACTOR_FAST.
Guessing too high is bad, as that can cause the PID contoller to oscillate
around the desired frequency without ever finding the correct frequency.
I have also used half of that value, with the downside being longer lock
time.

The proportional slow parameter should be much smaller, about 2% of the fast
parameter, and the I parameter of about half of the slow P parameter seems
about right.


# Experience and Future Work

The first use of this board was used to discipline a FOX 801 10mhz oscillator
using a much simpler version of the software.
It accomplishes bringing the oscillator close enough to 10mhz and keeping it
there to use it to drive a marker.

The second board disciplines an Isotemp 134-10 10mhz oscillator.
During the buidling of this, the software was redesigned.
The finer control of this oscillator showed that the original algorithim would
never find a good lock and that small adjustments were necessary.
It was also during this development that I began to understand the GPS
signal jitter better:
it really is accurate only when used over a long period of time.
Thus the motivation for switching to 1 minute samples and 10 minute summing of
errors.

The most significant design problem encountered was caused by driving the LEDs
from the same voltage regulator as the microcontroller.
I found turning on and off the LEDs would cause a dip and rise in the 3.3V
power rail that would then cause the oscillator frequency to change.
This voltage change was a few hundred micro-volts
(and not detectable by my DVM),
and was seen only when driving a synthesized oscillator whose output
was being watched on the 10ghz band.
I solved this problem by driving the LEDs from the +5V regulator,
through a 74HCT125 buffer.

A second version of the PCB has been designed, to fix some problems:

* I've switched to using the Arduino standard for all serial ports
* Added more voltage regulators to improve noise isolation
* Stopped powering the 3.3V regulator from the output of the 5V regulator, also for improved noise isolation.
* A second MMIC buffer was added to the board, to integrate one that was built
dead-bug style on another board.

Not discussed here is a project that provides a GPS lock indicator signal to
this board.
That project uses an Atmel AVR (similar to an Arduino) to watch the
serial data stream from the GPS and provide a TTL signal for lock or no lock.
It also provides a GPS health indicator on an LED.

The software, schematic, and PCB designs are available from:
https://github.com/glenoverby/gpsdo

