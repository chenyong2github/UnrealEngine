// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

class SSlider;
class FReply;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FChaosVDFrameChangedDelegate, int32)

/** Simple timeline control widget */
class SChaosVDTimelineWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SChaosVDTimelineWidget ){}
		SLATE_ARGUMENT(int32, MaxFrames)
		SLATE_EVENT(FChaosVDFrameChangedDelegate, OnFrameChanged)
		SLATE_ARGUMENT(bool, HidePlayStopButtons)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void UpdateMinMaxValue(float NewMin, float NewMax);

	/** Brings back the state of the timeline to its original state*/
	void ResetTimeline();

	/** Called when a new frame is manually selected or auto-updated during playback */
	FChaosVDFrameChangedDelegate& OnFrameChanged() { return FrameChangedDelegate; }

protected:

	void SetCurrentTimelineFrame(float FrameNumber);

	FReply  Play();
	FReply  Stop();
	FReply  Next();
	FReply  Prev();

	const FSlateBrush* GetPlayOrPauseIcon() const;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedPtr<SSlider> TimelineSlider;

	int32 CurrentFrame = 0;
	int32 MinFrames = 0;
	int32 MaxFrames = 1000;

	bool bIsPlaying = false;
	float CurrentPlaybackTime = 0.0f;

	FChaosVDFrameChangedDelegate FrameChangedDelegate;
};
