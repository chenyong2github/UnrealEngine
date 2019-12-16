// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "TraceServices/Model/Frames.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventSearch.h" // for TTimingEventSearchCache
#include "Insights/ViewModels/TimingEventsTrack.h"

class FTimingEvent;
class FTimingEventSearchParameters;
class FFrameTimingTrack;
class STimingView;
struct FSlateBrush;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFrameSharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FFrameSharedState>
{
public:
	explicit FFrameSharedState(STimingView* InTimingView) : TimingView(InTimingView) {}
	virtual ~FFrameSharedState() = default;

	TSharedPtr<FFrameTimingTrack> GetFrameTrack(uint32 InFrameType);

	bool IsFrameTrackVisible(uint32 InFrameType) const;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	void ShowHideAllFrameTracks() { ShowHideAllFrameTracks_Execute(); }

private:
	bool ShowHideAllFrameTracks_IsChecked() const;
	void ShowHideAllFrameTracks_Execute();

private:
	STimingView* TimingView;

	bool bShowHideAllFrameTracks;

	/** Maps frame type to track pointer. */
	TMap<uint32, TSharedPtr<FFrameTimingTrack>> FrameTracks;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFrameTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FFrameTimingTrack, FTimingEventsTrack)

public:
	explicit FFrameTimingTrack(FFrameSharedState& InSharedState, const FString& InName, uint32 InFrameType)
		: FTimingEventsTrack(InName)
		, SharedState(InSharedState)
		, FrameType(InFrameType)
	{
	}

	virtual ~FFrameTimingTrack() {}

	//////////////////////////////////////////////////

	uint32 GetFrameType() const { return FrameType; }
	//void SetFrameType(uint32 InFrameType) { FrameType = InFrameType; }

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;

	virtual void OnEventSelected(const ITimingEvent& InSelectedEvent) const override;
	virtual void OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	const FString GetShortFrameName(const uint64 FrameIndex) const;
	const FString GetFrameName(const uint64 FrameIndex) const;
	const FString GetCompleteFrameName(const uint64 FrameIndex, const double FrameDuration) const;

private:
	void DrawSelectedEventInfo(const FTimingEvent& SelectedEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const;

	bool FindFrame(const FTimingEvent& InTimingEvent, TFunctionRef<void(double, double, uint32, const Trace::FFrame&)> InFoundPredicate) const;
	bool FindFrame(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FFrame&)> InFoundPredicate) const;

private:
	FFrameSharedState& SharedState;

	uint32 FrameType; // ETraceFrameType

	// Search cache
	mutable TTimingEventSearchCache<Trace::FFrame> SearchCache;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
