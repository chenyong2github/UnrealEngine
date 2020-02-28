// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARTrackableResource.h"

EARTrackingState FLuminARTrackableResource::GetTrackingState() const
{
	if (MagicLeap::FGuidIsValidHandle(TrackableHandle))
	{
		check(TrackedGeometry);
		return TrackedGeometry->GetTrackingState();
	}

	return EARTrackingState::NotTracking;
}

void FLuminARTrackableResource::UpdateGeometryData(FLuminARImplementation* InARSystemSupport)
{
	TrackedGeometry->UpdateTrackingState(GetTrackingState());
}

void FLuminARTrackableResource::ResetNativeHandle(LuminArTrackable* InTrackableHandle)
{
	TrackableHandle = MagicLeap::INVALID_FGUID;
	if (InTrackableHandle != nullptr)
	{
		TrackableHandle = InTrackableHandle->Handle;
	}

	UpdateGeometryData(nullptr);
}
