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
   
#ifndef SANE_BACKEND_FS4000_CONTROL_H__
#define SANE_BACKEND_FS4000_CONTROL_H__

#define MAX_FRAME_NEGATIVE_MOUNT 5  /* index E 0..5 */
#define MAX_FRAME_SLIDE_MOUNT 3

struct scanner;

extern int fs4k_GetLastError(const struct scanner* s); 

extern struct scanner *fs4k_init(struct scanner **s, const char *devname);
extern void fs4k_destroy(struct scanner *s);
extern void fs4k_InitData(struct scanner *s);
extern int fs4k_InitCommands(struct scanner *s);

extern const char *fs4k_devname(struct scanner *s);

/** 
 * Set a function for reporting progress messages that would be displayed
 *
 * only on a TTY if redirecting output stdout/stderr
 * Dangerous if set after everything has started, as use not thread protected
 */
extern void fs4k_SetFeedbackFunction( struct scanner *s, void (*f)(const char *));

/** 
 * Set a function for reporting warnings as the occur
 *
 * Dangerous if set after everything has started, as use not thread protected
 */
extern void fs4k_SetWarningFunction( struct scanner *s, void (*f)(const char *));

/** 
 * Set a function for polling whether lengthy operations should abort.
 * Usually would check a flag set by a signal CTRL+C handler (for example)
 *
 * Dangerous if set  after everything has started, as use not thread protected
 * @param f Function that should return 0 if in progress operatoin should abort
 */
extern void fs4k_SetAbortFunction( struct scanner *s, int (*f)(void));

/** 
 * Turn scanner lamp off, and optionally wait for specified time
 *
 * TODO Optionally poll for cancellation
 * @param s Handle to Scanner
 * @param iOffSeconds Sleep this long after is > 0
 * @return 0 on success
 *         -1 if transport error occured (fs4k_GetLastError() > 0)
 */
extern int fs4k_LampOff(struct scanner* s, int iOffSecs);

/** 
 * Turn scanner lamp on, and ensure it had been on at least the specified time
 *
 * This will block up to iOnSecs.  If lamp had already been turned on, will
 * only block as long as needed. Will call the abort checker every half second.
 * 
 * @param s Handle to Scanner
 * @param iOnSeconds Wait for this long after if > 0
 * @return 0 on success
 *         -1 if transport error occured, or aborted
 *         If aborted, fs4k_GetLastError() will == 0
 */
extern int fs4k_LampOn(struct scanner* s, int iOnSecs);

/** Select 8, 14, or 16-bit input mode */
extern int fs4k_SetInMode (struct scanner *s, int iNewMode);

/** Execute scan of specified frame */
extern int fs4k_Scan(struct scanner *s, int iFrame, int left, int top, int width, int height, BOOL bAutoExp);

/** Get size and pointer to buffer from last successful fs4k_Scan() */
extern DWORD fs4k_GetScanResult(struct scanner *s, BYTE** buf);

extern void fs4k_FreeResult(struct scanner *s);

extern int fs4k_GetLastFrameInfo( struct scanner *s, SANE_Int* lines, SANE_Int* lineBytes, SANE_Int* linePixels, SANE_Int* depth);

#endif


