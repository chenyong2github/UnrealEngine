// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILuminARTracker.h"
#include "LuminARTypes.h"

class FLuminARImplementation;

class FLuminARLightTracker : public ILuminARTracker
{
public:
	FLuminARLightTracker(FLuminARImplementation& InARSystemSupport);

	virtual void CreateEntityTracker() override;
	virtual void DestroyEntityTracker() override;
	virtual void OnStartGameFrame() override;

public:
	ULuminARLightEstimate* LightEstimate;
};
