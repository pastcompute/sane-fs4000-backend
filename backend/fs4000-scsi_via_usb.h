#ifndef _SCSI_VIA_USB_H_
#define _SCSI_VIA_USB_H_

#include "wnaspi32.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

//--------------------------------------------------------------
//
//              For USB access via file
//
//      W98 rejects IOCTL_SEND_USB_REQUEST so we have to use
//      IOCTL_READ_REGISTERS and IOCTL_WRITE_REGISTERS instead.
//
//      W2k accepts all three functions.

#define FILE_DEVICE_USB_SCAN            0x8000
#define IOCTL_INDEX                     0x0800

#define IOCTL_READ_REGISTERS \
  CTL_CODE(FILE_DEVICE_USB_SCAN, IOCTL_INDEX + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE_REGISTERS \
  CTL_CODE(FILE_DEVICE_USB_SCAN, IOCTL_INDEX + 4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SEND_USB_REQUEST \
  CTL_CODE(FILE_DEVICE_USB_SCAN, IOCTL_INDEX + 9, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _IO_BLOCK_EX
{
        ULONG   uOffset;
        ULONG   uLength;
        PUCHAR  pbyData;
        ULONG   uIndex;
        UCHAR   bRequest;
        UCHAR   bmRequestType;
        UCHAR   fTransferDirectionIn;
}
  IO_BLOCK_EX, *PIO_BLOCK_EX;

//--------------------------------------------------------------
//
//              For USB access via LibUSB

#include <stdlib.h>

/* 'interface' is defined somewhere in the Windows header files. To avoid  */
/* conflicts and compilation errors, this macro is deleted here */

#ifdef interface
#undef interface
#endif

/* PATH_MAX from limits.h can't be used on Windows, when  the dll and
   import libraries are build/used by different compilers */

#define LIBUSB_PATH_MAX 512


/*
 * USB spec information
 *
 * This is all stuff grabbed from various USB specs and is pretty much
 * not subject to change
 */

/*
 * Device and/or Interface Class codes
 */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PRINTER		7
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_DATA			10
#define USB_CLASS_VENDOR_SPEC		0xff

/*
 * Descriptor types
 */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05

#define USB_DT_HID			0x21
#define USB_DT_REPORT			0x22
#define USB_DT_PHYSICAL			0x23
#define USB_DT_HUB			0x29

/*
 * Descriptor sizes per descriptor type
 */
#define USB_DT_DEVICE_SIZE		18
#define USB_DT_CONFIG_SIZE		9
#define USB_DT_INTERFACE_SIZE		9
#define USB_DT_ENDPOINT_SIZE		7
#define USB_DT_ENDPOINT_AUDIO_SIZE	9	/* Audio extension */
#define USB_DT_HUB_NONVAR_SIZE		7


/* ensure byte-packed structures */

#include <pshpack1.h>


/* All standard descriptors have these 2 fields in common */
struct usb_descriptor_header {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
};

/* String descriptor */
struct usb_string_descriptor {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
  unsigned short wData[1];
};

/* HID descriptor */
struct usb_hid_descriptor {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
  unsigned short bcdHID;
  unsigned char  bCountryCode;
  unsigned char  bNumDescriptors;
};

/* Endpoint descriptor */
#define USB_MAXENDPOINTS	32
struct usb_endpoint_descriptor {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
  unsigned char  bEndpointAddress;
  unsigned char  bmAttributes;
  unsigned short wMaxPacketSize;
  unsigned char  bInterval;
  unsigned char  bRefresh;
  unsigned char  bSynchAddress;

  unsigned char *extra;	/* Extra descriptors */
  int extralen;
};

#define USB_ENDPOINT_ADDRESS_MASK	0x0f    /* in bEndpointAddress */
#define USB_ENDPOINT_DIR_MASK		0x80

#define USB_ENDPOINT_TYPE_MASK		0x03    /* in bmAttributes */
#define USB_ENDPOINT_TYPE_CONTROL	0
#define USB_ENDPOINT_TYPE_ISOCHRONOUS	1
#define USB_ENDPOINT_TYPE_BULK		2
#define USB_ENDPOINT_TYPE_INTERRUPT	3

/* Interface descriptor */
#define USB_MAXINTERFACES	32
struct usb_interface_descriptor {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
  unsigned char  bInterfaceNumber;
  unsigned char  bAlternateSetting;
  unsigned char  bNumEndpoints;
  unsigned char  bInterfaceClass;
  unsigned char  bInterfaceSubClass;
  unsigned char  bInterfaceProtocol;
  unsigned char  iInterface;

  struct usb_endpoint_descriptor *endpoint;

  unsigned char *extra;	/* Extra descriptors */
  int extralen;
};

#define USB_MAXALTSETTING	128	/* Hard limit */
struct usb_interface {
  struct usb_interface_descriptor *altsetting;

  int num_altsetting;
};

/* Configuration descriptor information.. */
#define USB_MAXCONFIG		8
struct usb_config_descriptor {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
  unsigned short wTotalLength;
  unsigned char  bNumInterfaces;
  unsigned char  bConfigurationValue;
  unsigned char  iConfiguration;
  unsigned char  bmAttributes;
  unsigned char  MaxPower;

  struct usb_interface *interface;

  unsigned char *extra;	/* Extra descriptors */
  int extralen;
};

/* Device descriptor */
struct usb_device_descriptor {
  unsigned char  bLength;
  unsigned char  bDescriptorType;
  unsigned short bcdUSB;
  unsigned char  bDeviceClass;
  unsigned char  bDeviceSubClass;
  unsigned char  bDeviceProtocol;
  unsigned char  bMaxPacketSize0;
  unsigned short idVendor;
  unsigned short idProduct;
  unsigned short bcdDevice;
  unsigned char  iManufacturer;
  unsigned char  iProduct;
  unsigned char  iSerialNumber;
  unsigned char  bNumConfigurations;
};

struct usb_ctrl_setup {
  unsigned char  bRequestType;
  unsigned char  bRequest;
  unsigned short wValue;
  unsigned short wIndex;
  unsigned short wLength;
};

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
/* 0x02 is reserved */
#define USB_REQ_SET_FEATURE		0x03
/* 0x04 is reserved */
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

/*
 * Various libusb API related stuff
 */

#define USB_ENDPOINT_IN			0x80
#define USB_ENDPOINT_OUT		0x00

/* Error codes */
#define USB_ERROR_BEGIN			500000

/*
 * This is supposed to look weird. This file is generated from autoconf
 * and I didn't want to make this too complicated.
 */
#define USB_LE16_TO_CPU(x)

/* Data types */
struct usb_device;
struct usb_bus;

struct usb_device {
  struct usb_device *next, *prev;

  char filename[LIBUSB_PATH_MAX];

  struct usb_bus *bus;

  struct usb_device_descriptor descriptor;
  struct usb_config_descriptor *config;

  void *dev;		/* Darwin support */
};

struct usb_bus {
  struct usb_bus *next, *prev;

  char dirname[LIBUSB_PATH_MAX];

  struct usb_device *devices;
  unsigned long location;
};

/* Version information, Windows specific */
struct usb_version {
  struct {
    int major;
    int minor;
    int micro;
    int nano;
  } dll;
  struct {
    int major;
    int minor;
    int micro;
    int nano;
  } driver;
};


struct usb_dev_handle;

typedef struct usb_dev_handle usb_dev_handle;

/* Variables */
extern struct usb_bus *usb_busses;

#include <poppack.h>

//--------------------------------------------------------------

#ifndef _SCSI_VIA_ASPI_H_

typedef struct  /* LUN_INQUIRY */
{
        byte            reserved [8];
        byte            vendor   [8];
        byte            product  [16];
        byte            release  [4];
}
  LUN_INQUIRY;

#endif

typedef struct  /* USB_GLOBALS */
{
        HANDLE          hLibUsbDll;
        HANDLE          hScanner;
        int             HA_count;
        struct usb_device      *pDev;
        usb_dev_handle  *pUdev;
        BYTE            byInterfaceNumber;
}
  USB_GLOBALS;

typedef struct  /* USB_PER_USER */
{
        HANDLE          hEvent;
        BYTE            HA_count;
        BYTE            spare [3];
        DWORD           max_transfer;
        LUN_INQUIRY     LUN_inq;
}
  USB_PER_USER;

typedef struct  /* USB_FIELDS */
{
        USB_GLOBALS     g;
        USB_PER_USER    u;
}
  USB_FIELDS;

extern  USB_FIELDS      usb;

int     usb_init_user           (USB_PER_USER *pUser);

int     usb_deinit_user         (USB_PER_USER *pUser);

int     usb_init                (void);

int     usb_deinit              (void);

int     usb_do_request          (DWORD          dwValue,
                                 BOOL           bInput,
                                 void           *pBuf,
                                 DWORD          dwBufLen);

int     usb_scsi_exec           (void         *cdb,
                                 unsigned int cdb_length,
                                 int          mode_and_dir,
                                 void         *data_buf,
                                 unsigned int data_buf_len);

int     usb_unit_inquiry        (LUN_INQUIRY *pLI);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif /* _SCSI_VIA_USB_H_ */
