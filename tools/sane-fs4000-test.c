/*
 * Test harness for fs4000 library code
 *
 */
#define __MAIN__
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

#include <unistd.h>

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include <string.h>


               

typedef struct  /* LUN_INQUIRY
 */
{
        uint8_t            reserved [8];
        uint8_t            vendor   [8];
        uint8_t            product  [16];
        uint8_t            release  [4];
}
  LUN_INQUIRY;

static SANE_Status sanei_fs4000_attach(SANE_String_Const devname)
{
    int er;
  printf("Found: %s\n", devname);
  /* Does this need a mutex? */
  if (g_saneFs4000UsbDn == -1) {
    printf("Opening: %s\n", devname);
    if (SANE_STATUS_GOOD != (er=sanei_usb_open( devname, &g_saneFs4000UsbDn))) {
      fprintf(stderr, "Failed to open! %d\n", er);
      return er;
    }
  } else {
    fprintf(stderr, "Only one supported! %d\n", g_saneFs4000UsbDn);
    return SANE_STATUS_INVAL;
  }
  printf("dn=%d\n", g_saneFs4000UsbDn);

  if (TRUE) {
    FS4000_INQUIRY_DATA_IN rLI;
    if ( (er=fs4000_inquiry( &rLI)))
      return er;
  
    printf("vendor=%8s product=%16s release=%4s\n", rLI.vendor_id, rLI.product_id, rLI.rev_level);

    if (strncmp( "CANON ", (char*)rLI.vendor_id, 6) || strncmp( "IX-40015G ", (char*)rLI.product_id, 10))
      return SANE_STATUS_INVAL;
  }
  return SANE_STATUS_GOOD;
}



int main(int argc, char*argv[])
{ 

  int iSpeed = 2;

  FS4000_GET_FILM_STATUS_DATA_IN_28        rFS         ;
  FS4000_GET_LAMP_DATA_IN          rLI         ;
  FS4000_SCAN_MODE_INFO rSMI        ;
  FS4000_GET_WINDOW_DATA_IN        rWI         ;

  memset( &rFS, 0, sizeof(rFS));
  memset( &rLI, 0, sizeof(rLI));
  memset( &rSMI, 0, sizeof(rSMI));
  memset( &rWI, 0, sizeof(rWI));

  fs4000_do_scsi    = fs4000_usb_scsi_exec;

  /* usbinit */
  DBG_INIT();
  DBG (1, "sane_init: SANE Fs4000 backend version %d.%d.%d from %s\n",
       SANE_CURRENT_MAJOR, V_MINOR, 0, PACKAGE_STRING);
  sanei_usb_init ();
  
/*
  sanei_usb_attach_matching_devices (config_line, attach_one);
*/

  /* fs4k_BOJ maxtransfer=65536 */


  printf("searching...\n");
  g_saneFs4000UsbDn = -1;
  sanei_usb_find_devices( 0x04a9, 0x3042, sanei_fs4000_attach);
  printf("searched.\n");

  if (g_saneFs4000UsbDn == -1) { 
    printf("not found.\n");
    return 1;
  }

  fs4000_debug = 1;
  

  printf("waiting for ready...\n");
  while (fs4000_test_unit_ready ()) {
    /* kbhit / getch if (AbortWanted ()) */
    sleep(1);
  }
  printf("querying...\n");

      fs4000_cancel();

  if (fs4000_get_film_status_rec (&rFS)) {
    fprintf(stderr, "Error on get_film_status_rec\n");
  }
  else if (fs4000_get_lamp_rec (&rLI)) {
    fprintf(stderr, "Error on get_lamp_rec\n");
  }
  else if (fs4000_get_scan_mode_rec (&rSMI.scsi)) {
    fprintf(stderr, "Error on scan_mode_rec\n");
  }
  else {
    memset(&rSMI.control.unknown1b [2], 0, 9);
    rSMI.control.bySpeed      = iSpeed;
    rSMI.control.bySampleMods = 0;
    rSMI.control.unknown2a    = 0;
    rSMI.control.byImageMods  = 0;
    if (fs4000_put_scan_mode_rec (&rSMI))
    {
      fprintf(stderr, "Error on put_scan_mode_rec\n");
    }  
    else if (fs4000_get_window_rec (&rWI))
    {
      fprintf(stderr, "Error on get_window_rec\n");
    }  
    else if (fs4000_put_window_rec (&rWI))
    {
      fprintf(stderr, "Error on put_window_rec\n");
    }  
    else {
      fs4000_reserve_unit  ();
    fs4000_control_led (2); /* blink rate */
    fs4000_test_unit_ready ();
    
    fs4000_get_film_status_rec (&rFS);
    printf("Film Holder Type %d\n", rFS.film_holder_type);
    printf("Frame %d\n", (rFS.unknown1 [0] >> 3));


/*
  fs4000_set_frame (0); // R2L
  fs4000_set_frame (1); // L2R
  fs4000_set_frame (12); // No move?
  fs4000_set_frame (8); // R2L no move
*/
  /* position is the scanner itself not the holder frame */
    fs4000_set_frame (0);
    fs4000_move_position (0, 0, 0); /* carriage home, home */
    fs4000_move_position (1, 0, 0); /*    fs4k_MoveHolder (0);*/
      
    fs4000_move_position (1, 4, 2*337); /* white pos, positives  */
    sleep(2);
    fs4000_move_position (1, 4, 798); /* black pos, positivess  */
    sleep(2);
    fs4000_move_position (1, 4, 90); /* black pos, neg  */
    sleep(2);
    fs4000_move_position (1, 4, 2*17); /* white pos, neg  */
    sleep(2);
    
    sleep (2);

    fs4000_control_led (0);

      fs4000_release_unit  ();

    fs4000_move_position (0, 0, 0); /* carriage home, home */
      fs4000_set_lamp (0, 0); /*fs4k_LampOff    (0);*/
    fs4000_move_position (1, 0, 0); /*    fs4k_MoveHolder (0);*/


/* eject */
/*
    fs4000_move_position (0, 0, 0);
    fs4000_move_position (1, 1, 0);
*/    

    }
  }

  sanei_usb_close( g_saneFs4000UsbDn);
  
  return 0;
}

