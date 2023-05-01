// HidDevice.h: devicei-specific HID structures
#pragma once

/* Trackpad finger data offsets, le16-aligned */
#define HOFFSET_TYPE_USB_1		(13 * sizeof(USHORT))
#define HOFFSET_TYPE_USB_2		(15 * sizeof(USHORT))
#define HOFFSET_TYPE_USB_3		(19 * sizeof(USHORT))
#define HOFFSET_TYPE_USB_4		(23 * sizeof(USHORT))
#define HOFFSET_TYPE_USB_5		( 6 * sizeof(USHORT))
#define HOFFSET_TYPE_BTH_5		( 2 * sizeof(USHORT))

/* Trackpad button data offsets */
#define BOFFSET_TYPE1		0
#define BOFFSET_TYPE2		15
#define BOFFSET_TYPE3		23
#define BOFFSET_TYPE4		31
#define BOFFSET_TYPE5		1

/* Trackpad finger data block size */
#define FSIZE_TYPE1		(14 * sizeof(USHORT))
#define FSIZE_TYPE2		(14 * sizeof(USHORT))
#define FSIZE_TYPE3		(14 * sizeof(USHORT))
#define FSIZE_TYPE4		(15 * sizeof(USHORT))
#define FSIZE_TYPE5		(9)

/* List of device capability bits */
#define HAS_INTEGRATED_BUTTON	1

/* Offset from header to finger struct */
#define FDELTA_TYPE1		(0 * sizeof(USHORT))
#define FDELTA_TYPE2		(0 * sizeof(USHORT))
#define FDELTA_TYPE3		(0 * sizeof(USHORT))
#define FDELTA_TYPE4		(1 * sizeof(USHORT))
#define FDELTA_TYPE5		(0 * sizeof(USHORT))

/* Trackpad finger data size, empirically at least ten fingers */
#define MAX_FINGERS		16
#define MAX_FINGER_ORIENTATION	16384

#pragma pack( push, 1 )
#pragma warning( push )
#pragma warning( disable : 4200 )

struct TRACKPAD_FINGER_TYPE5
{
	UINT32 coords;			/* absolute x coodinate */
	UCHAR touchMajor;			/* touch area, major axis */
	UCHAR touchMinor;			/* touch area, minor axis */
	UCHAR size;					/* tool area, size */
	UCHAR pressure;				/* pressure on forcetouch touchpad */
	UCHAR id : 4;
	UCHAR orientation : 4;
};

struct TRACKPAD_REPORT_TYPE5
{
	UCHAR reportId;
	UINT8 clicks : 1;
	UINT8 : 2;
	UINT8 timestampLow : 5;
	UINT16 timestampHigh;
	struct TRACKPAD_FINGER_TYPE5 fingers[];
};

#pragma warning( pop )
#pragma pack( pop )

static_assert(sizeof(struct TRACKPAD_FINGER_TYPE5) == 9, "Unexpected MAGIC_TRACKPAD_INPUT_REPORT_FINGER size");
static_assert(sizeof(struct TRACKPAD_REPORT_TYPE5) == 4, "Unexpected MAGIC_TRACKPAD_INPUT_REPORT_FINGER size");
