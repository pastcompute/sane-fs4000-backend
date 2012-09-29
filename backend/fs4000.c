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
#define BUILD          2

/* Why is this needed when the build scripts define it? */
#define BACKEND_NAME   fs4000 

/* This needs to be in /etc/sane.d or equivalent */
#define FS4000_CONFIG_FILE "fs4000.conf"


/** FS4000 USB Vendor ID */
#define FS4000_USB_VENDOR 0x04a9
/** FS4000 USB Product ID */
#define FS4000_USB_PRODUCT 0x3042

/**
 * Sane scanner options.
 */
enum FS4000_OPTION {
  OPT_FIRST = 0,  /** Always 0 - always the first, required by SANE */
  OPT_PRODUCT,    /** For diagnostics, returns the product string from device */
  OPT_SLIDE_POSITION,
  OPT_NEG_POSITION,
  OPT_NUM         /** MUST BE LAST, is the number of optoin array elements */
};

/** 
 * A linked list to manage attached Fs4000.  
 * This is moot for the time being though as only the first USB Fs4000 detected
 * will be added to the list currently. 
 */
typedef struct fs4000_dev {
  struct fs4000_dev* next;   /** Next list element, or NULL if last */
  struct scanner* s;         /** fs4k scanner details structure, this is added
                               * in sane_open() and removed in sane_close() */
  SANE_Device sane;          /** Device handle allocated by SANE */
  int state;                 /** 0 == not started, 1 == just scanned, 2 == return EOF from next sane_read() call */
  DWORD frameSizeBytes;   /** Amount of data still to be returned from sane_read() */
  SANE_Int readBytesToGo;    /** Amount of data still to be returned from sane_read() */
  SANE_Byte* frameBuffer;
  SANE_Bool cancelled;
  SANE_Int holder_mode;     
    /* Value depends on whether --iframe-neg or --iframe-slide set by user - one onf FILM_HOLDER_NEG or FILM_HOLDER_POS */
  
  /** 
   * Array of option descriptions. Although this will be the same for all
   * scanner instances, the usual design pattern is to have this per device
   * because some backends support multiple scanner types for which this may
   * vary.
   */
  SANE_Option_Descriptor opts[OPT_NUM];
  /** Values for the options */
  Option_Value val[OPT_NUM];
} fs4000_dev_t;

/* Option types (was hard to find meaning in doco:)
   SANE_CAP_ADVANCED Suitable for advanced users,
   SANE_CAP_HARD_SELECT Retrieved by operating on scanner
   SANE_CAP_SOFT_DETECT Can be calculated in software; often in conjunction with SANE_CAP_HARD_SELECT
   SANE_CAP_SOFT_SELECT Can be set
   SANE_CAP_INACTIVE The option is not currently active, usually another option should be selected first
   SANE_CAP_EMULATED The option will be emulated by the backend, as the device does not directly support this option
   SANE_CAP_AUTOMATIC
   
   SANE_CONSTRAINT_NONE
   SANE_CONSTRAINT_RANGE Value has to be with in a certain range (Fixed or Int)
   SANE_CONSTRAINT_STRING_LIST Value can be one from a list of strings
   SANE_CONSTRAINT_WORD_LIST Value can be one from a list of integers
   */
   

static SANE_Option_Descriptor g_optFirst = {
  SANE_NAME_NUM_OPTIONS,
  SANE_TITLE_NUM_OPTIONS,
  SANE_DESC_NUM_OPTIONS,
  SANE_TYPE_INT,
  SANE_UNIT_NONE,
  sizeof (SANE_Word),
  SANE_CAP_SOFT_DETECT,
  SANE_CONSTRAINT_NONE,
  {NULL}
};

static SANE_Option_Descriptor g_optProduct = {
  "product",
  SANE_I18N ("Product"),
  SANE_I18N ("Product string detected from scanner."),
  SANE_TYPE_STRING,
  SANE_UNIT_NONE,
  0, /* Update this in the copy, to the length of the string... */
  SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT,
  SANE_CONSTRAINT_NONE,
  {NULL}
};
  
SANE_Range g_slideFrameRange = { 1, MAX_FRAME_SLIDE_MOUNT+1, 1 };
SANE_Range g_negFrameRange = { 1, MAX_FRAME_NEGATIVE_MOUNT+1, 1 };
  
  /* There is probably a better wat do to this, using a single --frame-pos arg ... 
     but this gets things working */
  
  /* These should be marked mutually exclusive somehow */
  
static SANE_Option_Descriptor g_optSlideFramePosition = {
  "iframe-slide",
  SANE_I18N ("Frame to scan assuming slide holder inserted"),
  SANE_I18N ("Frame to scan, 1..4 for slides"),
  SANE_TYPE_INT,
  SANE_UNIT_NONE,
  sizeof(SANE_Word),
  /* Validity is checked by examining to holder type, auto --> pos 1 */
  SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_AUTOMATIC ,
  SANE_CONSTRAINT_RANGE,
  { (void*)& g_slideFrameRange }
};
  
static SANE_Option_Descriptor g_optNegFramePosition = {
  "iframe-neg",
  SANE_I18N ("Frame to scan assuming negative holder inserted"),
  SANE_I18N ("Frame to scan, 1..6 for negatives"),
  SANE_TYPE_INT,
  SANE_UNIT_NONE,
  sizeof(SANE_Word),
  /* Validity is checked by examining to holder type */
  SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_AUTOMATIC ,
  SANE_CONSTRAINT_RANGE,
  { (void*)& g_negFrameRange }
};


/** 
 * Head of the list of attached devices
 */
static fs4000_dev_t* g_firstDevice = NULL;

/** Helper to count number of devices in the list 
 * @return Number of elements (devices attached)
 */
static int count_fs4000_dev(void)
{
  const fs4000_dev_t *p = g_firstDevice;
  int n=0;
  while (p) {
    n++;
    p = p->next;
  }
  return n;
}

/** Helper to walk the list and print found devices to debug log */
static void walk_fs4000_dev(void)
{
  fs4000_dev_t *p = g_firstDevice;
  while (p) {
    DBG (D_INFO, "Walk Device: %s\n", p->sane.name);
    p = p->next;
  }
}

/** Helper to find the device element given a scanner structure 
 * @param s fs4k scanner data
 * @return Matching device
 */
static fs4000_dev_t *find_fs4000_dev(struct scanner* s)
{
  fs4000_dev_t *p = g_firstDevice;
  while (p) {
    if (p->s == s) break;
    p = p->next;
  }
  return p;
}

/**
 * Sane callback helper to attach a device to this backend
 *
 * @note Current architecture:
 * This uses and release the fs4000 USB transport, and thus
 * sets the global g_saneFs4000UsbDn. It is thus not re-entrant,
 * and neither is it safe to use between sane_open() and sane_close()
 */
static SANE_Status fs4000_usb_sane_attach(SANE_String_Const devname)
{
  fs4000_dev_t* dev;
  FS4000_INQUIRY_DATA_IN rLI;
  int er;
  int dn = -1;
  
  DBG (D_INFO, "Possible Fs4000 USB device '%s'\n", devname);
  
  /* FIXME remove this when we upgrade the fs4000_ library */
  assert(g_saneFs4000UsbDn == -1); 

  if (g_saneFs4000UsbDn == -1) {
    if (SANE_STATUS_GOOD != (er=sanei_usb_open( devname, &dn))) {
      DBG (D_WARNING, "Could not open USB device '%s'.\n", devname);
      return er;
    }
  } else {
    DBG (D_WARNING, "Only one Fs4000 is supported, or .\n");
    return SANE_STATUS_UNSUPPORTED;
  }

  DBG (D_VERBOSE, "USB dn=%d\n", g_saneFs4000UsbDn);
  assert( g_saneFs4000UsbDn == -1);
  assert( dn >= 0);
  g_saneFs4000UsbDn = dn;

  memset(&rLI, 0, sizeof(rLI));
  if ( (er=fs4000_inquiry( &rLI))) {
    DBG (D_WARNING, "fs4000_inquiry() of '%s' failed.\n", devname);
    g_saneFs4000UsbDn = -1;
    sanei_usb_close( dn);
    return SANE_STATUS_IO_ERROR;
  }
  
  DBG (D_INFO, "INQ vendor=%*s product=%*s release=%*s\n", 
                (int)sizeof(rLI.vendor_id), rLI.vendor_id, 
                (int)sizeof(rLI.product_id), rLI.product_id,
                (int)sizeof(rLI.rev_level), rLI.rev_level);

  if (strncmp( "CANON ", (char*)rLI.vendor_id, 6) || 
      strncmp( "IX-40015G ", (char*)rLI.product_id, 10))
  {
    DBG (D_WARNING, "INQ result: Unexpected vendor / product string '%s, %s';"
                    " continuing anyway...\n", rLI.vendor_id, rLI.product_id);
  }

  /* FIXME Currently we only support one Fs4000 */
  assert( g_firstDevice == NULL);

  /* Create a list node for this device */
  dev = calloc(1, sizeof(fs4000_dev_t));
  if (!dev) {
    g_saneFs4000UsbDn = -1;
    sanei_usb_close( dn);
    return SANE_STATUS_NO_MEM;
  }
  dev->next = g_firstDevice;
  g_firstDevice = dev;

  dev->s = NULL;
  dev->sane.name = strdup (devname);
  dev->sane.vendor = "CANON";
  dev->sane.model = strndup (rLI.product_id, 9);
  dev->sane.type = "slide scanner";
  dev->state = -1;
  dev->frameBuffer = NULL;
  dev->frameSizeBytes = 0;
  dev->holder_mode = FILM_HOLDER_NONE_OR_EMPTY; /* nothing set yet */
  
  if (DBG_LEVEL > 1)
    walk_fs4000_dev();

  /* Dont keep device open (wait until we are ready to scan) */
  sanei_usb_close( dn);
  g_saneFs4000UsbDn = -1;
  
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

#ifdef HAVE_LIBUSB_1_0
  DBG (D_VERBOSE, "backend built with libusb-1.0\n");
#endif
#ifdef HAVE_LIBUSB
  DBG (D_VERBOSE, "backend built with libusb\n");
#endif

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
  sanei_usb_init ();
  fs4000_do_scsi    = fs4000_usb_scsi_exec;
  g_saneFs4000UsbDn = -1;

  /* This will call the sane_attach function and build the list */
  assert (g_firstDevice == NULL);
  sanei_usb_find_devices( FS4000_USB_VENDOR, FS4000_USB_PRODUCT, 
                          fs4000_usb_sane_attach);
                          
  if (g_firstDevice == NULL) { 
    DBG (D_INFO, "Found no matching USB devices (%.4x:%.4x)\n", 
                  FS4000_USB_VENDOR, FS4000_USB_PRODUCT);
  }

  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
void
sane_exit (void)
{
  DBG (D_VERBOSE, "sane_exit()\n");
  
  /* Release all attached devices */
  while (g_firstDevice) {
    fs4000_dev_t *p = g_firstDevice;
    DBG (D_VERBOSE, "sane_exit() : destroy : %s\n", p->sane.name);
    assert( p->s == NULL);  /* should not happen if sane_close() properly called */
    fs4k_destroy(p->s);
    g_firstDevice = p->next;
    free(p);
  }
  
  /* We should not get here between sane_open() and sane_close() */
  assert( g_saneFs4000UsbDn == -1);
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

/** fs4k API call for reporting feedback such as progress messages */
static void fs4000_sane_feedback(const char *text)
{
  DBG( 1, "[FEEDBACK] %s\n", text);
}

/** fs4k API call for testing if we should abort the current operation */
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
  assert( g_saneFs4000UsbDn == -1);
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
  
  /* Open the USB device; from here, this device will own the global USB DN */
  if (SANE_STATUS_GOOD != sanei_usb_open( devicename, &g_saneFs4000UsbDn)) {
    DBG (D_WARNING, "Could not open USB device '%s'.\n", devicename);
    fs4k_destroy(s);
    return SANE_STATUS_IO_ERROR;
  }
  assert( g_saneFs4000UsbDn != -1);

  dev->s = s;
  dev->state = 0;
  dev->cancelled = SANE_FALSE;
  *handle = s;

  /* The first optoin is read-only and  returns the number of options available 
     It should be the first option in the options array */

  dev->opts[OPT_FIRST] = g_optFirst;
  dev->val[OPT_FIRST].w = OPT_NUM;

  dev->opts[OPT_PRODUCT] = g_optProduct;
  dev->opts[OPT_PRODUCT].size = strlen( dev->sane.model)+1;
  dev->val[OPT_PRODUCT].s = strdup(dev->sane.model);
  
  dev->opts[OPT_SLIDE_POSITION] = g_optSlideFramePosition;
  dev->val[OPT_SLIDE_POSITION].w = 1; /* if unspecified use the first (outermost) position */

  dev->opts[OPT_NEG_POSITION] = g_optNegFramePosition;
  dev->val[OPT_NEG_POSITION].w = 1; /* if unspecified use the first (outermost) position */

  fs4k_InitData(s);
  fs4k_SetFeedbackFunction( s, fs4000_sane_feedback);
  fs4k_SetAbortFunction( s, fs4000_sane_check_abort);

  /* preinitialise the scanner to a default sane state ready for scanning. */
  if (fs4k_InitCommands(s)) {
    DBG (D_WARNING, "Unable to initialise scanner: code=%d.\n", fs4k_GetLastError(s));
    fs4k_destroy(s);
    return SANE_STATUS_IO_ERROR;
  }

  /* For now make it 8-bit for speed. This needs to be converted into an option. */  
  fs4k_SetInMode(s, 8); /* should be 8, 14 or 16 */



  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
void
sane_close (SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev = find_fs4000_dev(s);

  DBG (D_VERBOSE, "sane_close '%s'\n", fs4k_devname(s));
  assert( g_saneFs4000UsbDn != -1);
  assert( dev->state == -1 || dev->state == 0);

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
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev = find_fs4000_dev(s);

  DBG( D_VERBOSE, "sane_control_option %d{%s}\n", option, dev->opts[option].name);

  switch (option) {
  case OPT_FIRST:
    if (action != SANE_ACTION_GET_VALUE) return SANE_STATUS_UNSUPPORTED;
    *(SANE_Word*)val = OPT_NUM;
    break;
  case OPT_PRODUCT:
    if (action != SANE_ACTION_GET_VALUE) return SANE_STATUS_UNSUPPORTED;
    strcpy (val, dev->val[option].s);
    break;

  /* TODO: Somehow integrate the ScanIMage batch op to do a whole slide holder */
    
  case OPT_NEG_POSITION:
    if (action == SANE_ACTION_GET_VALUE) {
      *(SANE_Word*)val = dev->val[option].w;
      break;
    }
    if (dev->holder_mode == FILM_HOLDER_NONE_OR_EMPTY) {
      DBG(1, "Setting Position %u for NEGATIVE holder\n", *(SANE_Word*)val);
      dev->holder_mode = FILM_HOLDER_NEG;
      dev->val[option].w = *(SANE_Word*)val;
      break;
    }
    DBG(1, "Already selected a holder mode frame position\n");
    return SANE_STATUS_UNSUPPORTED;
        
  case OPT_SLIDE_POSITION:
    if (action == SANE_ACTION_GET_VALUE) {
      *(SANE_Word*)val = dev->val[option].w;
      break;
    }
    if (dev->holder_mode == FILM_HOLDER_NONE_OR_EMPTY) {
      DBG(1, "Setting Position %u for SLIDE holder\n", *(SANE_Word*)val);
      dev->holder_mode = FILM_HOLDER_POS;
      dev->val[option].w = *(SANE_Word*)val;
      break;
    }
    DBG(1, "Already set a holder mode frame position\n");
    return SANE_STATUS_UNSUPPORTED;

  default:    
    return SANE_STATUS_UNSUPPORTED;
  }
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev = find_fs4000_dev(s);
  DWORD checkBytes;
  BYTE* checkBuffer;

  DBG (D_VERBOSE, "sane_get_parameters '%s' state=%d cancelled=%d\n", fs4k_devname(s), dev->state, dev->cancelled);

  /* Currently we assume this is only called after scan_start is called... */
  params->format = SANE_FRAME_RGB;
  params->last_frame = SANE_TRUE;
  
  if (fs4k_GetLastFrameInfo( s,
        &params->lines,
        &params->bytes_per_line,
        &params->pixels_per_line,
        &params->depth)) {
    return SANE_STATUS_INVAL;
  }

  checkBytes = fs4k_GetScanResult(s, &checkBuffer);
  
  /* should be same as that stored in dev ... */
  if (!(checkBytes==dev->frameSizeBytes && checkBuffer == dev->frameBuffer)) {
    /* Should never happen! */
    DBG (D_WARNING, "sane_get_parameters  buffer size mismatch! expect %p:%u got %p:%u\n", 
        dev->frameBuffer, dev->frameSizeBytes, checkBuffer, checkBytes);
    return SANE_STATUS_INVAL;
  }

  DBG (D_INFO, "sane_get_parameters: "
        "linebytes=%u lines=%u pixels/line=%u depth=%u expected_buf=%u\n", 
        params->bytes_per_line, params->lines, params->pixels_per_line, 
        params->depth, checkBytes
        );

  
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_start (SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev = find_fs4000_dev(s);
  SANE_Word frameIndex, frameMax;
  FS4000_GET_FILM_STATUS_DATA_IN_28 fs;
  SANE_Word left=0,top=0,width=4000,height=5904;
  
  /* Initial naive approach: do all the scanning here.
   * Then return the buffer through read.
   * TODO: Split fs4k_Read() up so we can initiate from here
   * then return bit by bit it sane_read(), that would make things 
   * much more efficient.
   */

  DBG (D_VERBOSE, "sane_start '%s' state=%d cancelled=%d\n", fs4k_devname(s), dev->state, dev->cancelled);
  if (dev->state < 0) return SANE_STATUS_INVAL;
  if (dev->state > 0) return SANE_STATUS_DEVICE_BUSY;
  if (dev->cancelled) return SANE_STATUS_CANCELLED;


  if (fs4000_get_film_status_rec (&fs)) {
    DBG( D_WARNING, "Unable to retrieve film status\n");
    return SANE_STATUS_IO_ERROR;
  }
  if (fs.film_holder_type == FILM_HOLDER_NONE_OR_EMPTY) {
    /* holder is out... */
    DBG( D_WARNING, "Holder is ejected...\n");
    return SANE_STATUS_NO_DOCS;
  }

  if (dev->holder_mode != fs.film_holder_type) {
    DBG(1, "Incorrect holder type detected for frame selection (select=%u detected=%u)\n", dev->holder_mode, fs.film_holder_type);
    return SANE_STATUS_NO_DOCS;
  }


  switch (fs.film_holder_type) {
  case FILM_HOLDER_NEG: 
    frameMax = MAX_FRAME_NEGATIVE_MOUNT;
    frameIndex = dev->val[OPT_NEG_POSITION].w - 1;
    break;
  case FILM_HOLDER_POS: 
    frameMax = MAX_FRAME_SLIDE_MOUNT;
    frameIndex = dev->val[OPT_SLIDE_POSITION].w - 1;
    break;
  }
  
  if (frameIndex > frameMax) {
    DBG( 1, "Bad frame index was specified. %u > %u\n", frameIndex, frameMax);
  }
  
  DBG (D_INFO, "SCANNING Frame Position %u of %u\n", frameIndex+1, frameMax+1);

  fs4k_Scan( s, frameIndex, left, top, width, height, SANE_FALSE);
  
  if (fs4k_GetLastError(s)) {
    fs4k_FreeResult(s);
    return SANE_STATUS_IO_ERROR;
  }
  
  dev->state = 1;
  dev->frameSizeBytes = dev->readBytesToGo = fs4k_GetScanResult(s, &dev->frameBuffer);

  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * buf,
	   SANE_Int max_len, SANE_Int * len)
{
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev = find_fs4000_dev(s);
  size_t framePos;
  BYTE* checkBuffer = NULL;
  DWORD checkBytes;

  DBG (D_VERBOSE, "sane_read '%s' state=%d remain=%u\n", fs4k_devname(s), dev->state, dev->readBytesToGo);

  if (dev->cancelled) { if (len) *len = 0; return SANE_STATUS_CANCELLED; }
  if (!dev->frameBuffer) { if (len) *len = 0; return SANE_STATUS_EOF; }
  if (!(dev->state == 1 || dev->state == 2)) { 
    DBG (D_WARNING, "sane_read called in invalid state.\n");
    if (len) *len = 0; 
    return SANE_STATUS_EOF; 
  }

  checkBytes = fs4k_GetScanResult(s, &checkBuffer);
  if (!(checkBytes==dev->frameSizeBytes && checkBuffer == dev->frameBuffer)) {
    /* Should never happen! */
    DBG (D_WARNING, "sane_read  buffer size mismatch! expect %p:%u got %p:%u\n", 
        dev->frameBuffer, dev->frameSizeBytes, checkBuffer, checkBytes);
    if (len) *len = 0; 
    return SANE_STATUS_EOF;
  }
  
  switch (dev->state) {
  case 1:
    if (dev->readBytesToGo < max_len) {
      if (len) *len = dev->readBytesToGo;
    } else {
      if (len) *len = max_len;
    }
    framePos = dev->frameSizeBytes - dev->readBytesToGo;
    memcpy( buf, dev->frameBuffer + framePos, *len);
    dev->readBytesToGo -= *len;
    assert( dev->readBytesToGo >= 0);
    if (dev->readBytesToGo == 0)
      dev->state = 2;
    return SANE_STATUS_GOOD;
  case 2:
  default:
    fs4k_FreeResult(s);
    DBG (D_VERBOSE, "sane_read '%s' finished\n", fs4k_devname(s));
    dev->state = 0;
    if (len) *len = 0;
    return SANE_STATUS_EOF;
  }  
}

/* ------------------------------------------------------------------------- */
void
sane_cancel (SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  fs4000_dev_t* dev = find_fs4000_dev(s);

  DBG (D_VERBOSE, "sane_cancel '%s' state=%d remain=%u\n", fs4k_devname(s), dev->state, dev->readBytesToGo);

  dev->cancelled = SANE_TRUE;

  fs4k_FreeResult(s);

  dev->state = 0;
  dev->frameBuffer = NULL;
  dev->frameSizeBytes = 0;
  dev->readBytesToGo = 0;

  fs4000_control_led (0);
  fs4000_release_unit  ();
  fs4000_move_position (0, 0, 0); /* carriage home, home */
  fs4000_move_position (1, 0, 0); /*    fs4k_MoveHolder (0);*/
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_set_io_mode (SANE_Handle UNUSEDARG handle, SANE_Bool UNUSEDARG non_blocking)
{
  return SANE_STATUS_UNSUPPORTED;
}

/* ------------------------------------------------------------------------- */
SANE_Status
sane_get_select_fd (SANE_Handle UNUSEDARG handle, SANE_Int UNUSEDARG * fd)
{
  return SANE_STATUS_UNSUPPORTED;
}

int fs4000_scsi_log( const char *msg, ...)
{
  char buf[512];
  int r;
  va_list ap;
  va_start(ap, msg);
  r = vsnprintf( buf, 511, msg, ap);
  DBG(3, buf);
  va_end(ap);
  return r;
}

