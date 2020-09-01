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

	virtual void SetEnabled(bool bInEnabled) = 0;
	virtual bool GetEnabled() = 0;
	virtual void SetGameLatencyMarkerStart(uint64 FrameNumber) = 0;
	virtual void SetGameLatencyMarkerEnd(uint64 FrameNumber) = 0;
	virtual void SetRenderLatencyMarkerStart(uint64 FrameNumber) = 0;
	virtual void SetRenderLatencyMarkerEnd(uint64 FrameNumber) = 0;
	
	virtual void SetCustomLatencyMarker(uint32 MarkerId, uint64 FrameNumber) = 0;

	virtual float GetTotalLatencyInMs() = 0;
	virtual float GetGameLatencyInMs() = 0;
	virtual float GetRenderLatencyInMs() = 0;
	virtual float GetDriverLatencyInMs() = 0;
	virtual float GetOSWorkQueueLatencyInMs() = 0;
	virtual float GetGPURenderLatencyInMs() = 0;

	virtual float GetRenderOffsetFromGameInMs() = 0;
	virtual float GetDriverOffsetFromGameInMs() = 0;
	virtual float GetOSWorkQueueOffsetFromGameInMs() = 0;
	virtual float GetGPURenderOffsetFromGameInMs() = 0;
};
