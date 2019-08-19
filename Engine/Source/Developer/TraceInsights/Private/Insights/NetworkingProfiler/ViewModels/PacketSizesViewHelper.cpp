// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PacketSizesViewHelper.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/ViewModels/PacketSizesViewport.h"
#include "Insights/ViewModels/DrawHelpers.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////
// FNetworkPacketSeriesBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkPacketSeriesBuilder::FNetworkPacketSeriesBuilder(FNetworkPacketSeries& InSeries, const FPacketSizesViewport& InViewport)
	: Series(InSeries)
	, Viewport(InViewport)
	, NumAddedPackets(0)
{
	SampleW = Viewport.GetSampleWidth();
	PacketsPerSample = Viewport.GetNumPacketsPerSample();
	NumSamples = FMath::Max(0, FMath::CeilToInt(Viewport.GetWidth() / SampleW));
	FirstFrameIndex = Viewport.GetFirstFrameIndex();

	Series.NumAggregatedPackets = 0;
	Series.Samples.Reset();
	Series.Samples.AddDefaulted(NumSamples);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkPacketSeriesBuilder::AddPacket(int32 FrameIndex, int64 Size, double TimeSent, double TimeAck, ENetworkPacketStatus Status)
{
	NumAddedPackets++;

	int32 SampleIndex = (FrameIndex - FirstFrameIndex) / PacketsPerSample;
	if (SampleIndex >= 0 && SampleIndex < NumSamples)
	{
		FNetworkPacketAggregatedSample& Sample = Series.Samples[SampleIndex];
		Sample.NumPackets++;

		if (TimeSent < Sample.StartTime)
		{
			Sample.StartTime = TimeSent;
		}
		if (TimeSent > Sample.EndTime)
		{
			Sample.EndTime = TimeSent;
		}

		if (Size > Sample.LargestPacket.Size)
		{
			Sample.LargestPacket.FrameIndex = FrameIndex;
			Sample.LargestPacket.Size = Size;
			Sample.LargestPacket.TimeSent = TimeSent;
			Sample.LargestPacket.TimeAck = TimeAck;
			Sample.LargestPacket.Status = Status;
		}

		ensure(Status != ENetworkPacketStatus::Unknown);
		if (static_cast<int32>(Status) > static_cast<int32>(Sample.AggregatedStatus))
		{
			Sample.AggregatedStatus = Status;
		}

		Series.NumAggregatedPackets++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPacketSizesViewDrawHelper
////////////////////////////////////////////////////////////////////////////////////////////////////

FPacketSizesViewDrawHelper::FPacketSizesViewDrawHelper(const FDrawContext& InDrawContext, const FPacketSizesViewport& InViewport)
	: DrawContext(InDrawContext)
	, Viewport(InViewport)
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	//, EventBorderBrush(FInsightsStyle::Get().GetBrush("EventBorder"))
	, HoveredEventBorderBrush(FInsightsStyle::Get().GetBrush("HoveredEventBorder"))
	, SelectedEventBorderBrush(FInsightsStyle::Get().GetBrush("SelectedEventBorder"))
	, NumPackets(0)
	, NumDrawSamples(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketSizesViewDrawHelper::DrawBackground() const
{
	const FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();

	const float X0 = 0.0f;
	const float X1 = ViewportX.GetMinPos() - ViewportX.GetPos();
	const float X2 = ViewportX.GetMaxPos() - ViewportX.GetPos();
	const float X3 = FMath::CeilToFloat(Viewport.GetWidth());

	const float Y = 0.0f;
	const float H = FMath::CeilToFloat(Viewport.GetHeight());

	FDrawHelpers::DrawBackground(DrawContext, WhiteBrush, X0, X1, X2, X3, Y, H);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FPacketSizesViewDrawHelper::GetColorByStatus(ENetworkPacketStatus Status)
{
	const float Alpha = 1.0f;
	switch (Status)
	{
	case ENetworkPacketStatus::Unknown:
		return FLinearColor(0.25f, 0.25f, 0.25f, Alpha);

	case ENetworkPacketStatus::Sent:
		return FLinearColor(0.75f, 0.75f, 0.75f, Alpha);

	case ENetworkPacketStatus::ConfirmedReceived:
		return FLinearColor(0.5f, 1.0f, 0.5f, Alpha);

	case ENetworkPacketStatus::ConfirmedLost:
		return FLinearColor(1.0f, 0.5f, 0.5f, Alpha);

	default:
		return FLinearColor(1.0f, 0.0f, 0.0f, Alpha);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketSizesViewDrawHelper::DrawCached(const FNetworkPacketSeries& Series) const
{
	if (Series.NumAggregatedPackets == 0)
	{
		return;
	}

	NumPackets += Series.NumAggregatedPackets;

	const float SampleW = Viewport.GetSampleWidth();
	const int32 NumSamples = Series.Samples.Num();

	const FAxisViewportDouble& ViewportY = Viewport.GetVerticalAxisViewport();

	const float ViewHeight = FMath::RoundToFloat(Viewport.GetHeight());
	const float BaselineY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(0.0));

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FNetworkPacketAggregatedSample& Sample = Series.Samples[SampleIndex];
		if (Sample.NumPackets == 0)
		{
			continue;
		}

		NumDrawSamples++;

		const float X = SampleIndex * SampleW;

		const float ValueY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(static_cast<double>(Sample.LargestPacket.Size)));

		const float H = ValueY - BaselineY;
		const float Y = ViewHeight - H;

		const FLinearColor ColorFill = GetColorByStatus(Sample.AggregatedStatus);
		const FLinearColor ColorBorder(ColorFill.R * 0.75f, ColorFill.G * 0.75f, ColorFill.B * 0.75f, 1.0);

		if (SampleW > 2.0f)
		{
			DrawContext.DrawBox(X + 1.0f, Y + 1.0f, SampleW - 2.0f, H - 2.0f, WhiteBrush, ColorFill);

			// Draw border.
			DrawContext.DrawBox(X, Y, 1.0, H, WhiteBrush, ColorBorder);
			DrawContext.DrawBox(X + SampleW - 1.0f, Y, 1.0, H, WhiteBrush, ColorBorder);
			DrawContext.DrawBox(X + 1.0f, Y, SampleW - 2.0f, 1.0f, WhiteBrush, ColorBorder);
			DrawContext.DrawBox(X + 1.0f, Y + H - 1.0f, SampleW - 2.0f, 1.0f, WhiteBrush, ColorBorder);
		}
		else
		{
			DrawContext.DrawBox(X, Y, SampleW, H, WhiteBrush, ColorBorder);
		}
	}

	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketSizesViewDrawHelper::DrawSampleHighlight(const FNetworkPacketAggregatedSample& Sample, EHighlightMode Mode) const
{
	const float SampleW = Viewport.GetSampleWidth();
	const int32 PacketsPerSample = Viewport.GetNumPacketsPerSample();
	const int32 FirstFrameIndex = Viewport.GetFirstFrameIndex();
	const int32 SampleIndex = (Sample.LargestPacket.FrameIndex - FirstFrameIndex) / PacketsPerSample;
	const float X = SampleIndex * SampleW;

	const FAxisViewportDouble& ViewportY = Viewport.GetVerticalAxisViewport();

	const float ViewHeight = FMath::RoundToFloat(Viewport.GetHeight());
	const float BaselineY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(0.0));
	const float ValueY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(static_cast<double>(Sample.LargestPacket.Size)));

	const float H = ValueY - BaselineY;
	const float Y = ViewHeight - H;

	if (Mode == EHighlightMode::Hovered)
	{
		const FLinearColor Color(1.0f, 1.0f, 0.0f, 1.0f); // yellow

		// Draw border around the timing event box.
		DrawContext.DrawBox(X - 1.0f, Y - 1.0f, SampleW + 2.0f, H + 2.0f, HoveredEventBorderBrush, Color);
	}
	else // EHighlightMode::Selected or EHighlightMode::SelectedAndHovered
	{
		// Animate color from white (if selected and hovered) or yellow (if only selected) to black, using a squared sine function.
		const double Time = static_cast<double>(FPlatformTime::Cycles64()) * FPlatformTime::GetSecondsPerCycle64();
		float S = FMath::Sin(2.0 * Time);
		S = S * S; // squared, to ensure only positive [0 - 1] values
		const float Blue = (Mode == EHighlightMode::SelectedAndHovered) ? 0.0f : S;
		const FLinearColor Color(S, S, Blue, 1.0f);

		// Draw border around the timing event box.
		DrawContext.DrawBox(X - 1.0f, Y - 1.0f, SampleW + 2.0f, H + 2.0f, SelectedEventBorderBrush, Color);
	}
	DrawContext.LayerId++;
}



////////////////////////////////////////////////////////////////////////////////////////////////////
