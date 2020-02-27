// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILuminARTracker.h"
#include "MagicLeapImageTrackerTypes.h"
#include "LuminARTrackableResource.h"

class FLuminARImplementation;

class FLuminARImageTracker : public ILuminARTracker
{
public:
	FLuminARImageTracker(FLuminARImplementation& InARSystemSupport);
	virtual ~FLuminARImageTracker();

	virtual void CreateEntityTracker() override;
	virtual void DestroyEntityTracker() override;
	virtual void OnStartGameFrame() override;
	virtual bool IsHandleTracked(const FGuid& Handle) const override;
	const FString* GetTargetNameFromHandle(const FGuid& Handle) const;
	virtual UARTrackedGeometry* CreateTrackableObject() override;
	virtual IARRef* CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject) override;

	void OnSetImageTargetSucceeded(FMagicLeapImageTrackerTarget& Target);

private:
	TMap<FGuid, FString> TrackedTargetNames;
};

class FLuminARTrackedImageResource : public FLuminARTrackableResource
{
public:
	FLuminARTrackedImageResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry, const FLuminARImageTracker& InTracker)
		: FLuminARTrackableResource(InTrackableHandle, InTrackedGeometry)
		, Tracker(InTracker)
	{
	}

	void UpdateGeometryData(FLuminARImplementation* InARSystemSupport) override;

private:
	const FLuminARImageTracker& Tracker;
};
