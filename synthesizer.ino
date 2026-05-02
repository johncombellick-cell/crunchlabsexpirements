#pragma region README
/*
  ************************************************************************************
  *
  * STOCK SYNTHESIZER - Flexible scale system with clean architecture!
  *
  * Features:
  * • Flexible scale system with 12 scale types (Major, Minor, Pentatonic, Blues, Modal, etc.)
  * • Semantic control variables for easy remapping
  * • Clean modular function structure with comprehensive documentation
  * • Multiple waveforms (sin, saw, square, triangle)
  * • Smooth volume control with proximity sensor or LFO modulation
  * • Configurable parameters via config.h for easy customization
  * • Touch-triggered Ode to Joy melody playback
  *
  * CONTROLS:
  * • Touch pad: Triggers Ode to Joy melody (touch to play, release to stop)
  * • botPot (A5): Wave selection (0-3: Triangle, Square, Saw, Noise)
  * • hiPot (A7): Master volume control
  * • Proximity sensor: Reduces volume when triggered OR LFO modulation from fidget spinner
  *
  ************************************************************************************
  */
#pragma endregion README
#pragma region LICENSE
/*
  ************************************************************************************
  * MIT License
  *
  * Copyright (c) 2025 Crunchlabs LLC (Laser Synthesizer Code)

  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is furnished
  * to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in all
  * copies or substantial portions of the Software.
  *
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
  * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
  * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  *
  ************************************************************************************
*/
#pragma endregion LICENSE
//////////////////////////////////////////////////
//  INCLUDES AND CONFIGURATION //
//////////////////////////////////////////////////
#pragma region INCLUDES

// Configuration files
#include "config.h"
#include "system_config.h"

// Mozzi audio library setup
#include <MozziConfigValues.h>
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#include <MozziGuts.h>
#include <LowPassFilter.h>
#include <mozzi_rand.h>
#include <mozzi_midi.h>
#include <Smooth.h>

// Project-specific includes
#include "wavetables.h"      // All wavetable includes and oscillator declarations
#include "scale_tables.h"    // All scale table definitions

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLOBAL VARIABLES
#pragma region Global Variables

// === INPUT STATE VARIABLES ===
uint16_t botPot = 0;  // Wave selection potentiometer (POT_PIN1)
uint16_t midPot = 0;  // Second note control potentiometer (POT_PIN2)
uint16_t hiPot  = 0;  // Master volume potentiometer (POT_PIN3)

// === AUDIO ENGINE STATE ===
float targetNote1 = 0;
float targetNote2 = 0;
float frequency1  = 0;
float frequency2  = 0;
long  asig;

// === VOLUME AND GAIN CONTROL ===
int  volume  = 0;
byte gain    = 0;
byte volume1 = 0;
byte volume2 = 0;
byte volume3 = 0;
byte volume4 = 0;

// === TOUCHPAD AND CONTROL STATE ===
int  xVal, yVal;
int  xBuff, yBuff;
int  prox, proxBuff;
bool axis = false;

// === LFO SYSTEM ===
bool     lfoMode          = false;
uint16_t proxTriggerCount = 0;
uint16_t proxInterval     = 0;
float    spinnerRPM       = 0;
float    lfoBPM           = LFO_MIN_BPM;
float    lfoFreq          = 1.0;
uint16_t decayCounter     = 0;

uint8_t proxLowCount      = 0;
uint8_t proxHighCount     = 0;
bool    validNodeTrigger  = false;

// === SEMANTIC CONTROL VARIABLES ===
uint8_t  waveSet   = DEFAULT_WAVE;
uint8_t  noteSet1  = 0;
uint8_t  noteSet2  = 0;
uint16_t gainSet   = 0;
uint8_t  volumeSet2 = 0;

waveState wave2 = TRIANGLE;

// === NAME SPELLING: "LOGHANISC OOL" ===
// Each letter maps to a MIDI note using a chromatic scale starting at C3 (MIDI 48).
// A=48, B=49, C=50, D=51, E=52, F=53, G=54, H=55, I=56, J=57, K=58, L=59,
// M=60, N=61, O=62, P=63, Q=64, R=65, S=66, T=67, U=68, V=69, W=70, X=71,
// Y=72, Z=73
// Each letter creates a unique frequency and a distinct laser waveform pattern.
static const uint8_t NAME_NOTES[] PROGMEM = {
  59,  // L (position 11) = B3
  62,  // O (position 14) = D4
  54,  // G (position  6) = F#3
  55,  // H (position  7) = G3
  48,  // A (position  0) = C3
  61,  // N (position 13) = C#4
  56,  // I (position  8) = G#3
  66,  // S (position 18) = F#4
  50,  // C (position  2) = D3
  62,  // O (position 14) = D4
  62,  // O (position 14) = D4
  59   // L (position 11) = B3
};

#define NAME_LENGTH      12   // Letters in "LOGHANISC OOL"
// Each letter = exactly 0.5 s = 64 control cycles at 128 Hz.
// Split into 56 cycles of sound + 8 cycles of silence so letters are distinct.
#define NOTE_PLAY_CYCLES 56
#define NOTE_REST_CYCLES  8

// === NAME PLAYBACK STATE ===
uint8_t  nameIndex   = 0;     // Current letter in NAME_NOTES
uint16_t noteTimer   = 0;     // Counts 0 .. (NOTE_PLAY_CYCLES + NOTE_REST_CYCLES - 1)
bool     namePlaying = false; // True while finger is on pad
bool     noteRest    = false; // True during the silent gap between letters

#pragma endregion Global Variables

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// SMOOTH OBJECTS
#pragma region Smooth Objects
Smooth<int> kSmoothFreq1(SMOOTH_FREQ_NORMAL);
Smooth<int> kSmoothFreq2(SMOOTH_FREQ_NORMAL);
Smooth<int> kSmoothGain(SMOOTH_GAIN_FAST);
Smooth<int> kSmoothGain2(SMOOTH_GAIN_SLOW);
#pragma endregion Smooth Objects
#pragma endregion INCLUDES

//////////////////////////////////////////////////
//  S E T U P //
//////////////////////////////////////////////////
#pragma region SETUP
void setup(){
  startMozzi(CONTROL_RATE);
  generateScale(DEFAULT_SCALE_TYPE, DEFAULT_ROOT_NOTE);
  kLFO.setFreq(1.0f);

  #if USE_SERIAL
    Serial.begin(9600);
    Serial.println(F("CRUNCHLABS SYNTH - Ode to Joy on touch"));
  #endif
}
#pragma endregion SETUP


//////////////////////////////////////////////////
//  FUNCTIONS  //
//////////////////////////////////////////////////
#pragma region FUNCTIONS

#pragma region Control Helpers

void updateControl(){
  readInputs();
  selectNotes();
  setGains();
  proxBuff = prox;
}

//////////////////////////////////////////////////
//  L O O P //
//////////////////////////////////////////////////
#pragma region LOOP
void loop(){
  audioHook();
}
#pragma endregion LOOP

void readInputs() {
  if (axis){
    yVal = ycoor();
    yVal = constrain(yVal, TOUCHPAD_Y_MIN, TOUCHPAD_Y_MAX);
    axis = !axis;
  } else {
    int rawXVal = xcoor();
    xVal = constrain(rawXVal, TOUCHPAD_X_MIN, TOUCHPAD_X_MAX);
    if (rawXVal > TOUCHPAD_EDGE_DETECT) {
      xVal = rawXVal;
    }
    axis = !axis;
  }

  botPot = mozziAnalogRead<ADC_RESOLUTION>(POT_PIN1);
  midPot = mozziAnalogRead<ADC_RESOLUTION>(POT_PIN2);
  hiPot  = mozziAnalogRead<ADC_RESOLUTION>(POT_PIN3);

  prox = digitalRead(PROX_PIN);

  if (lfoMode) {
    updateSpinnerLFO();
  }

  waveSet   = map(botPot, 0, ADC_MAX_VALUE, 0, WAVE_COUNT - 1);
  gainSet   = map(hiPot,  0, ADC_MAX_VALUE, 0, MASTER_GAIN_MAX);
  volumeSet2 = xVal >> VOLUME_SHIFT_AMOUNT;
}

/**
 * @brief Spells "LOGHANISC OOL" on the laser when the pad is touched.
 *
 * Each letter plays its unique frequency for NOTE_PLAY_CYCLES control cycles,
 * then silences for NOTE_REST_CYCLES cycles, giving clear letter separation.
 * Total per letter = 64 cycles = exactly 0.5 s at CONTROL_RATE 128 Hz.
 * Releasing the pad resets to the first letter.
 */
void selectNotes() {
  if (yVal < TOUCHPAD_EDGE_DETECT) {  // Finger on pad
    if (!namePlaying) {
      namePlaying = true;
      nameIndex   = 0;
      noteTimer   = 0;
    }

    if (noteTimer < NOTE_PLAY_CYCLES) {
      // Sound phase: hold the current letter's frequency
      noteRest    = false;
      uint8_t midiNote = pgm_read_byte(&NAME_NOTES[nameIndex]);
      targetNote1 = mtof(midiNote);
      targetNote2 = targetNote1;
      frequency1  = kSmoothFreq1.next(targetNote1);
      frequency2  = kSmoothFreq2.next(targetNote2);
      wave2 = (waveState)waveSet;
      setFrequencies();
    } else {
      // Rest phase: brief silence between letters
      noteRest = true;
    }

    noteTimer++;
    if (noteTimer >= NOTE_PLAY_CYCLES + NOTE_REST_CYCLES) {
      noteTimer = 0;
      nameIndex = (nameIndex + 1) % NAME_LENGTH;
    }

  } else {
    // Finger off — reset to first letter for next touch
    namePlaying = false;
    noteRest    = false;
    nameIndex   = 0;
    noteTimer   = 0;
  }
}

void setGains() {
  if (yVal < TOUCHPAD_EDGE_DETECT && !noteRest) {
    if (lfoMode) {
      float lfoMod  = getLFOModulation();
      float lfoGain = gainSet * lfoMod;
      gain = kSmoothGain.next(constrain(lfoGain, 0, MASTER_GAIN_MAX));
    } else {
      if (prox == LOW){
        gain = kSmoothGain.next(gainSet >> PROX_GAIN_REDUCTION);
      } else {
        gain = kSmoothGain.next(gainSet);
      }
    }

    byte gainAdjusted = gain >> 2;

    volume1 = gain;
    volume2 = gain;
    volume3 = (volumeSet2 * gainAdjusted);
    volume4 = (volumeSet2 * gainAdjusted);
  } else {
    volume1 = kSmoothGain2.next(0);
    volume2 = volume1;
    volume3 = volume1;
    volume4 = volume1;
  }
}
#pragma endregion Control Helpers

//////////////////////////////////////////////////
//  TOUCHPAD INTERFACE FUNCTIONS //
//////////////////////////////////////////////////
#pragma region Touchpad Helpers

int ycoor(){
  pinMode(TOUCHPAD_Y1, OUTPUT);
  pinMode(TOUCHPAD_Y2, OUTPUT);
  pinMode(TOUCHPAD_X1, INPUT);
  pinMode(TOUCHPAD_X2, INPUT);
  digitalWrite(TOUCHPAD_Y1, HIGH);
  digitalWrite(TOUCHPAD_Y2, LOW);
  return mozziAnalogRead<ADC_RESOLUTION>(TOUCHPAD_X1);
}

int xcoor(){
  pinMode(TOUCHPAD_X1, OUTPUT);
  pinMode(TOUCHPAD_X2, OUTPUT);
  pinMode(TOUCHPAD_Y1, INPUT);
  pinMode(TOUCHPAD_Y2, INPUT);
  digitalWrite(TOUCHPAD_X1, LOW);
  digitalWrite(TOUCHPAD_X2, HIGH);
  return mozziAnalogRead<ADC_RESOLUTION>(TOUCHPAD_Y1);
}

#pragma endregion Touchpad Helpers

//////////////////////////////////////////////////
//  LFO SYSTEM - FIDGET SPINNER INTEGRATION //
//////////////////////////////////////////////////
#pragma region LFO System

void updateSpinnerLFO() {
  static bool lastValidNodeState = false;
  static uint16_t lastTriggerTime = 0;
  bool currentProxState = (prox == LOW);

  proxTriggerCount++;
  decayCounter++;

  if (currentProxState) {
    proxLowCount++;
    proxHighCount = 0;
    if (proxLowCount >= LFO_MIN_NODE_WIDTH) {
      validNodeTrigger = true;
    }
  } else {
    proxHighCount++;
    proxLowCount = 0;
    if (proxHighCount >= LFO_MIN_NODE_WIDTH) {
      validNodeTrigger = false;
    }
  }

  if (validNodeTrigger && !lastValidNodeState) {
    if (lastTriggerTime > 0) {
      proxInterval = proxTriggerCount - lastTriggerTime;
      float intervalMs = (proxInterval * 1000.0f) / CONTROL_RATE;

      if (intervalMs > 50 && intervalMs < 5000) {
        spinnerRPM = 60000.0f / (intervalMs * LFO_SPINNER_NODES);
        lfoBPM = constrain(spinnerRPM * LFO_BPM_MULTIPLIER, LFO_MIN_BPM, LFO_MAX_BPM);
        lfoFreq = lfoBPM / 60.0f;
        kLFO.setFreq(lfoFreq);
        decayCounter = 0;
      }
    }
    lastTriggerTime = proxTriggerCount;
  }

  lastValidNodeState = validNodeTrigger;

  if (decayCounter > LFO_DECAY_TIMEOUT) {
    lfoBPM = lfoBPM * LFO_DECAY_RATE;
    if (lfoBPM < LFO_MIN_BPM) lfoBPM = LFO_MIN_BPM;
    lfoFreq = lfoBPM / 60.0f;
    kLFO.setFreq(lfoFreq);
    decayCounter = LFO_DECAY_TIMEOUT - 16;
  }
}

float getLFOModulation() {
  int lfoValue = kLFO.next();
  float lfoNormalized = lfoValue / 127.0f;
  return 1.0f + (lfoNormalized * 0.5f);
}
#pragma endregion LFO System

void generateScale(uint8_t scaleIndex, uint8_t root) {
  scaleIndex = constrain(scaleIndex, 0, NUM_SCALES - 1);

  currentScale = scaleIndex;
  rootNote     = root;
  scaleLength  = scaleLengths[scaleIndex];

  const uint8_t* intervals = scaleTypes[scaleIndex];

  uint8_t noteIndex = 0;
  for (uint8_t octave = 0; octave < SCALE_OCTAVES; octave++) {
    for (uint8_t i = 0; i < scaleLength; i++) {
      if (noteIndex < MAX_SCALE_NOTES) {
        generatedScale[noteIndex] = root + intervals[i] + (octave * OCTAVE_SEMITONES);
        noteIndex++;
      }
    }
  }

  #if USE_SERIAL
    if (Serial) {
      Serial.print(F("Scale: "));
      Serial.print(scaleNames[scaleIndex]);
      Serial.print(F(" root="));
      Serial.println(root);
    }
  #endif
}

//////////////////////////////////////////////////
//  A U D I O   O U T P U T //
//////////////////////////////////////////////////
#pragma region Audio Output

void setFrequencies(){
  switch (wave2) {
   case TRIANGLE:
      aSin1.setFreq(frequency1);
      aSin2.setFreq(frequency2);
      aTri1.setFreq(frequency1);
      aTri2.setFreq(frequency2);
     break;
   case SQUARE:
      aSin1.setFreq(frequency1);
      aSin2.setFreq(frequency2);
      aSqu1.setFreq(frequency1);
      aSqu2.setFreq(frequency2 * 2);
     break;
   case SAW:
      aSin1.setFreq(frequency1);
      aSin2.setFreq(frequency2);
      aSaw1.setFreq(frequency1);
      aSaw2.setFreq(frequency2);
     break;
   case SINE:
   default:
      aSin1.setFreq(frequency1);
      aSin2.setFreq(frequency2);
      aSin3.setFreq(frequency1 * 2);
      aSin4.setFreq(frequency2 * 2);
    break;
 }
}

AudioOutput_t updateAudio(){
  switch (wave2) {
   case TRIANGLE:
    asig = (long)
      aSin1.next() * volume1 +
      aSin2.next() * volume3 +
      aTri1.next() * volume2 +
      aTri2.next() * volume4;
     break;
   case SQUARE:
    asig = (long)
      aSin1.next() * volume1 +
      aSin2.next() * volume3 +
      aSqu1.next() * volume2 +
      aSqu2.next() * (volume4 >> 1);
     break;
   case SAW:
    asig = (long)
      aSin1.next() * volume1 +
      aSin2.next() * volume3 +
      aSaw1.next() * volume2 +
      aSaw2.next() * volume4;
     break;
   case SINE:
   default:
      asig = (long)
      aSin1.next() * volume1 +
      aSin2.next() * volume3 +
      aSin3.next() * (volume2 >> 1) +
      aSin4.next() * (volume4 >> 1);
     break;
 }
  return StereoOutput::fromAlmostNBit(AUDIO_OUTPUT_BITS, asig, asig).clip();
}
#pragma endregion Audio Output
#pragma endregion FUNCTIONS

//////////////////////////////////////////////////
//  E N D   C O D E  //
//////////////////////////////////////////////////
