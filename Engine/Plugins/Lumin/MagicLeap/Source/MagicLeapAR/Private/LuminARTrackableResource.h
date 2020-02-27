// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapHandle.h"
#include "ARTypes.h"
#include "ARTrackable.h"
#include "LuminARTypes.h"
#include "LuminARTypesPrivate.h"

class FLuminARImplementation;

class FLuminARTrackableResource : public IARRef
{
public:
	// IARRef interface
	virtual void AddRef() override { }

	virtual void RemoveRef() override
	{
		TrackableHandle = MagicLeap::INVALID_FGUID;
	}

public:
	FLuminARTrackableResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry)
		: TrackableHandle(InTrackableHandle)
		, TrackedGeometry(InTrackedGeometry)
	{
		ensure(MagicLeap::FGuidIsValidHandle(TrackableHandle));
	}

	virtual ~FLuminARTrackableResource()
	{
	}

	EARTrackingState GetTrackingState() const;

	virtual void UpdateGeometryData(FLuminARImplementation* InARSystemSupport);

	const FGuid& GetNativeHandle() const { return TrackableHandle; }

	void ResetNativeHandle(LuminArTrackable* InTrackableHandle);

protected:
	FGuid TrackableHandle;
	UARTrackedGeometry* TrackedGeometry;
};
