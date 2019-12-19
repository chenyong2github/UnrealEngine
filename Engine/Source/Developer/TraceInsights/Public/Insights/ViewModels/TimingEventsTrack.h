// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API ITimingEventsTrackDrawStateBuilder
{
public:
	virtual ~ITimingEventsTrackDrawStateBuilder() = default;

	virtual void AddEvent(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName, uint64 InEventType = 0, uint32 InEventColor = 0) = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FTimingEventsTrack : public FBaseTimingTrack
{
	INSIGHTS_DECLARE_RTTI(FTimingEventsTrack, FBaseTimingTrack)

public:
	explicit FTimingEventsTrack();
	explicit FTimingEventsTrack(const FString& InTrackName);
	virtual ~FTimingEventsTrack();

	//////////////////////////////////////////////////
	// FBaseTimingTrack

	virtual void Reset() override;

	virtual void PreUpdate(const ITimingTrackUpdateContext& Context) override;
	virtual void PostUpdate(const ITimingTrackUpdateContext& Context) override;

	virtual void Draw(const ITimingTrackDrawContext& Context) const override;
	virtual void DrawEvent(const ITimingTrackDrawContext& Context, const ITimingEvent& InTimingEvent, EDrawEventMode InDrawMode) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;

	virtual TSharedPtr<ITimingEventFilter> GetFilterByEvent(const TSharedPtr<const ITimingEvent> InTimingEvent) const override;

	//////////////////////////////////////////////////

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) = 0;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) {}

protected:
	int32 GetNumLanes() const { return NumLanes; }
	void SetNumLanes(int32 InNumLanes) { NumLanes = InNumLanes; }

	void UpdateTrackHeight(const ITimingTrackUpdateContext& Context);

	void DrawEvents(const ITimingTrackDrawContext& Context, const float OffsetY = 1.0f) const;
	void DrawHeader(const ITimingTrackDrawContext& Context) const;

	void DrawMarkers(const ITimingTrackDrawContext& Context, float LineY, float LineH) const;

	int32 GetHeaderBackgroundLayerId(const ITimingTrackDrawContext& Context) const;
	int32 GetHeaderTextLayerId(const ITimingTrackDrawContext& Context) const;

private:
	int32 NumLanes; // number of lanes (sub-tracks)
	TSharedRef<struct FTimingEventsTrackDrawState> DrawState;
	TSharedRef<struct FTimingEventsTrackDrawState> FilteredDrawState;

	struct FFilteredDrawStateInfo
	{
		double ViewportStartTime = 0.0;
		double ViewportScaleX = 0.0;
		double LastBuildDuration = 0.0;
		TWeakPtr<ITimingEventFilter> LastEventFilter;
		uint32 LastFilterChangeNumber = 0;
		uint32 Counter = 0;
		mutable float Opacity = 0.0f;
	};
	FFilteredDrawStateInfo FilteredDrawStateInfo;

public:
	static bool bUseDownSampling; // toggle to enable/disable downsampling, for debugging purposes only
};

////////////////////////////////////////////////////////////////////////////////////////////////////
