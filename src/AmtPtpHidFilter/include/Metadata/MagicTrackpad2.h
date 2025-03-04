// MagicTrackpad2.h: Magic Trackpad 2 specific HID information
#pragma once

#define AAPL_MAGIC_TRACKPAD2_PTP_FINGER_COLLECTION_1 \
	BEGIN_COLLECTION, 0x02, /* Begin Collection: Logical */ \
		/* Begin a byte */ \
		LOGICAL_MAXIMUM, 0x01, /* Logical Maximum: 1 */ \
		USAGE, 0x47, /* Usage: Confidence */ \
		USAGE, 0x42, /* Usage: Tip switch */ \
		REPORT_COUNT, 0x02, /* Report Count: 2 */ \
		REPORT_SIZE, 0x01, /* Report Size: 1 */ \
		INPUT, 0x02, /* Input: (Data, Var, Abs) */ \
		LOGICAL_MAXIMUM, 0x0f, /* Logical Maximum: 0x0f */ \
		USAGE, 0x51, /* Usage: Contract Identifier */ \
		REPORT_SIZE, 0x06, /* Report Size: 6 */ \
		REPORT_COUNT, 0x01, /* Report Count: 1 */ \
		INPUT, 0x02, /* Input: (Var, Abs) */ \
		/* End of a byte */ \
		/* Begin of 4 bytes */ \
		/* Size is hard-coded at this moment */ \
		USAGE_PAGE, 0x01, /* Usage Page: Generic Desktop */ \
		LOGICAL_MAXIMUM_2, 0xbc, 0x1d, /* Logical Maximum: 7612 (See defintion) */ \
		REPORT_SIZE, 0x10, /* Report Size: 0x10 (2 bytes) */ \
		UNIT_EXPONENT, 0x0e, /* Unit exponent: -2 */ \
		UNIT, 0x11, /* Unit: SI Length (cm) */ \
		USAGE, 0x30, /* Usage: X */ \
		PHYSICAL_MAXIMUM_2, 0x40, 0x06, /* Physical Maximum: 1600 (See Apple Spec) */ \
		REPORT_COUNT, 0x01, /* Report count: 1 */ \
		INPUT, 0x02, /* Input: (Data, Var, Abs) */ \
		PHYSICAL_MAXIMUM_2, 0x7d, 0x04, /* Physical Maximum: 1149 (See Apple Spec) */ \
		LOGICAL_MAXIMUM_2, 0xc9, 0x13, /* Logical Maximum: 5065 (See definition) */ \
		USAGE, 0x31, /* Usage: Y */ \
		INPUT, 0x02, /* Input: (Data, Var, Abs) */ \
		/* End of 4 bytes */ \
	END_COLLECTION /* End Collection */ \

#define AAPL_MAGIC_TRACKPAD2_PTP_TLC \
	USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
	USAGE, 0x05, /* Usage: Touch Pad */ \
	BEGIN_COLLECTION, 0x01, /* Begin Collection: Application */ \
		REPORT_ID, REPORTID_MULTITOUCH, /* Report ID: Multi-touch */ \
		USAGE, 0x22, /* Usage: Finger */ \
		AAPL_MAGIC_TRACKPAD2_PTP_FINGER_COLLECTION_1, /* 1 */ \
		USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
		USAGE, 0x22, /* Usage: Finger */ \
		AAPL_MAGIC_TRACKPAD2_PTP_FINGER_COLLECTION_1, /* 2 */ \
		USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
		USAGE, 0x22, /* Usage: Finger */ \
		AAPL_MAGIC_TRACKPAD2_PTP_FINGER_COLLECTION_1, /* 3 */ \
		USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
		USAGE, 0x22, /* Usage: Finger */ \
		AAPL_MAGIC_TRACKPAD2_PTP_FINGER_COLLECTION_1, /* 4 */ \
		USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
		USAGE, 0x22, /* Usage: Finger */ \
		AAPL_MAGIC_TRACKPAD2_PTP_FINGER_COLLECTION_1, /* 5 */ \
		USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
		UNIT_EXPONENT, 0x0c, /* Unit exponent: -4 */ \
		UNIT_2, 0x01, 0x10, /* Time: Second */ \
		PHYSICAL_MAXIMUM_3, 0xff, 0xff, 0x00, 0x00, \
		LOGICAL_MAXIMUM_3, 0xff, 0xff, 0x00, 0x00, \
		USAGE, 0x56, /* Usage: Scan Time */ \
		INPUT, 0x02, /* Input: (Data, Var, Abs) */ \
		USAGE, 0x54, /* Usage: Contact Count */ \
		LOGICAL_MAXIMUM, 0x7f, \
		REPORT_SIZE, 0x08, \
		INPUT, 0x02, /* Input: (Data, Var, Abs) */ \
		USAGE_PAGE, 0x09, /* Usage Page: Button */ \
		USAGE, 0x01, /* Button 1 */ \
		LOGICAL_MAXIMUM, 0x01, \
		REPORT_SIZE, 0x01, \
		INPUT, 0x02, /* Input: (Data, Var, Abs) */ \
		REPORT_COUNT, 0x07, \
		INPUT, 0x03, /* Input: (Const, Var, Abs) */ \
		USAGE_PAGE, 0x0d, /* Usage Page: Digitizer */ \
		REPORT_ID, REPORTID_DEVICE_CAPS, \
		USAGE, 0x55, /* Usage: Maximum Contacts */ \
		USAGE, 0x59, /* Usage: Touchpad Button Type*/ \
		LOGICAL_MINIMUM, 0x00, \
		LOGICAL_MAXIMUM_2, 0xff, 0x00, \
		REPORT_SIZE, 0x08, \
		REPORT_COUNT, 0x02, \
		FEATURE, 0x02, \
		USAGE_PAGE_1, 0x00, 0xff, \
		REPORT_ID, REPORTID_PTPHQA, \
		USAGE, 0xc5, \
		LOGICAL_MINIMUM, 0x00, \
		LOGICAL_MAXIMUM_2, 0xff, 0x00, \
		REPORT_SIZE, 0x08, \
		REPORT_COUNT_2, 0x00, 0x01, \
		FEATURE, 0x02, \
	END_COLLECTION /* End Collection */
