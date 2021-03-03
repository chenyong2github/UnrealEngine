// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReflexLatencyMarkers.h" 

#include "HAL/IConsoleManager.h"

#include "RHI.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <D3D11.h>
#include <D3D12.h>
#include "nvapi.h"
#include "Windows/HideWindowsPlatformTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogLatencyMarkers, Log, All);

int32 DisableLatencyMarkers = 0;
static FAutoConsoleVariableRef CVarDisableLatencyMarkers(
	TEXT("t.DisableLatencyMarkers"),
	DisableLatencyMarkers,
	TEXT("Disable Latency Markers")
);

void FReflexLatencyMarkers::Initialize()
{
	if (IsRHIDeviceNVIDIA())
	{
		NvU32 DriverVersion;
		NvAPI_ShortString BranchString;

		// Driver version check, 455 and above required for Reflex
		NvAPI_SYS_GetDriverAndBranchVersion(&DriverVersion, BranchString);
		if (DriverVersion >= 45500)
		{
			bProperDriverVersion = true;
		}
	}
}

void FReflexLatencyMarkers::Tick(float DeltaTime)
{
	if (DisableLatencyMarkers == 0 && bEnabled && bProperDriverVersion && IsRHIDeviceNVIDIA())
	{
		NvAPI_Status LatencyStatus = NVAPI_OK;
		NV_LATENCY_RESULT_PARAMS_V1 LatencyResults = { 0 };
		LatencyResults.version = NV_LATENCY_RESULT_PARAMS_VER1;

		LatencyStatus = NvAPI_D3D_GetLatency(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &LatencyResults);

		if (LatencyStatus == NVAPI_OK)
		{
			// frameReport[63] contains the latest completed frameReport
			const NvU64 TotalLatencyUs = LatencyResults.frameReport[63].gpuRenderEndTime - LatencyResults.frameReport[63].simStartTime;
			if (TotalLatencyUs != 0)
			{
				// frameReport results available, get latest completed frame latency data

				// A 3/4, 1/4 split gets close to a simple 10 frame moving average
				AverageTotalLatencyMs = AverageTotalLatencyMs * 0.75f + TotalLatencyUs / 1000.0f * 0.25f;
				AverageGameLatencyMs = AverageGameLatencyMs * 0.75f + (LatencyResults.frameReport[63].simEndTime - LatencyResults.frameReport[63].simStartTime) / 1000.0f * 0.25f;
				AverageRenderLatencyMs = AverageRenderLatencyMs * 0.75f + (LatencyResults.frameReport[63].renderSubmitEndTime - LatencyResults.frameReport[63].renderSubmitStartTime) / 1000.0f * 0.25f;
				AverageDriverLatencyMs = AverageDriverLatencyMs * 0.75f + (LatencyResults.frameReport[63].driverEndTime - LatencyResults.frameReport[63].driverStartTime) / 1000.0f * 0.25f;
				AverageOSWorkQueueLatencyMs = AverageOSWorkQueueLatencyMs * 0.75f + (LatencyResults.frameReport[63].osRenderQueueEndTime - LatencyResults.frameReport[63].osRenderQueueStartTime) / 1000.0f * 0.25f;
				AverageGPURenderLatencyMs = AverageGPURenderLatencyMs * 0.75f + (LatencyResults.frameReport[63].gpuRenderEndTime - LatencyResults.frameReport[63].gpuRenderStartTime) / 1000.0f * 0.25f;

				RenderOffsetMs = (LatencyResults.frameReport[63].renderSubmitStartTime - LatencyResults.frameReport[63].simStartTime) / 1000.0f;
				DriverOffsetMs = (LatencyResults.frameReport[63].driverStartTime - LatencyResults.frameReport[63].simStartTime) / 1000.0f;
				OSWorkQueueOffsetMs = (LatencyResults.frameReport[63].osRenderQueueStartTime - LatencyResults.frameReport[63].simStartTime) / 1000.0f;
				GPURenderOffsetMs = (LatencyResults.frameReport[63].gpuRenderStartTime - LatencyResults.frameReport[63].simStartTime) / 1000.0f;

 				UE_LOG(LogLatencyMarkers, VeryVerbose, TEXT("AverageTotalLatencyMs: %f"), AverageTotalLatencyMs);
				UE_LOG(LogLatencyMarkers, VeryVerbose, TEXT("AverageGameLatencyMs: %f"), AverageGameLatencyMs);
 				UE_LOG(LogLatencyMarkers, VeryVerbose, TEXT("AverageRenderLatencyMs: %f"), AverageRenderLatencyMs);
 				UE_LOG(LogLatencyMarkers, VeryVerbose, TEXT("AverageDriverLatencyMs: %f"), AverageDriverLatencyMs);
 				UE_LOG(LogLatencyMarkers, VeryVerbose, TEXT("AverageOSWorkQueueLatencyMs: %f"), AverageOSWorkQueueLatencyMs);
 				UE_LOG(LogLatencyMarkers, VeryVerbose, TEXT("AverageGPURenderLatencyMs: %f"), AverageGPURenderLatencyMs);
			}
		}
	}
}

void FReflexLatencyMarkers::SetGameLatencyMarkerStart(uint64 FrameNumber)
{
	if (DisableLatencyMarkers == 0 && bProperDriverVersion && bEnabled && IsRHIDeviceNVIDIA())
	{
		NvAPI_Status status = NVAPI_OK;
		NV_LATENCY_MARKER_PARAMS_V1 params = { 0 };
		params.version = NV_LATENCY_MARKER_PARAMS_VER1;
		params.frameID = FrameNumber;
		params.markerType = SIMULATION_START;

		status = NvAPI_D3D_SetLatencyMarker(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);
	}
}

void FReflexLatencyMarkers::SetGameLatencyMarkerEnd(uint64 FrameNumber)
{
	if (DisableLatencyMarkers == 0 && bProperDriverVersion && bEnabled && IsRHIDeviceNVIDIA())
	{
		NvAPI_Status status = NVAPI_OK;
		NV_LATENCY_MARKER_PARAMS_V1 params = { 0 };
		params.version = NV_LATENCY_MARKER_PARAMS_VER1;
		params.frameID = FrameNumber;
		params.markerType = SIMULATION_END;

		status = NvAPI_D3D_SetLatencyMarker(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);
	}
}

void FReflexLatencyMarkers::SetRenderLatencyMarkerStart(uint64 FrameNumber)
{
	if (DisableLatencyMarkers == 0 && bProperDriverVersion && bEnabled && IsRHIDeviceNVIDIA())
	{
		NvAPI_Status status = NVAPI_OK;
		NV_LATENCY_MARKER_PARAMS_V1 params = { 0 };
		params.version = NV_LATENCY_MARKER_PARAMS_VER1;
		params.frameID = FrameNumber;
		params.markerType = RENDERSUBMIT_START;

		status = NvAPI_D3D_SetLatencyMarker(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);
	}
}

void FReflexLatencyMarkers::SetRenderLatencyMarkerEnd(uint64 FrameNumber)
{
	if (DisableLatencyMarkers == 0 && bProperDriverVersion && bEnabled && IsRHIDeviceNVIDIA())
	{
		NvAPI_Status status = NVAPI_OK;
		NV_LATENCY_MARKER_PARAMS_V1 params = { 0 };
		params.version = NV_LATENCY_MARKER_PARAMS_VER1;
		params.frameID = FrameNumber;
		params.markerType = RENDERSUBMIT_END;

		status = NvAPI_D3D_SetLatencyMarker(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);
	}
}

void FReflexLatencyMarkers::SetCustomLatencyMarker(uint32 MarkerId, uint64 FrameNumber)
{
	// Allow trigger flash here
	if (NV_LATENCY_MARKER_TYPE(MarkerId) == TRIGGER_FLASH)
	{
		if (DisableLatencyMarkers == 0 && bProperDriverVersion && bEnabled && IsRHIDeviceNVIDIA())
		{
			NvAPI_Status status = NVAPI_OK;
			NV_LATENCY_MARKER_PARAMS_V1 params = { 0 };
			params.version = NV_LATENCY_MARKER_PARAMS_VER1;
			params.frameID = FrameNumber;
			params.markerType = NV_LATENCY_MARKER_TYPE(MarkerId);

			status = NvAPI_D3D_SetLatencyMarker(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);
		}
	}
}

bool FReflexLatencyMarkers::GetEnabled()
{
	if (DisableLatencyMarkers == 1 || !bProperDriverVersion || !IsRHIDeviceNVIDIA())
	{
		return false;
	}

	return bEnabled;
}