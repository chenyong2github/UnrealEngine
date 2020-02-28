// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLightEstimationModule.h"
#include "Lumin/CAPIShims/LuminAPILightingTracking.h"

DEFINE_LOG_CATEGORY(LogMagicLeapLightEstimation);

void FMagicLeapLightEstimationModule::StartupModule()
{
#if WITH_MLSDK
	Tracker = ML_INVALID_HANDLE;
#endif // WITH_MLSDK
}

void FMagicLeapLightEstimationModule::ShutdownModule()
{
	DestroyTracker();
}

bool FMagicLeapLightEstimationModule::CreateTracker()
{
#if WITH_MLSDK
	if (!MLHandleIsValid(Tracker))
	{
		MLResult Result = MLLightingTrackingCreate(&Tracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapLightEstimation, Error, TEXT("MLLightingTrackingCreate failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
	}

	return MLHandleIsValid(Tracker);
#else
	return false;
#endif // WITH_MLSDK
}

void FMagicLeapLightEstimationModule::DestroyTracker()
{
#if WITH_MLSDK
	if (MLHandleIsValid(Tracker))
	{
		MLResult Result = MLLightingTrackingDestroy(Tracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapLightEstimation, Error, TEXT("MLLightingTrackingDestroy failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		Tracker = ML_INVALID_HANDLE;
	}
#endif // WITH_MLSDK
}

bool FMagicLeapLightEstimationModule::IsTrackerValid() const
{
#if WITH_MLSDK
	return MLHandleIsValid(Tracker);
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapLightEstimationModule::GetAmbientGlobalState(FMagicLeapLightEstimationAmbientGlobalState& GlobalAmbientState) const
{
#if WITH_MLSDK
	MLResult Result = MLResult_InvalidParam;
	if (MLHandleIsValid(Tracker))
	{
		MLLightingTrackingAmbientGlobalState State;
		Result = MLLightingTrackingGetAmbientGlobalState(Tracker, &State);
		if (Result == MLResult_Ok)
		{
			GlobalAmbientState.AmbientIntensityNits.Reset(MLLightingTrackingCamera_Count);
			GlobalAmbientState.AmbientIntensityNits.AddUninitialized(MLLightingTrackingCamera_Count);
			for (int32 i = 0; i < MLLightingTrackingCamera_Count; ++i)
			{
				GlobalAmbientState.AmbientIntensityNits[i] = State.als_global[i];				
			}
			GlobalAmbientState.Timestamp = FTimespan::FromMicroseconds(State.timestamp_ns / 1000.0f);
		}
	}

	return Result == MLResult_Ok;
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapLightEstimationModule::GetColorTemperatureState(FMagicLeapLightEstimationColorTemperatureState& ColorTemperatureState) const
{
#if WITH_MLSDK
	MLResult Result = MLResult_InvalidParam;
	if (MLHandleIsValid(Tracker))
	{
		MLLightingTrackingColorTemperatureState State;
		Result = MLLightingTrackingGetColorTemperatureState(Tracker, &State);
		if (Result == MLResult_Ok)
		{
			ColorTemperatureState.ColorTemperatureKelvin = static_cast<float>(State.color_temp);
			ColorTemperatureState.AmbientColor = FLinearColor(FVector(State.R_raw_pixel_avg, State.G_raw_pixel_avg, State.B_raw_pixel_avg));
			ColorTemperatureState.Timestamp = FTimespan::FromMicroseconds(State.timestamp_ns / 1000.0f);
		}
	}

	return Result == MLResult_Ok;
#else
	return false;
#endif // WITH_MLSDK
}

IMPLEMENT_MODULE(FMagicLeapLightEstimationModule, MagicLeapLightEstimation);
