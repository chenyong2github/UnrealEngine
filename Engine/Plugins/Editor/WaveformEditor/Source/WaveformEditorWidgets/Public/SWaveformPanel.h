// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Widgets/SCompoundWidget.h"

class FWaveformEditorGridData;
class FWaveformEditorRenderData;
class FWaveformEditorStyle;
class FWaveformEditorTransportCoordinator;
class FWaveformEditorZoomController;
class SWaveformEditorTimeRuler;
class SWaveformTransformationsOverlay;
class SSampledSequenceViewer;
class SWaveformViewerOverlay;
enum class EWaveformEditorDisplayUnit;

class WAVEFORMEDITORWIDGETS_API SWaveformPanel : public SCompoundWidget
{
public: 

	SLATE_BEGIN_ARGS(SWaveformPanel) {}
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FWaveformEditorRenderData> InRenderData, TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedPtr<SWaveformTransformationsOverlay> InWaveformTransformationsOverlay = nullptr);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private: 
	void CreateLayout();

	void SetUpWaveformViewer(TSharedRef<FWaveformEditorGridData> InGridData, TSharedRef<FWaveformEditorRenderData> InRenderData);
	void SetUpWaveformViewerOverlay(TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomManager);
	void SetUpTimeRuler(TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorGridData> InGridData);
	void SetUpGridData(TSharedRef<FWaveformEditorRenderData> InRenderData);
	void SetUpZoomManager(TSharedRef<FWaveformEditorZoomController> InZoomManager, TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator);

	void OnRenderDataUpdated();
	void OnDisplayRangeUpdated(const TRange<float> NewDisplayRange);

	//temporary float render data conversion while widget are made generic
	void GenerateFloatRenderData();
	TArray<float> FloatRenderData;

	void UpdateDisplayUnit(const EWaveformEditorDisplayUnit InDisplayUnit);

	TSharedPtr<FWaveformEditorRenderData> RenderData = nullptr;
	TSharedPtr<FWaveformEditorGridData> GridData = nullptr;
	TSharedPtr<FWaveformEditorTransportCoordinator> TransportCoordinator = nullptr;

	TSharedPtr<SWaveformEditorTimeRuler> TimeRuler = nullptr;
	TSharedPtr<SSampledSequenceViewer> WaveformViewer = nullptr;
	TSharedPtr<SWaveformTransformationsOverlay> WaveformTransformationsOverlay = nullptr;
	TSharedPtr<SWaveformViewerOverlay> WaveformViewerOverlay = nullptr;

	float CachedPixelWidth = 0.f;

	EWaveformEditorDisplayUnit DisplayUnit;

	FWaveformEditorStyle* WaveformEditorStyle;

};