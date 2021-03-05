// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReflexMaxTickRateHandler.h" 

#include "HAL/IConsoleManager.h"

#include "RHI.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <D3D11.h>
#include <D3D12.h>
#include "nvapi.h"
#include "Windows/HideWindowsPlatformTypes.h"

int32 DisableCustomTickRateHandler = 0;
static FAutoConsoleVariableRef CVarDisableCustomTickRateHandler(
	TEXT("t.DisableCustomTickRateHandler"),
	DisableCustomTickRateHandler,
	TEXT("Disable Tick Rate Handler")
);

DEFINE_LOG_CATEGORY_STATIC(LogMaxTickRateHandler, Log, All);

void FReflexMaxTickRateHandler::Initialize()
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

bool FReflexMaxTickRateHandler::HandleMaxTickRate(float DesiredMaxTickRate)
{
	if (DisableCustomTickRateHandler == 0 && bProperDriverVersion && !GIsEditor && IsRHIDeviceNVIDIA())
	{
		if (bEnabled)
		{
			const float DesiredMinimumInterval = DesiredMaxTickRate > 0 ? ((1000.0f / DesiredMaxTickRate) * 1000.0f) : 0.0f;
			if (MinimumInterval != DesiredMinimumInterval || LastCustomFlags != CustomFlags)
			{
				NvAPI_Status status = NVAPI_OK;
				NV_SET_SLEEP_MODE_PARAMS_V1 params = { 0 };
				params.version = NV_SET_SLEEP_MODE_PARAMS_VER1;
				params.bLowLatencyMode = bUltraLowLatency;
				params.bLowLatencyBoost = bGPUBoost;
				MinimumInterval = DesiredMinimumInterval;
				params.minimumIntervalUs = MinimumInterval;
				status = NvAPI_D3D_SetSleepMode(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);

				UE_LOG(LogMaxTickRateHandler, Log, TEXT("SetSleepMode MI:%f L:%s B:%s"), MinimumInterval, bUltraLowLatency ? TEXT("On") : TEXT("Off"), bGPUBoost ? TEXT("On") : TEXT("Off"));

				// Need to verify that Low Latency flag actually applied, NVIDIA says the return code can still be good, but it might be incompatible
				NV_GET_SLEEP_STATUS_PARAMS_V1 SleepStatusParams = { 0 };
				SleepStatusParams.version = NV_GET_SLEEP_STATUS_PARAMS_VER1;
				status = NvAPI_D3D_GetSleepStatus(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &SleepStatusParams);
				if (bUltraLowLatency && SleepStatusParams.bLowLatencyMode == false)
				{
					UE_LOG(LogMaxTickRateHandler, Warning, TEXT("Unable to turn on low latency"));
					// Clear the ULL flag
					CustomFlags = CustomFlags & ~1;
					bUltraLowLatency = false;
				}

				LastCustomFlags = CustomFlags;
				bWasEnabled = true;
			}

			NvAPI_Status StatusSleep = NVAPI_OK;
			StatusSleep = NvAPI_D3D_Sleep(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()));

			return true;
		}
		else
		{
			// When disabled, if we ever called SetSleepMode, we need to clean up after ourselves
			if (bWasEnabled)
			{
				LastCustomFlags = 0;
				bWasEnabled = false;

				NvAPI_Status status = NVAPI_OK;
				NV_SET_SLEEP_MODE_PARAMS_V1 params = { 0 };
				params.version = NV_SET_SLEEP_MODE_PARAMS_VER1;
				params.bLowLatencyMode = false;
				params.bLowLatencyBoost = false;
				params.minimumIntervalUs = 0;
				status = NvAPI_D3D_SetSleepMode(static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice()), &params);
				UE_LOG(LogMaxTickRateHandler, Log, TEXT("SetSleepMode clean up"));
			}
		}
	}

	return false;
}

void FReflexMaxTickRateHandler::SetFlags(uint32 Flags)
{
	CustomFlags = Flags;
	if ((Flags & 1) > 0)
	{
		bUltraLowLatency = true;
	}
	else
	{
		bUltraLowLatency = false;
	}
	if ((Flags & 2) > 0)
	{
		bGPUBoost = true;
	}
	else
	{
		bGPUBoost = false;
	}
}

uint32 FReflexMaxTickRateHandler::GetFlags()
{
	return CustomFlags;	
}