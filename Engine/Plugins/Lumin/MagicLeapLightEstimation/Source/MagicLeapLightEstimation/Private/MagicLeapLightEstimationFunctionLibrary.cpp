// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLightEstimationFunctionLibrary.h"
#include "MagicLeapLightEstimationModule.h"

bool UMagicLeapLightEstimationFunctionLibrary::CreateTracker()
{
	return IMagicLeapLightEstimationPlugin::Get().CreateTracker();
}

void UMagicLeapLightEstimationFunctionLibrary::DestroyTracker()
{
	IMagicLeapLightEstimationPlugin::Get().DestroyTracker();
}

bool UMagicLeapLightEstimationFunctionLibrary::IsTrackerValid()
{
	return IMagicLeapLightEstimationPlugin::Get().IsTrackerValid();
}

bool UMagicLeapLightEstimationFunctionLibrary::GetAmbientGlobalState(FMagicLeapLightEstimationAmbientGlobalState& GlobalAmbientState)
{
	return IMagicLeapLightEstimationPlugin::Get().GetAmbientGlobalState(GlobalAmbientState);
}

bool UMagicLeapLightEstimationFunctionLibrary::GetColorTemperatureState(FMagicLeapLightEstimationColorTemperatureState& ColorTemperatureState)
{
	return IMagicLeapLightEstimationPlugin::Get().GetColorTemperatureState(ColorTemperatureState);
}
