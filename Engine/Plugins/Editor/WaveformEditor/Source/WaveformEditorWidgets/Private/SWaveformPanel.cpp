// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWaveformPanel.h"

#include "SWaveformEditorTimeRuler.h"
#include "SWaveformTransformationsOverlay.h"
#include "SSampledSequenceViewer.h"
#include "SWaveformViewerOverlay.h"
#include "WaveformEditorDisplayUnit.h"
#include "WaveformEditorGridData.h"
#include "WaveformEditorRenderData.h"
#include "WaveformEditorStyle.h"
#include "WaveformEditorTransportCoordinator.h"
#include "WaveformEditorZoomController.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

void SWaveformPanel::Construct(const FArguments& InArgs, TSharedRef<FWaveformEditorRenderData> InRenderData, TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedPtr<SWaveformTransformationsOverlay> InWaveformTransformationsOverlay)
{
	DisplayUnit = EWaveformEditorDisplayUnit::Seconds;

	WaveformEditorStyle = &FWaveformEditorStyle::Get();
	check(WaveformEditorStyle);

	RenderData = InRenderData;
	GenerateFloatRenderData();
	InRenderData->OnRenderDataUpdated.AddSP(this, &SWaveformPanel::OnRenderDataUpdated);

	TransportCoordinator = InTransportCoordinator;
	TransportCoordinator->OnDisplayRangeUpdated.AddSP(this, &SWaveformPanel::OnDisplayRangeUpdated);

	SetUpGridData(InRenderData);
	SetUpWaveformViewer(GridData.ToSharedRef(), InRenderData);
	SetUpZoomManager(InZoomManager, InTransportCoordinator);

	if (InWaveformTransformationsOverlay)
	{
		WaveformTransformationsOverlay = InWaveformTransformationsOverlay;
	}
	
	SetUpWaveformViewerOverlay(InTransportCoordinator, InZoomManager);
	SetUpTimeRuler(InTransportCoordinator, GridData.ToSharedRef());
	CreateLayout();
}

void SWaveformPanel::CreateLayout()
{
	check(TimeRuler);
	check(WaveformViewer);
	check(WaveformViewerOverlay);

	TSharedPtr<SOverlay> WaveformView = SNew(SOverlay);
	WaveformView->AddSlot()
	[
		WaveformViewer.ToSharedRef()
	];

	if (WaveformTransformationsOverlay)
	{
		WaveformView->AddSlot()
		[
			WaveformTransformationsOverlay.ToSharedRef()
		];
	}

	WaveformView->AddSlot()
	[
		WaveformViewerOverlay.ToSharedRef()
	];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			TimeRuler.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		[
			WaveformView.ToSharedRef()
		]
	];
}

void SWaveformPanel::SetUpTimeRuler(TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorGridData> InGridData)
{
	FWaveformEditorTimeRulerStyle* TimeRulerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FWaveformEditorTimeRulerStyle>("WaveformEditorRuler.Style").Get();
	check(TimeRulerStyle);

	TimeRuler = SNew(SWaveformEditorTimeRuler, InTransportCoordinator, InGridData).DisplayUnit(DisplayUnit).Style(TimeRulerStyle);
	TimeRulerStyle->OnStyleUpdated.AddSP(TimeRuler.ToSharedRef(), &SWaveformEditorTimeRuler::OnStyleUpdated);
	TimeRuler->OnTimeUnitMenuSelection.AddSP(this, &SWaveformPanel::UpdateDisplayUnit);
}

void SWaveformPanel::SetUpWaveformViewerOverlay(TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomManager)
{
	FWaveformViewerOverlayStyle* OverlayStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FWaveformViewerOverlayStyle>("WaveformViewerOverlay.Style").Get();
	check(OverlayStyle);

	WaveformViewerOverlay = SNew(SWaveformViewerOverlay, InTransportCoordinator, WaveformTransformationsOverlay.ToSharedRef(), GridData.ToSharedRef()).Style(OverlayStyle);
	OverlayStyle->OnStyleUpdated.AddSP(WaveformViewerOverlay.ToSharedRef(), &SWaveformViewerOverlay::OnStyleUpdated);
	WaveformViewerOverlay->OnNewMouseDelta.BindSP(InZoomManager, &FWaveformEditorZoomController::ZoomByDelta);
}

void SWaveformPanel::SetUpWaveformViewer(TSharedRef<FWaveformEditorGridData> InGridData, TSharedRef<FWaveformEditorRenderData> InRenderData)
{
	FSampledSequenceViewerStyle* WaveViewerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FSampledSequenceViewerStyle>("WaveformViewer.Style").Get();
	check(WaveViewerStyle);
	
	InGridData->UpdateGridMetrics(WaveViewerStyle->DesiredWidth);
	TimeSeriesDrawingUtils::FSampledSequenceDrawingParams WaveformViewerDrawingParams;
	WaveformViewerDrawingParams.MaxDisplayedValue = TNumericLimits<int16>::Max();

	WaveformViewer = SNew(SSampledSequenceViewer, MakeArrayView(FloatRenderData.GetData(), FloatRenderData.Num()), InRenderData->GetNumChannels(), InGridData).Style(WaveViewerStyle).SequenceDrawingParams(WaveformViewerDrawingParams);
	WaveViewerStyle->OnStyleUpdated.AddSP(WaveformViewer.ToSharedRef(), &SSampledSequenceViewer::OnStyleUpdated);
}

void SWaveformPanel::SetUpGridData(TSharedRef<FWaveformEditorRenderData> InRenderData)
{
	GridData = MakeShared<FWaveformEditorGridData>(InRenderData->GetNumSamples() / InRenderData->GetNumChannels(), InRenderData->GetSampleRate());

	const ISlateStyle* WaveEditorStyle = FSlateStyleRegistry::FindSlateStyle("WaveformEditorStyle");
	if (ensure(WaveEditorStyle))
	{
		const FWaveformEditorTimeRulerStyle& RulerStyle = WaveEditorStyle->GetWidgetStyle<FWaveformEditorTimeRulerStyle>("WaveformEditorRuler.Style");
		GridData->SetTicksTimeFont(&RulerStyle.TicksTextFont);
	}
}

void SWaveformPanel::SetUpZoomManager(TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator)
{
	InZoomManager->OnZoomRatioChanged.AddSP(InTransportCoordinator, &FWaveformEditorTransportCoordinator::OnZoomLevelChanged);
}

void SWaveformPanel::OnRenderDataUpdated()
{
	check(TransportCoordinator)
	OnDisplayRangeUpdated(TransportCoordinator->GetDisplayRange());
}

void SWaveformPanel::OnDisplayRangeUpdated(const TRange<float> NewDisplayRange)
{
	check (RenderData)

	const uint8 MinFramesToDisplay = 1;
	const uint32 MinSamplesToDisplay = MinFramesToDisplay * RenderData->GetNumChannels();
	const uint32 NumOriginalSamples = RenderData->GetSampleData().Num();
	const uint32 NumOriginalFrames = NumOriginalSamples / RenderData->GetNumChannels();

	const uint32 FirstRenderedSample = FMath::Clamp(FMath::RoundToInt32(NumOriginalFrames * NewDisplayRange.GetLowerBoundValue()), 0, NumOriginalFrames - MinFramesToDisplay) * RenderData->GetNumChannels();	
	const uint32 NumFramesToRender = FMath::RoundToInt32(NumOriginalFrames * NewDisplayRange.Size<float>());
	const uint32 NumSamplesToRender = FMath::Clamp(NumFramesToRender * RenderData->GetNumChannels(), MinSamplesToDisplay, NumOriginalSamples - FirstRenderedSample);
	
	check(NumSamplesToRender % RenderData->GetNumChannels() == 0 && FirstRenderedSample % RenderData->GetNumChannels() == 0);
	
	if (GridData)
	{
		const uint32 FirstRenderedFrame = FirstRenderedSample / RenderData->GetNumChannels();
		GridData->UpdateDisplayRange(TRange<uint32>(FirstRenderedFrame, FirstRenderedFrame + NumFramesToRender));
	}

	if (TimeRuler)
	{
		TimeRuler->UpdateGridMetrics();
	}

	if (WaveformViewer)
	{
		TArrayView<const float> RenderedView = MakeArrayView(FloatRenderData.GetData(), FloatRenderData.Num());
		RenderedView = RenderedView.Slice(FirstRenderedSample, NumSamplesToRender);
		WaveformViewer->UpdateView(RenderedView, RenderData->GetNumChannels());
	}
}

void SWaveformPanel::GenerateFloatRenderData()
{
	TArrayView<const int16> SampleData = RenderData->GetSampleData();
	FloatRenderData.SetNumUninitialized(SampleData.Num());

	for (int32 Sample = 0; Sample < SampleData.Num(); ++Sample)
	{
		FloatRenderData[Sample] = SampleData[Sample];
	}
}

void SWaveformPanel::UpdateDisplayUnit(const EWaveformEditorDisplayUnit InDisplayUnit)
{
	DisplayUnit = InDisplayUnit;
	TimeRuler->UpdateDisplayUnit(DisplayUnit);
}

void SWaveformPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const float PaintedWidth = AllottedGeometry.GetLocalSize().X;

	if (PaintedWidth != CachedPixelWidth)
	{
		CachedPixelWidth = PaintedWidth;

		if (GridData)
		{
			GridData->UpdateGridMetrics(PaintedWidth);
		}	
	}
}