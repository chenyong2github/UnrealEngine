// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

class ILatencyMarkerModule : public IModularFeature
{
public:
	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("LatencyMarker"));
		return FeatureName;
	}

	virtual void Initialize() = 0;

	virtual void SetGameLatencyMarkerStart(uint64 FrameNumber) = 0;
	virtual void SetGameLatencyMarkerEnd(uint64 FrameNumber) = 0;
	virtual void SetRenderLatencyMarkerStart(uint64 FrameNumber) = 0;
	virtual void SetRenderLatencyMarkerEnd(uint64 FrameNumber) = 0;
	
	virtual void SetCustomLatencyMarker(uint32 MarkerId, uint64 FrameNumber) = 0;
};
