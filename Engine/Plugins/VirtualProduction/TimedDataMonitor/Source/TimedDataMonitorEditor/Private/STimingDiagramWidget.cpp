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
	SLATE_BEGIN_ARGS(STimingDiagramWidgetGraphic) {}
		SLATE_ARGUMENT(bool, ShowFurther)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		bShowFurther = InArgs._ShowFurther;

		DarkBrush = &FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox").BackgroundImageFocused;
		BrightBrush = &FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox").BackgroundImageNormal;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		LayerId++;

		const int32 SizeY = AllottedGeometry.GetLocalSize().Y;
		const float SizeX = AllottedGeometry.GetLocalSize().X;

		const float SizeOfFurthur = bShowFurther ? 10.f : 0.f;
		const float LocationOfCenter = FMath::Max((SizeX - SizeOfFurthur - SizeOfFurthur) * 0.5f, 0.f);
		const float SizePerSeconds = 100.f;

		const float LocationMinX = LocationOfCenter - ((EvaluationTime - MinSampleTime) * SizePerSeconds);
		const float LocationMaxX = LocationOfCenter + (MaxSampleTime - EvaluationTime) * SizePerSeconds;

		// square to show that the data goes further
		if (bShowFurther && LocationMinX < SizeOfFurthur)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(0, 0), FVector2D(SizeOfFurthur, SizeY)),
				DarkBrush, ESlateDrawEffect::None,
				BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
		}

		// data in relation with evaluation time
		{
			const float DrawLocationX = FMath::Clamp(LocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
			const float DrawSizeX = FMath::Clamp(LocationMaxX - LocationMinX, SizeOfFurthur, SizeX - SizeOfFurthur);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(DrawLocationX, 0), FVector2D(DrawSizeX, SizeY)),
				DarkBrush, ESlateDrawEffect::None,
				BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Green);
		}

		// square to show that the data goes further
		if (bShowFurther && LocationMaxX > SizeX-SizeOfFurthur)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(SizeX- SizeOfFurthur, 0), FVector2D(SizeOfFurthur, SizeY)),
				DarkBrush, ESlateDrawEffect::None,
				BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Yellow);
		}

		// Draw red line
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(LocationOfCenter, 0), FVector2D(1, SizeY)),
			DarkBrush, ESlateDrawEffect::None,
			BrightBrush->GetTint(InWidgetStyle) * FLinearColor::Red);

		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(100, 10);
	}

	void UpdateCachedValue()
	{
		EvaluationTime = 0.0;
		switch(EvaluationType)
		{
		case ETimedDataInputEvaluationType::Timecode:
			if (FApp::GetCurrentFrameTime().IsSet())
			{
				EvaluationTime = FApp::GetCurrentFrameTime().GetValue().AsSeconds();
			}
			break;
		case ETimedDataInputEvaluationType::PlatformTime:
			EvaluationTime = FApp::GetCurrentTime();
			break;
		case ETimedDataInputEvaluationType::None:
		default:
			break;
		}
	}

	const FSlateBrush* DarkBrush;
	const FSlateBrush* BrightBrush;
	double EvaluationTime = 0.0;

	bool bShowFurther = false;
	double MinSampleTime = 0.0;
	double MaxSampleTime = 0.0;
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
		.ShowFurther(InArgs._ShowFurther);

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
	if (bIsInput)
	{
		MinSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetInputOldestDataTime(InputIdentifier));
		MaxSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetInputNewestDataTime(InputIdentifier));
	}
	else
	{
		MinSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetChannelOldestDataTime(ChannelIdentifier));
		MaxSampleTime = GetSeconds(GraphicWidget->EvaluationType, TimedDataMonitorSubsystem->GetChannelNewestDataTime(ChannelIdentifier));
	}
	GraphicWidget->MinSampleTime = MinSampleTime - EvaluationOffset;
	GraphicWidget->MaxSampleTime = MaxSampleTime - EvaluationOffset;

	GraphicWidget->UpdateCachedValue();
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