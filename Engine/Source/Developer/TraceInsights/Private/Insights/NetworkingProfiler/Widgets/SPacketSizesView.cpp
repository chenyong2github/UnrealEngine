// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SPacketSizesView.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SScrollBar.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SPacketSizesView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketSizesView::SPacketSizesView()
	: PacketSeries(MakeShareable(new FNetworkPacketSeries()))
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketSizesView::~SPacketSizesView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::Reset()
{
	Viewport.Reset();

	FValueAxisViewport& ViewportY = Viewport.GetVerticalAxisViewport();
	ViewportY.SetScaleLimits(0.0001, 50.0);
	ViewportY.SetScale(0.08);
	bIsViewportDirty = true;

	PacketSeries->Reset();

	bIsStateDirty = true;

	bIsAutoZoomEnabled = true;

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

	SelectedSample.Reset();
	HoveredSample.Reset();
	TooltipDesiredOpacity = 0.9f;
	TooltipOpacity = 0.0f;

	//ThisGeometry

	CursorType = ECursorType::Default;

	NumUpdatedPackets = 0;
	UpdateDurationHistory.Reset();
	DrawDurationHistory.Reset();
	OnPaintDurationHistory.Reset();
	LastOnPaintTime = FPlatformTime::Cycles64();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::Construct(const FArguments& InArgs)
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
			.OnUserScrolled(this, &SPacketSizesView::HorizontalScrollBar_OnUserScrolled)
		]
	];

	UpdateHorizontalScrollBar();

	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
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

	FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();

	if (!bIsScrolling)
	{
		// Elastic snap to horizontal limits.
		if (ViewportX.UpdatePosWithinLimits())
		{
			bIsStateDirty = true;
		}
	}

	uint64 Time = FPlatformTime::Cycles64();
	if (Time > AnalysisSyncNextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.1 / FPlatformTime::GetSecondsPerCycle64()); // 100ms
		AnalysisSyncNextTimestamp = Time + WaitTime;

		//TODO
	}

	if (bIsStateDirty)
	{
		bIsStateDirty = false;
		UpdateState();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::UpdateState()
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Reset stats.
	PacketSeries->NumAggregatedPackets = 0;
	NumUpdatedPackets = 0;

	// Init series with mock data.
	{
		FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();

		constexpr int32 NumPackets = 100000;

		ViewportX.SetMinMaxIndexInterval(0, NumPackets);

		const int32 StartIndex = FMath::Max(0, ViewportX.GetIndexAtOffset(0.0f));
		const int32 EndIndex = FMath::Min(NumPackets, ViewportX.GetIndexAtOffset(ViewportX.GetSize()));

		FNetworkPacketSeriesBuilder Builder(*PacketSeries, Viewport);
		for (int32 FrameIndex = StartIndex; FrameIndex < EndIndex; ++FrameIndex)
		{
			FRandomStream RandomStream((FrameIndex * FrameIndex * FrameIndex) ^ 0x2c2c57ed);
			int64 Size = RandomStream.RandRange(0, 2000);
			const float Fraction = RandomStream.GetFraction();
			ENetworkPacketStatus Status = ENetworkPacketStatus::ConfirmedReceived;
			if (Fraction < 0.01) // 1%
			{
				Status = ENetworkPacketStatus::ConfirmedLost;
			}
			else if (Fraction < 0.05) // 4%
			{
				Status = ENetworkPacketStatus::Sent;
			}

			double TimeSent = ((double)FrameIndex * 100.0) / (double)NumPackets + RandomStream.GetFraction() * 0.1;
			double TimeAck = 0.0;
			if (Status != ENetworkPacketStatus::Sent)
			{
				TimeAck = TimeSent + RandomStream.GetFraction() * 0.1;
			}

			Builder.AddPacket(FrameIndex, Size, TimeSent, TimeAck, Status);
		}
		NumUpdatedPackets += Builder.GetNumAddedPackets();
	}

	Stopwatch.Stop();
	UpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkPacketSampleRef SPacketSizesView::GetSampleAtMousePosition(float X, float Y)
{
	if (!bIsStateDirty)
	{
		float SampleW = Viewport.GetSampleWidth();
		int32 SampleIndex = FMath::FloorToInt(X / SampleW);
		if (SampleIndex >= 0)
		{
			if (PacketSeries->NumAggregatedPackets > 0 &&
				SampleIndex < PacketSeries->Samples.Num())
			{
				const FNetworkPacketAggregatedSample& Sample = PacketSeries->Samples[SampleIndex];
				if (Sample.NumPackets > 0)
				{
					const FValueAxisViewport& ViewportY = Viewport.GetVerticalAxisViewport();

					const float ViewHeight = FMath::RoundToFloat(Viewport.GetHeight());
					const float BaselineY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(0.0));
					const float ValueY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(static_cast<double>(Sample.LargestPacket.Size)));

					constexpr float ToleranceY = 3.0f; // [pixels]

					const float BottomY = FMath::Min(ViewHeight, ViewHeight - BaselineY + ToleranceY);
					const float TopY = FMath::Max(0.0f, ViewHeight - ValueY - ToleranceY);

					if (Y >= TopY && Y < BottomY)
					{
						return FNetworkPacketSampleRef(PacketSeries, MakeShareable(new FNetworkPacketAggregatedSample(Sample)));
					}
				}
			}
		}
	}
	return FNetworkPacketSampleRef(nullptr, nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::SelectSampleAtMousePosition(float X, float Y)
{
	FNetworkPacketSampleRef SampleRef = GetSampleAtMousePosition(X, Y);
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
		SelectedSample = SampleRef;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* StatusToString(ENetworkPacketStatus Status)
{
	switch (Status)
	{
		case ENetworkPacketStatus::ConfirmedReceived: return TEXT("Received");
		case ENetworkPacketStatus::Sent:              return TEXT("Sent");
		case ENetworkPacketStatus::ConfirmedLost:     return TEXT("Lost");
		case ENetworkPacketStatus::Unknown:
		default:                                      return TEXT("Unknown");
	};
}

const TCHAR* AggregatedStatusToString(ENetworkPacketStatus Status)
{
	switch (Status)
	{
		case ENetworkPacketStatus::ConfirmedReceived: return TEXT("all are Received");
		case ENetworkPacketStatus::Sent:              return TEXT("some are Sent, but have no Ack");
		case ENetworkPacketStatus::ConfirmedLost:     return TEXT("some are Lost");
		case ENetworkPacketStatus::Unknown:
		default:                                      return TEXT("Unknown");
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SPacketSizesView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	const float ViewWidth = AllottedGeometry.Size.X;
	const float ViewHeight = AllottedGeometry.Size.Y;

	int32 NumDrawSamples = 0;

	{
		FStopwatch Stopwatch;
		Stopwatch.Start();

		FPacketSizesViewDrawHelper Helper(DrawContext, Viewport);

		Helper.DrawBackground();

		DrawHorizontalAxisGrid(DrawContext, WhiteBrush, SummaryFont);

		Helper.DrawCached(*PacketSeries);

		NumDrawSamples = Helper.GetNumDrawSamples();

		// Highlight the selected and/or hovered sample.
		bool bIsSelectedAndHovered = SelectedSample.Equals(HoveredSample);
		if (SelectedSample.IsValid())
		{
			Helper.DrawSampleHighlight(*SelectedSample.Sample, bIsSelectedAndHovered ? FPacketSizesViewDrawHelper::EHighlightMode::SelectedAndHovered : FPacketSizesViewDrawHelper::EHighlightMode::Selected);
		}
		if (HoveredSample.IsValid() && !bIsSelectedAndHovered)
		{
			Helper.DrawSampleHighlight(*HoveredSample.Sample, FPacketSizesViewDrawHelper::EHighlightMode::Hovered);
		}

		DrawVerticalAxisGrid(DrawContext, WhiteBrush, SummaryFont);

		// Draw tooltip for hovered sample.
		if (HoveredSample.IsValid())
		{
			if (TooltipOpacity < TooltipDesiredOpacity)
			{
				TooltipOpacity = TooltipOpacity * 0.9f + TooltipDesiredOpacity * 0.1f;
			}
			else
			{
				TooltipOpacity = TooltipDesiredOpacity;
			}

			const double Precision = 0.01; // 10ms
			int NumLines;
			FString Text;
			if (HoveredSample.Sample->NumPackets == 1)
			{
				NumLines = 5;
				Text = FString::Format(TEXT("Frame Index: {0}\n"
											"Size: {1} bits\n"
											"Sent Timestamp: {2}\n"
											"Ack Timestamp: {3}\n"
											"Status: {4}"),
					{
						FText::AsNumber(HoveredSample.Sample->LargestPacket.FrameIndex).ToString(),
						FText::AsNumber(HoveredSample.Sample->LargestPacket.Size).ToString(),
						TimeUtils::FormatTimeHMS(HoveredSample.Sample->LargestPacket.TimeSent, Precision),
						HoveredSample.Sample->LargestPacket.TimeAck == 0 ? TEXT("N/A") : TimeUtils::FormatTimeHMS(HoveredSample.Sample->LargestPacket.TimeAck, Precision),
						::StatusToString(HoveredSample.Sample->LargestPacket.Status)
					});
			}
			else
			{
				NumLines = 8;
				Text = FString::Format(TEXT("{0} network packets\n"
											"({1})\n"
											"Largest Packet\n"
											"    Frame Index: {2}\n"
											"    Size: {3} bits\n"
											"    Sent Timestamp: {4}\n"
											"    Ack Timestamp: {5}\n"
											"    Status: {6}"),
					{
						HoveredSample.Sample->NumPackets,
						::AggregatedStatusToString(HoveredSample.Sample->AggregatedStatus),
						FText::AsNumber(HoveredSample.Sample->LargestPacket.FrameIndex).ToString(),
						FText::AsNumber(HoveredSample.Sample->LargestPacket.Size).ToString(),
						TimeUtils::FormatTimeHMS(HoveredSample.Sample->LargestPacket.TimeSent, Precision),
						HoveredSample.Sample->LargestPacket.TimeAck == 0 ? TEXT("N/A") : TimeUtils::FormatTimeHMS(HoveredSample.Sample->LargestPacket.TimeAck, Precision),
						::StatusToString(HoveredSample.Sample->LargestPacket.Status)
					});
			}

			FVector2D TextSize = FontMeasureService->Measure(Text, SummaryFont);

			const float DX = 2.0f;
			const float W2 = TextSize.X / 2 + DX;

			const FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();

			float X1 = ViewportX.GetOffsetForIndex(HoveredSample.Sample->LargestPacket.FrameIndex);
			float CX = X1 + FMath::RoundToFloat(Viewport.GetSampleWidth() / 2);
			if (CX + W2 > ViewportX.GetSize())
			{
				CX = FMath::RoundToFloat(ViewportX.GetSize() - W2);
			}
			if (CX - W2 < 0)
			{
				CX = W2;
			}

			const float Y = 10.0f;
			const float H = 2.0f + 13.0f * NumLines;
			DrawContext.DrawBox(CX - W2, Y, 2 * W2, H, WhiteBrush, FLinearColor(0.7, 0.7, 0.7, TooltipOpacity));
			DrawContext.LayerId++;
			DrawContext.DrawText(CX - W2 + DX, Y + 1.0f, Text, SummaryFont, FLinearColor(0.0, 0.0, 0.0, TooltipOpacity));
			DrawContext.LayerId++;
		}

		Stopwatch.Stop();
		DrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}

	//////////////////////////////////////////////////

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 280.0f;
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
			FString::Format(TEXT("U: {0} packets, D: {1} samples"),
				{
					FText::AsNumber(NumUpdatedPackets).ToString(),
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
			Viewport.GetHorizontalAxisViewport().ToDebugString(TEXT("VX: "), TEXT("packet")),
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
			Viewport.GetVerticalAxisViewport().ToDebugString(TEXT("VY: ")),
			SummaryFont,
			DrawEffects,
			DbgTextColor
		);
		DbgY += DbgDY;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::DrawVerticalAxisGrid(FDrawContext& DrawContext, const FSlateBrush* Brush, const FSlateFontInfo& Font) const
{
	const FValueAxisViewport& ViewportY = Viewport.GetVerticalAxisViewport();

	const float RoundedViewHeight = FMath::RoundToFloat(ViewportY.GetSize());

	constexpr float MinDY = 32.0f; // min vertical distance between horizontal grid lines

	const double TopValue = ViewportY.GetValueForOffset(RoundedViewHeight);
	const double GridValue = ViewportY.GetValueForOffset(MinDY);
	const double BottomValue = ViewportY.GetValueForOffset(0.0f);
	const double Delta = GridValue - BottomValue;

	if (Delta > 0.0)
	{
		const int64 DeltaBits = static_cast<int64>(Delta);
		// Compute rounding based on magnitude of visible range of values (Delta).
		int64 Power10 = 1;
		int64 Delta10 = DeltaBits;
		while (Delta10 > 0)
		{
			Delta10 /= 10;
			Power10 *= 10;
		}
		if (Power10 >= 100)
		{
			Power10 /= 100;
		}
		else
		{
			Power10 = 1;
		}

		const double Grid = static_cast<double>(((DeltaBits + Power10) / Power10) * Power10); // next value divisible with a multiple of 10

		const double StartValue = FMath::GridSnap(BottomValue, Grid);

		const FLinearColor GridColor(0.0f, 0.0f, 0.0f, 0.1f);
		const FLinearColor TextBgColor(0.05f, 0.05f, 0.05f, 1.0f);
		const FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		const float ViewWidth = Viewport.GetWidth();

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		for (double Value = StartValue; Value < TopValue; Value += Grid)
		{
			const float Y = RoundedViewHeight - FMath::RoundToFloat(ViewportY.GetOffsetForValue(Value));

			// Draw horizontal grid line.
			DrawContext.DrawBox(0, Y, ViewWidth, 1, Brush, GridColor);

			const int64 ValueBits = static_cast<int64>(Value);
			const FString Text = (ValueBits == 0) ? TEXT("0") : FString::Format(TEXT("{0} bits"), { FText::AsNumber(ValueBits).ToString() });
			const FVector2D TextSize = FontMeasureService->Measure(Text, Font);
			constexpr float TextH = 14.0f;

			// Draw background for value text.
			DrawContext.DrawBox(ViewWidth - TextSize.X - 4.0f, Y - TextH, TextSize.X + 4.0f, TextH, Brush, TextBgColor);

			// Draw value text.
			DrawContext.DrawText(ViewWidth - TextSize.X - 2.0f, Y - TextH + 1.0f, Text, Font, TextColor);
		}
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::DrawHorizontalAxisGrid(FDrawContext& DrawContext, const FSlateBrush* Brush, const FSlateFontInfo& Font) const
{
	const FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();

	const float RoundedViewWidth = FMath::RoundToFloat(ViewportX.GetSize());

	constexpr float MinDX = 100.0f; // min horizontal distance between vertical grid lines

	int32 LeftIndex = ViewportX.GetIndexAtOffset(0.0f);
	int32 GridIndex = ViewportX.GetIndexAtOffset(MinDX);
	int32 RightIndex = ViewportX.GetIndexAtOffset(RoundedViewWidth);
	int32 Delta = GridIndex - LeftIndex;

	if (Delta > 0)
	{
		// Compute rounding based on magnitude of visible range of samples (Delta).
		int32 Power10 = 1;
		int32 Delta10 = Delta;
		while (Delta10 > 0)
		{
			Delta10 /= 10;
			Power10 *= 10;
		}
		if (Power10 >= 100)
		{
			Power10 /= 100;
		}
		else
		{
			Power10 = 1;
		}

		const int32 Grid = ((Delta + Power10) / Power10) * Power10; // next value divisible with a multiple of 10

		const FLinearColor GridColor(0.0f, 0.0f, 0.0f, 0.1f);
		//const FLinearColor TextBgColor(0.05f, 0.05f, 0.05f, 1.0f);
		//const FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		const FLinearColor TopTextColor(1.0f, 1.0f, 1.0f, 0.7f);

		// Skip grid lines for negative indices.
		double StartIndex = ((LeftIndex + Grid - 1) / Grid) * Grid;
		while (StartIndex < 0)
		{
			StartIndex += Grid;
		}

		const float ViewHeight = Viewport.GetHeight();

		//const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		for (int32 Index = StartIndex; Index < RightIndex; Index += Grid)
		{
			const float X = FMath::RoundToFloat(ViewportX.GetOffsetForIndex(Index));

			// Draw vertical grid line.
			DrawContext.DrawBox(X, 0, 1, ViewHeight, Brush, GridColor);

			const FString Text = FText::AsNumber(Index).ToString();
			//const FVector2D TextSize = FontMeasureService->Measure(Text, Font);
			//constexpr float TextH = 14.0f;

			// Draw background for index text.
			//DrawContext.DrawBox(X, ViewHeight - TextH, TextSize.X + 4.0f, TextH, Brush, TextBgColor);

			// Draw index text.
			//DrawContext.DrawText(X + 2.0f, ViewHeight - TextH + 1.0f, Text, Font, TextColor);

			DrawContext.DrawText(X + 2.0f, 10.0f, Text, Font, TopTextColor);
		}
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketSizesView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	ViewportPosXOnButtonDown = Viewport.GetHorizontalAxisViewport().GetPos();
	ViewportPosYOnButtonDown = Viewport.GetVerticalAxisViewport().GetPos();

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

FReply SPacketSizesView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, MOUSE_SNAP_DISTANCE);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				SelectSampleAtMousePosition(MousePositionOnButtonUp.X, MousePositionOnButtonUp.Y);
			}

			bIsLMB_Pressed = false;

			// Release the mouse.
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				ShowContextMenu(MouseEvent);
			}

			bIsRMB_Pressed = false;

			// Release mouse as we no longer scroll.
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketSizesView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		if (HasMouseCapture() && !MouseEvent.GetCursorDelta().IsZero())
		{
			if (!bIsScrolling)
			{
				bIsScrolling = true;
				CursorType = ECursorType::Hand;

				HoveredSample.Reset();
			}

			FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();
			const float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
			ViewportX.ScrollAtIndex(ViewportX.GetIndexAtPos(PosX)); // align pos with at index
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
				CursorType = ECursorType::Hand;

				HoveredSample.Reset();
			}

			FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();
			const float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
			ViewportX.ScrollAtIndex(ViewportX.GetIndexAtPos(PosX)); // align pos with at index
			UpdateHorizontalScrollBar();
			bIsStateDirty = true;

			Reply = FReply::Handled();
		}
	}
	else
	{
		if (!HoveredSample.IsValid())
		{
			TooltipOpacity = 0.0f;
		}
		HoveredSample = GetSampleAtMousePosition(MousePosition.X, MousePosition.Y);
		if (!HoveredSample.IsValid())
		{
			HoveredSample = GetSampleAtMousePosition(MousePosition.X - 1.0f, MousePosition.Y);
		}
		if (!HoveredSample.IsValid())
		{
			HoveredSample = GetSampleAtMousePosition(MousePosition.X + 1.0f, MousePosition.Y);
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;

		HoveredSample.Reset();

		CursorType = ECursorType::Default;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketSizesView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetModifierKeys().IsShiftDown())
	{
		FValueAxisViewport& ViewportY = Viewport.GetVerticalAxisViewport();

		// Zoom in/out vertically.
		const float Delta = MouseEvent.GetWheelDelta();
		constexpr float ZoomStep = 0.25f; // as percent
		float ScaleY;

		if (Delta > 0)
		{
			ScaleY = ViewportY.GetScale() * FMath::Pow(1.0f + ZoomStep, Delta);
		}
		else
		{
			ScaleY = ViewportY.GetScale() * FMath::Pow(1.0f / (1.0f + ZoomStep), -Delta);
		}

		ViewportY.SetScale(ScaleY);
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

void SPacketSizesView::ZoomHorizontally(const float Delta, const float X)
{
	FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.RelativeZoomWithFixedOffset(Delta, X);
	ViewportX.ScrollAtIndex(ViewportX.GetIndexAtPos(ViewportX.GetPos())); // align pos with at index
	UpdateHorizontalScrollBar();
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketSizesView::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SPacketSizesView::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FCursorReply CursorReply = FCursorReply::Unhandled();

	if (CursorType == ECursorType::Arrow)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	else if (CursorType == ECursorType::Hand)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return CursorReply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::ShowContextMenu(const FPointerEvent& MouseEvent)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	MenuBuilder.BeginSection("Misc");
	{
		FUIAction Action_AutoZoom
		(
			FExecuteAction::CreateSP(this, &SPacketSizesView::ContextMenu_AutoZoom_Execute),
			FCanExecuteAction::CreateSP(this, &SPacketSizesView::ContextMenu_AutoZoom_CanExecute),
			FIsActionChecked::CreateSP(this, &SPacketSizesView::ContextMenu_AutoZoom_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_AutoZoom", "Auto Zoom"),
			LOCTEXT("ContextMenu_AutoZoom_Desc", "Enable auto zoom. Makes entire graph series to fit into view."),
			FSlateIcon(),
			Action_AutoZoom,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	TSharedRef<SWidget> MenuWidget = MenuBuilder.MakeWidget();

	FWidgetPath EventPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	FSlateApplication::Get().PushMenu(SharedThis(this), EventPath, MenuWidget, ScreenSpacePosition, FPopupTransitionEffect::ContextMenu);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::ContextMenu_AutoZoom_Execute()
{
	FIndexAxisViewport& ViewportX = Viewport.GetHorizontalAxisViewport();

	bIsAutoZoomEnabled = !(bIsAutoZoomEnabled && ViewportX.GetPos() == 0.0f);
	if (bIsAutoZoomEnabled)
	{
		ViewportX.ScrollAtPos(0.0f);

		// Auto zoom in.
		while (ViewportX.GetMaxPos() - ViewportX.GetMinPos() < ViewportX.GetSize())
		{
			ZoomHorizontally(+0.1f, 0.0f);
			ViewportX.ScrollAtPos(0.0f);
		}

		// Auto zoom out (until entire session time range fits into view).
		while (ViewportX.GetMaxPos() - ViewportX.GetMinPos() > ViewportX.GetSize())
		{
			ZoomHorizontally(-0.1f, 0.0f);
			ViewportX.ScrollAtPos(0.0f);
		}

		UpdateHorizontalScrollBar();
		bIsStateDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SPacketSizesView::ContextMenu_AutoZoom_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SPacketSizesView::ContextMenu_AutoZoom_IsChecked()
{
	return bIsAutoZoomEnabled && Viewport.GetHorizontalAxisViewport().GetPos() == 0.0f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::BindCommands()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	Viewport.GetHorizontalAxisViewport().OnUserScrolled(HorizontalScrollBar, ScrollOffset);
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketSizesView::UpdateHorizontalScrollBar()
{
	Viewport.GetHorizontalAxisViewport().UpdateScrollBar(HorizontalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
