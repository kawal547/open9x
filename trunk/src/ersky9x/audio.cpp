/*
 * Authors (alphabetical order)
 * - Bertrand Songis <bsongis@gmail.com>
 * - Bryan J. Rentoul (Gruvin) <gruvin@gmail.com>
 * - Cameron Weeks <th9xer@gmail.com>
 * - Erez Raviv
 * - Jean-Pierre Parisy
 * - Karl Szmutny <shadow@privy.de>
 * - Michael Blandford
 * - Michal Hlavinka
 * - Pat Mackenzie
 * - Philip Moss
 * - Rob Thomson
 * - Romolo Manfredini <romolo.manfredini@gmail.com>
 * - Thomas Husterer
 *
 * open9x is based on code named
 * gruvin9x by Bryan J. Rentoul: http://code.google.com/p/gruvin9x/,
 * er9x by Erez Raviv: http://code.google.com/p/er9x/,
 * and the original (and ongoing) project by
 * Thomas Husterer, th9x: http://code.google.com/p/th9x/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "../open9x.h"

const char * audioFilenames[] = {
  "inactiv",
  "lowbatt",
  "error",
  "keyup",
  "keydown",
  "menus",
  "trim",
  "warning1",
  "warning2",
  "warning3",
  "midtrim",
  "endtrim",
  "tada",
  "midpot",
  "mixwarn1",
  "mixwarn2",
  "mixwarn3",
  "timerlt3",
  "timer10",
  "timer20",
  "timer30"
#if 0
  AU_FRSKY_WARN1 = AU_FRSKY_FIRST,
  AU_FRSKY_WARN2,
  AU_FRSKY_CHEEP,
  AU_FRSKY_RING,
  AU_FRSKY_SCIFI,
  AU_FRSKY_ROBOT,
  AU_FRSKY_CHIRP,
  AU_FRSKY_TADA,
  AU_FRSKY_CRICKET,
  AU_FRSKY_SIREN,
  AU_FRSKY_ALARMC,
  AU_FRSKY_RATATA,
  AU_FRSKY_TICK,
#endif
};

uint32_t sdAvailableAudioFiles;

#if defined(SDCARD)
void retrieveAvailableAudioFiles()
{
  FILINFO info;
  char filename[32] = SYSTEM_SOUNDS_PATH "/";

  assert(sizeof(audioFilenames)==AU_FRSKY_FIRST*sizeof(char *));
  assert(sizeof(sdAvailableAudioFiles)*8 > AU_FRSKY_FIRST);

  sdAvailableAudioFiles = 0;

  for (uint32_t i=0; i<AU_FRSKY_FIRST; i++) {
    strcpy(filename+sizeof(SYSTEM_SOUNDS_PATH), audioFilenames[i]);
    strcat(filename+sizeof(SYSTEM_SOUNDS_PATH), SOUNDS_EXT);
    if (f_stat(filename, &info) == FR_OK)
      sdAvailableAudioFiles |= ((uint32_t)1 << i);
  }
}

inline bool isAudioFileAvailable(uint8_t i, char * filename)
{
  if (sdAvailableAudioFiles & ((uint32_t)1 << i)) {
    strcpy(filename+sizeof(SYSTEM_SOUNDS_PATH), audioFilenames[i]);
    strcat(filename+sizeof(SYSTEM_SOUNDS_PATH), SOUNDS_EXT);
    return true;
  }
  else {
    return false;
  }
}
#else
#define isAudioFileAvailable(i, f) false
#endif

uint16_t alawTable[256];

#define         SIGN_BIT        (0x80)      /* Sign bit for a A-law byte. */
#define         QUANT_MASK      (0xf)       /* Quantization field mask. */
#define         SEG_SHIFT       (4)         /* Left shift for segment number. */
#define         SEG_MASK        (0x70)      /* Segment field mask. */

static short alaw2linear(unsigned char a_val)
{
  int t;
  int seg;

  a_val ^= 0x55;

  t = a_val & QUANT_MASK;
  seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
  if(seg) t= (t + t + 1 + 32) << (seg + 2);
  else    t= (t + t + 1     ) << 3;

  return (a_val & SIGN_BIT) ? t : -t;
}

void alawInit()
{
  for (uint32_t i=0; i<256; i++)
    alawTable[i] = (0x8000 + alaw2linear(i)) >> 4;
}

uint8_t audioState = 0;

uint8_t t_queueRidx;
uint8_t t_queueWidx;

uint8_t toneFreq;
int8_t toneFreqIncr;
uint8_t toneTimeLeft;
uint8_t tonePause;

uint8_t tone2Freq;
uint8_t tone2TimeLeft;
uint8_t tone2Pause;

char toneWavFile[32+1] = "";

// queue arrays
uint8_t queueToneFreq[AUDIO_QUEUE_LENGTH];
int8_t queueToneFreqIncr[AUDIO_QUEUE_LENGTH];
uint8_t queueToneLength[AUDIO_QUEUE_LENGTH];
uint8_t queueTonePause[AUDIO_QUEUE_LENGTH];
uint8_t queueToneRepeat[AUDIO_QUEUE_LENGTH];

void audioInit()
{
  toneTimeLeft = 0;
  tonePause = 0;

  t_queueRidx = 0;
  t_queueWidx = 0;
}

void audioTimerHandle(void)
{
  CoSetFlag(audioFlag);
}

// TODO Should be here!
extern uint16_t Sine_values[];

#define WAV_HEADER_SIZE 44

#define CODEC_ID_PCM_S16LE  1
#define CODEC_ID_PCM_ALAW   6
uint8_t pcmCodec;

void audioTask(void* pdata)
{
#if defined(SDCARD) && !defined(SIMU)
  static FIL wavFile;
#endif

  while (1) {
    CoWaitForSingleFlag(audioFlag, 0);

    audioState = 1; // TODO #define

#if defined(SDCARD) && !defined(SIMU)
    if (toneWavFile[0]) {
      FRESULT result = FR_OK;
      UINT read = 0;
      uint16_t * bufdata = wavSamplesBuffer;
      if (toneWavFile[1]) {
        result = f_open(&wavFile, toneWavFile, FA_OPEN_EXISTING | FA_READ);
        if (result == FR_OK) {
          result = f_read(&wavFile, (uint8_t *)wavSamplesArray, WAV_HEADER_SIZE, &read);
          if (result == FR_OK && read == WAV_HEADER_SIZE && !memcmp(wavSamplesArray, "RIFF", 4)) {
            CoSetFlag(audioFlag);
            bufdata = wavSamplesArray;
            wavSamplesBuffer = wavSamplesArray + WAV_BUFFER_SIZE;
            pcmCodec = wavSamplesArray[10];
            setFrequency(wavSamplesArray[12]);
            if (pcmCodec != CODEC_ID_PCM_S16LE) {
              result = f_read(&wavFile, (uint8_t *)wavSamplesArray, 12, &read);
            }
          }
          else {
            result = FR_DENIED;
          }
        }
      }

      read = 0;
      uint16_t bufsize = (pcmCodec == CODEC_ID_PCM_S16LE ? 2*WAV_BUFFER_SIZE : WAV_BUFFER_SIZE);
      if (result != FR_OK || f_read(&wavFile, (uint8_t *)bufdata, bufsize, &read) != FR_OK || read != bufsize) {
        DACC->DACC_TNCR = read/4;
        toneStop();
        toneWavFile[0] = '\0';
        toneWavFile[1] = '\0';
        f_close(&wavFile);
        audioState = 0;
      }

      if (pcmCodec == CODEC_ID_PCM_S16LE) {
        read /= 2;
        uint32_t i = 0;
        for (; i<read; i++)
          bufdata[i] = ((uint16_t)0x8000 + ((int16_t)(bufdata[i]))) >> 4;
        for (; i<WAV_BUFFER_SIZE; i++)
          bufdata[i] = 0x8000;
      }
      else if (pcmCodec == CODEC_ID_PCM_ALAW) {
        int32_t i;
        for (i=read-1; i>=0; i--)
          bufdata[i] = alawTable[((uint8_t *)bufdata)[i]];
        for (i=read; i<WAV_BUFFER_SIZE; i++)
          bufdata[i] = 0x8000;
      }

      if (toneWavFile[1]) {
        toneWavFile[1] = '\0';
        register Dacc *dacptr = DACC;
        dacptr->DACC_TPR = CONVERT_PTR(wavSamplesArray);
        dacptr->DACC_TNPR = CONVERT_PTR(wavSamplesBuffer);
        dacptr->DACC_TCR = WAV_BUFFER_SIZE/2;
        dacptr->DACC_TNCR = WAV_BUFFER_SIZE/2;
        toneStart();
      }
    }
    else
#endif
    if (toneTimeLeft > 0) {
      // TODO function for that ...
      setFrequency(toneFreq * 6100 / 2);
      if (toneFreqIncr) {
        CoSetTmrCnt(audioTimer, 5/*10ms*/, 0);
        toneFreq += toneFreqIncr;
        toneTimeLeft--;
      }
      else {
        CoSetTmrCnt(audioTimer, toneTimeLeft*5, 0);
        toneTimeLeft = 0;
      }
      toneStart();
      CoStartTmr(audioTimer);
    }
    else if (tonePause > 0) {
      CoSetTmrCnt(audioTimer, tonePause*5, 0);
      tonePause = 0;
      toneStop();
      CoStartTmr(audioTimer);
    }
    else if (t_queueRidx != t_queueWidx) {
      toneFreq = queueToneFreq[t_queueRidx];
      toneTimeLeft = queueToneLength[t_queueRidx];
      toneFreqIncr = queueToneFreqIncr[t_queueRidx];
      tonePause = queueTonePause[t_queueRidx];
      if (!queueToneRepeat[t_queueRidx]--) {
        t_queueRidx = (t_queueRidx + 1) % AUDIO_QUEUE_LENGTH;
      }
      CoSetFlag(audioFlag);
    }
    else if (tone2TimeLeft > 0) {
      CoSetTmrCnt(audioTimer, tone2TimeLeft*5, 0);
      tone2TimeLeft = 0;
      setFrequency(tone2Freq * 6100 / 2);
      toneStart();
      CoStartTmr(audioTimer);
    }
    else {
      audioState = 0;
      toneStop();
    }
  }
}

inline uint8_t getToneLength(uint8_t tLen)
{
  uint8_t result = tLen; // default
  if (g_eeGeneral.beeperLength < 0) {
    result /= (1-g_eeGeneral.beeperLength);
  }
  if (g_eeGeneral.beeperLength > 0) {
    result *= (1+g_eeGeneral.beeperLength);
  }
  return result;
}

void pause(uint8_t tLen)
{
  play(0, 0, tLen); // a pause
}	

extern OS_MutexID audioMutex;

void play(uint8_t tFreq, uint8_t tLen, uint8_t tPause,
    uint8_t tFlags, int8_t tFreqIncr)
{
  CoEnterMutexSection(audioMutex);

  if (tFlags & PLAY_SOUND_VARIO) {
    tone2Freq = tFreq;
    tone2TimeLeft = tLen;
    tone2Pause = tPause;
    if (audioState == 0) {
      audioState = 1;
      CoSetFlag(audioFlag);
    }
  }
  else {
    if (tFreq > 0) { //we dont add pitch if zero as this is a pause only event
      tFreq += g_eeGeneral.speakerPitch + BEEP_OFFSET; // add pitch compensator
    }
    tLen = getToneLength(tLen);
    if ((tFlags & PLAY_NOW) || !audioBusy()) {
      toneWavFile[0] = '\0';
      toneFreq = tFreq;
      toneTimeLeft = tLen;
      tonePause = tPause;
      toneFreqIncr = tFreqIncr;
      t_queueWidx = t_queueRidx;
      audioState = 1;
      CoSetFlag(audioFlag);
    }
    else {
      tFlags++;
    }

    tFlags &= 0x0f;
    if (tFlags) {
      uint8_t next_queueWidx = (t_queueWidx + 1) % AUDIO_QUEUE_LENGTH;
      if (next_queueWidx != t_queueRidx) {
        queueToneFreq[t_queueWidx] = tFreq;
        queueToneLength[t_queueWidx] = tLen;
        queueTonePause[t_queueWidx] = tPause;
        queueToneRepeat[t_queueWidx] = tFlags - 1;
        queueToneFreqIncr[t_queueWidx] = tFreqIncr;
        t_queueWidx = next_queueWidx;
      }
    }
  }

  CoLeaveMutexSection(audioMutex);
}

void playFile(const char *filename)
{
  CoEnterMutexSection(audioMutex);
  strcpy(toneWavFile, filename);
  CoSetFlag(audioFlag);
  CoLeaveMutexSection(audioMutex);
}

void audioEvent(uint8_t e, uint8_t f)
{
  char filename[32] = SYSTEM_SOUNDS_PATH "/";

#ifdef HAPTIC
  haptic.event(e); //do this before audio to help sync timings
#endif

  if (g_eeGeneral.flashBeep && (e <= AU_ERROR || e >= AU_WARNING1)) {
    if (g_LightOffCounter < FLASH_DURATION)
      g_LightOffCounter = FLASH_DURATION;
  }

  if (g_eeGeneral.beeperMode>0 || (g_eeGeneral.beeperMode==0 && e>=AU_TRIM_MOVE) || (g_eeGeneral.beeperMode>=-1 && e<=AU_ERROR)) {
    if (e < AU_FRSKY_FIRST && isAudioFileAvailable(e, filename)) {
      playFile(filename);
    }
    else if (e < AU_FRSKY_FIRST || !audioBusy()) {
      switch (e) {
        // inactivity timer alert
        case AU_INACTIVITY:
          play(70, 20, 4, 2|PLAY_NOW);
          break;
        // low battery in tx
        case AU_TX_BATTERY_LOW:
          if (!audioBusy()) {
            play(60, 40, 6, 2, 1);
            play(80, 40, 6, 2, -1);
          }
          break;
        // error
        case AU_ERROR:
          play(BEEP_DEFAULT_FREQ, 50, 2, PLAY_NOW);
          break;
        // keypad up (seems to be used when going left/right through system menu options. 0-100 scales etc)
        case AU_KEYPAD_UP:
          play(BEEP_KEY_UP_FREQ, 20, 2, PLAY_NOW);
          break;
        // keypad down (seems to be used when going left/right through system menu options. 0-100 scales etc)
        case AU_KEYPAD_DOWN:
          play(BEEP_KEY_DOWN_FREQ, 20, 2, PLAY_NOW);
          break;
        // menu display (also used by a few generic beeps)
        case AU_MENUS:
          play(BEEP_DEFAULT_FREQ, 20, 4, PLAY_NOW);
          break;
        // trim move
        case AU_TRIM_MOVE:
          play(f, 12, 2, PLAY_NOW);
          break;
        // trim center
        case AU_TRIM_MIDDLE:
          play(f, 20, 4, PLAY_NOW);
          break;
        // trim center
        case AU_TRIM_END:
          play(f, 20, 4, PLAY_NOW);
          break;          
        // warning one
        case AU_WARNING1:
          play(BEEP_DEFAULT_FREQ, 20, 2, PLAY_NOW);
          break;
        // warning two
        case AU_WARNING2:
          play(BEEP_DEFAULT_FREQ, 40, 2, PLAY_NOW);
          break;
        // warning three
        case AU_WARNING3:
          play(BEEP_DEFAULT_FREQ, 50, 2, PLAY_NOW);
          break;
        // startup tune
        case AU_TADA:
          play(50, 20, 10);
          play(90, 20, 10);
          play(110, 10, 8, 2);
          break;
        // pot/stick center
        case AU_POT_STICK_MIDDLE:
          play(BEEP_DEFAULT_FREQ + 50, 20, 2, PLAY_NOW);
          break;
        // mix warning 1
        case AU_MIX_WARNING_1:
          play(BEEP_DEFAULT_FREQ + 50, 12, 0, PLAY_NOW);
          break;
        // mix warning 2
        case AU_MIX_WARNING_2:
          play(BEEP_DEFAULT_FREQ + 52, 12, 0, PLAY_NOW);
          break;
        // mix warning 3
        case AU_MIX_WARNING_3:
          play(BEEP_DEFAULT_FREQ + 54, 12, 0, PLAY_NOW);
          break;
        // time 30 seconds left
        case AU_TIMER_30:
          play(BEEP_DEFAULT_FREQ + 50, 30, 6, 2|PLAY_NOW);
          break;
        // time 20 seconds left
        case AU_TIMER_20:
          play(BEEP_DEFAULT_FREQ + 50, 30, 6, 1|PLAY_NOW);
          break;
        // time 10 seconds left
        case AU_TIMER_10:
          play(BEEP_DEFAULT_FREQ + 50, 30, 6, PLAY_NOW);
          break;
        // time <3 seconds left
        case AU_TIMER_LT3:
          play(BEEP_DEFAULT_FREQ + 50, 30, 6, PLAY_NOW);
          break;
        case AU_FRSKY_WARN1:
          play(BEEP_DEFAULT_FREQ+20,30,10,2);
          pause(200);
          break;
        case AU_FRSKY_WARN2:
          play(BEEP_DEFAULT_FREQ+30,30,10,2);
          pause(200);
          break;
        case AU_FRSKY_CHEEP:
          play(BEEP_DEFAULT_FREQ+30,20,4,2,2);
          pause(200);
          break;
        case AU_FRSKY_RING:
          play(BEEP_DEFAULT_FREQ+25,10,4,10);
          play(BEEP_DEFAULT_FREQ+25,10,20,1);
          play(BEEP_DEFAULT_FREQ+25,10,4,10);
          pause(200);
          break;
        case AU_FRSKY_SCIFI:
          play(80,20,6,2,-1);
          play(60,20,6,2,1);
          play(70,20,2,0);
          pause(200);
          break;
        case AU_FRSKY_ROBOT:
          play(70,10,2,1);
          play(50,30,4,1);
          play(80,30,4,1);
          pause(200);
          break;
        case AU_FRSKY_CHIRP:
          play(BEEP_DEFAULT_FREQ+40,10,2,2);
          play(BEEP_DEFAULT_FREQ+54,10,2,3);
          pause(200);
          break;
        case AU_FRSKY_TADA:
          play(50,10,10);
          play(90,10,10);
          play(110,6,8,2);
          pause(200);
          break;
        case AU_FRSKY_CRICKET:
          play(80,10,20,3);
          play(80,10,40,1);
          play(80,10,20,3);
          pause(200);
          break;
        case AU_FRSKY_SIREN:
          play(10,40,10,2,1);
          pause(200);
          break;
        case AU_FRSKY_ALARMC:
          play(50,8,20,2);
          play(70,16,40,1);
          play(50,16,20,2);
          play(70,8,40,1);
          pause(200);
          break;
        case AU_FRSKY_RATATA:
          play(BEEP_DEFAULT_FREQ+50,10,20,10);
          break;
        case AU_FRSKY_TICK:
          play(BEEP_DEFAULT_FREQ+50,10,100,2);
          pause(200);
          break;
        default:
          break;
      }
    }
  }
}
