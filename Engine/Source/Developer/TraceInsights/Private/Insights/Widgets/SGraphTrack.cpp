// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SGraphTrack.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Containers/ArrayBuilder.h"
#include "Containers/MapBuilder.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/TimingProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SGraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

SGraphTrack::SGraphTrack()
	: TimeRulerTrack(0)
	, GraphTrack(1)
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SGraphTrack::~SGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::Reset()
{
	TimeRulerTrack.Reset();
	GraphTrack.Reset();
	Viewport.ScaleX = (5 * 20) / 0.1; // 100ms between major tick marks
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		//+SOverlay::Slot()
		//.HAlign(HAlign_Left)
		//.VAlign(VAlign_Top)
		//.Padding(FMargin(48.0f, 16.0f, 48.0f, 16.0f))	// Make some space for graph labels
		//[
		//	SAssignNew(GraphDescriptionsVBox, SVerticalBox)
		//]
	];

	//BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	//ThisGeometry = AllottedGeometry;

	float TrackWidth = AllottedGeometry.GetAbsoluteSize().X;
	float TrackHeight = AllottedGeometry.GetAbsoluteSize().Y;

	bool bIsGraphDirty = false;

	if (Viewport.UpdateSize(TrackWidth, TrackHeight))
	{
		bIsGraphDirty = true;
	}

	if (GraphTrack.GetHeight() != TrackHeight)
	{
		bIsGraphDirty = true;
	}

	if (bIsGraphDirty)
	{
		bIsGraphDirty = false;

		GraphTrack.SetPosY(TimeRulerTrack.GetHeight());
		GraphTrack.SetHeight(Viewport.Height - TimeRulerTrack.GetHeight());
		GraphTrack.Update(Viewport);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

//PRAGMA_DISABLE_OPTIMIZATION
int32 SGraphTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DC(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	GraphTrack.Draw(DC, Viewport);
	TimeRulerTrack.Draw(DC, Viewport);

	/*
	static double TotalTime = 0.0f;
	static uint32 NumCalls = 0;
	const double StartTime = FPlatformTime::Seconds();

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Rendering info.
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	const float Width = AllottedGeometry.GetLocalSize().X;
	const float Height = AllottedGeometry.GetLocalSize().Y;

	// Draw background.
	const FSlateBrush* AreaBrush = FEditorStyle::GetBrush("Profiler.LineGraphArea");
	FSlateDrawElement::MakeBox
	(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2D(0,0), FVector2D(Width, Height)),
		AreaBrush,
		DrawEffects,
		AreaBrush->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
	);
	LayerId++;

	// Draw a test graph...
	static const int EventCount = 1000;
	const FLinearColor LineColor(0.0f, 0.5f, 1.0f, 1.0f);
	TArray<FVector2D> LinePoints;
	FVector2D V1(0.0f, 0.0f);
	FVector2D V2;
	FRandomStream RandomStream(0);
	for (int Index = 0; Index < EventCount; ++Index)
	{
		V2.X = FMath::TruncToFloat((Width / (float)EventCount) * (float)Index);
		V2.Y = Height * RandomStream.GetFraction();
		if ((V2 - V1).Size() > 1.0)
		{
			LinePoints.Add(V1);
			LinePoints.Add(V2);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, DrawEffects, LineColor);
			LinePoints.Empty();
			V1 = V2;
		}
	}
	*/

	//double sec = FPlatformTime::GetSecondsPerCycle();

	//FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	//const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;

	/*
	// Debug draw (slate fps)
	++LayerId;
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2D(8.0f, 8.0f), FVector2D(200.0, MaxFontCharHeight + 4.0)),
		EventBrush,
		DrawEffects,
		FLinearColor(FColor(255, 255, 255))
	);
	static uint64 Time = 0;
	const uint64 CurrentTime = FPlatformTime::Cycles64();
	const uint64 Duration = CurrentTime - Time;
	Time = CurrentTime;
	static const int DurationCount = 64;
	static uint64 Durations[DurationCount];
	static uint64 DurationOffset = 0;
	Durations[(DurationOffset++)% DurationCount] = Duration;
	uint64 AvgDuration = 0;
	for (int I = 0; I < DurationCount; ++I)
		AvgDuration += Durations[I];
	AvgDuration = AvgDuration / DurationCount;
	const double AvgFps = AvgDuration > 0 ? 1.0 / (FPlatformTime::GetSecondsPerCycle64() * (double)AvgDuration) : 0.0;
	const FString DebnugStatsStr = FString::Printf(TEXT("%d ms (%d ms) %.1f fps %d events"),
		FMath::TruncToInt(AvgDuration * 1000 * FPlatformTime::GetSecondsPerCycle64()),
		FMath::TruncToInt(Duration * 1000 * FPlatformTime::GetSecondsPerCycle64()),
		AvgFps,
		EventCount);
	FSlateDrawElement::MakeText
	(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToOffsetPaintGeometry(FVector2D(10.0f, 10.0f)),
		DebnugStatsStr,
		SummaryFont,
		DrawEffects,
		FLinearColor::Red
	);
	*/

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}
//PRAGMA_ENABLE_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);

	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragLeave(DragDropEvent);

	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//...

	return SCompoundWidget::OnDragOver(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//...

	return SCompoundWidget::OnDrop(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SGraphTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	//...

	return FCursorReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
