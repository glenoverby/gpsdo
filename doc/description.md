# Introduction

This is a microcontroller based GPS Disciplined Oscillator.
The purpose of this device is to synchronize a 10MHz voltage controlled
oscillator with the timing signal from a GPS receiver such that the oscillator
operates at exactly 10mhz.

This project started out as a derivative of the article by VE2ZAZ in the
October 2006 QEX.
The author has a web site describing his project at:
[http://ve2zaz.net/GPS_Std/GPS_Std.htm](http://ve2zaz.net/GPS_Std/GPS_Std.htm)

During the adventure of assembling the hardware and writing the software,
I've arrived at a somewhat different device.

The idea behind this project is to replace the phased locked loop of a GPS
disciplined oscillator with a microcontroller and software.

# Theory of Operation

This device has a microcontroller which count clocks from the 10mhz
oscillator, checking the count against the GPS' 1 pulse-per-second output
(1PPS) every second.  The microcontroller calculates the error and adjusts
the oscillator's voltage control input to synchronize it with the GPS signal.

That theory sounds simple.  However, the GPS signal (and possibly the GPS
receiver) has some jitter -- the clock is not accurate to nanoseconds on
every single clock.  Commerical oscillators evaluate the GPS signal over
several minutes to several hours.  The software on this device operates on
several seconds of data, a minute of data, or 10 minutes of data depending
on what mode it is in.

The GPS receiver's 1PPS signal is not necessarily accurate (or generated)
when the GPS receiver doesn't have lock, so the output data stream must be
monitored, in this case for a 3D lock.


# Hardware

Shortly before starting this project, I purchased a TI MSP430 LaunchPad
board and was looking for a project to use it for.
The MSP430 microcontroller turned out to be an excellent choice because features of
it's peripherals are ideally suited for this project.

The MSP430G2553 microcontroller in the LaunchPad has two counters which can
count an external clock at up to the processor's clock speed.
The processor can run from an internal clock at up to 16mhz.
The thing that really made this chip ideal for the project is 
the counter includes a sample-and-hold register, which is triggered by an
external signal.
Thus, the 10mhz clock is connected to the counter clock input
and the GPS 1PPS signal to the counter's sample and hold trigger.

A second counter is configured to produce a pulse-width modulated (PWM)
output, which is filtered to produce an analog voltage.
This is a common way to make a digital-to-analog converter with a
microcontroller.  This voltage goes to the oscillator's voltage control input.

But the processor architects left behind a complication for me: the counter's
sample-and hold has two sample inputs, and both of them use the same chip pins
as the serial port!
As a result I had to choose between serial input and serial output.
I would have liked to monitor the GPS' serial stream to see when it had
3D lock, but the ability to debug software using the serial output was more
important.
I could have "bit banged" a serial port using another output pin (I've done
that on AVR chips) but I wasn't excited about that.
Instead, I use a separate microcontroller to monitor the GPS.

## Board Inputs and Outputs

### Oscillator
* 10MHZ Input
* VFO Tune Output
* VFO Tune Voltage Input

### GPS
* GPS Serial input and output
* GPS 1PPS input
* +5V power for the GPS

* GPS Serial Port Access for an external device

### GPS Lock Detect

* GPS Serial Input
* Lock Detect Input
* +5V power

### Debug
* Microcontroller Serial Output ("Console")

### Misc
* Power (6.5v - 12v)

## Serial

The serial port pinouts all use 6-pin headers and the pinout from the Arduino
boards.

## Circuit Description

This description references part numbers on the schematic.

The 10mhz signal enters the board and is amplified by a MAR-5 (ERA-1) MMIC, U2.
This is to isolate the oscillator from the on board circuitry and to amplify it.
The output of the MMIC goes through a 5 pole low-pass filter to eliminate
high-frequency harmonics.

In verison 2 of the PCB, a second MMIC is available as a buffer output to
drive external circuitry.
I always use a buffer on the output of oscillators, as many of them are
load sensitive.  That is, their frequency varies depending on the output
load, requring some settling time to come back on frequency after connecting
or powering on the devices using their signal.

The 10mhz analog signal goes through a variable resistor to a 74HCT125 TTL
(digital) buffer.
The resistors pull the signal up to a level that the TTL buffer will 'see' it as 1s and 0s.
HCT type logic has a nice feature that it consideres a fairly low voltage to be a "1".
This is useful for converting from 3.3v logic levels to 5v logic levels, as
a 3.3v "1" is high enough to register as a "1" at 5v.  The chip is also
tollerant of 5v logic levels when it's VCC is 3.3v.

The GPS 1 Pulse Per second (1PPS) enters the board and goes through
a 74HCT125 gate, to convert it from 5v to 3.3v.

A second counter is configured to produce a square wave whose "on" pulse
width can be adjusted by the software
(a pulse-width modulated output, or PWM)
This is done by configuring the counter to use two registers: 
one for the maximum count
and the other for the length of the "on" pulse.
The counter counts from zero to the on count in the on pulse length register,
at which time the PWM output is reset.
The counter continues counting to the value in the maximum count register
at which time the output is set and the counter value is reset to zero and
the cycle starts over again.

This PWM signal is passed through a pair of 1hz low-pass filters which smooth
out the pulse to a constant voltage (U1).
The ratio of the 1 / 0 signal determines the voltage.

An ovenized (heated) oscillator will likely have an output to indicate when
the heater has heated up to it's specified temperature.  
The voltage of this output was not known for certain at the time this board
was built, and may vary from oscillator to  oscillator so it drives a 2N2222
transistor (Q2).
This input can be selected with JP6, or shorted to ground.

An input to indicate GPS lock, supplied by another microcontroller,
It also goes through a 74HCT125 gate for voltage conversion.

The serial output port is routed to an off-board header to go to a
usb-to-serial adapter then a computer for monitoring of the software.
Version 2 of the board uses a connector compatible with the popular Arduino
pinout and conversion cables and boards are readily (and cheaply) available.
I have severl of the SparkFun Electronics DEV-09873 - FTDI Basic Breakout
boards.

There are three headers available for GPS interfacing: one can supply power
to the GPS, and has the input for the 1PPS signal.  It also routes the GPS
serial lines to two headers: one for input and output (to connect another
usb-to-serial adapter) and a second to connect to a board, such as an Arduino,
to monitor the GPS serial streem to determine the lock state.


Note the use of a 74HCT125 as a buffer for the LEDs, which is connected to
the +5v power supply.  This was because I found turning on LEDs would cause
a dip in the 3.3v rail that would cause the oscillator frequency to change.
This was only detectable when driving a synthesized oscillator whose output
was being watched on 10ghz.

# Software

The software is written in "C" and is built using TI's MPLAB development
environment.
I created the project as a CCS project and listed the microcontroller.

The software as written as a state machine, and operates one of two PID
controllers (Proportional, Integral, Differential) depending on the state.

## States
The states are:

+ Check For Errors
+ Oscillator is cold
+ No GPS lock
+ No 1PPS from the GPS
+ No oscillator clock
+ Fast Lock
+ Slow Lock

### Check for Errors state

This is a state that looks for problems:

 + oscillator is cold
 + no GPS lock
 + no 1PPS signal from the GPS
 + no 10mhz clock from the oscillator

It is called at start-up, and constantly when not in one of the error
resolution states.

### Error resolution states:  

 These states wait for their respective errors to clear.
When the error clears, the state machine goes back to Check For Errors state:

+ Oscillator is cold
+ No GPS lock
+ No 1PPS from the GPS
+ No oscillator clock

### 1PPS  
 The PPS state triggers every time a 1PPS signal is seen.
In this state, data is collected from the counters then,
goes to one of two states:

### Fast Lock
 FAST Synchronization to GPS 1PPS.
This uses a Proportional controller.
Much has been written about PID controllers;
a Proportional controller looks at the error (in this case, how
far off of 10mhz the oscillator is) and changes the PWM's
output by a constant * error.  This is to bring the oscillator
close to 10mhz as quickly as possible.

This state counts pulses for several seconds (currently 8) before acting to
minimize jitter from the GPS 1PPS signal.

After the error is within +/- 1 count of 10mhz for 5
samples (that is, 5 * 8 seconds) it switches to Slow Lock state.

### Slow Lock
 Slow tracking of GPS.  
The slow lock uses a Proportional, Integral controller.  The P controller
operates on data every 60 seconds, while the I controller looks at data
over 10 minuts.

The error is calculated every second for 1 minute.
Every minute the sum of the error is saved in a circular list.
Also at the minute mark the errors from the past 10 minutes
is calculated, and if it is more than 4 errors in 10 minutes,
an adjustment is made to "steer" the oscillator back on target.

The adjustment is a constant, currently 1/20th of the P constant

If, at any 1 minute time, a large error is detected the state
goes back to Fast Lock.


# Configuration

Configuring the software requires "only" choosing two factors for the PID controllers:

+ P_FACTOR_FAST
+ I_FACTOR_SLOW

The P factor is used by the Proportional (Fast) phase

The I factor is used by the Integral (slow) phase

To assist with this is the freq-find program.
It uses a binary search for three values (9,999,999, 10,000,000, 10,000,001) to
provide the data for calulating these factors.


# Experience and Future Work

The first use of this board was used to discipline a FOX 801 10mhz oscillator
using a much simpler version of the software.

The second board disciplines an Isotemp 134-10 10mhz oscillator.
During the buidling of this, the software wase redesigned.
The finer control of this oscillator showed that the original algorithim would
never find a good lock.
I also learned that GPS 1PPS signal has some jitter -- it is accurate when 
used over a long period of time but not over a short period of time.
Thus the motivation for switching to 1 minute samples and 10 minute summing of
errors.





