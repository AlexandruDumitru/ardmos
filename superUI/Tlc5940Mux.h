/*  Copyright (c) 2009 by Alex Leone <acleone ~AT~ gmail.com>

    This file is part of the Arduino TLC5940 Library.

    The Arduino TLC5940 Library is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    The Arduino TLC5940 Library is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with The Arduino TLC5940 Library.  If not, see
    <http://www.gnu.org/licenses/>. */

#ifndef TLC5940MUX_H
#define TLC5940MUX_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#include "tlcMux_config.h"

/** Enables the output of XLAT pulses */
#define enable_XLAT_pulses()    TCCR1A = _BV(COM1A1) | _BV(COM1B1)
/** Disables the output of XLAT pulses */
#define disable_XLAT_pulses()   TCCR1A = _BV(COM1B1)

static void TlcMux_init(uint16_t initialValue = 0);
static void TlcMux_clear(void);
static void TlcMux_clearRow(uint8_t row);
static uint16_t TlcMux_get(uint8_t row, TLC_CHANNEL_TYPE channel);
static void TlcMux_set(uint8_t row, TLC_CHANNEL_TYPE channel, uint16_t value);
static void TlcMux_setAll(uint16_t value);
static void TlcMux_setRow(uint8_t row, uint16_t value);
static inline void TlcMux_shiftRow(uint8_t row);

static uint8_t tlcMux_GSData[NUM_ROWS][NUM_TLCS * 24];

/** Pin i/o and Timer setup.  The grayscale register will be reset to all
    zeros, or whatever initialValue is set to and the Timers will start.
    \param initialValue = 0, optional parameter specifing the inital startup
           value */
static void TlcMux_init(uint16_t initialValue)
{
  /* Pin Setup */
    pinMode(XLAT_PIN, OUTPUT);
    pinMode(BLANK_PIN, OUTPUT);
    pinMode(GSCLK_PIN, OUTPUT);
    
    digitalWrite(BLANK_PIN,HIGH);
    
    TlcMux_setAll(initialValue);

    /* Timer 1 - BLANK / XLAT */
    TCCR1A = _BV(COM1B1);  // non inverting, output on OC1B, BLANK
    TCCR1B = _BV(WGM13);   // Phase/freq correct PWM, ICR1 top
    OCR1A = 1;             // duty factor on OC1A, XLAT is inside BLANK
    OCR1B = 2;             // duty factor on BLANK (larger than OCR1A (XLAT))
    ICR1 = TLC_PWM_PERIOD; // see tlc_config.h

    #ifdef TLC_ATMEGA_8_H
        TIFR |= _BV(TOV1);
        TIMSK = _BV(TOIE1);
    #else
        TIFR1 |= _BV(TOV1);
        TIMSK1 = _BV(TOIE1);
    #endif
    
    /* Timer 2 - GSCLK */
    #if defined(TLC_ATMEGA_8_H)
        TCCR2  = _BV(COM20)       // set on BOTTOM, clear on OCR2A (non-inverting),
               | _BV(WGM21);      // output on OC2B, CTC mode with OCR2 top
        OCR2   = TLC_GSCLK_PERIOD / 2; // see tlc_config.h
        TCCR2 |= _BV(CS20);       // no prescale, (start pwm output)
    #elif defined(TLC_TIMER3_GSCLK)
        TCCR3A = _BV(COM3A1)      // set on BOTTOM, clear on OCR3A (non-inverting),
                                  // output on OC3A
               | _BV(WGM31);      // Fast pwm with ICR3 top
        OCR3A = 0;                // duty factor (as short a pulse as possible)
        ICR3 = TLC_GSCLK_PERIOD;  // see tlc_config.h
        TCCR3B = _BV(CS30)        // no prescale, (start pwm output)
               | _BV(WGM32)       // Fast pwm with ICR3 top
               | _BV(WGM33);      // Fast pwm with ICR3 top
    #else
        TCCR2A = _BV(COM2B1)      // set on BOTTOM, clear on OCR2A (non-inverting),
                                  // output on OC2B
               | _BV(WGM21)       // Fast pwm with OCR2A top
               | _BV(WGM20);      // Fast pwm with OCR2A top
        TCCR2B = _BV(WGM22);      // Fast pwm with OCR2A top
        OCR2B = 0;                // duty factor (as short a pulse as possible)
        OCR2A = TLC_GSCLK_PERIOD; // see tlc_config.h
        TCCR2B |= _BV(CS20);      // no prescale, (start pwm output)
    #endif
    
    TCCR1B |= _BV(CS10);      // no prescale, (start pwm output)
}

/** Clears the grayscale data array, #tlc_GSData, but does not shift in any
    data.  This call should be followed by update() if you are turning off
    all the outputs. */
static void TlcMux_clear(void)
{
    for (uint8_t row = 0; row < NUM_ROWS; row++) {
        TlcMux_clearRow(row);
    }
}

static void TlcMux_clearRow(uint8_t row)
{
    uint8_t *rowp = tlcMux_GSData[row];
    uint8_t * const end = rowp + NUM_TLCS * 24;
    while (rowp < end) {
        *rowp++ = 0;
    }
}

/** Gets the current grayscale value for a channel
    \param channel (0 to #NUM_TLCS * 16 - 1).  OUT0 of the first TLC is
           channel 0, OUT0 of the next TLC is channel 16, etc.
    \returns current grayscale value (0 - 4095) for channel
    \see set */
static uint16_t TlcMux_get(uint8_t row, TLC_CHANNEL_TYPE channel)
{
    TLC_CHANNEL_TYPE index8 = (NUM_TLCS * 16 - 1) - channel;
    uint8_t *index12p = tlcMux_GSData[row] + ((((uint16_t)index8) * 3) >> 1);
    return (index8 & 1)? // starts in the middle
            (((uint16_t)(*index12p & 15)) << 8) | // upper 4 bits
            *(index12p + 1)                       // lower 8 bits
        : // starts clean
            (((uint16_t)(*index12p)) << 4) | // upper 8 bits
            ((*(index12p + 1) & 0xF0) >> 4); // lower 4 bits
    // that's probably the ugliest ternary operator I've ever created.
}

/** Sets channel to value in the grayscale data array, #tlc_GSData.
    \param channel (0 to #NUM_TLCS * 16 - 1).  OUT0 of the first TLC is
           channel 0, OUT0 of the next TLC is channel 16, etc.
    \param value (0-4095).  The grayscale value, 4095 is maximum.
    \see get */
static void TlcMux_set(uint8_t row, TLC_CHANNEL_TYPE channel, uint16_t value)
{
    TLC_CHANNEL_TYPE index8 = (NUM_TLCS * 16 - 1) - channel;
    uint8_t *index12p = tlcMux_GSData[row] + ((((uint16_t)index8) * 3) >> 1);
    if (index8 & 1) { // starts in the middle
                      // first 4 bits intact | 4 top bits of value
        *index12p = (*index12p & 0xF0) | (value >> 8);
                      // 8 lower bits of value
        *(++index12p) = value & 0xFF;
    } else { // starts clean
                      // 8 upper bits of value
        *(index12p++) = value >> 4;
                      // 4 lower bits of value | last 4 bits intact
        *index12p = ((uint8_t)(value << 4)) | (*index12p & 0xF);
    }
}

/** Sets all channels to value.
    \param value grayscale value (0 - 4095) */
static void TlcMux_setAll(uint16_t value)
{
    for (uint8_t row = 0; row < NUM_ROWS; row++) {
        TlcMux_setRow(row, value);
    }
}

static void TlcMux_setRow(uint8_t row, uint16_t value)
{
    uint8_t firstByte = value >> 4;
    uint8_t secondByte = (value << 4) | (value >> 8);
    uint8_t *p = tlcMux_GSData[row];
    uint8_t * const end = p + NUM_TLCS * 24;
    while (p < end) {
        *p++ = firstByte;
        *p++ = secondByte;
        *p++ = (uint8_t)value;
    }
}

static inline void TlcMux_shiftRow(uint8_t row)
{
    uint8_t *p = tlcMux_GSData[row];
    uint8_t * const end = p + NUM_TLCS * 16; // bb changed this from 24 to 16..  it seems like the number of bits, but was maybe the number of pins? 
  
 
   digitalWrite(rowLatchPin, 0); 
   
   // shift out the rgb data
    while (p < end) {      
        for (uint8_t bit = 0x80; bit; bit >>= 1) {
            if (bit & *p++) {
              digitalWrite(SIN_PIN, HIGH);
            } else {
              digitalWrite(SIN_PIN, LOW);
            }
           
              //shift row pin 
             byte currentValue = row & _BV(7 - bit);
             digitalWrite(rowDataPin, currentValue);
               
            // clock  
            digitalWrite(13,0);
            digitalWrite(13,1);      
        
          digitalWrite(rowDataPin, 0);
          
        }
    }

   
   digitalWrite(rowLatchPin, 1); 
  
 
} 

#endif

