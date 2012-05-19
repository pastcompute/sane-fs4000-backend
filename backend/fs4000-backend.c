#include "../include/sane/config.h"
#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_scsi.h"

#define BACKEND_NAME fs4000

#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"

#define FS4000_CONFIG_FILE "fs4000.conf"

#ifdef __GNUC__
#define UNUSEDARG __attribute__ ((unused))
#else
#define UNUSEDARG
#endif

#ifndef SANE_I18N
#define SANE_I18N(text)	text
#endif

#define V_MINOR 0

void sanei_fs4000_reserve_unit() {}
void sanei_fs4000_release_unit() {}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * Backend entry point.
 * @param version_code Set it to our backend version code
 * @param authorize Currently unused
 */ 
SANE_Status
sane_init (SANE_Int * version_code, SANE_Auth_Callback UNUSEDARG authorize)
{
  DBG_INIT ();
  DBG (1, ">> sane_init\n");

#if defined PACKAGE && defined VERSION
  DBG (2, "sane_init: " PACKAGE " " VERSION "\n");
#endif

  if (version_code)
    *version_code = SANE_VERSION_CODE (SANE_CURRENT_MAJOR, V_MINOR, 0);

  DBG (1, "<< sane_init\n");
  return SANE_STATUS_GOOD;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * Backend exit point.
 */ 
void
sane_exit (void)
{
  DBG (1, ">> sane_exit\n");

  DBG (1, "<< sane_exit\n");
}

static SANE_Device *dummy[1] = { 0 };

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool local_only)
{
  DBG (1, "sane_get_devices\n");

  *device_list = dummy;

  return SANE_STATUS_GOOD;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  SANE_Status status;

  DBG (1, "sane_open\n");

  return SANE_STATUS_INVAL;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
void
sane_close (SANE_Handle handle)
{
  DBG (11, "sane_close\n");
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  return 0;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option,
		     SANE_Action action, void *val,
		     SANE_Int * info)
{
  return SANE_STATUS_INVAL;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  DBG (1, "sane_get_parameters");
  params->format =  SANE_FRAME_RGB;
  params->depth = 16;
  params->pixels_per_line = 640; /* pixels_per_line (scanner);*/
  params->lines = 640; /* lines_per_scan (scanner);*/
  params->bytes_per_line = 1280; /* write_bytes_per_line (scanner); */
  params->last_frame = 1;
  return SANE_STATUS_GOOD;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_start (SANE_Handle handle)
{
  DBG (1, "sane_start\n");
  return SANE_STATUS_DEVICE_BUSY;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * buf,
	   SANE_Int max_len, SANE_Int * len)
{
  DBG (1, "sane_read:");
  return SANE_STATUS_IO_ERROR;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
void
sane_cancel (SANE_Handle handle)
{
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  DBG (1, "sane_set_io_mode: non_blocking=%d\n", non_blocking);
  return SANE_STATUS_GOOD;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * ...
 */ 
SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  DBG (1, "sane_get_select_fd\n");
  return SANE_STATUS_INVAL;
}

