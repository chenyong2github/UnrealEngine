// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformTransformationRenderLayerFactory.h"

#include "IWaveformTransformation.h"
#include "Styling/AppStyle.h"
#include "SWaveformTransformationDurationHighlight.h"
#include "SWaveformTransformationRenderLayer.h"
#include "SWaveformTransformationTrimFadeLayer.h"
#include "WaveformEditorTransportCoordinator.h"
#include "WaveformEditorZoomController.h"
#include "WaveformTransformationTrimFade.h"

FWaveformTransformationRenderLayerFactory::FWaveformTransformationRenderLayerFactory(TSharedRef<FWaveformEditorRenderData> InWaveformRenderData, TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, TSharedRef<FWaveformEditorZoomController> InZoomController)
	: WaveformRenderData(InWaveformRenderData)
	, TransportCoordinator(InTransportCoordinator)
	, ZoomController(InZoomController)
{
}


TSharedPtr<SWaveformTransformationRenderLayer> FWaveformTransformationRenderLayerFactory::Create(TObjectPtr<UWaveformTransformationBase> InTransformationToRender) const
{
	TSharedPtr<SWaveformTransformationRenderLayer> OutWidget = nullptr;
	UClass* TransformationClass = InTransformationToRender->GetClass();

	if (TransformationClass == UWaveformTransformationTrimFade::StaticClass())
	{
		TSharedPtr<SWaveformTransformationTrimFadeLayer> TrimFadeLayer = SNew(SWaveformTransformationTrimFadeLayer, Cast<UWaveformTransformationTrimFade>(InTransformationToRender), WaveformRenderData.ToSharedRef());
		TrimFadeLayer->OnZoomLevelChanged(ZoomController->GetZoomRatio());
		TrimFadeLayer->UpdateDisplayRange(TransportCoordinator->GetDisplayRange());
		TransportCoordinator->OnDisplayRangeUpdated.AddSP(TrimFadeLayer.Get(), &SWaveformTransformationTrimFadeLayer::UpdateDisplayRange);
		ZoomController->OnZoomRatioChanged.AddSP(TrimFadeLayer.Get(), &SWaveformTransformationTrimFadeLayer::OnZoomLevelChanged);
		OutWidget = TrimFadeLayer;
	}

	return OutWidget;
}

TSharedPtr<SWaveformTransformationRenderLayer> FWaveformTransformationRenderLayerFactory::CreateDurationHiglightLayer() const
{
	TSharedPtr<SWaveformTransformationDurationHiglight> DurationHiglightLayer = SNew(SWaveformTransformationDurationHiglight, WaveformRenderData.ToSharedRef());
	DurationHiglightLayer->OnZoomLevelChanged(ZoomController->GetZoomRatio());
	DurationHiglightLayer->UpdateDisplayRange(TransportCoordinator->GetDisplayRange());
	TransportCoordinator->OnDisplayRangeUpdated.AddSP(DurationHiglightLayer.Get(), &SWaveformTransformationDurationHiglight::UpdateDisplayRange);
	ZoomController->OnZoomRatioChanged.AddSP(DurationHiglightLayer.Get(), &SWaveformTransformationDurationHiglight::OnZoomLevelChanged);

	return DurationHiglightLayer;
}
