// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SChaosVDTimelineWidget.h"

#include "ChaosVDStyle.h"
#include "Input/Reply.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

void SChaosVDTimelineWidget::Construct(const FArguments& InArgs)
{
	MaxFrames = InArgs._MaxFrames;
	FrameChangedDelegate = InArgs._OnFrameChanged;

	SetCanTick(false);

	ChildSlot
	[
		SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.FillWidth(0.2)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(SButton)
					.Visibility(InArgs._HidePlayStopButtons ? EVisibility::Collapsed : EVisibility::Visible)
					.OnClicked( FOnClicked::CreateRaw(this, &SChaosVDTimelineWidget::Play))
					.ContentPadding( 2.0f )
					.ForegroundColor( FSlateColor::UseForeground() )
					.IsFocusable( false )
					[
						SNew( SImage )
						.Image_Raw(this, &SChaosVDTimelineWidget::GetPlayOrPauseIcon)
						.DesiredSizeOverride(FVector2D(16.0f,16.0f))
						.ColorAndOpacity( FSlateColor::UseForeground() )
					]
				]
				+SHorizontalBox::Slot()
				[
					SNew(SButton)
					.Visibility(InArgs._HidePlayStopButtons ? EVisibility::Collapsed : EVisibility::Visible)
					.OnClicked( FOnClicked::CreateRaw(this, &SChaosVDTimelineWidget::Stop))
					.ContentPadding( 2.0f )
					.ForegroundColor( FSlateColor::UseForeground() )
					.IsFocusable( false )
					[
						SNew( SImage )
						.Image(FChaosVDStyle::Get().GetBrush("StopIcon"))
						.DesiredSizeOverride(FVector2D(16.0f,16.0f))
						.ColorAndOpacity( FSlateColor::UseForeground() )
					]
				]
				+SHorizontalBox::Slot()
				[
					SNew(SButton)
					.OnClicked( FOnClicked::CreateRaw(this, &SChaosVDTimelineWidget::Prev))
					.ContentPadding( 2.0f )
					.ForegroundColor( FSlateColor::UseForeground() )
					.IsFocusable( false )
					[
						SNew( SImage )
						.DesiredSizeOverride(FVector2D(16.0f,16.0f))
						.Image(FChaosVDStyle::Get().GetBrush("PrevIcon"))
						.ColorAndOpacity( FSlateColor::UseForeground() )
					]
				]
				+SHorizontalBox::Slot()
				[
					SNew(SButton)
					.OnClicked( FOnClicked::CreateRaw(this, &SChaosVDTimelineWidget::Next))
					.ContentPadding( 2.0f )
					.ForegroundColor( FSlateColor::UseForeground() )
					.IsFocusable( false )
					[
						SNew( SImage )
						.DesiredSizeOverride(FVector2D(16.0f,16.0f))
						.Image(FChaosVDStyle::Get().GetBrush("NextIcon"))
						.ColorAndOpacity( FSlateColor::UseForeground() )
					]
				]
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(0.75)
			[
			  SAssignNew(TimelineSlider, SSlider)
			  .ToolTipText_Lambda([this]()-> FText{ return FText::AsNumber(CurrentFrame); })
			  .Value(CurrentFrame)
			  .OnValueChanged_Raw(this, &SChaosVDTimelineWidget::SetCurrentTimelineFrame)
			  .StepSize(1)
			  .MaxValue(MaxFrames)
			  .MinValue(0)
			]
			+SHorizontalBox::Slot()
			.FillWidth(0.05)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([this]()->FText{ return FText::Format(LOCTEXT("FramesCounter","{0} / {1}"), CurrentFrame, MaxFrames);})
			]
	];
}

void SChaosVDTimelineWidget::UpdateMinMaxValue(float NewMin, float NewMax)
{
	if (!TimelineSlider.IsValid())
	{
		return;
	}

	TimelineSlider->SetMinAndMaxValues(NewMin, NewMax);

	MinFrames = NewMin;
	MaxFrames = NewMax;
	CurrentFrame = NewMin;
}

void SChaosVDTimelineWidget::ResetTimeline()
{
	TimelineSlider->SetValue(MinFrames);
	CurrentFrame = MinFrames;
}

void SChaosVDTimelineWidget::SetCurrentTimelineFrame(float FrameNumber)
{
	CurrentFrame = static_cast<int32>(FrameNumber);
	FrameChangedDelegate.ExecuteIfBound(CurrentFrame);
}

FReply SChaosVDTimelineWidget::Play()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		SetCanTick(false);
	}
	else
	{
		bIsPlaying = true;
		SetCanTick(true);
	}

	return FReply::Handled();
}

FReply SChaosVDTimelineWidget::Stop()
{
	CurrentFrame = 0;
	CurrentPlaybackTime = 0.0f;
	bIsPlaying = false;
	TimelineSlider->SetValue(CurrentFrame);

	SetCurrentTimelineFrame(CurrentFrame);

	SetCanTick(false);

	return FReply::Handled();
}

FReply SChaosVDTimelineWidget::Next()
{
	if (CurrentFrame >= MaxFrames)
	{
		CurrentFrame = MaxFrames;
		return FReply::Handled();
	}

	CurrentFrame++;
	TimelineSlider->SetValue(CurrentFrame);
	
	SetCurrentTimelineFrame(CurrentFrame);

	return FReply::Handled();
}

FReply SChaosVDTimelineWidget::Prev()
{
	if (CurrentFrame == 0)
	{
		return FReply::Handled();
	}

	CurrentFrame--;
	TimelineSlider->SetValue(CurrentFrame);

	SetCurrentTimelineFrame(CurrentFrame);

	return FReply::Handled();
}

const FSlateBrush* SChaosVDTimelineWidget::GetPlayOrPauseIcon() const
{
	return bIsPlaying ? FChaosVDStyle::Get().GetBrush("PauseIcon") : FChaosVDStyle::Get().GetBrush("PlayIcon");
}


void SChaosVDTimelineWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (bIsPlaying)
	{
		if (CurrentFrame == MaxFrames)
		{
			Stop();
		}

		//TODO: Allow to customize the playback frametimes so it can be made an option or read from the recorded file
		constexpr float PlaybackFrameTime = 0.016f;
		
		CurrentPlaybackTime += InDeltaTime;

		if (CurrentPlaybackTime > PlaybackFrameTime)
		{
			CurrentPlaybackTime = 0.0f;
			Next();
		}
	}
}

#undef LOCTEXT_NAMESPACE
