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
#include <stdarg.h>
#include <errno.h>

#define FS4000_CONFIG_FILE "fs4000.conf"

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_scsi.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"

#define FRAME_R2L 0

#define MAX_MSG 256

#define DEBUG(s, m...) printf( m)

#define LONGLONG uint64_t

/** Structure to hold meta-data and buffr for current scan data being read */
typedef struct
{
  void*           pBuf;         /** Pointer to actual raw scan pixel data */
  size_t          dwBufSize;    /** Allocated size of pBuf */
  size_t          dwHeaderSize; /** Size of this structure ... Q. AMM - why? */
  DWORD           dwReadBytes;
  DWORD           dwBytesRead;  /** Number of bytes in scan bulk read */
  UINT4           dwLines;
  UINT4           dwLineBytes;
  UINT4           dwLinesDone;
  BYTE            byBitsPerSample;
  BYTE            bySamplesPerPixel;
  BYTE            byAbortReqd;
  BYTE            byReadStatus;
  BYTE            bySetFrame;
  BYTE            byLeftToRight;
  BYTE            byShift;
  BYTE            bySpare;
  WORD            wLPI;       /** Lines per inch ... note it always seems to be 4000 across? */
  WORD            wMin;
  WORD            wMax;
  WORD            wAverage;
} FS4K_SCAN_BUFFER_INFO;

/** Free scan memory buffer */
static void fs4k_FreeBuf(FS4K_SCAN_BUFFER_INFO *pBI)
{
  if (pBI->pBuf)
    free (pBI->pBuf);
  return;
}

/** 
 * Free scan memory buffer.
 *
 * @param pBI Buffer info structure. Prerequisite: on input, 
 *            pBI should have dwLineBytes and dwLines initialised.
 * @param bySamplesPerPixel Samples per pxiel, typ. 3
 * @param byBitsPerSample   Bits per sample, typ. 14
 * @return 1 if memory allocated OK
 */
static int fs4k_AllocBuf(FS4K_SCAN_BUFFER_INFO *pBI, BYTE bySamplesPerPixel,
                                 BYTE byBitsPerSample)
{
  fs4k_FreeBuf (pBI);
  pBI->dwHeaderSize = sizeof (FS4K_SCAN_BUFFER_INFO);
  pBI->dwBufSize = pBI->dwLineBytes * pBI->dwLines;
  /** @todo On Linux, consider posix_memalign for better efficiency, 
            consider also mlock */
  pBI->pBuf = calloc( pBI->dwBufSize, 1);
  if (!pBI->pBuf) {
    pBI->dwBufSize = 0;
    return 1;
  }
  pBI->dwBytesRead       = 0;
  pBI->dwLinesDone       = 0;
  pBI->bySamplesPerPixel = bySamplesPerPixel;
  pBI->byBitsPerSample   = byBitsPerSample;
  if (pBI->byBitsPerSample == 14) /* Why is this hack here for? */
    pBI->byBitsPerSample   =  16;
  pBI->byAbortReqd       = 0;
  pBI->byReadStatus      = 0;
  pBI->bySetFrame        = 0;
  pBI->byLeftToRight     = 0;
  pBI->byShift           = 0;
  pBI->wLPI              = 0;
  return 0;
}

typedef struct  /* FS4K_CAL_ENT */
{
  int             iOffset;
  int             iMult;
}
FS4K_CAL_ENT;

/** Structure to hold all state information for scanner */
struct scanner
{
   const char *devname;

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
  FS4000_GET_WINDOW_DATA_IN rWI;
  FS4K_SCAN_BUFFER_INFO rBI;
  FS4K_CAL_ENT    rCal     [12120]    ;
  
  /* working buffers */
  char task[MAX_MSG+1];
  char step[MAX_MSG+1];
  char info[MAX_MSG+1];
  char warn[MAX_MSG+1];
  int savedDebug;
  
  /* Callback methods */  
  void (*feedbackFunction)(const char *);
  int (*abortFunction)(void);
};

static void fs4k_sleep(int milliseconds)
{
  /* TODO : make this posix thread interurptible somehow */
  usleep(milliseconds * 1000);
}

/** 
 * Change global level and save previous level.
 * This does not nest currently... (it may need to... )
 */
static void fs4k_PushDebug(struct scanner *s, int debug)
{
  s->savedDebug = fs4000_debug;
  fs4000_debug = debug;
}

static void fs4k_PopDebug(struct scanner *s)
{
  fs4000_debug = s->savedDebug;
}

/* These 'news' functions I think are designed to fall through to any GUI.
   Currently these are not designed to cope with any re-entrance, 
   and in particular, cache the last message in s->msg
   
   @todo For the moment we write _ALL_ messages (tty Feedback, 'news', ...)
   through the 'feedbackFuntion'; sort out where it should go later
 */


/* This should write to the tty if stdout redirected to a file ... */
static void fs4k_Feedback(struct scanner* s, const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vsnprintf(s->info, MAX_MSG, msg, ap);
  va_end(ap);
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->info);
}

static void fs4k_NewsTask(struct scanner *s, const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vsnprintf(s->task, MAX_MSG, msg, ap);
  va_end(ap);
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->task);
}


/* For now is same as task, fix up later */
static void fs4k_NewsStep(struct scanner* s, const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vsnprintf(s->step, MAX_MSG, msg, ap);
  va_end(ap);
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->step);
}

/* For now, warnings are cached separately from news/feedback
   but are printed using the same callback */
static void fs4k_Warning(struct scanner *s, const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vsnprintf(s->warn, MAX_MSG, msg, ap);
  va_end(ap);
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->warn);
}

/* For now, warnings are cached separately from news/feedback
   but are printed using the same callback */
static void fs4k_Fatal(struct scanner *s, const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vsnprintf(s->warn, MAX_MSG, msg, ap);
  va_end(ap);
  if (s->feedbackFunction)
    (s->feedbackFunction)(s->warn);
}

/* Equivalent to AbortWanted()  - return 0 to not abort */
static int fs4k_checkAbort(const struct scanner* s)
{
  if (s->abortFunction) 
    return (s->abortFunction)();
  return 0;
}

/* Equivalent to FS4_Halt() - return 0 to not halt */
static int fs4k_Halt(const struct scanner *s)
{
  if (s->abortFunction) 
    return (s->abortFunction)();
  return 0;
}

int fs4k_GetLastError(const struct scanner* s)
{
  return s->lastError;
}

struct scanner *fs4k_init(struct scanner **s, const char *devname) 
{
  *s = malloc(sizeof(struct scanner));

  if (*s) {
    memset( *s, 0, sizeof(struct scanner));
    (*s)->devname = devname;
  }
  return *s;
}

void fs4k_destroy(struct scanner *s) 
{
  free(s);
}

const char *fs4k_devname(struct scanner *s)
{
  return s->devname;
}


void fs4k_SetFeedbackFunction( struct scanner *s, void (*f)(const char *))
{
  s->feedbackFunction = f;
}

void fs4k_SetAbortFunction( struct scanner *s, int (*f)(void))
{
  s->abortFunction = f;
}

static int fs4k_LampTest(struct scanner* s)
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
  s->lastError = 0;
  do {
    if ( (s->lastError = fs4000_set_lamp (1, 0)))       /* Always make sure */
      return -1;
    if ( (s->lastError = fs4000_get_lamp_rec(&s->rLI))) /* See how long its been on */ 
      return -1;
    if ((int) s->rLI.visible_lamp_duration >= iOnSecs)  /* Finish if had been on long enough already */
      break;
    fs4k_Feedback(s, "Waiting for lamp (%d)", iOnSecs - s->rLI.visible_lamp_duration);
    fs4k_sleep (500);
  } while (!(abort=fs4k_checkAbort(s)));
  
  return abort ? -1 : 0;
}

/** Reset calibration array */
static void fs4k_InitCalArray(struct scanner *s)
{
  int x;

  for (x = 0; x < 12120; x++)
  {
    s->rCal [x].iOffset = 0;
    s->rCal [x].iMult   = 16384;
  }
  return;
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

int fs4k_SetInMode (struct scanner *s, int iNewMode)
{
  if ((iNewMode != 8) && (iNewMode != 14) && (iNewMode != 16))
    return -1;
  s->iInMode = iNewMode;
  return 0;
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

/** 
 * Read the raw pxiel data into a scan buffer structure 
 * Although in practice *pBI lives inside *s, it may become needed
 * use multiple or separate *pBI...
 **/
static int fs4k_ReadScanFromScanner(struct scanner* s, FS4K_SCAN_BUFFER_INFO *pBI)
{
  enum { WAITING=0, PENDING, ABORTED };
  int readStatus = WAITING;
  size_t bufSize = pBI->dwBufSize;
  size_t bytesRead = 0;
  size_t bytesNextBlock;
  size_t maxBulkTransfer = 65536; /* TODO Get this from scanner_t */
  BYTE* pcBuf;

  /* Hueristic to get byter each pass to within minimum size and a multiple of lines */
  bytesNextBlock = bufSize;
  if (bytesNextBlock > maxBulkTransfer) bytesNextBlock = maxBulkTransfer;
  bytesNextBlock /= pBI->dwLineBytes; /* Number of whole lines */
  bytesNextBlock *= pBI->dwLineBytes; /* Back to exact lines worth of bytes */

  pcBuf = (BYTE*)pBI->pBuf;

  DEBUG(s, "Lines %u LineBytes %u BlockBytes %lu Total %lu\n", pBI->dwLines, pBI->dwLineBytes, bytesNextBlock, bufSize);
  
  while (bytesRead < bufSize)
  {
    size_t bytesToGo;

    if (readStatus != WAITING)
      break;
    bytesToGo = bufSize - bytesRead;
    if (bytesNextBlock > bytesToGo)              /* short read at end ? */
      bytesNextBlock = bytesToGo;

    readStatus = PENDING;
    if (fs4000_read (bytesNextBlock, (PIXEL*)pcBuf)) {
      fs4k_Fatal( s, "Failed to read next %lu bytes of scan data.", bytesNextBlock);
      readStatus = ABORTED; /* AMM Q. can be improved? - why not just break out? */
    } else {
      readStatus = WAITING;
      pcBuf += bytesNextBlock;
      bytesRead += bytesNextBlock;       /* update read count */
      DEBUG(s, "Read %lu/%lu bytes from scanner. togo=%lu next=%lu\n", bytesRead, bufSize, bytesToGo, bytesNextBlock);
    }
  }
  pBI->dwBytesRead = bytesRead;
  return 0;
}

/** Deinterlacing as required
 ** and also apply tune calibration if requested */
static void fs4k_Deinterlace(struct scanner* s, FS4K_SCAN_BUFFER_INFO *pBI, BOOL bCorrectSamples)
{
  DWORD dwToDo;
  DWORD dwBytesDone = 0;
  DWORD dwSamples = 0;  /* stats .. */
  WORD* pwBuf = (WORD*)pBI->pBuf;
  BYTE* pbBuf = (BYTE*)pBI->pBuf;
  int iIndex = 0;
  int             iShift, iShift2, iOff [3];
  int             iLineEnts, iLine, iCol, iColour;
  int             iWorstUnder, iWorstIndex, iWorstLine;
  int             iLimit, iScale, iUnderflows, iOverflows;
  LONGLONG        qwSum;
  DWORD dwMaxPerLoop;

  
/* Coment from Fs4000.cpp :
      Shifting lines (de-interlacing) here isn't really worthwhile unless
      we are doing colour correction as it is a processing overhead.
      Shifting while saving the scan isn't an overhead as we are
      rebuilding the lines anyway.

      However, shifting here does give us a proper image now which may
      assist code that wants to examine it.  So, we shift here unless we
      are creating a raw dump file.  We can't shift when creating a raw
      file as the file writing code doesn't wait for the shift so the
      result is indeterminate.
*/
  iLineEnts = s->rBI.dwLineBytes / ((pBI->byBitsPerSample + 7) >> 3);
  iShift = iShift2 = 0;
/*//if (s->rBI.wLPI ==  160) iShift = 0;*/
  if (s->rBI.wLPI ==  500) iShift = 1;
  if (s->rBI.wLPI == 1000) iShift = 2;
  if (s->rBI.wLPI == 2000) iShift = 4;
  if (s->rBI.wLPI == 4000) iShift = 8;
#ifdef AMM_TODO_DEAL_WITH_DUMPFILE
  if (hFile)                                    /* no shift if raw output */
    iShift = 0;
#endif
  s->rBI.byShift = iShift;
  iShift2 = iShift << 1;
  iOff [0] = 0;                                 /* red */
  iOff [1] = 0 - (iShift * iLineEnts);          /* green */
  iOff [2] = 0;                                 /* blue */
  if (s->rBI.byLeftToRight)                       /* L2R */
    iOff [0] -= (iShift2 * iLineEnts);
  else                                          /* R2L */
    iOff [2] -= (iShift2 * iLineEnts);
  s->rBI.dwLines -= iShift2;

#if 0
  wsprintf (msg, "LineEnts = %d, R off = %d, G off = %d, B off = %d\r\n",
                 iLineEnts,     iOff [0],   iOff [1],   iOff [2]);
  spout ();
#endif

  qwSum = 0;                                    /* reset stats */
  dwSamples = 0;
  s->rBI.wMin = 65535;
  s->rBI.wMax = 0;
  iUnderflows = iOverflows = 0;
  iWorstUnder = iWorstIndex = iWorstLine = 0;
  iLimit = 65535;

  /* This is left over from Fs4000.cpp.  But we can put a loop in here for now
   * and push out status updates during deinterlacing if it takes a long time */
  dwMaxPerLoop = pBI->dwLineBytes * 16; /* max data per loop (ensure */
  /*dwMaxPerLoop = pBI->dwBytesRead; -- all at once */

  iCol = 0;
  iColour = 0;
  /* This reduces the data in place. Each pass through the loop is reading from
   * later in memory and writing earlier... */    
  do {
    /* Used for disk write ... BYTE* pNext = (BYTE*)pBI->pBuf + dwBytesDone; */

    dwToDo = dwMaxPerLoop;
    if (dwToDo > (pBI->dwBytesRead - dwBytesDone)) 
      dwToDo = pBI->dwBytesRead - dwBytesDone;

    DEBUG( s, "[Deinterlace] Input from byte %9u to %9u; Output Position %u\n", dwBytesDone, dwBytesDone+dwToDo-1, dwSamples);

    dwBytesDone += dwToDo;                      /* premature update */
    if (pBI->byBitsPerSample > 8)
    {
      /* Here we have 14- or 16-bit data */
      /* So the number of words to process is 1/2 the number of bytes ... */
      dwToDo >>= 1;
      for ( ; dwToDo--; dwSamples++)            /* for each sample */
      {
        int iSample = pwBuf [dwSamples];
        if (s->iInMode == 14)                    /* change 14-bit to 16-bit */
          iSample <<= 2;
        if ((WORD) iSample < pBI->wMin)
          pBI->wMin = iSample;
        if ((WORD) iSample > pBI->wMax)
          pBI->wMax = iSample;
        qwSum += iSample;
        if (bCorrectSamples && (iCol >= s->iMargin))   /* if post margin */ /* TODO init iMargin */
        {
          iSample += s->rCal [iCol].iOffset;     /* add offset for sample */ /* TODO init rCal */
          if (iSample < 0)
          {
            if (iSample < iWorstUnder)
            {
              iWorstUnder = iSample;
              iWorstIndex = iCol;
              iWorstLine  = iLine;
            }
            iSample = 0;
            iUnderflows++;
          } else {
            iSample *= s->rCal [iCol].iMult;     /* *= factor */
            iSample += 8192;                    /* for rounding */
            iSample >>= 14;                     /* /= 16384 */
            iScale = s->iBoost [iColour];
            if (iScale > 256)                   /* if extra boost */
            {
              iSample *= iScale;
              iSample >>= 8;
            }
            if (iSample > iLimit)
            {
              iSample = iLimit;
              iOverflows++;
            }
          }
        } /* endif correct samples */
        iIndex = dwSamples + iOff [iColour];
        if (iIndex >= 0)
          pwBuf [iIndex] = iSample;
        if (++iCol == iLineEnts)                /* EOL ? */
        {
          iCol = 0;
          iLine++;
        }
        if (++iColour == 3)
          iColour = 0;
      } /* end for */
    } else {                                        
      /* 8-bit input */
      for ( ; dwToDo--; dwSamples++)
      {
        int iSample = pbBuf [dwSamples];
        if ((WORD) iSample < pBI->wMin)
          pBI->wMin = iSample;
        if ((WORD) iSample > pBI->wMax)
          pBI->wMax = iSample;
        qwSum += iSample;
        iIndex = dwSamples + iOff [iColour];    /* slow code */
        if (iIndex >= 0)                        /* generated */
          pbBuf [iIndex] = iSample;             /* here !!! */
        if (++iCol == iLineEnts)                /* EOL ? */
        {
          iCol = 0;
          iLine++;
        }
        if (++iColour == 3)
          iColour = 0;
      }
    }

    iIndex = iLine - iShift2;
    if (iIndex > 0)
      pBI->dwLinesDone = iIndex;
  } while (dwBytesDone < pBI->dwBytesRead);

  DEBUG( s, "[Deinterlace] dwBytesDone=%u, dwSamples=%u\n", dwBytesDone, dwSamples);


  if (iUnderflows)                              /* common error */
  {
    fs4k_Warning(s,  "Underflows = %d (worst = %d at %d %s on line %d)\r\n",
                   iUnderflows, iWorstUnder,
                   (iWorstIndex - s->iMargin) / 3,
                   (iWorstIndex % 3 == 0) ? "red" :
                   (iWorstIndex % 3 == 1) ? "green" : "blue",
                   iWorstLine);
  }

  if (iOverflows)                               /* common error */
  {
    fs4k_Warning (s, "Overflows = %d\r\n", iOverflows);
  }
}

/** 
 * Read scan data
 *
 * This routine starts the background reads and processes
 * the scan data as required.
 * @todo Currently we dont spawn a separate thread to do the reading
 *       Fs4000.cpp used a separate thread to read from the USB
 *       so that the shifting and saling could be done concurrently.
 *       We will come back and implement threading later after everything works 
 *       in the slow lane first.
 *
 * Each scan line goes from the bottom up.
 * For a thumbnail the first line is the leftmost.
 * For a forward scan the first line is the rightmost.
 * For a reverse scan the first line is the leftmost.
 *
 * Like most of these functions, this is not re-entrant:
 * it will clobber s->rBI, for example, and change fs4000_debug during its lifetime
 *
 * 
 * @param s Scanner data
 * @param bySamplesPerPixel Samples per pixel, typ. 3
 * @param byBitsPerSample Bits per pixel, typ. 14
 * @param bCorrectSamples Fix negative samples if 'post margin', typ FALSE
 * @param iLPI ...?
 * @param dumpFileName Dump file filename path
 */
static int fs4k_ReadScan(struct scanner *s, BYTE bySamplesPerPixel,
                         BYTE byBitsPerSample, BOOL bCorrectSamples,
                         int iLPI, const char *dumpFileName)
{
  const char *fault = NULL;

  DEBUG( s, "[ReadScan] samplesPerPixel=%d, bitsPerSample=%d, applyCal=%d, LPI=%d dump=%s\n", bySamplesPerPixel, byBitsPerSample, bCorrectSamples, iLPI, dumpFileName);

  fs4k_FreeBuf (&s->rBI);

  /* To avoid overloading with messages, temporarily change the debug level */
  fs4k_PushDebug(s, 1/*0*/);

  if ((s->lastError=fs4000_get_data_status (&s->rBI.dwLines, &s->rBI.dwLineBytes)))
  {
    fault = "Failed to acquire frame buffer size from scanner.";
  } 
  else if (fs4k_AllocBuf (&s->rBI, bySamplesPerPixel, byBitsPerSample))
  {
    fault = "Unable to allocate memory for frame.";
  }
  if (fault) {
    fs4k_PopDebug(s);
    fs4000_cancel ();  /* AMM Q. Is this the correct place to do this? */
    fs4k_Fatal(s, fault);
    /* ReturnCode = RC_SW_ABORT; AMM Q. Mixing of presentation layer? */
    return -1;
  }

  s->rBI.wLPI = iLPI;
  s->rBI.bySetFrame = s->iFrame;
  s->rBI.byLeftToRight = (s->rBI.bySetFrame & 0x01);

  
  /* At this point the Windows version made a thread to read the USB
   * and then proceeded to concurrently do shifting and scaling
   * on already received data.
   * For now, we will instead read the entire data, then process it.
   */
  
  fs4k_ReadScanFromScanner( s, &s->rBI);
  fs4k_Deinterlace(s, &s->rBI, bCorrectSamples);

  if (dumpFileName) {
    FILE *f = fopen( dumpFileName, "wb");
    if (!f) { 
      fs4k_Warning( s, "Failed to open dump file '%s': %s\n", dumpFileName, strerror(errno));
    } else {
      if (1 != fwrite( s->rBI.pBuf, s->rBI.dwBytesRead, 1, f)) {
        fs4k_Warning( s, "Failed to write %u bytes to dump file '%s': %s\n", s->rBI.dwBytesRead, dumpFileName, strerror(errno));
      }
      fclose(f);
    }
  }

  fs4k_PopDebug(s);
  return 0;
}

int fs4k_Scan(struct scanner *s, int iFrame, BOOL bAutoExp)
{
  /* These magic numbers retained from Fs4000.cpp */
  int             iNegOff [6] = { 600, 1080, 1558, 2038, 2516, 2996};
  int             iPosOff [4] = { 552, 1330, 2110, 2883};
  int             iOffset;
  int             iSetFrame;

  fs4k_NewsTask(s, "Scan frame %d", iFrame + 1);
  
  fs4000_reserve_unit ();
  fs4000_control_led (2); /* Blink speed ... */
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
#define ENABLE_LAMP 
#ifdef ENABLE_LAMP /* off for now so we dont burn it out */
  fs4k_LampOn (s, 15);                             /* lamp on for 15 secs min so it properly warms up */
#endif
  if (fs4k_Halt (s)) return fs4k_release(s, -1);

  fs4k_SetFrame (s, FRAME_R2L);                    /* may help home func below */
  fs4000_move_position (0, 0, 0);                  /* carriage home */
  fs4000_move_position (1, 4, iOffset - 236);      /* focus position */
  if (fs4k_Halt (s)) return fs4k_release(s, -1);

  fs4k_NewsStep (s, "Focussing");
  fs4k_SetScanModeEx (s, 4, fs4k_U24 (s), -1);     /* mid exposure */
  fs4000_execute_afae (1, 0, 0, 0, 500, 3500);
  if (fs4k_Halt (s)) return fs4k_release(s, -1);
  
  fs4000_move_position (1, 4, iOffset);

  iSetFrame = FRAME_R2L;                           /* R2L */
  if (bAutoExp) {                                  /* exposure pre-pass */
    /* TODO */
  }

  fs4k_SetFrame (s, iSetFrame);                    /* L2R or R2L */

  fs4k_Feedback(s, "Frame %d, speed = %d, red = %d, green = %d, blue = %d\n",
                 iFrame + 1, s->iSpeed, s->iShutter [0], s->iShutter [1], s->iShutter [2]);
  
  fs4k_SetScanModeEx (s, s->iSpeed, fs4k_U24 (s), -1);
  fs4000_set_window(4000,                         /*  UINT2 x_res, */
                    4000,                         /*  UINT2 y_res, */
                    0,                            /*  UINT4 x_upper_left, */
                    0,                            /*  UINT4 y_upper_left, */
                    4000,                         /*  UINT4 width, */
                    5904,                           /*  UINT4 height, */ /* TODO was 5904, using 16 for test speed */
                    fs4k_WBPS (s));               /*  BYTE bits_per_pixel_code */
  if (fs4k_Halt (s)) return fs4k_release(s, -1);

  fs4k_NewsStep (s, "Reading");
  fs4000_scan ();
  
  fs4k_ReadScan(s, 3, fs4k_WBPS (s), FALSE/*TRUE in Fs4000.cpp*/, 4000, "/tmp/fs4000.rgb"); 
  
  fs4k_NewsStep (s, "Done");

  fs4k_FreeBuf (&s->rBI); /* For now ... */

  return fs4k_release(s, 0);
}

void fs4k_InitData(struct scanner *s)
{
  s->iAGain    [0]       =   47;
  s->iAGain    [1]       =   36;
  s->iAGain    [2]       =   36;
  s->iAOffset  [0]       =  -25;
  s->iAOffset  [1]       =   -8;
  s->iAOffset  [2]       =   -5;
  s->iShutter  [0]       =  750;
  s->iShutter  [1]       =  352;
  s->iShutter  [2]       =  235;
  s->iInMode             =   14;
  s->iBoost    [0]       =  256;
  s->iBoost    [1]       =  256;
  s->iBoost    [2]       =  256;
  s->iSpeed              =    2;
  s->iFrame              =    0;
  s->ifs4000_debug       =    0;
  s->iMaxShutter         =  890;
  s->iAutoExp            =    2;
  s->iMargin             =  120; /* 0 = no margin (stuffs black level !!!!) */
  
  fs4k_InitCalArray(s);

}

int fs4k_InitCommands(struct scanner *s)
{
  fs4000_cancel         ();
  fs4k_SetInMode        (s, 14);

  while (fs4000_test_unit_ready ())
    if (fs4k_checkAbort(s))
      return -1;

  if ((s->lastError = fs4000_get_film_status_rec (&s->rFS))) {
    fs4k_Warning(s, "Error on BOJ GetFilmStatus");
    return -1;
  }

  ;
  if ((s->lastError = fs4000_get_lamp_rec (&s->rLI))) {
    fs4k_Warning(s, "Error on BOJ GetLampInfo");
    return -1;
  }

  if ((s->lastError = fs4000_get_scan_mode_rec (&s->rSMI.scsi))) {
    fs4k_Warning(s, "Error on BOJ GetScanModeInfo");
    return -1;
  }
  memset(&s->rSMI.control.unknown1b [2], 0, 9);
  s->rSMI.control.bySpeed      = s->iSpeed;
  s->rSMI.control.bySampleMods = 0;
  s->rSMI.control.unknown2a    = 0;
  s->rSMI.control.byImageMods  = 0;
  if ((s->lastError = fs4000_put_scan_mode_rec (&s->rSMI.scsi))) {
    fs4k_Warning(s, "Error on BOJ PutScanModeInfo");
    return -1;
  }

  if ((s->lastError = fs4000_get_window_rec (&s->rWI))) {
    fs4k_Warning(s, "Error on BOJ GetWindowInfo");
    return -1;
  }
  if ((s->lastError = fs4000_put_window_rec((FS4000_SET_WINDOW_DATA_OUT*)&s->rWI)))
  {
    fs4k_Warning(s, "Error on BOJ PutWindowInfo");
    return -1;
  }
  return 0;
}

