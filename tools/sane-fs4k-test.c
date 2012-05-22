/*
 * Test harness for fs4k library code
 *
 */
#ifndef BACKEND_NAME
#define BACKEND_NAME "fs4000"
#endif

#include <stdio.h>
#include "../include/sane/config.h"

#define DEBUG_NOT_STATIC
#include "../include/sane/sanei_debug.h"
#include "../backend/fs4000.h"

#include "../backend/fs4000-scsi.h"
#include "../backend/fs4000-scsidefs.h"
#include "../backend/fs4000-usb.h"
#include "../backend/fs4000-control.h"

#include <unistd.h>

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include <string.h>

static void feedback(const char *text)
{
  printf("[FEEDBACK] %s\n", text);
}

static int check_abort(void)
{
  return 0;
}

static SANE_Status sanei_fs4000_attach(SANE_String_Const devname)
{
  int er;
  printf("Found: %s\n", devname);
  if (g_saneUsbDn == -1) {
    printf("Opening: %s\n", devname);
    if (SANE_STATUS_GOOD != (er=sanei_usb_open( devname, &g_saneUsbDn))) {
      fprintf(stderr, "Failed to open! %d\n", er);
      return er;
    }
  } else {
    fprintf(stderr, "Only one supported! %d\n", g_saneUsbDn);
    return SANE_STATUS_INVAL;
  }
  printf("dn=%d\n", g_saneUsbDn);
  return SANE_STATUS_GOOD;
}
int main(int argc, char*argv[])
{
  struct scanner *s;

  fs4000_do_scsi    = fs4000_usb_scsi_exec;

  DBG_INIT();
  sanei_usb_init ();
  printf("searching...\n");
  g_saneUsbDn = -1;
  sanei_usb_find_devices( 0x04a9, 0x3042, sanei_fs4000_attach);
  printf("searched.\n");
  if (g_saneUsbDn == -1) { 
    printf("not found.\n");
    return 1;
  }
  fs4000_debug = 1;
  printf("waiting for ready...\n");
  
  
  fs4k_init(&s);
  fs4k_SetFeedbackFunction( s, feedback);
  fs4k_SetAbortFunction( s, check_abort);
  
  while (fs4000_test_unit_ready ()) {
    /* kbhit / getch if (AbortWanted ()) */
    sleep(1);
  }
  printf("querying...\n");
  
  fs4000_cancel();

  fs4000_reserve_unit ();

    fs4000_move_position (1, 4, 90); /* black pos, neg  */

  printf("lamp on 1...\n");
  fs4k_LampOn( s, 1);
  
  printf("lamp off...\n");
  fs4k_LampOff( s, 1);
  
  fs4k_Scan( s, 2, 0);
  
  printf("cleanup\n");
  
  fs4000_control_led (0);
  fs4000_release_unit  ();
  fs4000_move_position (0, 0, 0); /* carriage home, home */
  fs4000_move_position (1, 0, 0); /*    fs4k_MoveHolder (0);*/
  
  fs4k_destroy(s);
}

