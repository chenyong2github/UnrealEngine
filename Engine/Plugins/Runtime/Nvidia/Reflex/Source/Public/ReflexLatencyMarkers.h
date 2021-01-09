// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Performance/LatencyMarkerModule.h"

class FReflexLatencyMarkers : public ILatencyMarkerModule, public FTickableGameObject
{
public:
	virtual ~FReflexLatencyMarkers() {}

	bool bProperDriverVersion = false;

	float AverageTotalLatencyMs = 0.0f;
	float AverageGameLatencyMs = 0.0f;
	float AverageRenderLatencyMs = 0.0f;
	float AverageDriverLatencyMs = 0.0f;
	float AverageOSWorkQueueLatencyMs = 0.0f;
	float AverageGPURenderLatencyMs = 0.0f;

	float RenderOffsetMs = 0.0f;
	float DriverOffsetMs = 0.0f;
	float OSWorkQueueOffsetMs = 0.0f;
	float GPURenderOffsetMs = 0.0f;

	bool bEnabled = false;

	virtual void Tick(float DeltaTime);
	virtual bool IsTickable() const { return true; }
	virtual bool IsTickableInEditor() const { return true; }
	virtual bool IsTickableWhenPaused() const { return true; }
	virtual TStatId GetStatId(void) const { RETURN_QUICK_DECLARE_CYCLE_STAT(FLatencyMarkers, STATGROUP_Tickables); }

	virtual void Initialize() override;

	virtual void SetEnabled(bool bInEnabled) override { bEnabled = bInEnabled; }
	virtual bool GetEnabled() override;

	virtual void SetGameLatencyMarkerStart(uint64 FrameNumber) override;
	virtual void SetGameLatencyMarkerEnd(uint64 FrameNumber) override;
	virtual void SetRenderLatencyMarkerStart(uint64 FrameNumber) override;
	virtual void SetRenderLatencyMarkerEnd(uint64 FrameNumber) override;
	virtual void SetCustomLatencyMarker(uint32 MarkerId, uint64 FrameNumber) override;

	virtual float GetTotalLatencyInMs() override { return AverageTotalLatencyMs; }
	virtual float GetGameLatencyInMs() override { return AverageGameLatencyMs; }
	virtual float GetRenderLatencyInMs() override { return AverageRenderLatencyMs; }
	virtual float GetDriverLatencyInMs() override { return AverageDriverLatencyMs; }
	virtual float GetOSWorkQueueLatencyInMs() override { return AverageOSWorkQueueLatencyMs; }
	virtual float GetGPURenderLatencyInMs() override { return AverageGPURenderLatencyMs; }

	virtual float GetRenderOffsetFromGameInMs() override { return RenderOffsetMs; }
	virtual float GetDriverOffsetFromGameInMs() override { return DriverOffsetMs; }
	virtual float GetOSWorkQueueOffsetFromGameInMs() override { return OSWorkQueueOffsetMs; }
	virtual float GetGPURenderOffsetFromGameInMs() override { return GPURenderOffsetMs; }
};