// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILuminARTracker.h"
#include "MagicLeapImageTrackerTypes.h"
#include "LuminARTrackableResource.h"
#include "ARTypes.h"

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
	virtual UARTrackedGeometry* CreateTrackableObject() override;
	virtual UClass* GetARComponentClass(const UARSessionConfig& SessionConfig) override;
	virtual IARRef* CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject) override;

	void AddCandidateImageForTracking(UARCandidateImage* NewCandidateImage);

private:
	void OnSetImageTargetSucceeded(const FString& TargetName);
	void AddPredefinedCandidateImages();
	void EnableImageTracker();

	FMagicLeapSetImageTargetCompletedStaticDelegate SuccessDelegate;
	TMap<FGuid, UARCandidateImage*> TrackedTargetNames;
	bool bAttemptedTrackerCreation;
};

class FLuminARTrackedImageResource : public FLuminARTrackableResource
{
public:
	FLuminARTrackedImageResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry, const FLuminARImageTracker& InTracker, UARCandidateImage* InCandidateImage)
		: FLuminARTrackableResource(InTrackableHandle, InTrackedGeometry)
		, Tracker(InTracker)
		, CandidateImage(InCandidateImage)
	{
	}

	// TODO : try to match the signature of other trackable resources
	void UpdateTrackerData(FLuminARImplementation* InARSystemSupport, const FMagicLeapImageTargetState& TargetState);

private:
	const FLuminARImageTracker& Tracker;
	UARCandidateImage* CandidateImage;
};
