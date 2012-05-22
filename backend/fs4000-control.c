/* sane - Scanner Access Now Easy.

   Copyright (C) 2012 Andrew McDonnell bugs@andrewmcdonnell.net
   Parts copyright (C) 2004-2007 Steven Saunderson
   Parts copyright (C) 2004 Dave Burns
   
   This SANE backend leverages code originally developed by 
   Steven Saunderson, http://home.exetel.com.au/phelum/fs4.htm, 
   a Windows-based FS4000 calibration, thumbnail and scanning program, and
   Dave Burns, http://www.burnsorama.com/fs4000/ .

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.

   This file implements higher level algorithms for scanning and tuning with
   Canon FS4000 Slide Scanners.
   Currently only the USB interface is implemented.  */
#include "../include/sane/config.h"

#include "fs4000.h"
#include "fs4000-control.h"
#include "fs4000-scsi.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define FS4000_CONFIG_FILE "fs4000.conf"

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_scsi.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"

#define MAX_MSG 256
#define MAX_WARN 256

struct scanner
{
  /* Latest state */
  SANE_Int lastError;
  
  BOOL            bShowProgress       ; /* = TRUE; */
  BOOL            bStepping           ; /* = FALSE; */
  BOOL            bTesting            ; /* = FALSE; */
  BOOL            bNegMode            ; /* = FALSE; */
  BOOL            bAutoExp            ; /* = FALSE; */
  BOOL            bSaveRaw            ; /* = FALSE; */
  BOOL            bMakeTiff           ; /* = TRUE; */
  BOOL            bUseHelper          ; /* = FALSE; */

  BOOL            bDisableShutters    ; /* = FALSE; */

  int             iAGain    [3]       ; /* = {  47,   36,   36}; */
  int             iAOffset  [3]       ; /* = { -25,   -8,   -5}; */
  int             iShutter  [3]       ; /* = { 750,  352,  235}; */
  int             iInMode             ; /* = 14; */
  int             iBoost    [3]       ; /* = { 256,  256,  256}; */
  int             iSpeed              ; /* = 2; */
  int             iFrame              ; /* = 0; */
  int             ifs4000_debug       ; /* = 0; */
  int             iMaxShutter         ; /* = 890; */
  int             iAutoExp            ; /* = 2; */
  int             iMargin             ; /* = 120; */

  FS4000_GET_LAMP_DATA_IN rLI;
  FS4000_GET_FILM_STATUS_DATA_IN_28 rFS;
  FS4000_SCAN_MODE_INFO rSMI;
  
  /* working buffers */
  char msg[MAX_MSG+1];
  char warn[MAX_WARN+1];
  
  /* Callback methods */  
  void (*feedbackFunction)(const char *);
  int (*abortFunction)(void);
} ;

static void fs4k_sleep(int milliseconds)
{
  /* TODO : make this posix thread interurptible somehow */
  usleep(milliseconds * 1000);
}

/* This should write to the tty if stdout redirected to a file ... */
static void fs4k_Feedback(struct scanner* s)
{
  s->msg[MAX_MSG] = 0;
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->msg);
}

static void fs4k_NewsTask(struct scanner* s)
{
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->msg);
}

/* For now is same as task, fix up later */
static void fs4k_NewsStep(struct scanner* s, const char *text)
{
  snprintf( s->msg, MAX_MSG, text);
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->msg);
}

static void fs4k_Warning(struct scanner *s, const char *text)
{
  snprintf( s->warn, MAX_WARN, text);
  if (s->feedbackFunction)
    (s->feedbackFunction)(text);
}

/* Equivalent to AbortWanted()  - return 0 to not abort */
static int fs4k_checkAbort(const struct scanner* s)
{
  if (s->abortFunction) 
    return (s->abortFunction)();
  return 1;
}

/* Equivalent to FS4_Halt() - return 0 to not halt */
static int fs4k_Halt(const struct scanner *s)
{
  if (s->abortFunction) 
    return (s->abortFunction)();
  return 0;
}

void fs4k_init(struct scanner **s) 
{
  *s = malloc(sizeof(struct scanner));
  memset( *s, 0, sizeof(struct scanner));
}

void fs4k_destroy(struct scanner *s) 
{
  free(s);
}

void fs4k_SetFeedbackFunction( struct scanner *s, void (*f)(const char *))
{
  s->feedbackFunction = f;
}

void fs4k_SetAbortFunction( struct scanner *s, int (*f)(void))
{
  s->abortFunction = f;
}


int fs4k_GetLastError(const struct scanner* s)
{
  return s->lastError;
}

int fs4k_LampTest(struct scanner* s)
{
  s->lastError = fs4000_get_lamp_rec (&s->rLI);
  if (s->rLI.is_visible_lamp_on)
    return s->rLI.visible_lamp_duration;
  return -1;
}

int fs4k_LampOff(struct scanner* s, int iOffSecs)
{
  s->lastError = fs4000_set_lamp (0, 0);
  if (iOffSecs > 0) {
    fs4k_sleep(iOffSecs * 1000);
  }
  return 0;
}

int fs4k_LampOn(struct scanner* s, int iOnSecs)
{
  int abort=0;
  do {
    if ( (s->lastError = fs4000_set_lamp (1, 0))) 
      return -1;
    if ( (s->lastError = fs4000_get_lamp_rec(&s->rLI))) 
      return -1;
    if ((int) s->rLI.visible_lamp_duration >= iOnSecs)
      break;
    snprintf (s->msg, MAX_MSG, "Waiting for lamp (%d)  \r", iOnSecs - s->rLI.visible_lamp_duration);
    fs4k_NewsStep(s, s->msg);
    if ((abort=fs4k_checkAbort(s))) 
      break;
    fs4k_sleep (500);
  } while (!(abort=fs4k_checkAbort(s)));
  
  return abort ? -1 : 0;
}


/** Return config byte for ScanModeData */
static int fs4k_U24(const struct scanner *s)
{
  if (s->iInMode == 8)
    return 0x03;
  if (s->iInMode == 16)
    return 0x02;
  return 0x00;
}

/** Return analogue offset in AD9814 format */
static int fs4k_AOffset(int iOffset)
{
  if (iOffset < -255)
    iOffset   = -255;
  if (iOffset >  255)
    iOffset   =  255;
  if (iOffset <    0)
    iOffset = 256 - iOffset;
  return iOffset;
}

/** Return bits/sample for window */
static int fs4k_WBPS(const struct scanner *s)
{
  return s->iInMode;
}



/*--------------------------------------------------------------

This comment retained verbatim for historical reference, 
from original Fs4000.cpp file by Steven Saunderson.

                Scan a frame

        'Fs4000.exe tune out8 ae scan 1'

For best scan quality :-
a) Gamma table is now linear from 0 to 32 (0 to 8 in 8-bit output).
b) 14-bit input is now converted to 16-bit in read stage.
c) Setting reads to 16-bit (rather than 14) reduces chance of gappy histogram.
d) General boost now reduces scan speed to achieve effect as far as possible
   and then adjusts multipliers to achieve final effect.
e) Individual boost (R or G or B) uses multiplier to achieve effect.

We use gamma 2.2 for 8-bit output as it is *the* standard.  Gamma 1.5 would be
better given the realistic Dmax (about 3) of the scanner.

It is better to achieve exposure and colour balance by specifying boosts here
rather than adjusting the scan file in Photoshop, etc.

The holder offsets listed below for each frame are all 36 more than the
offset of the frame in a thumbnail view.  This is probably because the
carriage advances a bit (past the home sensor) before the scan data
collection starts.

--------------------------------------------------------------*/

static int fs4k_SetFrame(struct scanner* s, int iSetFrame)
{
  if ( (s->lastError = fs4000_set_frame (iSetFrame))) 
    return -1;
  s->iFrame = iSetFrame;
  return 0;
}

static int fs4k_SetScanModeEx(struct scanner *s, BYTE bySpeed, 
        BYTE byUnk2_4, int iUnk6)
{
  int     x;

  fs4000_get_scan_mode_rec (&s->rSMI.scsi);
  s->rSMI.control.bySpeed = bySpeed;
  s->rSMI.control.bySampleMods = byUnk2_4;               /* 0-3 valid, 0 is bad if 8-bit */
  if (s->iMargin == 0)
    s->rSMI.control.bySampleMods |= 0x20;                /* stops margin */
  for (x = 0; x < 3; x++)
    {
    s->rSMI.control.byAGain  [x] = s->iAGain   [x];       /* analogue gain (AD9814) */
    s->rSMI.control.wAOffset [x] = fs4k_AOffset (s->iAOffset [x]); /* analogue offset */
    s->rSMI.control.wShutter [x] = s->iShutter [x];       /* CCD shutter pulse width */
    if (s->bDisableShutters)                     /* if special for tuning, */
      s->rSMI.control.wShutter [x] = 0;
    }
  if (iUnk6 >= 0)
    s->rSMI.control.byImageMods = iUnk6;
  return fs4000_put_scan_mode_rec (&s->rSMI.scsi);
}

static int fs4k_release(struct scanner *s, int code)
{
  fs4k_SetFrame        (s, 0);
  fs4000_move_position (0, 0, 0);
  fs4000_control_led   (0);
  fs4000_release_unit  ();
  
  return code;
}

/** scaffolding ... */
static int fs4k_HackRead(struct scanner *s)
{
  enum { WAITING=0, PENDING, ABORTED };
  int readStatus = WAITING;

  size_t bufSize; /* linebytes * lines */
  size_t bytesRead = 0;
  int bytesNextBlock;
  
  PIXEL* buffer, *pcBuf;

  unsigned int lineBytes, lines;
  if (fs4000_get_data_status (&lines, &lineBytes))
    return -1;
  bufSize = lines * lineBytes;
  /* Hueristic to get byter each pass to within minimum size and a multiple of lines */
  bytesNextBlock = bufSize;
  if (bytesNextBlock > 65536) bytesNextBlock = 65536;
  bytesNextBlock /= lineBytes; /* Number of whole lines */
  bytesNextBlock *= lineBytes; /* Back to exact lines worth of bytes */

  buffer = pcBuf = malloc( lineBytes *bytesNextBlock);

  printf("Lines %d LineBytes %d BlockBytes %d\n", lines, lineBytes, bytesNextBlock);


  while (bytesRead < bufSize)
  {
    int bytesToGo;

    if (readStatus != WAITING)
      break;

    bytesToGo = bufSize - bytesRead;
    if (bytesNextBlock > bytesToGo)              /* short read at end ? */
      bytesNextBlock = bytesToGo;

    readStatus = PENDING;
    if (fs4000_read (bytesNextBlock, pcBuf)) {
      printf("Failed to read %d bytes\n", bytesNextBlock);
      readStatus = ABORTED; /* can be improved - why not just break out? */
    } else {
      readStatus = WAITING;
      printf("Read %d/%lu bytes\n", bytesRead, bufSize);
  
    }
    pcBuf += bytesNextBlock;
    bytesRead += bytesNextBlock;       /* update read count */
  }
  printf("READ STATUS: %d\n", readStatus);
  free(buffer);
  return 0;
}


int fs4k_Scan(struct scanner *s, int iFrame, BOOL bAutoExp)
{
  /* These magic numbers retained from Fs4000.cpp */
  int             iNegOff [6] = { 600, 1080, 1558, 2038, 2516, 2996};
  int             iPosOff [4] = { 552, 1330, 2110, 2883};
  int             iOffset, iSaveSpeed, iSaveShutter [3];
  int             iSetFrame;
  char            szRawFID [256], *pRawFID;

  snprintf (s->msg, MAX_MSG, "Scan frame %d", iFrame + 1);
  fs4k_NewsTask(s);

  fs4000_reserve_unit ();
  fs4000_control_led (2);
  fs4000_test_unit_ready ();
  s->lastError = fs4000_get_film_status_rec (&s->rFS);

  switch (s->rFS.film_holder_type)
  {
  case 1:     if (iFrame > MAX_FRAME_NEGATIVE_MOUNT) 
                return fs4k_release(s, -1);

              iOffset = iNegOff [iFrame];
              break;

  case 2:     if (iFrame > MAX_FRAME_SLIDE_MOUNT)
                return fs4k_release(s, -1);
              iOffset = iPosOff [iFrame];
              break;

  default:    fs4k_Warning(s, "No film holder");
              fs4000_set_lamp (0, 0);
              return fs4k_release(s, -1);
  }

  fs4k_LampOn (s, 15);                             /* lamp on for 15 secs min */
  if (fs4k_Halt (s)) return fs4k_release(s, -1);

  fs4k_SetFrame (s, 0);                            /* may help home func below */
  fs4000_move_position (0, 0, 0);               /* carriage home */
  fs4000_move_position (1, 4, iOffset - 236);   /* focus position */
  if (fs4k_Halt (s)) return fs4k_release(s, -1);

  fs4k_NewsStep (s, "Focussing");
  fs4k_SetScanModeEx (s, 4, fs4k_U24 (s), -1);            /* mid exposure */
  fs4000_execute_afae (1, 0, 0, 0, 500, 3500);
  if (fs4k_Halt (s)) return fs4k_release(s, -1);
  
  fs4000_move_position (1, 4, iOffset);

  iSetFrame = 0;                                /* R2L */
  if (bAutoExp) {                                /* exposure pre-pass */
    /* TODO */
  }

  fs4k_SetFrame (s, iSetFrame);                    /* L2R or R2L */

  snprintf (s->msg, MAX_MSG, "Frame %d, speed = %d, red = %d, green = %d, blue = %d\n",
                 iFrame + 1, s->iSpeed, s->iShutter [0], s->iShutter [1], s->iShutter [2]);
  fs4k_Feedback(s);
  
  fs4k_SetScanModeEx (s, s->iSpeed, fs4k_U24 (s), -1);
  fs4000_set_window(4000,                         /* UINT2 x_res, */
                  4000,                         /*  UINT2 y_res, */
                  0,                            /*  UINT4 x_upper_left, */
                  0,                            /*  UINT4 y_upper_left, */
                  4000,                         /*  UINT4 width, */
                  5904,                         /*  UINT4 height, */
                  fs4k_WBPS (s));                /*  BYTE bits_per_pixel); */
  if (fs4k_Halt (s)) return fs4k_release(s, -1);

  fs4k_NewsStep (s, "Reading");
  fs4000_scan ();
  
  fs4k_HackRead(s);
  
  fs4k_NewsStep (s, "Done");
  return fs4k_release(s, 0);


#ifdef FIXME

  iSaveSpeed = g.iSpeed;                        // save globals
  iSaveShutter [0] = g.iShutter [0];
  iSaveShutter [1] = g.iShutter [1];
  iSaveShutter [2] = g.iShutter [2];

  fs4000_reserve_unit ();
  fs4000_control_led (2);
  fs4000_test_unit_ready ();
  fs4k_GetFilmStatus ();
  switch (g.rFS.byHolderType)
    {
    case 1:     if (iFrame > 5)
                  goto release;
                iOffset = iNegOff [iFrame];
                break;

    case 2:     if (iFrame > 3)
                  goto release;
                iOffset = iPosOff [iFrame];
                break;

    default:    spout ("No film holder\r\n");
                fs4000_set_lamp (0, 0);
                goto release;
    }

  fs4k_LampOn (15);                             // lamp on for 15 secs min
  if (FS4_Halt ()) goto release;

  fs4k_SetFrame (0);                            // may help home func below
  fs4000_move_position (0, 0, 0);               // carriage home
  fs4000_move_position (1, 4, iOffset - 236);   // focus position
  if (FS4_Halt ()) goto release;

  fs4k_NewsStep ("Focussing");
  fs4k_SetScanMode (4, fs4k_U24 ());            // mid exposure
  fs4000_execute_afae (1, 0, 0, 0, 500, 3500);
  if (FS4_Halt ()) goto release;

  fs4000_move_position (1, 4, iOffset);

  iSetFrame = 0;                                // R2L
  if (bAutoExp)                                 // exposure pre-pass
    {
    fs4k_SetFrame ( 8);                         // R2L, no return after
    fs4k_SetWindow (4000,                       // UINT2 x_res,
                    500,                        // UINT2 y_res,
                    0,                          // UINT4 x_upper_left,
                    0,                          // UINT4 y_upper_left,
                    4000,                       // UINT4 width,
                    5904,                       // UINT4 height,
                    fs4k_WBPS ());              // BYTE bits_per_pixel);
    g.iSpeed = g.iAutoExp;
    fs4k_SetScanMode (g.iSpeed, fs4k_U24 ());
    if (FS4_Halt ()) goto release;

    fs4k_NewsStep ("Exposure pass");
    fs4000_scan ();
    if (fs4k_ReadScan (&g.rScan, 3, fs4k_WBPS (), TRUE, 500))
      goto release;
    if (FS4_Halt ()) goto release;
    fs4k_NewsStep ("Calc exposure");
    fs4k_CalcSpeed (&g.rScan);
    iSetFrame = 1;                              // L2R
    }

#if GET_DIG_OFFS_AT_SCAN

  fs4k_LampOff (1);
  if (fs4k_TuneDOffsets (g.iSpeed, 100)) goto release;
  fs4k_LampOn  (3);

#endif

  fs4k_SetFrame (iSetFrame);                    // L2R or R2L

  wsprintf (msg, "Frame %d, speed = %d, red = %d, green = %d, blue = %d\r\n",
                 iFrame + 1, g.iSpeed, g.iShutter [0], g.iShutter [1], g.iShutter [2]);
  spout ();

  fs4k_SetScanMode (g.iSpeed, fs4k_U24 ());
  fs4k_SetWindow (4000,                         // UINT2 x_res,
                  4000,                         // UINT2 y_res,
                  0,                            // UINT4 x_upper_left,
                  0,                            // UINT4 y_upper_left,
                  4000,                         // UINT4 width,
                  5904,                         // UINT4 height,
                  fs4k_WBPS ());                // BYTE bits_per_pixel);
  if (FS4_Halt ()) goto release;

  GetNextSpareName (szRawFID, g.szRawFID);
  pRawFID = szRawFID;

  fs4k_NewsStep ("Reading");
  fs4000_scan ();

  if (!(g.bSaveRaw || g.bUseHelper))
    pRawFID = NULL;
  if (fs4k_ReadScan (&g.rScan, 3, fs4k_WBPS (), TRUE, 4000, pRawFID) == 0)
    {
    fs4k_NewsStep ("Processing");
    if (g.bMakeTiff)
      if (g.bUseHelper)
        {
        fs4k_FreeBuf (&g.rScan);
        fs4k_StartMake (pTifFID, szRawFID);
        }
      else
        fs4k_SaveScan (pTifFID, &g.rScan, 4000);
    }
  FS4_Halt ();

release:
  fs4k_SetFrame        (0);
  fs4000_move_position (0, 0, 0);
  fs4000_control_led   (0);
  fs4000_release_unit  ();

  g.iSpeed = iSaveSpeed;                        // restore globals
  g.iShutter [0] = iSaveShutter [0];
  g.iShutter [1] = iSaveShutter [1];
  g.iShutter [2] = iSaveShutter [2];

  fs4k_NewsDone      ();
  return AbortWanted ();
#endif  
}
