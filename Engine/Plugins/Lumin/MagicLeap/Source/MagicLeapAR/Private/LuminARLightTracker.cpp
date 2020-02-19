// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARLightTracker.h"
#include "LuminARTrackingSystem.h"
#include "LuminARModule.h"
#include "IMagicLeapLightEstimationPlugin.h"

FLuminARLightTracker::FLuminARLightTracker(FLuminARImplementation& InARSystemSupport)
	: ILuminARTracker(InARSystemSupport)
	, LightEstimate(nullptr)
{}

void FLuminARLightTracker::CreateEntityTracker()
{
	IMagicLeapLightEstimationPlugin::Get().CreateTracker();
}

void FLuminARLightTracker::DestroyEntityTracker()
{
	IMagicLeapLightEstimationPlugin::Get().DestroyTracker();
}

void FLuminARLightTracker::OnStartGameFrame()
{
	if (IMagicLeapLightEstimationPlugin::Get().IsTrackerValid())
	{
		FMagicLeapLightEstimationAmbientGlobalState AmbientGlobalState;
		FMagicLeapLightEstimationColorTemperatureState ColorTemperatureState;

		if (IMagicLeapLightEstimationPlugin::Get().GetAmbientGlobalState(AmbientGlobalState) 
			&& IMagicLeapLightEstimationPlugin::Get().GetColorTemperatureState(ColorTemperatureState))
		{
			if (LightEstimate == nullptr)
			{
				LightEstimate = NewObject<ULuminARLightEstimate>();
			}

			LightEstimate->SetLightEstimate(AmbientGlobalState.AmbientIntensityNits, ColorTemperatureState.ColorTemperatureKelvin, ColorTemperatureState.AmbientColor);
		}
	}
}
