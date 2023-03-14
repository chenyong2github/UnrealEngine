// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Widgets/SCompoundWidget.h"

enum class EWaveformEditorDisplayUnit;
class FSamplesSequenceTransportCoordinator;
class FWaveformEditorGridData;
class FWaveformEditorRenderData;
class FWaveformEditorStyle;
class FWaveformEditorZoomController;
class SPlayheadOverlay;
class SSampledSequenceViewer;
class SWaveformEditorTimeRuler;
class SWaveformTransformationsOverlay;
class SWaveformViewerOverlay;

namespace WaveformEditorPanel
{
	enum class EReceivedInteractionType
	{
		MouseButtonUp,
		MouseButtonDown,
		MouseMove
	};
}

class WAVEFORMEDITORWIDGETS_API SWaveformPanel : public SCompoundWidget
{
public: 

	SLATE_BEGIN_ARGS(SWaveformPanel) {}
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FWaveformEditorRenderData> InRenderData, TSharedRef<FSamplesSequenceTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedPtr<SWaveformTransformationsOverlay> InWaveformTransformationsOverlay = nullptr);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void CreateLayout();

	void SetUpGridData(TSharedRef<FWaveformEditorRenderData> InRenderData);
	void SetupPlayheadOverlay();
	void SetUpTimeRuler(TSharedRef<FWaveformEditorGridData> InGridData);
	void SetUpWaveformViewer(TSharedRef<FWaveformEditorGridData> InGridData, TSharedRef<FWaveformEditorRenderData> InRenderData);
	void SetUpWaveformViewerOverlay(TSharedRef<FWaveformEditorZoomController> InZoomManager);
	void SetUpZoomManager(TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedRef<FSamplesSequenceTransportCoordinator> InTransportCoordinator);

	void OnRenderDataUpdated();
	void OnDisplayRangeUpdated(const TRange<float> NewDisplayRange);

	//child widgets input overrides
	FReply HandleTimeRulerMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent);
	FReply HandleTimeRulerMouseMove(const FGeometry& Geometry, const FPointerEvent& MouseEvent);
	FReply HandleTimeRulerMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent);
	FReply HandleTimeRulerInteraction(const WaveformEditorPanel::EReceivedInteractionType MouseInteractionType, const FPointerEvent& MouseEvent, const FGeometry& Geometry);
	FReply HandlePlayheadOverlayMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent);
	
	//temporary float render data conversion while widgets are made generic
	void GenerateFloatRenderData();
	TArray<float> FloatRenderData;

	void UpdateDisplayUnit(const EWaveformEditorDisplayUnit InDisplayUnit);
	void UpdatePlayheadPosition(const float PaintedWidth);

	TSharedPtr<FWaveformEditorRenderData> RenderData;
	TSharedPtr<FWaveformEditorGridData> GridData;
	TSharedPtr<FSamplesSequenceTransportCoordinator> TransportCoordinator;

	TSharedPtr<SWaveformEditorTimeRuler> TimeRuler;
	TSharedPtr<SSampledSequenceViewer> WaveformViewer;
	TSharedPtr<SWaveformTransformationsOverlay> WaveformTransformationsOverlay;
	TSharedPtr<SWaveformViewerOverlay> WaveformViewerOverlay;
	TSharedPtr<SPlayheadOverlay> PlayheadOverlay;

	float CachedPixelWidth = 0.f;

	EWaveformEditorDisplayUnit DisplayUnit;

	FWaveformEditorStyle* WaveformEditorStyle;
};