// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectPtr.h"
#include "Templates/SharedPointer.h"

class FWaveformEditorRenderData;
class FWaveformEditorTransportCoordinator;
class FWaveformEditorZoomController;
class SWaveformTransformationRenderLayer;
class UWaveformTransformationBase;

class FWaveformTransformationRenderLayerFactory
{
public:
	explicit FWaveformTransformationRenderLayerFactory(
		TSharedRef<FWaveformEditorRenderData> InWaveformRenderData, 
		TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, 
		TSharedRef<FWaveformEditorZoomController> InZoomController);

	~FWaveformTransformationRenderLayerFactory() = default;

	TSharedPtr<SWaveformTransformationRenderLayer> Create(TObjectPtr<UWaveformTransformationBase> InTransformationToRender) const;
	TSharedPtr<SWaveformTransformationRenderLayer> CreateDurationHiglightLayer () const;

private:
	TSharedPtr<FWaveformEditorRenderData> WaveformRenderData = nullptr;
	TSharedPtr<FWaveformEditorTransportCoordinator> TransportCoordinator = nullptr;
	TSharedPtr<FWaveformEditorZoomController> ZoomController = nullptr;
};