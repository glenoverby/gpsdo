# gpsdo
A GPS Disciplined Oscillator Controller

**There is a newer version of this project in my 'gpsdo3' repository.**

This is a microcontroller based GPS disciplined oscillator.

The purpose of this device is to synchronize a 10MHz voltage controlled
oscillator with the timing signal from a GPS receiver such that the oscillator
operates at exactly 10mhz.

The process is similar to using a phase-locked loop (PLL) but without the
analog components of a PLL.  The details, however, are not so simple.

The PCB design is version 2, which I have not constructed yet.
Version 1 requires many changes to make it work.

The software directory contains several progams:

* Freq-Find  uses a binary search algorithm to search for a target
frequency.  The program searches for 10,000,000 hz, 9,999,999 hz, and
10,000,001 hz.  The PWM values from this program are used to calculate
the PID constants used in the main control program.

* measure, A program that measures the clock frequency at several PWM settings.

* gpsdo-p, Proportional controller which disciplines an oscillator.
I don't use this version anymore.

* gpsdo-pid2, PID (Proportional, Integral, Derivitive) controller which
disciplines an oscillator to a GPS. 
This has code for an Isotemp 134-10 OCXO and a FOX-801 VCO. This version is still a work in progress.

![Image of board wired up](https://raw.githubusercontent.com/glenoverby/GPSDO/master/doc/v1-debug.jpg)

