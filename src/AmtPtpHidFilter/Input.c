// Input.c: Input handler routines

#include <Driver.h>
#include <HidDevice.h>
#include "Input.tmh"

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

static VOID
PtpFilterParsePacket(
	_In_ PUCHAR buffer,
	_In_ SIZE_T bufferLength,
	_In_ PDEVICE_CONTEXT deviceContext
) {
	NTSTATUS status;

	WDFREQUEST ptpRequest;
	PTP_REPORT ptpOutputReport;
	WDFMEMORY  ptpRequestMemory;

	const struct TRACKPAD_REPORT_TYPE5* report;
	const struct TRACKPAD_FINGER_TYPE5* f;
	size_t raw_n;
	INT x, y = 0;

	// Pre-flight check: the response size should be sane
	if (bufferLength < sizeof(struct TRACKPAD_REPORT_TYPE5) || (bufferLength - sizeof(struct TRACKPAD_REPORT_TYPE5)) % sizeof(struct TRACKPAD_FINGER_TYPE5) != 0) {
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Malformed input received. Length = %llu. Attempt to reconfigure the device.", bufferLength);
		//WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_SEC(3));
		return;
	}

	report = (struct TRACKPAD_REPORT_TYPE5*)buffer;
	if (report->reportId != 0x31) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! Incorrect report id %x", buffer[0]);
		return;
	}

	// Read report and fulfill PTP request. If no report is found, just exit.
	status = WdfIoQueueRetrieveNextRequest(deviceContext->HidReadQueue, &ptpRequest);
	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfIoQueueRetrieveNextRequest failed with %!STATUS!", status);
		return;
	}

	// Report header
	ptpOutputReport.ReportID = REPORTID_MULTITOUCH;
	ptpOutputReport.IsButtonClicked = report->clicks;
	ptpOutputReport.ScanTime = (report->timestampLow | (report->timestampHigh << 5)) * 10;

	// Report fingers
	raw_n = (bufferLength - sizeof(struct TRACKPAD_REPORT_TYPE5)) / sizeof(struct TRACKPAD_FINGER_TYPE5);
	if (raw_n >= PTP_MAX_CONTACT_POINTS) raw_n = PTP_MAX_CONTACT_POINTS;
	ptpOutputReport.ContactCount = (UCHAR)raw_n;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_INPUT,
		"%!FUNC!: New report at %d ms with %d fingers =========",
		ptpOutputReport.ScanTime / 10,
		(UCHAR) raw_n
	);

	for (size_t i = 0; i < raw_n; i++) {
		f = &report->fingers[i];

		USHORT tmp_x = f->coords & 0x1fff;
		USHORT tmp_y = (f->coords >> 13) & 0x1fff;
		UCHAR finger = (f->coords >> 26) & 0x7;
		UCHAR state = (f->coords >> 29) & 0x7;

		x = (SHORT)(tmp_x << 3) >> 3;
		y = -(SHORT)(tmp_y << 3) >> 3;

		x = (x - deviceContext->X.min) > 0 ? (x - deviceContext->X.min) : 0;
		y = (y - deviceContext->Y.min) > 0 ? (y - deviceContext->Y.min) : 0;

		ptpOutputReport.Contacts[i].ContactID = f->id;
		ptpOutputReport.Contacts[i].X = (USHORT)x;
		ptpOutputReport.Contacts[i].Y = (USHORT)y;
		ptpOutputReport.Contacts[i].TipSwitch = (state & 0x4) != 0 && (state & 0x2) == 0;
		// The Microsoft spec says reject any input larger than 25mm. This is not ideal
		// for Magic Trackpad 2 - so we raised the threshold a bit higher.
		// Or maybe I used the wrong unit? IDK
		ptpOutputReport.Contacts[i].Confidence = finger != 6;
		
		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_INPUT,
			"%!FUNC!: Point %llu, X = %d, Y = %d, Pres: %d, Size: %d, TipSwitch = %d, Confidence = %d, tMajor = %d, tMinor = %d, id = %d, finger = %d, state = %d",
			i,
			ptpOutputReport.Contacts[i].X,
			ptpOutputReport.Contacts[i].Y,
			f->pressure,
			f->size,
			ptpOutputReport.Contacts[i].TipSwitch,
			ptpOutputReport.Contacts[i].Confidence,
			f->touchMajor,
			f->touchMinor,
			f->id,
			finger,
			state
		);
	}

	status = WdfRequestRetrieveOutputMemory(ptpRequest, &ptpRequestMemory);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfRequestRetrieveOutputBuffer failed with %!STATUS!", status);
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		return;
	}

	status = WdfMemoryCopyFromBuffer(ptpRequestMemory, 0, (PVOID) &ptpOutputReport, sizeof(PTP_REPORT));
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfMemoryCopyFromBuffer failed with %!STATUS!", status);
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		return;
	}

	WdfRequestSetInformation(ptpRequest, sizeof(PTP_REPORT));
	WdfRequestComplete(ptpRequest, status);
}

VOID
PtpFilterInputRequestCompletionCallback(
	_In_ WDFREQUEST Request,
	_In_ WDFIOTARGET Target,
	_In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
	_In_ WDFCONTEXT Context
)
{
	PWORKER_REQUEST_CONTEXT requestContext;
	PDEVICE_CONTEXT deviceContext;

	size_t responseLength, pkt1Size, pkt2Size;
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

	// Pre-flight check: if size is 0, this is not something we need. Ignore the read, and issue next request.
	if (responseLength <= 0) {
		WdfWorkItemEnqueue(requestContext->DeviceContext->HidTransportRecoveryWorkItem);
		goto cleanup;
	}

	switch (responseBuffer[0]) {

	// Check the report ID. Sometimes two reports can be sent within a packet
	case 0xF7:
		pkt1Size = responseBuffer[1];
		pkt2Size = responseLength - 2 - pkt1Size;
		PtpFilterParsePacket(responseBuffer + 2, pkt1Size, deviceContext);
		PtpFilterParsePacket(responseBuffer + 2 + pkt1Size, pkt2Size, deviceContext);
		break;
	case 0x31:
		PtpFilterParsePacket(responseBuffer, responseLength, deviceContext);
		break;
	case 0x90:
		// Report id 0x13 when powered off, 0x90 when powered up?
		// Byte 2 = 0x83 on shutdown, 0x64 on startup
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Powered on?? (%x %x)", responseBuffer[1], responseBuffer[2]);
		WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_MS(250));
		break;
	case 0x13:
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INPUT, "%!FUNC! Powered down?? (%x %x)", responseBuffer[1], responseBuffer[2]);
		break;
	default:
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! Invalid Report ID %x from MT2 with length %d!", responseBuffer[0], (int)responseLength);
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! First few bytes: %x %x %x %x", responseBuffer[1], responseBuffer[2], responseBuffer[3], responseBuffer[4]);
		break;
	}

cleanup:
	// Cleanup
	WdfObjectDelete(Request);
	if (requestContext->RequestMemory != NULL) {
		WdfObjectDelete(requestContext->RequestMemory);
	}
}
