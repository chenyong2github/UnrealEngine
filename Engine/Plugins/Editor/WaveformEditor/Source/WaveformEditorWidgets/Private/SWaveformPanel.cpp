// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWaveformPanel.h"

#include "SampledSequenceDisplayUnit.h"
#include "SFixedSampledSequenceRuler.h"
#include "SFixedSampledSequenceViewer.h"
#include "SparseSampledSequenceTransportCoordinator.h"
#include "SPlayheadOverlay.h"
#include "SWaveformTransformationsOverlay.h"
#include "SWaveformViewerOverlay.h"
#include "WaveformEditorGridData.h"
#include "WaveformEditorRenderData.h"
#include "WaveformEditorStyle.h"
#include "WaveformEditorZoomController.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

void SWaveformPanel::Construct(const FArguments& InArgs, TSharedRef<FWaveformEditorRenderData> InRenderData, TSharedRef<FSparseSampledSequenceTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedPtr<SWaveformTransformationsOverlay> InWaveformTransformationsOverlay)
{

	DisplayUnit = ESampledSequenceDisplayUnit::Seconds;
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

	SetupPlayheadOverlay();
	SetUpWaveformViewerOverlay(InZoomManager);
	SetUpTimeRuler(GridData.ToSharedRef());
	
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
		PlayheadOverlay.ToSharedRef()
	];

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

void SWaveformPanel::SetUpTimeRuler(TSharedRef<FWaveformEditorGridData> InGridData)
{
	FFixedSampleSequenceRulerStyle* TimeRulerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FFixedSampleSequenceRulerStyle>("WaveformEditorRuler.Style").Get();

	check(TimeRulerStyle);

	TimeRuler = SNew(SFixedSampledSequenceRuler, InGridData).DisplayUnit(DisplayUnit).Style(TimeRulerStyle);

	GridData->OnGridMetricsUpdated.AddSP(TimeRuler.ToSharedRef(), &SFixedSampledSequenceRuler::UpdateGridMetrics);

	TimeRulerStyle->OnStyleUpdated.AddSP(TimeRuler.ToSharedRef(), &SFixedSampledSequenceRuler::OnStyleUpdated);

	TimeRuler->OnTimeUnitMenuSelection.AddSP(this, &SWaveformPanel::UpdateDisplayUnit);

	TimeRuler->SetOnMouseButtonDown(FPointerEventHandler::CreateSP(this, &SWaveformPanel::HandleTimeRulerMouseButtonDown));
	TimeRuler->SetOnMouseButtonUp(FPointerEventHandler::CreateSP(this, &SWaveformPanel::HandleTimeRulerMouseButtonUp));
	TimeRuler->SetOnMouseMove(FPointerEventHandler::CreateSP(this, &SWaveformPanel::HandleTimeRulerMouseMove));
}

void SWaveformPanel::SetUpWaveformViewerOverlay(TSharedRef<FWaveformEditorZoomController> InZoomManager)
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
	WaveformViewerOverlay = SNew(SWaveformViewerOverlay, OverlaidWidgets).Style(ViewerStyle);
	WaveformViewerOverlay->OnNewMouseDelta.BindSP(InZoomManager, &FWaveformEditorZoomController::ZoomByDelta);
}

void SWaveformPanel::SetupPlayheadOverlay()
{
	FPlayheadOverlayStyle* PlayheadOverlayStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FPlayheadOverlayStyle>("WaveformEditorPlayheadOverlay.Style").Get();
	check(PlayheadOverlayStyle);

	PlayheadOverlay = SNew(SPlayheadOverlay).Style(PlayheadOverlayStyle);
	PlayheadOverlay->SetOnMouseButtonDown(FPointerEventHandler::CreateSP(this, &SWaveformPanel::HandlePlayheadOverlayMouseButtonUp));
	PlayheadOverlayStyle->OnStyleUpdated.AddSP(PlayheadOverlay.ToSharedRef(), &SPlayheadOverlay::OnStyleUpdated);
}

void SWaveformPanel::SetUpWaveformViewer(TSharedRef<FWaveformEditorGridData> InGridData, TSharedRef<FWaveformEditorRenderData> InRenderData)
{
	FSampledSequenceViewerStyle* WaveViewerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FSampledSequenceViewerStyle>("WaveformViewer.Style").Get();
	check(WaveViewerStyle);

	SampledSequenceDrawingUtils::FSampledSequenceDrawingParams WaveformViewerDrawingParams;
	WaveformViewerDrawingParams.MaxDisplayedValue = TNumericLimits<int16>::Max();
	
	WaveformViewer = SNew(SFixedSampledSequenceViewer, MakeArrayView(FloatRenderData.GetData(), FloatRenderData.Num()), InRenderData->GetNumChannels(), InGridData).Style(WaveViewerStyle).SequenceDrawingParams(WaveformViewerDrawingParams);
	WaveViewerStyle->OnStyleUpdated.AddSP(WaveformViewer.ToSharedRef(), &SFixedSampledSequenceViewer::OnStyleUpdated);
	GridData->OnGridMetricsUpdated.AddSP(WaveformViewer.ToSharedRef(), &SFixedSampledSequenceViewer::UpdateGridMetrics);
}

void SWaveformPanel::SetUpGridData(TSharedRef<FWaveformEditorRenderData> InRenderData)
{
	FFixedSampleSequenceRulerStyle* RulerStyle = &WaveformEditorStyle->GetRegisteredWidgetStyle<FFixedSampleSequenceRulerStyle>("WaveformEditorRuler.Style").Get();
	check(RulerStyle)

	GridData = MakeShared<FWaveformEditorGridData>(InRenderData->GetNumSamples() / InRenderData->GetNumChannels(), InRenderData->GetSampleRate(), RulerStyle->DesiredWidth, &RulerStyle->TicksTextFont);
}

void SWaveformPanel::SetUpZoomManager(TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedRef<FSparseSampledSequenceTransportCoordinator> InTransportCoordinator)
{
	InZoomManager->OnZoomRatioChanged.AddSP(InTransportCoordinator, &FSparseSampledSequenceTransportCoordinator::SetZoomRatio);
}

void SWaveformPanel::OnRenderDataUpdated()
{
	check(TransportCoordinator)
	GenerateFloatRenderData();
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

	if (WaveformViewer)
	{
		TArrayView<const float> RenderedView = MakeArrayView(FloatRenderData.GetData(), FloatRenderData.Num());
		RenderedView = RenderedView.Slice(FirstRenderedSample, NumSamplesToRender);
		WaveformViewer->UpdateView(RenderedView, RenderData->GetNumChannels());
	}
}

void SWaveformPanel::GenerateFloatRenderData()
{
	check(RenderData)
	TArrayView<const int16> SampleData = RenderData->GetSampleData();

	FloatRenderData.SetNumUninitialized(SampleData.Num());

	for (int32 Sample = 0; Sample < SampleData.Num(); ++Sample)
	{
		FloatRenderData[Sample] = SampleData[Sample];
	}
}

void SWaveformPanel::UpdateDisplayUnit(const ESampledSequenceDisplayUnit InDisplayUnit)
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

	UpdatePlayheadPosition(PaintedWidth);
}

void SWaveformPanel::UpdatePlayheadPosition(const float PaintedWidth)
{

	float PlayheadX = 0.f;

	check(TransportCoordinator)

	PlayheadX = TransportCoordinator->GetFocusPoint() * PaintedWidth;
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

FReply SWaveformPanel::HandleTimeRulerMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	return HandleTimeRulerInteraction(WaveformEditorPanel::EReceivedInteractionType::MouseButtonDown, MouseEvent, Geometry);
}

FReply SWaveformPanel::HandleTimeRulerMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	return HandleTimeRulerInteraction(WaveformEditorPanel::EReceivedInteractionType::MouseButtonUp, MouseEvent, Geometry);
}

FReply SWaveformPanel::HandleTimeRulerMouseMove(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	return HandleTimeRulerInteraction(WaveformEditorPanel::EReceivedInteractionType::MouseMove, MouseEvent, Geometry);
}

FReply SWaveformPanel::HandleTimeRulerInteraction(const WaveformEditorPanel::EReceivedInteractionType MouseInteractionType, const FPointerEvent& MouseEvent, const FGeometry& Geometry)
{
	check(TimeRuler)

	check(TransportCoordinator)

	const float LocalWidth = Geometry.GetLocalSize().X;

	if (LocalWidth > 0.f)
	{
		const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
		const FVector2D CursorPosition = Geometry.AbsoluteToLocal(ScreenSpacePosition);

		const float CursorXRatio = CursorPosition.X / LocalWidth;

		switch (MouseInteractionType)
		{
		default:

			break;

		case WaveformEditorPanel::EReceivedInteractionType::MouseButtonDown:
			if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Handled().CaptureMouse(TimeRuler.ToSharedRef()).PreventThrottling();
			}

		case WaveformEditorPanel::EReceivedInteractionType::MouseMove:
			if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				TransportCoordinator->ScrubFocusPoint(CursorXRatio, true);
				return FReply::Handled().CaptureMouse(TimeRuler.ToSharedRef());
			}

		case WaveformEditorPanel::EReceivedInteractionType::MouseButtonUp:
			if (TimeRuler->HasMouseCapture())
			{
				TransportCoordinator->ScrubFocusPoint(CursorXRatio, false);
				return FReply::Handled().ReleaseMouseCapture();
			}
		}
	}

	return FReply::Handled();
}

FReply SWaveformPanel::HandlePlayheadOverlayMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	const bool HandleLeftMouseButton = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;

	if (HandleLeftMouseButton && TransportCoordinator)
	{

		const float LocalWidth = Geometry.GetLocalSize().X;
		if (LocalWidth > 0.f)
		{
			const float NewPosition = Geometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X / LocalWidth;
			TransportCoordinator->ScrubFocusPoint(NewPosition, false);
		}
	}
	return FReply::Handled();
}
