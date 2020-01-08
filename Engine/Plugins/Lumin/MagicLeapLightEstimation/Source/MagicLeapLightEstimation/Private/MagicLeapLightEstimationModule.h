// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMagicLeapLightEstimationPlugin.h"
#include "Lumin/CAPIShims/LuminAPI.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapLightEstimation, Verbose, All);

class FMagicLeapLightEstimationModule : public IMagicLeapLightEstimationPlugin
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

	virtual bool CreateTracker() override;
	virtual void DestroyTracker() override;
	virtual bool IsTrackerValid() const override;
	virtual bool GetAmbientGlobalState(FMagicLeapLightEstimationAmbientGlobalState& GlobalAmbientState) const override;
	virtual bool GetColorTemperatureState(FMagicLeapLightEstimationColorTemperatureState& ColorTemperatureState) const override;

private:
#if WITH_MLSDK
	MLHandle Tracker;
#endif // WITH_MLSDK
};
