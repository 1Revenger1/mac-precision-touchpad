// Input.c: Input handler routines

#include <Driver.h>
#include <HidDevice.h>
#include "Input.tmh"

#define STATUS_PTP_GOOD STATUS_SUCCESS  // Valid input packet
#define STATUS_PTP_SET_MODE 1           // Enter Multitouch mode again
#define STATUS_PTP_RESTART 2            // Restart Driver
#define STATUS_PTP_EXIT 3               // Exit Driver
#define STATUS_PTP_QUEUE 4              // Requeue worker

VOID
PtpFilterInputProcessRequest(
	_In_ WDFDEVICE Device,
	_In_ WDFREQUEST Request
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;

	deviceContext = PtpFilterGetContext(Device);
	status = WdfRequestForwardToIoQueue(Request, deviceContext->HidReadQueue);
	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfRequestForwardToIoQueue fails, status = %!STATUS!", status);
		WdfRequestComplete(Request, status);
		return;
	}

	// Only issue request when fully configured.
	// Otherwise we will let power recovery process to triage it
	if (deviceContext->DeviceConfigured == TRUE) {
		PtpFilterInputIssueTransportRequest(Device);
	}
}

VOID
PtpFilterWorkItemCallback(
	_In_ WDFWORKITEM WorkItem
)
{
	WDFDEVICE Device = WdfWorkItemGetParentObject(WorkItem);
	PtpFilterInputIssueTransportRequest(Device);
}

VOID
PtpFilterInputIssueTransportRequest(
	_In_ WDFDEVICE Device
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDFREQUEST hidReadRequest;
	WDFMEMORY hidReadOutputMemory;
	PWORKER_REQUEST_CONTEXT requestContext;
	BOOLEAN requestStatus = FALSE;

	deviceContext = PtpFilterGetContext(Device);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, WORKER_REQUEST_CONTEXT);
	attributes.ParentObject = Device;
	status = WdfRequestCreate(&attributes, deviceContext->HidIoTarget, &hidReadRequest);
	if (!NT_SUCCESS(status)) {
		// This can fail for Bluetooth devices. We will set up a 3 second timer for retry triage.
		// Typically this should not fail for USB transport.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfRequestCreate fails, status = %!STATUS!", status);
		deviceContext->DeviceConfigured = FALSE;
		WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_SEC(3));
		return;
	}

	status = WdfMemoryCreateFromLookaside(deviceContext->HidReadBufferLookaside, &hidReadOutputMemory);
	if (!NT_SUCCESS(status)) {
		// tbh if you fail here, something seriously went wrong...request a restart.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfMemoryCreateFromLookaside fails, status = %!STATUS!", status);
		WdfObjectDelete(hidReadRequest);
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		return;
	}

	// Assign context information
	// And format HID read request.
	requestContext = WorkerRequestGetContext(hidReadRequest);
	requestContext->DeviceContext = deviceContext;
	requestContext->RequestMemory = hidReadOutputMemory;
	status = WdfIoTargetFormatRequestForInternalIoctl(deviceContext->HidIoTarget, hidReadRequest,
		IOCTL_HID_READ_REPORT, NULL, 0, hidReadOutputMemory, 0);
	if (!NT_SUCCESS(status)) {
		// tbh if you fail here, something seriously went wrong...request a restart.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfIoTargetFormatRequestForInternalIoctl fails, status = %!STATUS!", status);

		if (hidReadOutputMemory != NULL) {
			WdfObjectDelete(hidReadOutputMemory);
		}

		if (hidReadRequest != NULL) {
			WdfObjectDelete(hidReadRequest);
		}

		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		return;
	}

	// Set callback
	WdfRequestSetCompletionRoutine(hidReadRequest, PtpFilterInputRequestCompletionCallback, requestContext);

	requestStatus = WdfRequestSend(hidReadRequest, deviceContext->HidIoTarget, NULL);
	if (!requestStatus) {
		// Retry after 3 seconds, in case this is a transportation issue.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! PtpFilterInputIssueTransportRequest request failed to sent");
		deviceContext->DeviceConfigured = FALSE;
		WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_SEC(3));

		if (hidReadOutputMemory != NULL) {
			WdfObjectDelete(hidReadOutputMemory);
		}

		if (hidReadRequest != NULL) {
			WdfObjectDelete(hidReadRequest);
		}
	}
}

static
NTSTATUS
PtpFilterParseTouchPacket(
	_In_ PUCHAR buffer,
	_In_ SIZE_T bufferLength,
	_In_ PDEVICE_CONTEXT deviceContext
) {
	NTSTATUS status;

	WDFREQUEST ptpRequest;
	WDFMEMORY  ptpRequestMemory;
	PTP_REPORT* ptpOutputReport;
	PTP_CONTACT* ptpContact;

	const struct TRACKPAD_REPORT_TYPE5* report;
	const struct TRACKPAD_FINGER_TYPE5* f;
	size_t raw_n;
	size_t memorySize;
	INT x, y = 0;

	// Pre-flight check: the response size should be sane
	if (bufferLength < sizeof(struct TRACKPAD_REPORT_TYPE5) || (bufferLength - sizeof(struct TRACKPAD_REPORT_TYPE5)) % sizeof(struct TRACKPAD_FINGER_TYPE5) != 0) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! Malformed input received. Length = %llu", bufferLength);
		return STATUS_PTP_GOOD;
	}

	report = (struct TRACKPAD_REPORT_TYPE5*)buffer;

	// Read report and fulfill PTP request. If no report is found, just exit.
	status = WdfIoQueueRetrieveNextRequest(deviceContext->HidReadQueue, &ptpRequest);
	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfIoQueueRetrieveNextRequest failed with %!STATUS!", status);
		return STATUS_PTP_GOOD;
	}

	status = WdfRequestRetrieveOutputMemory(ptpRequest, &ptpRequestMemory);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfRequestRetrieveOutputBuffer failed with %!STATUS!", status);
		return STATUS_PTP_RESTART;
	}

	ptpOutputReport = WdfMemoryGetBuffer(ptpRequestMemory, &memorySize);
	if (memorySize != sizeof(PTP_REPORT)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfMemoryGetBuffer failed with incorrect size!");
		return STATUS_PTP_EXIT;
	}

	// Report header
	ptpOutputReport->ReportID = REPORTID_MULTITOUCH;
	ptpOutputReport->IsButtonClicked = report->clicks;
	ptpOutputReport->ScanTime = (report->timestampLow | (report->timestampHigh << 5)) * 10;

	// Report fingers
	raw_n = (bufferLength - sizeof(struct TRACKPAD_REPORT_TYPE5)) / sizeof(struct TRACKPAD_FINGER_TYPE5);
	if (raw_n >= PTP_MAX_CONTACT_POINTS) raw_n = PTP_MAX_CONTACT_POINTS;
	ptpOutputReport->ContactCount = (UCHAR)raw_n;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_INPUT,
		"%!FUNC!: New report at %d ms with %d fingers =========",
		ptpOutputReport->ScanTime / 10,
		(UCHAR) raw_n
	);

	for (size_t i = 0; i < raw_n; i++) {
		f = &report->fingers[i];
		ptpContact = &ptpOutputReport->Contacts[i];

		USHORT tmp_x = f->coords & 0x1fff;
		USHORT tmp_y = (f->coords >> 13) & 0x1fff;
		UCHAR finger = (f->coords >> 26) & 0x7;
		UCHAR state = (f->coords >> 29) & 0x7;

		x = (SHORT)(tmp_x << 3) >> 3;
		y = -(SHORT)(tmp_y << 3) >> 3;

		x = (x - deviceContext->X.min) > 0 ? (x - deviceContext->X.min) : 0;
		y = (y - deviceContext->Y.min) > 0 ? (y - deviceContext->Y.min) : 0;

		ptpContact->ContactID = f->id;
		ptpContact->X = (USHORT)x;
		ptpContact->Y = (USHORT)y;
		ptpContact->TipSwitch = (state & 0x4) != 0 && (state & 0x2) == 0;
		// The Microsoft spec says reject any input larger than 25mm. This is not ideal
		// for Magic Trackpad 2 - so we raised the threshold a bit higher.
		// Or maybe I used the wrong unit? IDK
		ptpContact->Confidence = finger != 6;
		
		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_INPUT,
			"%!FUNC!: Point %llu, X = %d, Y = %d, Pres: %d, Size: %d, TipSwitch = %d, Confidence = %d, tMajor = %d, tMinor = %d, id = %d, finger = %d, state = %d",
			i,
			ptpContact->X,
			ptpContact->Y,
			f->pressure,
			f->size,
			ptpContact->TipSwitch,
			ptpContact->Confidence,
			f->touchMajor,
			f->touchMinor,
			f->id,
			finger,
			state
		);
	}

	WdfRequestSetInformation(ptpRequest, sizeof(PTP_REPORT));
	WdfRequestComplete(ptpRequest, status);
	return STATUS_PTP_GOOD;
}

static
NTSTATUS
PtpFilterParsePacket(
	_In_ PUCHAR buffer,
	_In_ SIZE_T bufferLength,
	_In_ PDEVICE_CONTEXT deviceContext
)
{
	size_t pkt1Size, pkt2Size;
	NTSTATUS status;

	if (bufferLength == 0) {
		return STATUS_PTP_QUEUE;
	}

	switch (buffer[0]) {
	case 0x02:
		// TODO: USB does combine Mouse and Wellspring report into one packet, unlike BT. Maybe also check report length here?
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Mouse Packet - Setting Wellspring mode");
		return STATUS_PTP_SET_MODE;
	case 0x13:
		// Byte 2 = 0x83
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Powered down (0x%x 0x%x)", buffer[1], buffer[2]);
		return STATUS_PTP_QUEUE; // Wait for next packet
	case 0x31:
		return PtpFilterParseTouchPacket(buffer, bufferLength, deviceContext);
	case 0x90:
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Battery Percentage = %d", buffer[2]);
		return STATUS_PTP_SET_MODE; // Sometimes sent when the trackpad is powered on
	case 0xF7:
		pkt1Size = buffer[1];
		pkt2Size = bufferLength - 2 - pkt1Size;
		status = PtpFilterParsePacket(buffer + 2, pkt1Size, deviceContext);
		if (status == STATUS_PTP_EXIT || status == STATUS_PTP_RESTART) {
			return status;
		}
		return PtpFilterParsePacket(buffer + 2 + pkt1Size, pkt2Size, deviceContext);
	case 0xFE:
		// First part of split packet
		// TODO: Combine with pt2 and parse
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Split Packet pt1");
		return STATUS_PTP_QUEUE;
	case 0xFC:
		// Second part of split packet
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Split Packet pt2");
		return STATUS_PTP_QUEUE;

		// Unknown reports!
	case 0x1C:
		// This seemed to kind of happen randomly
		// Only 4 bytes long
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_INPUT, "%!FUNC! Unknown Packet with bytes %x %x %x", buffer[1], buffer[2], buffer[3]);
	default:
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! Invalid Report ID %x from MT2 with length %d!", buffer[0], (int)bufferLength);
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! First few bytes: %x %x %x %x", buffer[1], buffer[2], buffer[3], buffer[4]);
		// Ignore and wait for next packet
		return STATUS_PTP_QUEUE;
	};
}

VOID
PtpFilterInputRequestCompletionCallback(
	_In_ WDFREQUEST Request,
	_In_ WDFIOTARGET Target,
	_In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
	_In_ WDFCONTEXT Context
)
{
	NTSTATUS status;
	PWORKER_REQUEST_CONTEXT requestContext;
	PDEVICE_CONTEXT deviceContext;

	size_t responseLength;
	PUCHAR responseBuffer;

	UNREFERENCED_PARAMETER(Target);
	
	requestContext = (PWORKER_REQUEST_CONTEXT)Context;
	deviceContext = requestContext->DeviceContext;
	responseLength = (size_t)(LONG)WdfRequestGetInformation(Request);
	responseBuffer = WdfMemoryGetBuffer(Params->Parameters.Ioctl.Output.Buffer, NULL);

	// Pre-flight check 0: Right now we only have Magic Trackpad 2 (BT and USB)
	if (deviceContext->VendorID != HID_VID_APPLE_USB && deviceContext->VendorID != HID_VID_APPLE_BT) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! Unsupported device entered this routine");
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedNoRestart);
		goto cleanup;
	}

	status = PtpFilterParsePacket(responseBuffer, responseLength, deviceContext);
	if (status == STATUS_PTP_EXIT) {
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedNoRestart);
	}
	else if (status == STATUS_PTP_RESTART) {
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
	}
	else if (status == STATUS_PTP_QUEUE) {
		WdfWorkItemEnqueue(deviceContext->HidTransportRecoveryWorkItem);
	}
	else if (status == STATUS_PTP_SET_MODE) {
		WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_SEC(3));
	}


cleanup:
	// Cleanup
	WdfObjectDelete(Request);
	if (requestContext->RequestMemory != NULL) {
		WdfObjectDelete(requestContext->RequestMemory);
	}
}
