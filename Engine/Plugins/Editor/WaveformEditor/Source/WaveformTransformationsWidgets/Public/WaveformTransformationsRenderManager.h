// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "IWaveformTransformation.h"
#include "WaveformTransformationRenderLayerFactory.h"

class USoundWave;
class UWaveformTransformationBase;
class SWaveformTransformationRenderLayer;
class SWidget;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLayersChainGenerated, TSharedPtr<SWidget>* /*First Layer ptr*/, const int32 /* NLayers */)
DECLARE_MULTICAST_DELEGATE_SixParams(FOnRenderDataGenerated, const uint8* /* First RawPCMData element */, const uint32 /*Num Samples*/, const uint32 /*First Edited Sample*/, const uint32 /*Last Edited Sample*/, const uint32/*Sample Rate*/, const uint16 /*Num Channels*/)


/*************************************************************************************/
/* FWaveformTransformationsRenderManager											 */
/* The Waveform Transformations render manager can produce UI to display a chain 	 */
/* of waveform transformations														 */
/*  																				 */
/* The main UI elements created are:												 */
/*  Render Data : a uint8 array containing the transformed samples of the waveform   */ 
/*  Transform Layers : an array of Widgets containing the UI for each transformation */ 
/*  in the chain																	 */
/* 																					 */
/* These are created so that the entire stack of transformation is displayable.		 */
/* E.g : if a 10s long file is trimmed from second 2, seconds 0 to 1 will 			 */
/* still be present in the render data.												 */
/* UI widgets are passed a struct with information about the transformation to       */
/* display property (e.g. StartFrameOffset, Sampleduration ecc)                      */
/* 																					 */
/* UIs for different transformations are registered withand spawned by 				 */
/* FWaveformTransformationRenderLayerFactory.										 */
/* Transformations don't necessarily have a widget UI. In that case they are 		 */
/* only reflected in  the render data. 												 */
/*************************************************************************************/


class WAVEFORMTRANSFORMATIONSWIDGETS_API FWaveformTransformationsRenderManager
{
public:
	explicit FWaveformTransformationsRenderManager(
		TObjectPtr<USoundWave> InSoundWave, 
		TSharedRef<FWaveformEditorRenderData> InWaveformRenderData, 
		TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator, 
		TSharedRef<FWaveformEditorZoomController> InZoomController
	);

	/** Used to generate the stack of transformations UI widgets			*/
	/** Should be called when the waveform transformation chain is changed	*/
	void GenerateLayersChain();

	/** Used to generate updated render data and pass transformation info to the widgets */
	/** Should be called when the transformations parameters are changed				 */
	void UpdateRenderElements();

	TArrayView<TSharedPtr<SWidget>> GetTransformLayers() const;

	/** Called when a new layer chain of transformations UI is generated */
	FOnLayersChainGenerated OnLayersChainGenerated;

	/** Called when new render data is generated */
	FOnRenderDataGenerated OnRenderDataGenerated;

private:
	void GenerateRenderDataInternal();

	void CreateDurationHighlightLayer();

	TArray<Audio::FTransformationPtr> CreateTransformations() const;
	const bool CanChainChangeFileLength(const TArray<Audio::FTransformationPtr>& TransformationArray) const;

	TArray<TObjectPtr<UWaveformTransformationBase>> TransformationsToRender;
	TArray<TSharedPtr<SWaveformTransformationRenderLayer>> RenderLayers;
	TArrayView<TSharedPtr<SWidget>> RenderLayersWidgetView;

	TObjectPtr<USoundWave> SoundWaveToRender = nullptr;

	TArray<uint8> RawPCMData;

	TUniquePtr<FWaveformTransformationRenderLayerFactory> LayersFactory = nullptr;
	TSharedPtr<SWaveformTransformationRenderLayer> DurationHiglightLayer = nullptr;
};