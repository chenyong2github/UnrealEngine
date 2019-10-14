// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

struct FDrawContext;
struct FSlateBrush;
struct FSlateFontInfo;
struct FTimingEvent;
class FTimingTrackViewport;
struct FTimingViewTooltip;
class ITimingViewDrawHelper;
class FTooltipDrawState;
namespace Trace { class IAnalysisSession; }

////////////////////////////////////////////////////////////////////////////////////////////////////

struct TRACEINSIGHTS_API FTimingEventsTrackLayout
{
	static constexpr float RealMinTimelineH = 13.0f;

	static constexpr float NormalLayoutEventH = 14.0f;
	static constexpr float NormalLayoutEventDY = 2.0f;
	static constexpr float NormalLayoutTimelineDY = 14.0f;
	static constexpr float NormalLayoutMinTimelineH = 0.0f;

	static constexpr float CompactLayoutEventH = 8.0f;
	static constexpr float CompactLayoutEventDY = 1.0f;
	static constexpr float CompactLayoutTimelineDY = 2.0f;
	static constexpr float CompactLayoutMinTimelineH = 0.0f;

	//////////////////////////////////////////////////

	bool bIsCompactMode;

	float EventH; // height of a timing event, in Slate units
	float EventDY; // vertical distance between two timing event sub-tracks, in Slate units
	float TimelineDY; // space at top and bottom of each timeline, in Slate units
	float MinTimelineH;
	float TargetMinTimelineH;

	//////////////////////////////////////////////////

	float GetLaneY(uint32 Depth) const { return 1.0f + TimelineDY + Depth * (EventDY + EventH); }

	void ForceNormalMode();
	void ForceCompactMode();
	void Update();
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FTimingEventsTrack : public FBaseTimingTrack
{
public:
	explicit FTimingEventsTrack(uint64 InTrackId, const FName& InType, const FName& InSubType, const FString& InName);
	virtual ~FTimingEventsTrack();

	int32 GetDepth() const { return NumLanes - 1; }
	int32 GetNumLanes() const { return NumLanes; }
	void SetDepth(int32 InDepth) { NumLanes = InDepth + 1; }

	virtual void Reset() override;
	virtual void UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport);

	virtual void Draw(ITimingViewDrawHelper& Helper) const = 0;
	virtual void DrawSelectedEventInfo(const FTimingEvent& SelectedTimingEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const {}
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const {}

	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const { return false; }
	virtual void ComputeTimingEventStats(FTimingEvent& InOutTimingEvent) const {}

	// Called back from the timing view when an event is selected
	virtual void OnEventSelected(const FTimingEvent& SelectedTimingEvent) const {}

private:
	int32 NumLanes; // number of lanes (sub-tracks)

	// TODO: Cached OnPaint state.
	//TArray<FEventBoxInfo> Boxes;
	//TArray<FEventBoxInfo> MergedBorderBoxes;
	//TArray<FEventBoxInfo> Borders;
	//TArray<FTextInfo> Texts;

public:
	static bool bUseDownSampling;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
