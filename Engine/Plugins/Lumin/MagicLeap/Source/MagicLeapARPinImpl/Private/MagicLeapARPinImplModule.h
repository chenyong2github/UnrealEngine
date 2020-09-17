// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IMagicLeapARPinFeature.h"
#include "AppEventHandler.h"
#include "IMagicLeapTrackerEntity.h"
#include "Lumin/CAPIShims/LuminAPI.h"
#include "MagicLeapARPinSettings.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapARPinImpl, Verbose, All);

class FMagicLeapARPinImplModule : public IModuleInterface, public IMagicLeapARPinFeature, public MagicLeap::IAppEventHandler, public IMagicLeapTrackerEntity
{
public:
	FMagicLeapARPinImplModule();
	virtual ~FMagicLeapARPinImplModule();

	/** IMagicLeapARPinFeature interface */
	virtual EMagicLeapPassableWorldError CreateTracker() override;
	virtual EMagicLeapPassableWorldError DestroyTracker() override;
	virtual bool IsTrackerValid() const override;
	virtual EMagicLeapPassableWorldError GetNumAvailableARPins(int32& Count) override;
	virtual EMagicLeapPassableWorldError GetAvailableARPins(int32 NumRequested, TArray<FGuid>& PinCoordinateFrames) override;
	virtual EMagicLeapPassableWorldError GetClosestARPin(const FVector& SearchPoint, FGuid& PinID) override;
	virtual EMagicLeapPassableWorldError QueryARPins(const FMagicLeapARPinQuery& Query, TArray<FGuid>& Pins) override;
	virtual bool GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) override;
	virtual bool GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) override;
	virtual EMagicLeapPassableWorldError GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State) override;

	/** IAppEventHandler interface */
	virtual void OnAppShutDown() override;
	virtual void OnAppTick() override;

	/** IMagicLeapTrackerEntity interface */
	void CreateEntityTracker() override;
	void DestroyEntityTracker() override;

private:
	bool IsDeltaGreaterThanThreshold(float OldState, float NewState, float Threshold) const;

	bool bCreateTracker;
	bool bPerceptionEnabled;

	const UMagicLeapARPinSettings* Settings;

	TArray<FGuid> PendingAdded;
	TArray<FGuid> PendingUpdated;
	TArray<FGuid> PendingDeleted;

	TMap<FGuid, FMagicLeapARPinState> OldPinsAndStates;

	double PreviousTime;

	bool bHasCompletedPrisonTime;

#if WITH_MLSDK
	MLHandle Tracker;
#endif //WITH_MLSDK
};
