/*
  CrunchLabs Synthesizer — Eye of the Tiger
  Touch the pad to play. Release to stop and reset to the beginning.
*/

#include "config.h"
#include "system_config.h"

#include <MozziConfigValues.h>
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#include <MozziGuts.h>
#include <mozzi_midi.h>
#include <Smooth.h>

#include "wavetables.h"

// ---------------------------------------------------------------------------
// Eye of the Tiger — main verse riff (C minor, ~109 BPM)
// Control rate = 128 Hz  →  16th ≈ 17 cycles, 8th ≈ 35, quarter ≈ 70, half ≈ 140
// 255 = rest (silence)
// ---------------------------------------------------------------------------
#define R 255

static const uint8_t SONG_NOTES[] PROGMEM = {
  72, 72, 72,  R, 70, 72,   // C5 C5 C5 [rest] Bb4 C5   — iconic three-hit opening
  70, 72, 68,               // Bb4 C5 Ab4               — first descent
  70, 72, 67,               // Bb4 C5 G4                — second descent
  65, 63,  R,               // F4 Eb4 [rest]            — phrase resolution
};

static const uint8_t SONG_DURATIONS[] PROGMEM = {
  17, 17, 17, 17, 17, 52,   // 16th 16th 16th 16th 16th dotted-8th
  17, 17, 70,               // 16th 16th quarter
  17, 17, 70,               // 16th 16th quarter
  70, 140, 35,              // quarter half 16th(gap)
};

#define SONG_LENGTH sizeof(SONG_NOTES)

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
uint16_t  botPot = 0;
uint16_t  hiPot  = 0;
int       yVal   = 0;
int       prox   = 0;

float     frequency1 = 0;
float     frequency2 = 0;
long      asig;

byte      gain    = 0;
byte      volume1 = 0;
byte      volume2 = 0;
byte      volume3 = 0;
byte      volume4 = 0;

uint8_t   waveSet = DEFAULT_WAVE;
uint16_t  gainSet = 0;
waveState wave2   = TRIANGLE;

uint8_t   songIndex   = 0;
uint16_t  songTimer   = 0;
bool      songPlaying = false;
bool      songRest    = false;

Smooth<int> kSmoothFreq1(SMOOTH_FREQ_NORMAL);
Smooth<int> kSmoothFreq2(SMOOTH_FREQ_NORMAL);
Smooth<int> kSmoothGain(SMOOTH_GAIN_FAST);
Smooth<int> kSmoothGain2(SMOOTH_GAIN_SLOW);

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
  startMozzi(CONTROL_RATE);
}

void loop() {
  audioHook();
}

// ---------------------------------------------------------------------------
// Touchpad
// ---------------------------------------------------------------------------
int ycoor() {
  pinMode(TOUCHPAD_Y1, OUTPUT);
  pinMode(TOUCHPAD_Y2, OUTPUT);
  pinMode(TOUCHPAD_X1, INPUT);
  pinMode(TOUCHPAD_X2, INPUT);
  digitalWrite(TOUCHPAD_Y1, HIGH);
  digitalWrite(TOUCHPAD_Y2, LOW);
  return mozziAnalogRead<ADC_RESOLUTION>(TOUCHPAD_X1);
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
void readInputs() {
  yVal   = constrain(ycoor(), TOUCHPAD_Y_MIN, TOUCHPAD_Y_MAX);
  botPot = mozziAnalogRead<ADC_RESOLUTION>(POT_PIN1);
  hiPot  = mozziAnalogRead<ADC_RESOLUTION>(POT_PIN3);
  prox   = digitalRead(PROX_PIN);

  waveSet = map(botPot, 0, ADC_MAX_VALUE, 0, WAVE_COUNT - 1);
  gainSet = map(hiPot,  0, ADC_MAX_VALUE, 0, MASTER_GAIN_MAX);
}

void setFrequencies() {
  switch (wave2) {
    case TRIANGLE:
      aSin1.setFreq(frequency1);  aSin2.setFreq(frequency2);
      aTri1.setFreq(frequency1);  aTri2.setFreq(frequency2);
      break;
    case SQUARE:
      aSin1.setFreq(frequency1);  aSin2.setFreq(frequency2);
      aSqu1.setFreq(frequency1);  aSqu2.setFreq(frequency2 * 2);
      break;
    case SAW:
      aSin1.setFreq(frequency1);  aSin2.setFreq(frequency2);
      aSaw1.setFreq(frequency1);  aSaw2.setFreq(frequency2);
      break;
    case SINE:
    default:
      aSin1.setFreq(frequency1);  aSin2.setFreq(frequency2);
      aSin3.setFreq(frequency1 * 2);  aSin4.setFreq(frequency2 * 2);
      break;
  }
}

void selectNotes() {
  if (yVal < TOUCHPAD_EDGE_DETECT) {
    if (!songPlaying) {
      songPlaying = true;
      songIndex   = 0;
      songTimer   = pgm_read_byte(&SONG_DURATIONS[0]);
    }

    uint8_t note = pgm_read_byte(&SONG_NOTES[songIndex]);
    if (note == R) {
      songRest = true;
    } else {
      songRest   = false;
      frequency1 = kSmoothFreq1.next(mtof(note));
      frequency2 = frequency1;
      wave2      = (waveState)waveSet;
      setFrequencies();
    }

    if (songTimer > 0) {
      songTimer--;
    } else {
      songIndex = (songIndex + 1) % SONG_LENGTH;
      songTimer = pgm_read_byte(&SONG_DURATIONS[songIndex]);
    }

  } else {
    songPlaying = false;
    songRest    = false;
    songIndex   = 0;
    songTimer   = 0;
  }
}

void setGains() {
  if (yVal < TOUCHPAD_EDGE_DETECT && !songRest) {
    gain    = kSmoothGain.next(prox == LOW ? gainSet >> PROX_GAIN_REDUCTION : gainSet);
    volume1 = gain;
    volume2 = gain;
    volume3 = gain;
    volume4 = gain;
  } else {
    volume1 = kSmoothGain2.next(0);
    volume2 = volume1;
    volume3 = volume1;
    volume4 = volume1;
  }
}

void updateControl() {
  readInputs();
  selectNotes();
  setGains();
}

// ---------------------------------------------------------------------------
// Audio output
// ---------------------------------------------------------------------------
AudioOutput_t updateAudio() {
  switch (wave2) {
    case TRIANGLE:
      asig = (long)aSin1.next() * volume1 + aSin2.next() * volume3 +
                   aTri1.next() * volume2 + aTri2.next() * volume4;
      break;
    case SQUARE:
      asig = (long)aSin1.next() * volume1 + aSin2.next() * volume3 +
                   aSqu1.next() * volume2 + aSqu2.next() * (volume4 >> 1);
      break;
    case SAW:
      asig = (long)aSin1.next() * volume1 + aSin2.next() * volume3 +
                   aSaw1.next() * volume2 + aSaw2.next() * volume4;
      break;
    case SINE:
    default:
      asig = (long)aSin1.next() * volume1 + aSin2.next() * volume3 +
                   aSin3.next() * (volume2 >> 1) + aSin4.next() * (volume4 >> 1);
      break;
  }
  return StereoOutput::fromAlmostNBit(AUDIO_OUTPUT_BITS, asig, asig).clip();
}
