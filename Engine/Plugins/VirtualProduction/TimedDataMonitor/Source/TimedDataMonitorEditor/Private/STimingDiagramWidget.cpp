// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimingDiagramWidget.h"

#include "Engine/Engine.h"
#include "Misc/App.h"
#include "TimedDataMonitorSubsystem.h"

#include "TimedDataMonitorEditorStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STimingDiagramWidget"


/* STimingDiagramWidgetGraphic
 *****************************************************************************/
class STimingDiagramWidgetGraphic : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(STimingDiagramWidgetGraphic)
		: _ShowFurther(false)
		, _ShowMean(true)
		, _ShowSigma(true)
		, _ShowSnapshot(true)
		, _UseNiceBrush(true)
		, _SizePerSeconds(100.f)
		{}
		SLATE_ARGUMENT(bool, ShowFurther)
		SLATE_ARGUMENT(bool, ShowMean)
		SLATE_ARGUMENT(bool, ShowSigma)
		SLATE_ARGUMENT(bool, ShowSnapshot)
		SLATE_ARGUMENT(bool, UseNiceBrush)
		SLATE_ATTRIBUTE(float, SizePerSeconds)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		bShowFurther = InArgs._ShowFurther;
		bShowMean = InArgs._ShowMean;
		bShowSigma = InArgs._ShowSigma;
		bShowSnapshot = InArgs._ShowSnapshot;
		SizePerSecondsAttibute = InArgs._SizePerSeconds;

		if (InArgs._UseNiceBrush)
		{
			DarkBrush = &FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox").BackgroundImageFocused;
			BrightBrush = &FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox").BackgroundImageNormal;
		}
		else
		{
			DarkBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
			BrightBrush = DarkBrush;
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		LayerId++;

		const int32 SizeY = AllottedGeometry.GetLocalSize().Y;
		const float SizeX = AllottedGeometry.GetLocalSize().X;

		const float SizeOfFurthur = bShowFurther ? 10.f : 0.f;
		const float LocationOfCenter = FMath::Max((SizeX - SizeOfFurthur - SizeOfFurthur) * 0.5f, 0.f);
		const float SizePerSeconds = SizePerSecondsAttibute.Get();
		const float SizeOfBoxY = (bShowMean && bShowSnapshot) ? SizeY/2.f : SizeY;
		const float LocationOfMeanBoxY = (bShowMean && bShowSnapshot) ? SizeY/2.f : 0.f;

		const float SnapshotLocationMinX = LocationOfCenter - ((EvaluationTime - MinSampleTime) * SizePerSeconds);
		const float SnapshotLocationMaxX = LocationOfCenter + (MaxSampleTime - EvaluationTime) * SizePerSeconds;
		const float MeanLocationMinX = LocationOfCenter - (OldestMean * SizePerSeconds);
		const float MeanLocationMaxX = LocationOfCenter + (NewestMean * SizePerSeconds);

		// square to show that the data goes further
		if (bShowFurther)
		{
			if (bShowSnapshot && SnapshotLocationMinX < SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(0, 0), FVector2D(SizeOfFurthur, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
			}
			if (bShowMean && MeanLocationMinX < SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(0, LocationOfMeanBoxY), FVector2D(SizeOfFurthur, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
			}
		}

		// data in relation with evaluation time
		if (bShowSnapshot)
		{
			const float DrawLocationX = FMath::Clamp(SnapshotLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
			const float DrawSizeX = FMath::Clamp(SnapshotLocationMaxX - SnapshotLocationMinX, 0.f, SizeX - SizeOfFurthur);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, 0), FVector2D(DrawSizeX, SizeOfBoxY)),
				DarkBrush, ESlateDrawEffect::None,
				BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Green);
		}

		if (bShowMean)
		{
			const float DrawLocationX = FMath::Clamp(MeanLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
			const float DrawSizeX = FMath::Clamp(MeanLocationMaxX - MeanLocationMinX, 0.f, SizeX - SizeOfFurthur);
			if (bShowMean)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, LocationOfMeanBoxY), FVector2D(DrawSizeX, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Blue);
			}
		}

		// square to show that the data goes further
		if (bShowFurther)
		{
			if (bShowSnapshot && SnapshotLocationMaxX > SizeX-SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(SizeX - SizeOfFurthur, 0), FVector2D(SizeOfFurthur, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
			}
			if (bShowMean && MeanLocationMaxX > SizeX - SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(SizeX - SizeOfFurthur, LocationOfMeanBoxY), FVector2D(SizeOfFurthur, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
			}
		}

		// show the sigma
		if (bShowSigma)
		{
			int32 NumberOfSigma = 3;
			{
				float SigmaLocationMinX = MeanLocationMinX - (OldestSigma * SizePerSeconds * NumberOfSigma);
				float SigmaLocationMaxX = MeanLocationMinX + (OldestSigma * SizePerSeconds * NumberOfSigma);
				const float DrawLocationX = FMath::Clamp(SigmaLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
				const float DrawSizeX = FMath::Clamp(SigmaLocationMaxX - SigmaLocationMinX, 0.f, SizeX - SizeOfFurthur);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, SizeY/2.f), FVector2D(DrawSizeX, 1)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::White);
			}
			{
				float SigmaLocationMinX = MeanLocationMaxX - (NewestSigma * SizePerSeconds * NumberOfSigma);
				float SigmaLocationMaxX = MeanLocationMaxX + (NewestSigma * SizePerSeconds * NumberOfSigma);
				const float DrawLocationX = FMath::Clamp(SigmaLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
				const float DrawSizeX = FMath::Clamp(SigmaLocationMaxX - SigmaLocationMinX, 0.f, SizeX - SizeOfFurthur);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, SizeY / 2.f), FVector2D(DrawSizeX, 1)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::White);
			}
		}

		// Draw red line
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(LocationOfCenter, 0), FVector2D(1, SizeY)),
			DarkBrush, ESlateDrawEffect::None,
			DarkBrush->GetTint(InWidgetStyle) * FLinearColor::Red);

		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(100, 20);
	}

	const FSlateBrush* DarkBrush;
	const FSlateBrush* BrightBrush;

	bool bShowFurther = false;
	bool bShowMean = true;
	bool bShowSigma = true;
	bool bShowSnapshot = true;
	TAttribute<float> SizePerSecondsAttibute;

	double EvaluationTime = 0.0;
	double MinSampleTime = 0.0;
	double MaxSampleTime = 0.0;
	double OldestMean = 0.0;
	double NewestMean = 0.0;
	double OldestSigma = 0.0;
	double NewestSigma = 0.0;
	ETimedDataInputEvaluationType EvaluationType;
};


/* STimingDiagramWidget
 *****************************************************************************/
void STimingDiagramWidget::Construct(const FArguments& InArgs, bool bInIsInput)
{
	bIsInput = bInIsInput;
	ChannelIdentifier = InArgs._ChannelIdentifier;
	InputIdentifier = InArgs._InputIdentifier;

	if (!bIsInput && !InputIdentifier.IsValid())
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);
		InputIdentifier = TimedDataMonitorSubsystem->GetChannelInput(ChannelIdentifier);
	}

	GraphicWidget = SNew(STimingDiagramWidgetGraphic)
		.ShowFurther(InArgs._ShowFurther)
		.ShowMean(InArgs._ShowMean)
		.ShowSigma(InArgs._ShowSigma)
		.ShowMean(InArgs._ShowSnapshot)
		.UseNiceBrush(InArgs._UseNiceBrush)
		.SizePerSeconds(InArgs._SizePerSeconds);

	UpdateCachedValue();
	
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			GraphicWidget.ToSharedRef()
		]
	];
	
	SetToolTip(SNew(SToolTip).Text(this, &STimingDiagramWidget::GetTooltipText));
}


void STimingDiagramWidget::UpdateCachedValue()
{
	auto GetSeconds = [](ETimedDataInputEvaluationType Evaluation, const FTimedDataChannelSampleTime & SampleTime) -> double
	{
		return Evaluation == ETimedDataInputEvaluationType::Timecode ? SampleTime.Timecode.AsSeconds() : SampleTime.PlatformSecond;
	};

	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	GraphicWidget->EvaluationType = TimedDataMonitorSubsystem->GetInputEvaluationType(InputIdentifier);

	double EvaluationOffset = TimedDataMonitorSubsystem->GetInputEvaluationOffsetInSeconds(InputIdentifier);
	double MinSampleTime = 0.0;
	double MaxSampleTime = 0.0;
	double NewestMean = 0.0;
	if (bIsInput)
	{
		MinSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetInputOldestDataTime(InputIdentifier));
		MaxSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetInputNewestDataTime(InputIdentifier));
	}
	else
	{
		MinSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetChannelOldestDataTime(ChannelIdentifier));
		MaxSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetChannelNewestDataTime(ChannelIdentifier));
		GraphicWidget->OldestMean = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToOldestSampleMean(ChannelIdentifier);
		GraphicWidget->NewestMean = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToNewestSampleMean(ChannelIdentifier);
		GraphicWidget->OldestSigma = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToOldestSampleStandardDeviation(ChannelIdentifier);
		GraphicWidget->NewestSigma = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToNewestSampleStandardDeviation(ChannelIdentifier);
	}
	GraphicWidget->MinSampleTime = MinSampleTime + EvaluationOffset;
	GraphicWidget->MaxSampleTime = MaxSampleTime + EvaluationOffset;

	switch (GraphicWidget->EvaluationType)
	{
	case ETimedDataInputEvaluationType::Timecode:
		if (FApp::GetCurrentFrameTime().IsSet())
		{
			GraphicWidget->EvaluationTime = FApp::GetCurrentFrameTime().GetValue().AsSeconds();
		}
		break;
	case ETimedDataInputEvaluationType::PlatformTime:
		GraphicWidget->EvaluationTime = FApp::GetCurrentTime();
		break;
	case ETimedDataInputEvaluationType::None:
	default:
		GraphicWidget->EvaluationTime = 0.0;
		break;
	}
}


FText STimingDiagramWidget::GetTooltipText() const
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);
	
	const float DistanceToNewestSampleAverage = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToNewestSampleMean(ChannelIdentifier);
	const float DistanceToOldestSampleAverage = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToOldestSampleMean(ChannelIdentifier);
	const float DistanceToNewestSampleSigma = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToNewestSampleStandardDeviation(ChannelIdentifier);
	const float DistanceToOldestSampleSigma = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToOldestSampleStandardDeviation(ChannelIdentifier);
	
	return FText::Format(LOCTEXT("TimingDiagramTooltip", "Distance to newest:\nMean: {0}\nSigma: {1}\n\nDistance to oldest:\nMean: {2}\nSigma: {3}")
		, DistanceToNewestSampleAverage
		, DistanceToNewestSampleSigma
		, DistanceToOldestSampleAverage
		, DistanceToOldestSampleSigma);
}

#undef LOCTEXT_NAMESPACE