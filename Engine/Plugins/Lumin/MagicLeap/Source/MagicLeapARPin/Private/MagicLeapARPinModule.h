// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapARPinModule.h"
#include "AppEventHandler.h"
#include "Lumin/CAPIShims/LuminAPI.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapARPin, Verbose, All);

class FMagicLeapARPinModule : public IMagicLeapARPinModule, public MagicLeap::IAppEventHandler
{
public:
	FMagicLeapARPinModule();

	/** IMagicLeapARPinModule interface */
	virtual EMagicLeapPassableWorldError CreateTracker() override;
	virtual EMagicLeapPassableWorldError DestroyTracker() override;
	virtual bool IsTrackerValid() const override;
	virtual EMagicLeapPassableWorldError GetNumAvailableARPins(int32& Count) override;
	virtual EMagicLeapPassableWorldError GetAvailableARPins(int32 NumRequested, TArray<FGuid>& Pins) override;
	virtual EMagicLeapPassableWorldError GetClosestARPin(const FVector& SearchPoint, FGuid& PinID) override;
	virtual bool GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) override;

	/** IAppEventHandler interface */
	virtual void OnAppShutDown() override;
	virtual void OnAppTick() override;
	virtual void OnAppPause() override;
	virtual void OnAppResume() override;

private:
	bool bWasTrackerValidOnPause;
	bool bCreateTracker;

#if WITH_MLSDK
	MLHandle Tracker;
#endif //WITH_MLSDK
};
