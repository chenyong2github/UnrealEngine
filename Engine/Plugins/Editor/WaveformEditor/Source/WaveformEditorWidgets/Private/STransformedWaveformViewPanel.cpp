// Copyright Epic Games, Inc. All Rights Reserved.

 #include "STransformedWaveformViewPanel.h"

#include "SampledSequenceDisplayUnit.h"
#include "SPlayheadOverlay.h"
#include "SFixedSampledSequenceRuler.h"
#include "SFixedSampledSequenceViewer.h"
#include "SWaveformTransformationsOverlay.h"
#include "SWaveformEditorInputRoutingOverlay.h"
#include "WaveformEditorGridData.h"
#include "WaveformEditorStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

void STransformedWaveformViewPanel::Construct(const FArguments& InArgs, const FFixedSampledSequenceView& InView)
{
	DisplayUnit = ESampledSequenceDisplayUnit::Seconds;
	DataView = InView;

	WaveformEditorStyle = &FWaveformEditorStyle::Get();
	check(WaveformEditorStyle);

	SetUpGridData();
	SetUpWaveformViewer(GridData.ToSharedRef(), DataView);

	if (InArgs._TransformationsOverlay)
	{
		WaveformTransformationsOverlay = InArgs._TransformationsOverlay;
	}

	SetupPlayheadOverlay();
	SetUpInputRoutingOverlay();
	SetUpTimeRuler(GridData.ToSharedRef());
	SetUpInputOverrides(InArgs);
	CreateLayout();
}

void STransformedWaveformViewPanel::CreateLayout()
{
	check(TimeRuler);
	check(WaveformViewer);
	check(InputRoutingOverlay);

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
		PlayheadOverlay.ToSharedRef()
	];

	WaveformView->AddSlot()
	[
		InputRoutingOverlay.ToSharedRef()
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

void STransformedWaveformViewPanel::SetUpTimeRuler(TSharedRef<FWaveformEditorGridData> InGridData)
{
	FFixedSampleSequenceRulerStyle* TimeRulerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FFixedSampleSequenceRulerStyle>("WaveformEditorRuler.Style").Get();
	check(TimeRulerStyle);

	TimeRuler = SNew(SFixedSampledSequenceRuler, InGridData).DisplayUnit(DisplayUnit).Style(TimeRulerStyle);
	GridData->OnGridMetricsUpdated.AddSP(TimeRuler.ToSharedRef(), &SFixedSampledSequenceRuler::UpdateGridMetrics);
	TimeRulerStyle->OnStyleUpdated.AddSP(TimeRuler.ToSharedRef(), &SFixedSampledSequenceRuler::OnStyleUpdated);
	TimeRuler->OnTimeUnitMenuSelection.AddSP(this, &STransformedWaveformViewPanel::UpdateDisplayUnit);
}

void STransformedWaveformViewPanel::SetUpInputRoutingOverlay()
{
	FSampledSequenceViewerStyle* ViewerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FSampledSequenceViewerStyle>("WaveformViewer.Style").Get();
	check(ViewerStyle);
	
	TArray<TSharedPtr<SWidget>> OverlaidWidgets;

	if (WaveformTransformationsOverlay)
	{
		OverlaidWidgets.Add(WaveformTransformationsOverlay);
	}

	check(PlayheadOverlay)
	OverlaidWidgets.Add(PlayheadOverlay);
	InputRoutingOverlay = SNew(SWaveformEditorInputRoutingOverlay, OverlaidWidgets).Style(ViewerStyle);
}

void STransformedWaveformViewPanel::SetupPlayheadOverlay()
{
	FPlayheadOverlayStyle* PlayheadOverlayStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FPlayheadOverlayStyle>("WaveformEditorPlayheadOverlay.Style").Get();
	check(PlayheadOverlayStyle);

	PlayheadOverlay = SNew(SPlayheadOverlay).Style(PlayheadOverlayStyle);
	PlayheadOverlayStyle->OnStyleUpdated.AddSP(PlayheadOverlay.ToSharedRef(), &SPlayheadOverlay::OnStyleUpdated);
}

void STransformedWaveformViewPanel::SetUpWaveformViewer(TSharedRef<FWaveformEditorGridData> InGridData, const FFixedSampledSequenceView& InView)
{
	FSampledSequenceViewerStyle* WaveViewerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FSampledSequenceViewerStyle>("WaveformViewer.Style").Get();
	check(WaveViewerStyle);

	WaveformViewer = SNew(SFixedSampledSequenceViewer, InView.SampleData, InView.NumDimensions, InGridData).Style(WaveViewerStyle);
	WaveViewerStyle->OnStyleUpdated.AddSP(WaveformViewer.ToSharedRef(), &SFixedSampledSequenceViewer::OnStyleUpdated);
	GridData->OnGridMetricsUpdated.AddSP(WaveformViewer.ToSharedRef(), &SFixedSampledSequenceViewer::UpdateGridMetrics);
}

void STransformedWaveformViewPanel::SetUpGridData()
{
	FFixedSampleSequenceRulerStyle* RulerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FFixedSampleSequenceRulerStyle>("WaveformEditorRuler.Style").Get();
	check(RulerStyle)
	
	GridData = MakeShared<FWaveformEditorGridData>(DataView.SampleData.Num() / DataView.NumDimensions, DataView.SampleRate, RulerStyle->DesiredWidth, &RulerStyle->TicksTextFont);
}

void STransformedWaveformViewPanel::ReceiveSequenceView(const FFixedSampledSequenceView InView, const uint32 FirstSampleIndex)
{
	DataView = InView;
	if (GridData)
	{
		const uint32 FirstRenderedFrame = FirstSampleIndex / InView.NumDimensions;
		const uint32 NumFrames = InView.SampleData.Num() / InView.NumDimensions;
		GridData->UpdateDisplayRange(TRange<uint32>(FirstRenderedFrame, FirstRenderedFrame + NumFrames));
	}

	if (WaveformViewer)
	{
		WaveformViewer->UpdateView(InView.SampleData, InView.NumDimensions);
	}
}

void STransformedWaveformViewPanel::SetPlayheadRatio(const float InRatio)
{
	CachedPlayheadRatio = InRatio;
}

void STransformedWaveformViewPanel::SetOnPlayheadOverlayMouseButtonUp(FPointerEventHandler InEventHandler)
{
	check(PlayheadOverlay)
	PlayheadOverlay->SetOnMouseButtonUp(InEventHandler);
}

void STransformedWaveformViewPanel::SetOnTimeRulerMouseButtonUp(FPointerEventHandler InEventHandler)
{
	check(TimeRuler)
	TimeRuler->SetOnMouseButtonUp(InEventHandler);
}

void STransformedWaveformViewPanel::SetOnTimeRulerMouseButtonDown(FPointerEventHandler InEventHandler)
{
	check(TimeRuler)
	TimeRuler->SetOnMouseButtonDown(InEventHandler);
}

void STransformedWaveformViewPanel::SetOnTimeRulerMouseMove(FPointerEventHandler InEventHandler)
{
	check(TimeRuler)
	TimeRuler->SetOnMouseMove(InEventHandler);
}

void STransformedWaveformViewPanel::SetOnMouseWheel(FPointerEventHandler InEventHandler)
{
	check(InputRoutingOverlay)
	InputRoutingOverlay->OnMouseWheelDelegate = InEventHandler;
}

void STransformedWaveformViewPanel::UpdateDisplayUnit(const ESampledSequenceDisplayUnit InDisplayUnit)
{
	DisplayUnit = InDisplayUnit;
	TimeRuler->UpdateDisplayUnit(DisplayUnit);
}

void STransformedWaveformViewPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
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

	UpdatePlayheadPosition(PaintedWidth);

}

void STransformedWaveformViewPanel::UpdatePlayheadPosition(const float PaintedWidth)
{

	float PlayheadX = CachedPlayheadRatio * PaintedWidth;
	PlayheadX = GridData ? GridData->SnapPositionToClosestFrame(PlayheadX) : PlayheadX;


	if (PlayheadOverlay)
	{
		PlayheadOverlay->SetPlayheadPosition(PlayheadX);
	}

	if (TimeRuler)
	{
		TimeRuler->SetPlayheadPosition(PlayheadX);
	}
}

void STransformedWaveformViewPanel::SetUpInputOverrides(const FArguments& InArgs)
{
	SetOnPlayheadOverlayMouseButtonUp(InArgs._OnPlayheadOverlayMouseButtonUp);

	SetOnTimeRulerMouseButtonUp(InArgs._OnTimeRulerMouseButtonUp);
	SetOnTimeRulerMouseButtonDown(InArgs._OnTimeRulerMouseButtonDown);
	SetOnTimeRulerMouseMove(InArgs._OnTimeRulerMouseMove);
	
	SetOnMouseWheel(InArgs._OnMouseWheel);
}