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

#ifndef SANE_BACKEND_FS4000_USB_H__
#define SANE_BACKEND_FS4000_USB_H__

#define SRB_DIR_IN                  0x08        /* Transfer from SCSI target to host*/
#define SRB_DIR_OUT                 0x10        /* Transfer from host to SCSI target*/

/** SANE USB Device ID for the scanner for subsequent operations by 
    fs4000_usb_scsi_exec */
extern SANE_Int g_saneUsbDn;

/** Perform FS4000 SCSI command over USB using the SANE libusb wrapper.
 *
 * @param usbDn  SANE USB device number
 * @param value  As per sanei_usb_control_msg()
 * @param bInput True for input request, true for output request
 * @param pBuf   Byte array of request data
 * @param bufLen SIze of pBuf
 *
 * @return
 * - SANE_STATUS_GOOD - on success
 * - SANE_STATUS_IO_ERROR - on error
 * - SANE_STATUS_UNSUPPORTED - if the feature is not supported by the OS or
 *   SANE.
 */
extern SANE_Int
fs4000_usb_do_request (SANE_Int usbDn, SANE_Int value, SANE_Bool bInput,
          SANE_Byte *pBuf, SANE_Int bufLen);

/** Perform FS4000 SCSI operation.  This function is a transport provider
 * function for use by fs4000-scsi.c and complies with the interface
 * for that code.
 *
 * @param cdb          SCSI CDB block
 * @param cdb_length   Number of CDB bytes
 * @param mode_and_dir Combination of SRB_DIR_IN, SRB_DIR_OUT
 * @param pdb          SCSI PDB block
 * @param pdb_length   Number of PDB bytes
 * @return -1 on failure, or 0 on success
 */
extern int
fs4000_usb_scsi_exec (void *cdb, unsigned int cdb_length, 
           int mode_and_dir, void *pdb, unsigned int pdb_len);


#endif

