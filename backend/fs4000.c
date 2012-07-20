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

   This file implements a SANE backend for Canon FS4000 Slide Scanners.
   Currently only the USB interface is implemented.  */


/* Architecture:
 * 
 * Control of the scanner is by the fs4000 SCSI API (provided by fs4000-scsi.c, 
 * developed by Dave Burns)
 * The USB interface implements a SCSI over USB protocol, the fs4000-scsi.c
 * uses a callback that allows either USB or SCSI as the transport.
 * 
 * Higher level control, i.e. use cases such as scanning, tuning and other
 * operations are provided in fs4000-cpp.c, this code is derived from the
 * Fs4000.cpp program developed by Steven Saunderson.
 */

#include "../include/sane/config.h"

#define DEBUG_NOT_STATIC
#include "../include/sane/sanei_debug.h"

#include "fs4000.h"

#define FS4000_CONFIG_FILE "fs4000.conf"

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_scsi.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"

#define V_MINOR 0

/* ------------------------------------------------------------------------- */
SANE_Status
sane_init (SANE_Int * version_code, SANE_Auth_Callback UNUSEDARG authorize)
{
  DBG_INIT ();
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
void
sane_exit (void)
{
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool local_only)
{
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  return SANE_STATUS_INVAL;
}

/* ------------------------------------------------------------------------- */
void
sane_close (SANE_Handle handle)
{
}

/* ------------------------------------------------------------------------- */
const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  return 0;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option,
		     SANE_Action action, void *val,
		     SANE_Int * info)
{
  return SANE_STATUS_INVAL;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_start (SANE_Handle handle)
{
  return SANE_STATUS_DEVICE_BUSY;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * buf,
	   SANE_Int max_len, SANE_Int * len)
{
  return SANE_STATUS_IO_ERROR;
}

/* ------------------------------------------------------------------------- */
void
sane_cancel (SANE_Handle handle)
{
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  return SANE_STATUS_INVAL;
}

