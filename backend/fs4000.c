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
 * operations are provided in fs4000-control.c, this code is derived from the
 * Fs4000.cpp program developed by Steven Saunderson.
 */

#include "../include/sane/config.h"

#define DEBUG_NOT_STATIC
#include "../include/sane/sanei_debug.h"

#include "fs4000.h"
#include "fs4000-usb.h"
#include "fs4000-scsi.h"
#include "fs4000-control.h"

#define FS4000_CONFIG_FILE "fs4000.conf"

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_scsi.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include "../include/lassert.h"

#include <string.h>

#define MINOR_VERSION  0
#define BUILD          1

/* Why is this needed when the build scripts define it? */
#define BACKEND_NAME   fs4000 

/* This needs to be in /etc/sane.d or equivalent */
#define FS4000_CONFIG_FILE "fs4000.conf"


/* debug levels - set using SANE_DEBUG_fs4000 env var 
   These apply to DBG() calls. */
#define D_WARNING 1
#define D_INFO    2
#define D_VERBOSE 3
#define D_TRACE   4

#define FS4000_VENDOR 0x04a9
#define FS4000_PRODUCT 0x3042

enum FS4000_OPTION {
  OPT_FIRST = 0,
  OPT_PRODUCT,
  OPT_NUM  /* last */
};

/* A linked list for multiple Fs4000.  Currently though we can only
   handle one, so this is moot for the time being */
typedef struct fs4000_dev {
  struct scanner* s;
  struct fs4000_dev* next;
  SANE_Device sane;
  SANE_Option_Descriptor opts[OPT_NUM];
  Option_Value val[OPT_NUM];
} fs4000_dev_t;

static fs4000_dev_t* g_firstDevice = NULL;

static int count_fs4000_dev(void)
{
  fs4000_dev_t *p = g_firstDevice;
  int n=0;
  while (p) {
    n++;
    p = p->next;
  }
  return n;
}

static void walk_fs4000_dev(void)
{
  fs4000_dev_t *p = g_firstDevice;
  while (p) {
    DBG (D_INFO, "Walk Device: %s\n", p->sane.name);
    p = p->next;
  }
}

static fs4000_dev_t *find_fs4000_dev(struct scanner* s)
{
  fs4000_dev_t *p = g_firstDevice;
  while (p) {
    if (p->s == s) break;
    p = p->next;
  }
  return p;
}

static SANE_Status fs4000_usb_sane_attach(SANE_String_Const devname)
{
  fs4000_dev_t* dev;
  FS4000_INQUIRY_DATA_IN rLI;
  int er;
  int dn = -1;
  
  DBG (D_INFO, "Possible Fs4000 USB device '%s'\n", devname);
  
  if (g_saneFs4000UsbDn == -1) {
    if (SANE_STATUS_GOOD != (er=sanei_usb_open( devname, &dn))) {
      DBG (D_WARNING, "Could not open USB device '%s'.\n", devname);
      return er;
    }
  } else {
    DBG (D_WARNING, "Only one Fs4000 is supported.\n");
    return SANE_STATUS_UNSUPPORTED;
  }

  /* Assume this does not have to be re-entrant code ... */
  DBG (D_VERBOSE, "dn=%d\n", g_saneFs4000UsbDn);
  assert( g_saneFs4000UsbDn == -1);
  assert( dn >= 0);
  g_saneFs4000UsbDn = dn;

  if ( (er=fs4000_inquiry( &rLI))) {
    DBG (D_WARNING, "fs4000_inquiry() of '%s' failed.\n", devname);
    g_saneFs4000UsbDn = -1;
    sanei_usb_close( dn);
    return SANE_STATUS_IO_ERROR;
  }
  
  DBG (D_INFO, "INQ vendor=%8s product=%16s release=%4s\n", rLI.vendor_id, rLI.product_id, rLI.rev_level);

  if (strncmp( "CANON ", (char*)rLI.vendor_id, 6) || strncmp( "IX-40015G ", (char*)rLI.product_id, 10))
  {
    DBG (D_WARNING, "INQ result: Unexpected vendor / product string '%s, %s'; continuing anyway...\n", rLI.vendor_id, rLI.product_id);
  }

  /* Create a list node for this device */
  dev = calloc(1, sizeof(fs4000_dev_t));
  if (!dev) {
    g_saneFs4000UsbDn = -1;
    sanei_usb_close( dn);
    return SANE_STATUS_NO_MEM;
  }

  /* Later, when adding support for multiple Fs4000, this will need to update */
  assert( g_firstDevice == NULL);

  dev->next = g_firstDevice;
  g_firstDevice = dev;

  dev->s = NULL;
  dev->sane.name = strdup (devname);
  dev->sane.vendor = "CANON";
  dev->sane.model = strndup (rLI.product_id, 9);
  dev->sane.type = "slide scanner";
  
  if (DBG_LEVEL > 1)
    walk_fs4000_dev();

  /* Dont keep device open (wait until we are ready to scan) */
  g_saneFs4000UsbDn = -1;
  sanei_usb_close( dn);
  
  return SANE_STATUS_GOOD;

}

/* ------------------------------------------------------------------------- */
/** 
 * Backend init called by SANE.  
 *
 * Currently only USB is supported. Look for a device matching our vendor and
 * product. 
 *
 * @note  For the time being, we can only support a single Scanner.
 * To fix things to work with multiple scanners will require refactoring the
 * fs4000-scsi.c library to support passing through a handle for the correct
 * USB device.  Only the first scanner found is used, any more are ignored.
 * 
 */
SANE_Status
sane_init (SANE_Int * version_code, SANE_Auth_Callback UNUSEDARG authorize)
{
  const char *pEnv;

  DBG_INIT ();
  DBG (D_VERBOSE, "sane_init()\n");
  DBG (D_INFO, "Canon Fs4000 backend version %d.%d.%d\n",
                   SANE_CURRENT_MAJOR, MINOR_VERSION, BUILD);

  if (version_code != NULL)
  {
      *version_code =
          SANE_VERSION_CODE (SANE_CURRENT_MAJOR, MINOR_VERSION, BUILD);
  }
  
  /* Set library debug level */
  fs4000_debug = 0;
  pEnv = getenv( "SANE_FS4000_LIBDEBUG");
  if (pEnv)
    fs4000_debug = atoi(pEnv);
  
  /* Enable USB transport layer */
  fs4000_do_scsi    = fs4000_usb_scsi_exec;
  g_saneFs4000UsbDn = -1;
  
  sanei_usb_find_devices( FS4000_VENDOR, FS4000_PRODUCT, fs4000_usb_sane_attach);
  if (g_firstDevice == NULL) { 
    DBG (D_INFO, "Found no matching USB devices (%.4x:%.4x)\n", FS4000_VENDOR, FS4000_PRODUCT);
  }

  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
void
sane_exit (void)
{
  DBG (D_VERBOSE, "sane_exit()\n");
  
  while (g_firstDevice) {
    fs4000_dev_t *p;
    p = g_firstDevice;
    DBG (D_VERBOSE, "sane_exit() : destroy : %s\n", p->sane.name);
    fs4k_destroy(p->s);
    g_firstDevice = p->next;
    free(p);
  }
  
  /* TODO Later, when adding support for multiple Fs4000, this needs to
     be updated accordingly */  
  if (g_saneFs4000UsbDn == -1) return;
  sanei_usb_close(g_saneFs4000UsbDn);
  g_saneFs4000UsbDn = -1;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool UNUSEDARG local_only)
{
  /* For some reason the SANE backends like using static const pointers */
  static const SANE_Device **devlist = NULL;
  fs4000_dev_t *p;
  int n;
  int i;

  DBG (D_VERBOSE, "sane_get_devices()\n");
  if (devlist) {
    free (devlist);
    devlist = NULL;
  }

  /* This wants to create an array of devices+1 length the last is NULLed */
  n = count_fs4000_dev();
  devlist = malloc ((n+1) * sizeof (devlist[0]));
  if (!devlist)
    return SANE_STATUS_NO_MEM;

  p=g_firstDevice;
  i=0;
  while (p) {
    devlist[i++] = &p->sane;
    DBG (D_VERBOSE, "sane_get_devices() : %s\n", p->sane.name);
    p=p->next;
  }
  devlist[i++] = NULL;
  *device_list = devlist;
  DBG (D_VERBOSE, "sane_get_devices() n=%d\n", n);
  return SANE_STATUS_GOOD;
}

static void fs4000_sane_feedback(const char *text)
{
  DBG( 1, "[FEEDBACK] %s\n", text);
}

static int fs4000_sane_check_abort(void)
{
  return 0;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  struct scanner *s;
  fs4000_dev_t* dev;
  int i;

  DBG (D_VERBOSE, "sane_open '%s'\n", devicename);

  if (devicename && devicename[0])
  {
    for (dev = g_firstDevice; dev; dev=dev->next) {
	    if (strcmp (dev->sane.name, devicename) == 0)
	      break;
      /* theoretically we can attach again here if needed ... */
    }
  }
  else
    dev = g_firstDevice;

  if (!dev)
    return SANE_STATUS_INVAL;
    
  /* No-one should have the USB device currently... */
  if (g_saneFs4000UsbDn != -1) return SANE_STATUS_INVAL;
    
  /* No-one should be linked to the fs4k lib yet */
  assert( dev->s == NULL);
  if (dev->s) return SANE_STATUS_INVAL;

  /* Setup our data structure.  This malloc()'s ... */
  s = NULL;
  fs4k_init(&s, devicename);
  if (!s) {
    return SANE_STATUS_NO_MEM;
  }
  
  /* Open the USB device again */
  if (SANE_STATUS_GOOD != sanei_usb_open( devicename, &g_saneFs4000UsbDn)) {
    DBG (D_WARNING, "Could not open USB device '%s'.\n", devicename);
    fs4k_destroy(s);
    return SANE_STATUS_IO_ERROR;
  }
  assert( g_saneFs4000UsbDn != -1);

  dev->s = s;
  *handle = s;

  for (i = 0; i < OPT_NUM; i++)
  {
    dev->opts[i].size = sizeof (SANE_Word);
    dev->opts[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  }

  dev->opts[OPT_FIRST].title = SANE_TITLE_NUM_OPTIONS;
  dev->opts[OPT_FIRST].desc = SANE_DESC_NUM_OPTIONS;
  dev->opts[OPT_FIRST].type = SANE_TYPE_INT;
  dev->opts[OPT_FIRST].cap = SANE_CAP_SOFT_DETECT;
  dev->val[OPT_FIRST].w = OPT_NUM;
  
  dev->opts[OPT_PRODUCT].name = "product";
  dev->opts[OPT_PRODUCT].title = "Product";
  dev->opts[OPT_PRODUCT].desc = "Detected Product string";
  dev->opts[OPT_PRODUCT].type = SANE_TYPE_STRING;
  dev->opts[OPT_PRODUCT].cap = SANE_CAP_SOFT_DETECT;
  dev->opts[OPT_PRODUCT].constraint_type = SANE_CONSTRAINT_NONE;
  dev->opts[OPT_PRODUCT].size = strlen( dev->sane.model)+1;
  dev->val[OPT_PRODUCT].s = strdup(dev->sane.model);
  
  fs4k_InitData(s);
  fs4k_SetFeedbackFunction( s, fs4000_sane_feedback);
  fs4k_SetAbortFunction( s, fs4000_sane_check_abort);

  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
void
sane_close (SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev;

  DBG (D_VERBOSE, "sane_close '%s'\n", fs4k_devname(s));
  if (g_saneFs4000UsbDn != -1) {
    sanei_usb_close(g_saneFs4000UsbDn);
  }
  fs4k_destroy(s);
  
  for (dev = g_firstDevice; dev; dev=dev->next) {
    if (dev->s == s) {
      dev->s = NULL;
      break;
    }
  }
  
  g_saneFs4000UsbDn = -1;
}

/* ------------------------------------------------------------------------- */
const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  struct scanner *s = (struct scanner *)handle;

  DBG( D_VERBOSE, "sane_get_option_descriptor(%d) of %d\n", option, OPT_NUM);

  if ((unsigned)option >= OPT_NUM)
    return NULL;

  return (find_fs4000_dev(s))->opts + option;

}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option,
		     SANE_Action action, void *val,
		     SANE_Int * info)
{
  return SANE_STATUS_GOOD;
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

