// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SFrameTrack.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"
#include "Widgets/Layout/SScrollBar.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/FrameTrackHelper.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SFrameTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

SFrameTrack::SFrameTrack()
	: HoveredSample(nullptr, nullptr)
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SFrameTrack::~SFrameTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::Reset()
{
	Viewport.Reset();
	bIsViewportDirty = true;

	CachedTimelines.Reset();
	TimelinesOrder.Reset();
	TimelinesOrder.Add(0);
	TimelinesOrder.Add(1);
	TimelinesOrder.Add(2);
	bIsStateDirty = true;

	AnalysisSyncNextTimestamp = 0;

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportPosXOnButtonDown = 0.0f;
	ViewportPosYOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsScrolling = false;

	SelectionStartFrameIndex = 0;
	SelectionEndFrameIndex = 0;

	HoveredSample.Timeline = nullptr;
	HoveredSample.Sample = nullptr;

	//ThisGeometry

	CursorType = EFrameTrackCursor::Default;

	NumUpdatedFrames = 0;
	UpdateDurationHistory.Reset();
	DrawDurationHistory.Reset();
	OnPaintDurationHistory.Reset();
	LastOnPaintTime = FPlatformTime::Cycles64();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		.Padding(FMargin(0, 0, 0, 0))
		[
			SAssignNew(HorizontalScrollBar, SScrollBar)
			.Orientation(Orient_Horizontal)
			.AlwaysShowScrollbar(false)
			.Visibility(EVisibility::Visible)
			.Thickness(FVector2D(5.0f, 5.0f))
			.RenderOpacity(0.75)
			.OnUserScrolled(this, &SFrameTrack::HorizontalScrollBar_OnUserScrolled)
		]
	];

	UpdateHorizontalScrollBar();

	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ThisGeometry != AllottedGeometry || bIsViewportDirty)
	{
		bIsViewportDirty = false;
		const float ViewWidth = AllottedGeometry.GetLocalSize().X;
		const float ViewHeight = AllottedGeometry.GetLocalSize().Y;
		Viewport.SetSize(ViewWidth, ViewHeight);
		bIsStateDirty = true;
	}

	ThisGeometry = AllottedGeometry;

	if (!bIsScrolling)
	{
		// Elastic snap to horizontal limits (+15%).

		float MinX, MaxX;
		if (Viewport.MaxX - Viewport.MinX < Viewport.Width)
		{
			MinX = Viewport.MaxX - Viewport.Width;
			MaxX = Viewport.MinX;
		}
		else
		{
			MinX = Viewport.MinX - 0.15f * Viewport.Width;
			MaxX = Viewport.MaxX - 0.85f * Viewport.Width;
		}
		const float U = 0.5f;

		float PosX = Viewport.PosX;
		if (PosX < MinX)
		{
			PosX = PosX * U + (1.0f - U) * MinX;
			if (FMath::IsNearlyEqual(PosX, MinX, 1.0f))
				PosX = MinX;
		}
		else if (PosX > MaxX)
		{
			PosX = PosX * U + (1.0f - U) * MaxX;
			if (FMath::IsNearlyEqual(PosX, MaxX, 1.0f))
				PosX = MaxX;
			if (PosX < MinX)
				PosX = MinX;
		}

		if (Viewport.PosX != PosX)
		{
			//Viewport.ScrollAtPosX(PosX);
			Viewport.ScrollAtIndex(Viewport.GetIndexAtPosX(PosX));
			bIsStateDirty = true;
		}
	}

	uint64 Time = FPlatformTime::Cycles64();
	if (Time > AnalysisSyncNextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.1 / FPlatformTime::GetSecondsPerCycle64()); // 100ms
		AnalysisSyncNextTimestamp = Time + WaitTime;

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(*Session.Get());

			for (int32 FrameType = 0; FrameType < TraceFrameType_Count; ++FrameType)
			{
				FFrameTrackTimeline& CachedTimeline = CachedTimelines.FindOrAdd(FrameType);
				CachedTimeline.Id = FrameType;

				int32 NumFrames = FramesProvider.GetFrameCount(static_cast<ETraceFrameType>(FrameType));
				if (NumFrames > Viewport.MaxIndex)
				{
					Viewport.SetMinMaxIndexInterval(0, NumFrames);
					UpdateHorizontalScrollBar();
					bIsStateDirty = true;

					// Auto zoom out.
					float SampleW = Viewport.GetSampleWidth();
					int32 NumSamples = FMath::CeilToInt(Viewport.Width / SampleW);
					if (NumFrames > NumSamples * Viewport.GetNumFramesPerSample())
					{
						ZoomHorizontally(-0.1f, 0.0f);
						Viewport.ScrollAtPosX(0.0f);
					}
				}
			}
		}
	}

	if (bIsStateDirty)
	{
		bIsStateDirty = false;
		UpdateState();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::UpdateState()
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Reset stats.
	for (TPair<uint64, FFrameTrackTimeline>& KeyValuePair : CachedTimelines)
	{
		FFrameTrackTimeline& Timeline = KeyValuePair.Value;
		Timeline.NumAggregatedFrames = 0;
	}
	NumUpdatedFrames = 0;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(*Session.Get());

		for (int32 FrameType = 0; FrameType < TraceFrameType_Count; ++FrameType)
		{
			FFrameTrackTimeline& Timeline = CachedTimelines.FindOrAdd(FrameType);
			Timeline.Id = FrameType;

			FFrameTrackTimelineBuilder Builder(Timeline, Viewport);

			uint64 StartIndex = static_cast<uint64>(FMath::Max(0, Viewport.GetIndexAtViewportX(0.0f)));
			uint64 EndIndex = static_cast<uint64>(Viewport.GetIndexAtViewportX(Viewport.Width));

			FramesProvider.EnumerateFrames(static_cast<ETraceFrameType>(FrameType), StartIndex, EndIndex, [&Builder](const Trace::FFrame& Frame)
			{
				Builder.AddFrame(Frame);
			});

			NumUpdatedFrames += Builder.GetNumAddedFrames();
		}
	}

	Stopwatch.Stop();
	UpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSampleRef SFrameTrack::GetSampleAtMousePosition(float X, float Y)
{
	float SampleW = Viewport.GetSampleWidth();
	int32 SampleIndex = FMath::FloorToInt(X / SampleW);
	if (SampleIndex >= 0)
	{
		const float ViewportHeight = FMath::RoundToFloat(Viewport.Height);
		const float SampleY2 = ViewportHeight - FMath::RoundToFloat(Viewport.GetViewportYForValue(0.0));

		// Search in reverse paint order.
		for (int32 TimelineIndex = TimelinesOrder.Num() - 1; TimelineIndex >= 0; --TimelineIndex)
		{
			int32 TimelineId = TimelinesOrder[TimelineIndex];
			if (CachedTimelines.Contains(TimelineId))
			{
				const FFrameTrackTimeline& CachedTimeline = CachedTimelines[TimelineId];
				if (CachedTimeline.NumAggregatedFrames > 0 &&
					SampleIndex < CachedTimeline.Samples.Num())
				{
					const FFrameTrackSample& Sample = CachedTimeline.Samples[SampleIndex];
					if (Sample.NumFrames > 0)
					{
						float SampleY1;
						if (Sample.LargestFrameDuration == std::numeric_limits<double>::infinity())
						{
							SampleY1 = 0.0;
						}
						else
						{
							SampleY1 = ViewportHeight - FMath::RoundToFloat(Viewport.GetViewportYForValue(Sample.LargestFrameDuration));
						}
						if (Y >= SampleY1 && Y <= SampleY2)
						{
							return FSampleRef(&CachedTimeline, &Sample);
						}
					}
				}
			}
		}
	}
	return FSampleRef(nullptr, nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::SelectFrameAtMousePosition(float X, float Y)
{
	FSampleRef SampleRef = GetSampleAtMousePosition(X, Y);
	if (!SampleRef.IsValid())
	{
		SampleRef = GetSampleAtMousePosition(X - 1.0f, Y);
	}
	if (!SampleRef.IsValid())
	{
		SampleRef = GetSampleAtMousePosition(X + 1.0f, Y);
	}

	if (SampleRef.IsValid())
	{
		TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Window.IsValid())
		{
			const double StartTime = SampleRef.Sample->LargestFrameStartTime;
			const double Duration = SampleRef.Sample->LargestFrameDuration;
			Window->TimingView->CenterOnTimeInterval(StartTime, Duration);
			Window->TimingView->SelectTimeInterval(StartTime, Duration);
			FSlateApplication::Get().SetKeyboardFocus(Window->TimingView);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SFrameTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	const float ViewWidth = AllottedGeometry.Size.X;
	const float ViewHeight = AllottedGeometry.Size.Y;

	int32 NumDrawSamples = 0;

	{
		FStopwatch Stopwatch;
		Stopwatch.Start();

		FFrameTrackDrawHelper Helper(DrawContext, Viewport);

		Helper.DrawBackground();

		// Draw frames, for each visible timeline.
		for (const TPair<uint64, FFrameTrackTimeline>& KeyValuePair : CachedTimelines)
		{
			const FFrameTrackTimeline& Timeline = KeyValuePair.Value;
			Helper.DrawCached(Timeline);
		}
		NumDrawSamples = Helper.GetNumDrawSamples();

		// Highlight the mouse hovered sample.
		if (HoveredSample.IsValid())
		{
			Helper.DrawHoveredSample(*HoveredSample.Sample);

			FString Text = FString::Format(TEXT("{0} frame {1} ({2})"),
				{
					(HoveredSample.Timeline->Id == 0) ? TEXT("Game") :
					(HoveredSample.Timeline->Id == 1) ? TEXT("Render") : TEXT("Misc"),
					FText::AsNumber(HoveredSample.Sample->LargestFrameIndex).ToString(),
					TimeUtils::FormatTimeAuto(HoveredSample.Sample->LargestFrameDuration)
				});

			FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
			FVector2D TextSize = FontMeasureService->Measure(Text, SummaryFont);

			const float DX = 2.0f;
			const float W2 = TextSize.X / 2 + DX;

			float X1 = Viewport.GetViewportXForIndex(HoveredSample.Sample->LargestFrameIndex);
			float CX = X1 + FMath::RoundToFloat(Viewport.GetSampleWidth() / 2);
			if (CX + W2 > Viewport.Width)
			{
				CX = FMath::RoundToFloat(Viewport.Width - W2);
			}
			if (CX - W2 < 0)
			{
				CX = W2;
			}

			const float Y = 10.0f;
			const float H = 14.0f;
			DrawContext.DrawBox(CX - W2, Y, 2 * W2, H, WhiteBrush, FLinearColor(0.7, 0.7, 0.7, 0.9));
			DrawContext.DrawText(CX - W2 + DX, Y + 1.0f, Text, SummaryFont, FLinearColor(0.0, 0.0, 0.0, 0.9));
		}

		TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Window && Window->TimingView && CachedTimelines.Num() > 0)
		{
			// Highlight the area corresponding to viewport of Timing View.
			const double StartTime = Window->TimingView->GetViewport().StartTime;
			const double EndTime = Window->TimingView->GetViewport().EndTime;
			Helper.DrawHighlightedInterval(CachedTimelines[0], StartTime, EndTime);
		}

		Stopwatch.Stop();
		DrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}

	//////////////////////////////////////////////////

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 240.0f;
		const float DbgH = DbgDY * 4 + 3.0f;
		const float DbgX = ViewWidth - DbgW - 20.0f;
		float DbgY = 7.0f;

		++LayerId;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, DbgX - 2.0f, DbgY - 2.0f, DbgW, DbgH),
			WhiteBrush,
			DrawEffects,
			FLinearColor(1.0, 1.0, 1.0, 0.9)
		);

		++LayerId;
		FLinearColor DbgTextColor(0.0, 0.0, 0.0, 0.9);

		// Time interval since last OnPaint call.
		const uint64 CurrentTime = FPlatformTime::Cycles64();
		const uint64 OnPaintDuration = CurrentTime - LastOnPaintTime;
		LastOnPaintTime = CurrentTime;
		OnPaintDurationHistory.AddValue(OnPaintDuration); // saved for last 32 OnPaint calls
		const uint64 AvgOnPaintDuration = OnPaintDurationHistory.ComputeAverage();
		const uint64 AvgOnPaintDurationMs = FStopwatch::Cycles64ToMilliseconds(AvgOnPaintDuration);
		const double AvgOnPaintFps = AvgOnPaintDurationMs != 0 ? 1.0 / FStopwatch::Cycles64ToSeconds(AvgOnPaintDuration) : 0.0;

		const uint64 AvgUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(UpdateDurationHistory.ComputeAverage());
		const uint64 AvgDrawDurationMs = FStopwatch::Cycles64ToMilliseconds(DrawDurationHistory.ComputeAverage());

		// Draw performance info.
		FSlateDrawElement::MakeText
		(
			OutDrawElements,
			LayerId,
			MAKE_PAINT_GEOMETRY_PT(AllottedGeometry, DbgX, DbgY),
			FString::Printf(TEXT("U: %llu ms, D: %llu ms + %llu ms = %llu ms (%d fps)"),
				AvgUpdateDurationMs, // caching time
				AvgDrawDurationMs, // drawing time
				AvgOnPaintDurationMs - AvgDrawDurationMs, // other overhead to OnPaint calls
				AvgOnPaintDurationMs, // average time between two OnPaint calls
				FMath::RoundToInt(AvgOnPaintFps)), // framerate of OnPaint calls
			SummaryFont,
			DrawEffects,
			DbgTextColor
		);
		DbgY += DbgDY;

		// Draw number of draw calls.
		FSlateDrawElement::MakeText
		(
			OutDrawElements,
			LayerId,
			MAKE_PAINT_GEOMETRY_PT(AllottedGeometry, DbgX, DbgY),
			FString::Format(TEXT("U: {0} frames, D: {1} samples"),
				{
					FText::AsNumber(NumUpdatedFrames).ToString(),
					FText::AsNumber(NumDrawSamples).ToString()
				}),
			SummaryFont,
			DrawEffects,
			DbgTextColor
		);
		DbgY += DbgDY;

		// Draw viewport's horizontal info.
		FSlateDrawElement::MakeText
		(
			OutDrawElements,
			LayerId,
			MAKE_PAINT_GEOMETRY_PT(AllottedGeometry, DbgX, DbgY),
			FString::Printf(TEXT("SX: %.3f (%d %s), X: %.2f"),
				Viewport.ScaleX,
				(Viewport.ScaleX >= 1.0f) ? FMath::RoundToInt(Viewport.ScaleX) : FMath::RoundToInt(1.0f / Viewport.ScaleX),
				(Viewport.ScaleX >= 1.0f) ? TEXT("px/frame") : TEXT("frames/px"),
				Viewport.PosX),
			SummaryFont,
			DrawEffects,
			DbgTextColor
		);
		DbgY += DbgDY;

		// Draw viewport's vertical info.
		FSlateDrawElement::MakeText
		(
			OutDrawElements,
			LayerId,
			MAKE_PAINT_GEOMETRY_PT(AllottedGeometry, DbgX, DbgY),
			FString::Printf(TEXT("SY: %g, Y: %.2f"), Viewport.ScaleY, Viewport.PosY),
			SummaryFont,
			DrawEffects,
			DbgTextColor
		);
		DbgY += DbgDY;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (!IsReady())
	{
		return Reply;
	}

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	ViewportPosXOnButtonDown = Viewport.PosX;
	ViewportPosYOnButtonDown = Viewport.PosY;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsLMB_Pressed = true;

		// Capture mouse.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bIsRMB_Pressed = true;

		// Capture mouse, so we can scroll outside this widget.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (!IsReady())
	{
		return Reply;
	}

	MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, MOUSE_SNAP_DISTANCE);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			// Release the mouse.
			Reply = FReply::Handled().ReleaseMouseCapture();

			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = EFrameTrackCursor::Default;
			}
			else if (bIsValidForMouseClick)
			{
				SelectFrameAtMousePosition(MousePositionOnButtonUp.X, MousePositionOnButtonUp.Y);
			}

			bIsLMB_Pressed = false;
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			// Release mouse as we no longer scroll.
			Reply = FReply::Handled().ReleaseMouseCapture();

			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = EFrameTrackCursor::Default;
			}
			else if (bIsValidForMouseClick)
			{
				ShowContextMenu(MouseEvent.GetScreenSpacePosition());
				Reply = FReply::Handled();
			}

			bIsRMB_Pressed = false;
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (!IsReady())
	{
		return Reply;
	}

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		if (HasMouseCapture() && !MouseEvent.GetCursorDelta().IsZero())
		{
			if (!bIsScrolling)
			{
				bIsScrolling = true;
				CursorType = EFrameTrackCursor::Hand;

				HoveredSample.Reset();
			}

			float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
			Viewport.ScrollAtIndex(Viewport.GetIndexAtPosX(PosX));
			UpdateHorizontalScrollBar();
			bIsStateDirty = true;

			Reply = FReply::Handled();
		}
	}
	else if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
	{
		if (HasMouseCapture() && !MouseEvent.GetCursorDelta().IsZero())
		{
			if (!bIsScrolling)
			{
				bIsScrolling = true;
				CursorType = EFrameTrackCursor::Hand;

				HoveredSample.Reset();
			}

			float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
			Viewport.ScrollAtIndex(Viewport.GetIndexAtPosX(PosX));
			UpdateHorizontalScrollBar();
			bIsStateDirty = true;

			Reply = FReply::Handled();
		}
	}
	else
	{
		HoveredSample = GetSampleAtMousePosition(MousePosition.X, MousePosition.Y);
		if (!HoveredSample.IsValid())
			HoveredSample = GetSampleAtMousePosition(MousePosition.X - 1.0f, MousePosition.Y);
		if (!HoveredSample.IsValid())
			HoveredSample = GetSampleAtMousePosition(MousePosition.X + 1.0f, MousePosition.Y);
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;

		HoveredSample.Reset();

		CursorType = EFrameTrackCursor::Default;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetModifierKeys().IsShiftDown())
	{
		// Zoom in/out vertically.
		const float Delta = MouseEvent.GetWheelDelta();
		constexpr float ZoomStep = 0.25f; // as percent
		float ScaleY;

		if (Delta > 0)
		{
			ScaleY = Viewport.ScaleY * FMath::Pow(1.0f + ZoomStep, Delta);
		}
		else
		{
			ScaleY = Viewport.ScaleY * FMath::Pow(1.0f / (1.0f + ZoomStep), -Delta);
		}

		Viewport.SetScaleY(ScaleY);
		//UpdateVerticalScrollBar();
	}
	else //if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Zoom in/out horizontally.
		const float Delta = MouseEvent.GetWheelDelta();
		ZoomHorizontally(Delta, MousePosition.X);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ZoomHorizontally(const float Delta, const float X)
{
	constexpr float ZoomStep = 0.25f; // as percent
	float ScaleX;

	if (Delta > 0)
	{
		ScaleX = Viewport.ScaleX * FMath::Pow(1.0f + ZoomStep, Delta);
	}
	else
	{
		ScaleX = Viewport.ScaleX * FMath::Pow(1.0f / (1.0f + ZoomStep), -Delta);
	}

	// Snap to integer value of either: "number of frames per pixel (Slate unit)" or "number of pixels per frame".
	if (ScaleX < 1.0f)
	{
		// frames per Slate unit
		if (Delta > 0)
			ScaleX = 1.0f / FMath::FloorToFloat(1.0f / ScaleX);
		else
			ScaleX = 1.0f / FMath::CeilToFloat(1.0f / ScaleX);
	}
	else
	{
		// Slate units per frame
		if (Delta > 0)
			ScaleX = FMath::CeilToFloat(ScaleX);
		else
			ScaleX = FMath::FloorToFloat(ScaleX);
	}

	//UE_LOG(TimingProfiler, Log, TEXT("%.2f, %.2f, %.2f"), Delta, Viewport.ScaleX, ScaleX);
	Viewport.ZoomWithFixedViewportX(ScaleX, MousePosition.X);
	UpdateHorizontalScrollBar();
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SFrameTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FCursorReply CursorReply = FCursorReply::Unhandled();

	if (CursorType == EFrameTrackCursor::Arrow)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	else if (CursorType == EFrameTrackCursor::Hand)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return CursorReply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ShowContextMenu(const FVector2D& ScreenSpacePosition)
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::BindCommands()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	const float SX = 1.0 / (Viewport.MaxX - Viewport.MinX);
	const float ThumbSizeFraction = FMath::Clamp<float>(Viewport.Width * SX, 0.0f, 1.0f);
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	const float PosX = Viewport.MinX + OffsetFraction * (Viewport.MaxX - Viewport.MinX);
	Viewport.ScrollAtPosX(PosX);
	bIsStateDirty = true;

	HorizontalScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::UpdateHorizontalScrollBar()
{
	const float SX = 1.0 / (Viewport.MaxX - Viewport.MinX);
	const float ThumbSizeFraction = FMath::Clamp<float>(Viewport.Width * SX, 0.0f, 1.0f);
	const float ScrollOffset = (Viewport.PosX - Viewport.MinX) * SX;
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	HorizontalScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/*
void SFrameTrack::AddThreadTime(int32 InFrameIndex, const TMap<uint32, float>& InThreadMS, const TSharedRef<FProfilerStatMetaData>& InStatMetaData)
{
	FFrameThreadTimes FrameThreadTimes;
	FrameThreadTimes.FrameNumber = InFrameIndex;
	FrameThreadTimes.ThreadTimes = InThreadMS;
	RecentlyAddedFrames.Add(FrameThreadTimes);

	if (!bIsActiveTimerRegistered)
	{
		bIsActiveTimerRegistered = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFrameTrack::EnsureDataUpdateDuringPreview));
	}

	StatMetadata = InStatMetaData;
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////
/*
EActiveTimerReturnType SFrameTrack::EnsureDataUpdateDuringPreview(double InCurrentTime, float InDeltaTime)
{
	//if (RecentlyAddedFrames.Num() > 0)
	//{
	//	bUpdateData = true;
	//	return EActiveTimerReturnType::Continue;
	//}

	bIsActiveTimerRegistered = false;
	return EActiveTimerReturnType::Stop;
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////
/*
void SFrameTrack::MoveSelectionBox(int32 FrameIndex)
{
	const int32 SelectionBoxSize = SelectionBoxFrameEnd - SelectionBoxFrameStart;
	const int32 SelectionBoxHalfSize = SelectionBoxSize / 2;
	const int32 CenterFrameIndex = FMath::Clamp(FrameIndex - SelectionBoxHalfSize, 0, Frames.Num() - 1 - SelectionBoxSize);

	// Inform other widgets that we have moved the selection box.
	SelectionBoxFrameStart = CenterFrameIndex;
	SelectionBoxFrameEnd = CenterFrameIndex + SelectionBoxSize;
	SelectionBoxChangedEvent.Broadcast(SelectionBoxFrameStart, SelectionBoxFrameEnd);
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
