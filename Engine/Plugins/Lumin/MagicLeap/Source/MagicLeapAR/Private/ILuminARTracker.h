// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMagicLeapTrackerEntity.h"
#include "ARTypes.h"
#include "ARTrackable.h"
#include "ARSessionConfig.h"

class FLuminARImplementation;

class ILuminARTracker : public IMagicLeapTrackerEntity
{
public:
	ILuminARTracker(FLuminARImplementation& InARSystemSupport)
	: ARSystemSupport(&InARSystemSupport)
	{}

	virtual ~ILuminARTracker() {}

	virtual void OnStartGameFrame() {}
	virtual bool IsHandleTracked(const FGuid& Handle) const { return false; }
	virtual UARTrackedGeometry* CreateTrackableObject() { return nullptr; }
	virtual UClass* GetARComponentClass(const UARSessionConfig& SessionConfig) { return nullptr;  }
	virtual IARRef* CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject) { return nullptr; }

protected:
	FLuminARImplementation* ARSystemSupport;
};
