// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Widgets/SCompoundWidget.h"
#include "IFixedSampledSequenceViewReceiver.h"

enum class ESampledSequenceDisplayUnit;
class SWaveformEditorInputRoutingOverlay;
class SWaveformTransformationsOverlay;
class SPlayheadOverlay;
class SFixedSampledSequenceViewer;
class SFixedSampledSequenceRuler;
class FWaveformEditorZoomController;
class FWaveformEditorStyle;
class FWaveformEditorRenderData;
class FWaveformEditorGridData;
class FSparseSampledSequenceTransportCoordinator;

class WAVEFORMEDITORWIDGETS_API STransformedWaveformViewPanel : public SCompoundWidget, public IFixedSampledSequenceViewReceiver 
{
public: 
	SLATE_BEGIN_ARGS(STransformedWaveformViewPanel) {}
		
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
		
		SLATE_ARGUMENT(TSharedPtr<SWaveformTransformationsOverlay>, TransformationsOverlay)

		SLATE_ARGUMENT(FPointerEventHandler, OnPlayheadOverlayMouseButtonUp)

		SLATE_ARGUMENT(FPointerEventHandler, OnTimeRulerMouseButtonUp)

		SLATE_ARGUMENT(FPointerEventHandler, OnTimeRulerMouseButtonDown)

		SLATE_ARGUMENT(FPointerEventHandler, OnTimeRulerMouseMove)

		SLATE_ARGUMENT(FPointerEventHandler, OnMouseWheel)

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const FFixedSampledSequenceView& InView);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void ReceiveSequenceView(const FFixedSampledSequenceView InView, const uint32 FirstSampleIndex = 0) override;
	void SetPlayheadRatio(const float InRatio);

	void SetOnPlayheadOverlayMouseButtonUp(FPointerEventHandler InEventHandler);
	void SetOnTimeRulerMouseButtonUp(FPointerEventHandler InEventHandler);
	void SetOnTimeRulerMouseButtonDown(FPointerEventHandler InEventHandler);
	void SetOnTimeRulerMouseMove(FPointerEventHandler InEventHandler);
	void SetOnMouseWheel(FPointerEventHandler InEventHandler);


private:
	void CreateLayout();

	void SetUpGridData();
	void SetupPlayheadOverlay();
	void SetUpTimeRuler(TSharedRef<FWaveformEditorGridData> InGridData);
	void SetUpWaveformViewer(TSharedRef<FWaveformEditorGridData> InGridData, const FFixedSampledSequenceView& InView);
	void SetUpInputRoutingOverlay();
	void SetUpInputOverrides(const FArguments& InArgs);

	void UpdateDisplayUnit(const ESampledSequenceDisplayUnit InDisplayUnit);
	void UpdatePlayheadPosition(const float PaintedWidth);

	TSharedPtr<FWaveformEditorGridData> GridData;

	TSharedPtr<SFixedSampledSequenceRuler> TimeRuler;
	TSharedPtr<SFixedSampledSequenceViewer> WaveformViewer;
 	TSharedPtr<SWaveformTransformationsOverlay> WaveformTransformationsOverlay;
	TSharedPtr<SWaveformEditorInputRoutingOverlay> InputRoutingOverlay;
	TSharedPtr<SPlayheadOverlay> PlayheadOverlay;

	float CachedPixelWidth = 0.f;

	ESampledSequenceDisplayUnit DisplayUnit;

	FWaveformEditorStyle* WaveformEditorStyle;

	FFixedSampledSequenceView DataView;
	
	float CachedPlayheadRatio = 0.f;
};