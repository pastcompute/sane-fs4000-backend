/*--------------------------------------------------------------

                USB INTERFACE CODE

        USB transport code for FS4000US scanner code

  Copyright (C) 2004  Steven Saunderson

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston,
  MA 02111-1307, USA.

                StevenSaunderson at tpg.com.au

        2004-12-08      Original version.

        2004-12-10      Add access using LibUSB.

        2004-12-12      File access code now works with W98.

--------------------------------------------------------------*/

#define STRICT
#include <windows.h>
#include <stdio.h>

#include "scsidefs.h"
#include "scsi_via_usb.h"

/*
        USB fields are split into global and per_user because I did this
        with the ASPI code.  Probably no value here.

        At start-up :
                call usb_init

        At shut-down :
                call usb_deinit
*/

#define Nullit(x) memset (&x, 0, sizeof (x))

        USB_FIELDS      usb;

usb_dev_handle* (*lusb_open)            (struct usb_device *dev);
int     (*lusb_close)                   (usb_dev_handle *dev);
int     (*lusb_bulk_read)               (usb_dev_handle *dev,
                                         int ep,
                                         char *bytes,
                                         int size,
		                         int timeout);
int     (*lusb_control_msg)             (usb_dev_handle *dev,
                                         int requesttype,
                                         int request,
		                         int value,
                                         int index,
                                         char *bytes,
                                         int size,
		                         int timeout);
int     (*lusb_set_configuration)       (usb_dev_handle *dev,
                                         int configuration);
int     (*lusb_claim_interface)         (usb_dev_handle *dev,
                                         int interface);
int     (*lusb_release_interface)       (usb_dev_handle *dev,
                                         int interface);
void    (*lusb_init)                    (void);
void    (*lusb_set_debug)               (int level);
int     (*lusb_find_busses)             (void);
int     (*lusb_find_devices)            (void);
struct usb_bus* (*lusb_get_busses)      (void);
struct usb_version* (*lusb_get_version) (void);


/*--------------------------------------------------------------

                USB user start

--------------------------------------------------------------*/

int
usb_init_user           (USB_PER_USER *pUser)
{
  Nullit (*pUser);
  pUser->hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
  if (!pUser->hEvent)
    {
    printf("ERROR: Could not create the event object.\n");
    return -1;
    }
  pUser->HA_count = usb.g.HA_count;
  pUser->max_transfer = 65536;
  return 0;
}


/*--------------------------------------------------------------

                USB user close

--------------------------------------------------------------*/

int
usb_deinit_user         (USB_PER_USER *pUser)
{
  CloseHandle (pUser->hEvent);
  Nullit      (*pUser);
  return 0;
}


/*--------------------------------------------------------------

                USB start

--------------------------------------------------------------*/

int
usb_init                (void)
{
        char            UsbFilename  [] = "\\\\.\\Usbscan%d";
        char            cFilename [24], *pFilename;
        int             x;
        LUN_INQUIRY     rLI;
        BOOL            bLibUsbErr;
        struct usb_bus          *bus;
        struct usb_device       *dev;
        struct usb_version      *version;
        usb_dev_handle          *udev;

  usb.g.hScanner   = NULL;
  usb.g.hLibUsbDll = NULL;
  usb.g.HA_count   = 0;

  for (x = 0; x < 10; x++)                      // check //./UsbScan?
    {
    wsprintf (cFilename, UsbFilename, x);
    pFilename = cFilename;
    usb.g.hScanner = CreateFile (pFilename,
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_WRITE | FILE_SHARE_READ,
                                 NULL,
                                 OPEN_EXISTING,
                                 0,
                                 NULL);
    if (usb.g.hScanner == INVALID_HANDLE_VALUE)
      continue;
    if ((0 == usb_unit_inquiry (&rLI))                  &&
        (0 == memcmp (rLI.vendor, "CANON ", 6))         &&
        (0 == memcmp (rLI.product, "IX-40015G ", 10))   )
      {
      printf ("Using %s for scanner access\n", pFilename);
      usb.g.HA_count = 1;
      return usb_init_user (&usb.u);
      }
    CloseHandle (usb.g.hScanner);
    }
  usb.g.hScanner = NULL;                        // 0 = no access via file

  usb.g.hLibUsbDll = LoadLibrary ("LIBUSB0.DLL");
  if (usb.g.hLibUsbDll == 0)
    {
//  printf ("ERROR: LIBUSB0.DLL not found.\n");
    goto bye;
    }

#define LoadPA(x,y) (FARPROC) x = GetProcAddress (usb.g.hLibUsbDll, y); \
                    if (!x) bLibUsbErr = TRUE

  bLibUsbErr = FALSE;
  LoadPA (lusb_open,              "usb_open");
  LoadPA (lusb_close,             "usb_close");
  LoadPA (lusb_bulk_read,         "usb_bulk_read");
  LoadPA (lusb_control_msg,       "usb_control_msg");
  LoadPA (lusb_set_configuration, "usb_set_configuration");
  LoadPA (lusb_claim_interface,   "usb_claim_interface");
  LoadPA (lusb_release_interface, "usb_release_interface");
  LoadPA (lusb_init,              "usb_init");
  LoadPA (lusb_set_debug,         "usb_set_debug");
  LoadPA (lusb_find_busses,       "usb_find_busses");
  LoadPA (lusb_find_devices,      "usb_find_devices");
  LoadPA (lusb_get_busses,        "usb_get_busses");
  LoadPA (lusb_get_version,       "usb_get_version");

  if (bLibUsbErr)
    {
    printf ("ERROR: LIBUSB0.DLL function(s) not found.\n");
    goto bye;
    }

  lusb_init ();
  lusb_find_busses ();
  lusb_find_devices ();

  version = lusb_get_version ();
  if (version)
    {
//  printf ("LibUSB DLL version: %i.%i.%i.%i\n",
//           version->dll.major, version->dll.minor,
//           version->dll.micro, version->dll.nano);
//
    if (version->driver.major == -1)
      {
      printf ("LibUSB driver not running\r\n");
      goto bye;
      }

//  printf ("Driver version:     %i.%i.%i.%i\n",
//           version->driver.major, version->driver.minor,
//           version->driver.micro, version->driver.nano);
    }

  for (bus = lusb_get_busses(); bus; bus = bus->next)
    {
    for (dev = bus->devices; dev; dev = dev->next)
      {
      if (!dev->config)
        continue;

      if ((dev->descriptor.idVendor  != 0x04A9) ||            // Canon
          (dev->descriptor.idProduct != 0x3042) )             // FS4000US
        continue;

      udev = lusb_open (dev);
      if (!udev)
        continue;

//    lusb_set_debug (1);
//    printf ("config value = %d\n", dev->config[0].bConfigurationValue);
      x = lusb_set_configuration (udev, dev->config[0].bConfigurationValue);
      if (x < 0)
        printf ("set config error = %d\n", x);

      usb.g.pDev = dev;
      usb.g.pUdev = udev;
      usb.g.byInterfaceNumber =
        dev->config[0].interface[0].altsetting[0].bInterfaceNumber;

//    printf ("interface = %d\n", usb.g.byInterfaceNumber);
      x = lusb_claim_interface (udev, usb.g.byInterfaceNumber);
      if (x < 0)
        printf ("claim interface error = %d\n", x);

      usb.g.HA_count = 1;
      printf ("Using LibUSB for scanner access\n");
      return usb_init_user (&usb.u);
      }
    }
bye:
  if (usb.g.hLibUsbDll)
    {
    FreeLibrary (usb.g.hLibUsbDll);
    usb.g.hLibUsbDll = NULL;
    }
  return -1;
}


/*--------------------------------------------------------------

                USB close

--------------------------------------------------------------*/

int
usb_deinit              (void)
{
        // release default user fields

  usb_deinit_user (&usb.u);

        // close handle

  if (usb.g.hScanner)
    {
    CloseHandle (usb.g.hScanner);
    usb.g.hScanner = NULL;
    }

  if (usb.g.hLibUsbDll)
    {
    lusb_release_interface (usb.g.pUdev, usb.g.byInterfaceNumber);
    lusb_close (usb.g.pUdev);

        // unload LIBUSB0.DLL

    FreeLibrary (usb.g.hLibUsbDll);
    usb.g.hLibUsbDll = NULL;
    }

  return 0;
}


/*--------------------------------------------------------------

                Execute USB request

--------------------------------------------------------------*/

int
usb_do_request          (DWORD          dwValue,
                         BOOL           bInput,
                         void           *pBuf,
                         DWORD          dwBufLen)
{
        BYTE            byRequest, byRequestType;
        BOOL            bRet;
        DWORD           cbRet, dwFunc;
        IO_BLOCK_EX     IoBlockEx;

        // bit 7        0 = output, 1 = input
        // bits 6-5     2 = vendor special
        // bits 4-0     0 = recipient is device
  byRequestType = bInput ? 0xC0 : 0x40;

        // loaded by driver according to ddk
  byRequest = (dwBufLen < 2) ? 0x0C : 0x04;     // is this significant ?

  if (usb.g.hLibUsbDll)                         // using LibUSB ?
    {
    lusb_control_msg (usb.g.pUdev,              // usb_dev_handle *dev,
                      byRequestType,            // int requesttype,
                      byRequest,                // int request,
                      dwValue,                  // int value,
                      0,                        // int index,
                      pBuf,                     // char *bytes,
                      dwBufLen,                 // int size,
                      0);                       // int timeout);
    return 0;
    }

  Nullit (IoBlockEx);                           // build I/O block
  IoBlockEx.uOffset              = dwValue;
  IoBlockEx.uLength              = dwBufLen;
  IoBlockEx.pbyData              = (BYTE*) pBuf;
  IoBlockEx.uIndex               = 0;
  IoBlockEx.bmRequestType        = byRequestType;
  IoBlockEx.bRequest             = byRequest;
  IoBlockEx.fTransferDirectionIn = bInput;

  cbRet = 0;
  dwFunc = bInput ? IOCTL_READ_REGISTERS : IOCTL_WRITE_REGISTERS;

  bRet = DeviceIoControl (usb.g.hScanner,       // control message
                          dwFunc,
                          &IoBlockEx,
                          sizeof (IoBlockEx),
                          IoBlockEx.pbyData,
                          IoBlockEx.uLength,
                          &cbRet,
                          NULL);

  if (bRet != 0x01)
    {
    printf ("USB DeviceIoControl error = %d\n", GetLastError ());
    return -1;
    }

  return 0;
}


/*--------------------------------------------------------------

                Execute USB SCSI command

--------------------------------------------------------------*/

int
usb_scsi_exec           (void           *cdb,
                         unsigned int   cdb_length,
                         int            mode_and_dir,
                         void           *pdb,
                         unsigned int   pdb_len)
{
        void            *save_pdb    = pdb;
        DWORD           save_pdb_len = pdb_len;
        DWORD           dwBytes, x;
        BYTE            byNull = 0, byOne = 1;
        BYTE            *pbyCmd = (BYTE*) cdb;
        BOOL            bInput;
        DWORD           dwValue, dwValueC5 = 0xC500, dwValue03 = 0x0300;
        BYTE            byStatPDB [4];          // from C500 request
        BYTE            bySensPDB [14];         // from 0300 request

  if (!pdb_len)                         // if no data, use dummy output
    {
    mode_and_dir &= ~SRB_DIR_IN;
    mode_and_dir |= SRB_DIR_OUT;
    pdb = &byNull;                              // default output
    pdb_len = 1;
    if (*pbyCmd == 0x00)                        // change for some
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
  if (*pbyCmd == 0x28)                          // if read
    {
    mode_and_dir &= ~SRB_DIR_IN;                // output not input
    mode_and_dir |= SRB_DIR_OUT;
    pdb = pbyCmd + 6;                           // send buf size
    pdb_len = 3;
    }

  dwValue =  pbyCmd [0] << 8;                   // build parameters
  if (cdb_length > 2)
    if ((*pbyCmd == 0x12) || (*pbyCmd == 0xD5))
      dwValue += pbyCmd [2];
  bInput = (mode_and_dir & SRB_DIR_IN) > 0;

  if (usb_do_request (dwValue, bInput, pdb, pdb_len))   // SCSI via USB
    return -1;

  if (*pbyCmd == 0x28)                          // if read, get bulk data
    {
    if (usb.g.hLibUsbDll)
      dwBytes = lusb_bulk_read (usb.g.pUdev, 0x81, save_pdb, save_pdb_len, 0);
    else
      ReadFile (usb.g.hScanner, save_pdb, save_pdb_len, &dwBytes, NULL);
    if (dwBytes != save_pdb_len)
      return -1;
    }

  if (usb_do_request (dwValueC5, TRUE, &byStatPDB, 4))  // get status
    return -1;

  if (byStatPDB [0] != *pbyCmd)
    if ((byStatPDB [0] != 0) || ((*pbyCmd != 0x16) && (*pbyCmd != 0x17)))
      printf ("cmd mismatch %02X %02X\n", *pbyCmd, byStatPDB [0]);

  if (byStatPDB [1] & 0xFF)                             // sense data ?
    {
    usb_do_request (dwValue03, TRUE, &bySensPDB, 14);   // get sense
    printf ("sense");
    for (x = 0; x < 14; x++)
      printf (" %02X", bySensPDB [x]);
    printf ("\n");
    }

  SetEvent (usb.u.hEvent);                              // I/O done

  return 0;
}


/*--------------------------------------------------------------

                Get LUN inquiry info

--------------------------------------------------------------*/

int
usb_unit_inquiry        (LUN_INQUIRY *pLI)
{
        BYTE            CDB [6];

  Nullit (CDB);
  CDB [0] = SCSI_INQUIRY;
  CDB [4] = 36;
  return usb_scsi_exec (CDB, 6, SRB_DIR_IN, pLI, sizeof (*pLI));
}


