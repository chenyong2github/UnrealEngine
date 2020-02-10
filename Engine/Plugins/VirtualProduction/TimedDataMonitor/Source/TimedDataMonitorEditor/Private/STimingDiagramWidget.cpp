// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimingDiagramWidget.h"

#include "Engine/Engine.h"
#include "Misc/App.h"
#include "TimedDataMonitorEditorSettings.h"
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

		NumberOfSigma = GetDefault<UTimedDataMonitorEditorSettings>()->NumberOfSampleStandardDeviation;

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

		const float SizeX = AllottedGeometry.GetLocalSize().X;
		const float SizeY = AllottedGeometry.GetLocalSize().Y;

		const float SizeOfFurthur = bShowFurther ? 20.f : 0.f;
		const float LocationOfCenter = FMath::Max((SizeX - SizeOfFurthur - SizeOfFurthur) * 0.5f, 0.f);
		const float SizePerSeconds = SizePerSecondsAttibute.Get();
		const float SizeOfBoxY = (bShowMean && bShowSnapshot) ? SizeY / 2.f : SizeY - 2.f;
		const float LocationOfSnapshotBoxY = (bShowMean && bShowSnapshot) ? 0.f : 2.f;
		const float LocationOfMeanBoxY = (bShowMean && bShowSnapshot) ? SizeY / 2.f : 2.f;

		const float SnapshotLocationMinX = LocationOfCenter - ((EvaluationTime - MinSampleTime) * SizePerSeconds);
		const float SnapshotLocationMaxX = LocationOfCenter + ((MaxSampleTime - EvaluationTime) * SizePerSeconds);
		const float MeanLocationMinX = LocationOfCenter - (OldestMean * SizePerSeconds);
		const float MeanLocationMaxX = LocationOfCenter + (NewestMean * SizePerSeconds);

		// square to show that the data goes further
		if (bShowFurther)
		{
			if (bShowSnapshot && SnapshotLocationMinX < SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(0, LocationOfSnapshotBoxY), FVector2D(SizeOfFurthur, SizeOfBoxY)),
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
			const float DrawSizeX = FMath::Clamp(SnapshotLocationMaxX - DrawLocationX, 1.f, SizeX - SizeOfFurthur);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, LocationOfSnapshotBoxY), FVector2D(DrawSizeX, SizeOfBoxY)),
				DarkBrush, ESlateDrawEffect::None,
				BrightBrush->GetTint(InWidgetStyle) * FLinearColor(0.5f, 0.5f, 0.5f));
		}

		if (bShowMean)
		{
			const float DrawLocationX = FMath::Clamp(MeanLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
			const float DrawSizeX = FMath::Clamp(MeanLocationMaxX - DrawLocationX, 1.f, SizeX - SizeOfFurthur);
			if (bShowMean)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, LocationOfMeanBoxY), FVector2D(DrawSizeX, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor(0.2f, 0.2f, 0.2f));
			}
		}

		// show the sigma
		if (bShowSigma)
		{
			const float SizeOfSigmaY = 4.f;
			const float LocationOfSigmaY = (SizeY / 2.f) - 2.f;
			{
				const float SigmaLocationMinX = MeanLocationMinX - (OldestSigma * SizePerSeconds * NumberOfSigma);
				const float SigmaLocationMaxX = MeanLocationMinX + (OldestSigma * SizePerSeconds * NumberOfSigma);
				if (SigmaLocationMaxX > SizeOfFurthur)
				{
					const float DrawLocationX = FMath::Clamp(SigmaLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
					const float DrawSizeX = FMath::Clamp(SigmaLocationMaxX - DrawLocationX, 0.f, SizeX - SizeOfFurthur);
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
						AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, LocationOfSigmaY), FVector2D(DrawSizeX, SizeOfSigmaY)),
						DarkBrush, ESlateDrawEffect::None,
						BrightBrush->GetTint(InWidgetStyle) * FLinearColor::White);
				}
			}
			{
				const float SigmaLocationMinX = MeanLocationMaxX - (NewestSigma * SizePerSeconds * NumberOfSigma);
				const float SigmaLocationMaxX = MeanLocationMaxX + (NewestSigma * SizePerSeconds * NumberOfSigma);
				if (SigmaLocationMinX < SizeX - SizeOfFurthur)
				{
					const float DrawLocationX = FMath::Clamp(SigmaLocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
					const float DrawSizeX = FMath::Clamp(SigmaLocationMaxX - DrawLocationX, 0.f, SizeX - SizeOfFurthur);
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
						AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, LocationOfSigmaY), FVector2D(DrawSizeX, SizeOfSigmaY)),
						DarkBrush, ESlateDrawEffect::None,
						BrightBrush->GetTint(InWidgetStyle) * FLinearColor::White);
				}
			}
		}

		// square to show that the data goes further
		if (bShowFurther)
		{
			if (bShowSnapshot && SnapshotLocationMaxX > SizeX - SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(SizeX - SizeOfFurthur, LocationOfSnapshotBoxY), FVector2D(SizeOfFurthur, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
			}

			if (bShowMean && MeanLocationMaxX > SizeX - SizeOfFurthur)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(SizeX - SizeOfFurthur, LocationOfSnapshotBoxY), FVector2D(SizeOfFurthur, SizeOfBoxY)),
					DarkBrush, ESlateDrawEffect::None,
					BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
			}
		}

		// Draw evaluation line
		{
			FLinearColor EvaluationColor = (EvaluationTime >= MinSampleTime && EvaluationTime<= MaxSampleTime) ? FLinearColor::Green : FLinearColor::Red;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(LocationOfCenter, 0), FVector2D(1, SizeY)),
				DarkBrush, ESlateDrawEffect::None,
				DarkBrush->GetTint(InWidgetStyle) * EvaluationColor);
		}

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
	int32 NumberOfSigma = 3;
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
		GraphicWidget->OldestMean = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToOldestSampleMean(InputIdentifier);
		GraphicWidget->NewestMean = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToNewestSampleMean(InputIdentifier);
		GraphicWidget->OldestSigma = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToOldestSampleStandardDeviation(InputIdentifier);
		GraphicWidget->NewestSigma = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToNewestSampleStandardDeviation(InputIdentifier);
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
	
	float DistanceToNewestSampleAverage, DistanceToOldestSampleAverage, DistanceToNewestSampleSigma, DistanceToOldestSampleSigma = 0.f;
	if (bIsInput)
	{
		DistanceToNewestSampleAverage = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToNewestSampleMean(InputIdentifier);
		DistanceToOldestSampleAverage = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToOldestSampleMean(InputIdentifier);
		DistanceToNewestSampleSigma = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToNewestSampleStandardDeviation(InputIdentifier);
		DistanceToOldestSampleSigma = TimedDataMonitorSubsystem->GetInputEvaluationDistanceToOldestSampleStandardDeviation(InputIdentifier);
	}
	else
	{
		DistanceToNewestSampleAverage = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToNewestSampleMean(ChannelIdentifier);
		DistanceToOldestSampleAverage = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToOldestSampleMean(ChannelIdentifier);
		DistanceToNewestSampleSigma = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToNewestSampleStandardDeviation(ChannelIdentifier);
		DistanceToOldestSampleSigma = TimedDataMonitorSubsystem->GetChannelEvaluationDistanceToOldestSampleStandardDeviation(ChannelIdentifier);
	}

	
	return FText::Format(LOCTEXT("TimingDiagramTooltip", "Distance to newest:\nMean: {0}\nSigma: {1}\n\nDistance to oldest:\nMean: {2}\nSigma: {3}")
		, DistanceToNewestSampleAverage
		, DistanceToNewestSampleSigma
		, DistanceToOldestSampleAverage
		, DistanceToOldestSampleSigma);
}

#undef LOCTEXT_NAMESPACE