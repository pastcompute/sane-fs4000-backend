/*
   (c) 2012 Andrew McDonnell bugs@andrewmcdonnell.net
   Parts copyright (C) 2004-2007 Steven Saunderson

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
*/

/* This code provides SCSI over USB transport for the Canon FS4000
   slide scanner. */
  
#include "../include/sane/config.h"

#include <stdio.h>
#include "../include/lalloca.h"

#include "fs4000.h"

#include "../include/sane/sane.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/sanei_debug.h"

#include "fs4000-usb.h"

/* We currently only support one Scanner. To fix this would require
   propagating the ID of the scanner through fs4000-scsi.c/.h, maybe as some
   kind of user handle... */
SANE_Int g_saneUsbDn = -1;

/* ------------------------------------------------------------------------- */
SANE_Int
fs4000_usb_do_request (SANE_Int usbDn, SANE_Int value, SANE_Bool bInput,
                       BYTE *pBuf, SANE_Int bufLen)
{
  BYTE byRequest, byRequestType;

  /* bit 7        0 = output, 1 = input */
  /* bits 6-5     2 = vendor special */
  /* bits 4-0     0 = recipient is device */
  byRequestType = bInput ? 0xC0 : 0x40;

  /* loaded by driver according to ddk */
  byRequest = (bufLen < 2) ? 0x0C : 0x04;     /* is this significant ?*/

  DBG (4, "usb_control_msg: dn=%d %.8x %.8x %.8x\n", usbDn, byRequestType, byRequest, value);

  return sanei_usb_control_msg (
            usbDn, byRequestType, byRequest, value, 0, bufLen, pBuf);
}

/* ------------------------------------------------------------------------- */
int
fs4000_usb_scsi_exec (void *cdb, unsigned int   cdb_length, 
          int mode_and_dir, void *pdb, unsigned int pdb_len)
{
  void            *save_pdb    = pdb;
  uint32_t        save_pdb_len = pdb_len;
  size_t          dwBytes;
  BYTE            byNull = 0, byOne = 1;
  BYTE            *pbyCmd = (uint8_t*) cdb;
  SANE_Bool       bInput;
  uint32_t        dwValue, dwValueC5 = 0xC500, dwValue03 = 0x0300;
  BYTE            byStatPDB [4];          /* from C500 request*/
  BYTE            bySensPDB [14];         /* from 0300 request*/
  int er;

  DBG (3, "fs4000_usb_scsi_exec: %u %.8x {%d}\n", cdb_length, mode_and_dir, pdb_len);
  if (!pdb_len)                         /* if no data, use dummy output*/
  {
    mode_and_dir &= ~SRB_DIR_IN;
    mode_and_dir |= SRB_DIR_OUT;
    pdb = &byNull;                              /* default output*/
    pdb_len = 1;
    if (*pbyCmd == 0x00)                        /* change for some*/
      pdb = &byOne;
    if (*pbyCmd == 0xE4)
      pdb = &byOne;
    if (*pbyCmd == 0xE6)
    {
      pdb = pbyCmd + 1;
      pdb_len = 5;
    }
    if (*pbyCmd == 0xE7)
    {
      pdb = pbyCmd + 2;
      pdb_len = 2;
    }
    if (*pbyCmd == 0xE8)
      pdb = pbyCmd + 1;
  }
  if (*pbyCmd == 0x28)                          /* if read*/
  {
    mode_and_dir &= ~SRB_DIR_IN;                /* output not input*/
    mode_and_dir |= SRB_DIR_OUT;
    pdb = pbyCmd + 6;                           /* send buf size*/
    pdb_len = 3;
  }

  dwValue =  pbyCmd [0] << 8;                   /* build parameters*/
  if (cdb_length > 2)
    if ((*pbyCmd == 0x12) || (*pbyCmd == 0xD5))
      dwValue += pbyCmd [2];
  bInput = (mode_and_dir & SRB_DIR_IN) > 0;

  er=fs4000_usb_do_request (g_saneUsbDn, dwValue, bInput, pdb, pdb_len);
  if (er==SANE_STATUS_IO_ERROR) {
    DBG (1, "fs4000_usb_scsi_exec: fs4000_usb_do_request IO Error");
    return -1;
  }
  if (er==SANE_STATUS_UNSUPPORTED) {
    DBG (1, "fs4000_usb_scsi_exec: fs4000_usb_do_request Unsupported");
    return -1;
  }

  if (*pbyCmd == 0x28)                          /* if read, get bulk data*/
  {
    dwBytes = save_pdb_len;
    er = sanei_usb_read_bulk( g_saneUsbDn, save_pdb, &dwBytes);
    if (er==SANE_STATUS_IO_ERROR) {
      DBG (1, "fs4000_usb_scsi_exec: usb_read_bulk IO Error");
      return -1;
    }
    if (er==SANE_STATUS_EOF) {
      DBG (1, "fs4000_usb_scsi_exec: usb_read_bulk EOF");
      return -1;
    }
    if (er==SANE_STATUS_INVAL) {
      DBG (1, "fs4000_usb_scsi_exec: usb_read_bulk INVAL");
      return -1;
    }
    if (dwBytes != save_pdb_len) {
      DBG (1, "fs4000_usb_scsi_exec: usb_read_bulk short read");
      return -1;
    }
  }

  if (fs4000_usb_do_request (g_saneUsbDn, dwValueC5, SANE_TRUE, byStatPDB, 4))  /* get status*/
  {
    if (er==SANE_STATUS_IO_ERROR) {
      DBG (1, "fs4000_usb_scsi_exec: fs4000_usb_do_request get status IO Error");
      return -1;
    }
    if (er==SANE_STATUS_UNSUPPORTED) {
      DBG (1, "fs4000_usb_scsi_exec: fs4000_usb_do_request get status Unsupported");
      return -1;
    }
    return -1;
  }

  if (byStatPDB [0] != *pbyCmd) {
    if ((byStatPDB [0] != 0) || ((*pbyCmd != 0x16) && (*pbyCmd != 0x17))) {
      DBG (1, "fs4000_usb_scsi_exec: cmd mismatch %02X %02X\n", *pbyCmd, byStatPDB [0]);
    }
  }

  if (byStatPDB [1] & 0xFF)                             /* sense data ?*/
  {
    const int maxx=14;
    char *buf = alloca(maxx*3+1);
    int x;
    er = fs4000_usb_do_request (g_saneUsbDn, dwValue03, SANE_TRUE, bySensPDB, 14);   /* get sense*/
    for (x = 0; x < maxx; x++)
      snprintf (buf+x*3, (maxx-x)*3, " %02X", bySensPDB [x]);
    DBG (1, "fs4000_usb_scsi_exec: sense error: %s", buf);
  }
  return 0;
}          



