// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILuminARTracker.h"
#include "LuminARTrackableResource.h"

class FLuminARImplementation;

class FLuminARPointsTracker : public ILuminARTracker
{
public:
	FLuminARPointsTracker(FLuminARImplementation& InARSystemSupport);

	virtual void CreateEntityTracker() override;
	virtual void DestroyEntityTracker() override;
	virtual void OnStartGameFrame() override;
	virtual bool IsHandleTracked(const FGuid& Handle) const override;
	virtual UARTrackedGeometry* CreateTrackableObject() override;
	virtual UClass* GetARComponentClass(const UARSessionConfig& SessionConfig) override;
	virtual IARRef* CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject) override;

private:
	TArray<FGuid> TrackedPoints;
};

class FLuminARTrackedPointResource : public FLuminARTrackableResource
{
public:
	FLuminARTrackedPointResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry)
		: FLuminARTrackableResource(InTrackableHandle, InTrackedGeometry)
	{
	}

	void UpdateGeometryData(FLuminARImplementation* InARSystemSupport) override;
};
