// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WindowsMixedRealityStereoLayers.h"
#include "WindowsMixedRealityHMD.h"

namespace WindowsMixedReality
{
	IStereoLayers* FWindowsMixedRealityHMD::GetStereoLayers()
	{
		if (!DefaultStereoLayers.IsValid())
		{
			TSharedPtr<FWindowsMixedRealityStereoLayers, ESPMode::ThreadSafe> NewLayersPtr = FSceneViewExtensions::NewExtension<FWindowsMixedRealityStereoLayers>(this);
			DefaultStereoLayers = StaticCastSharedPtr<FDefaultStereoLayers>(NewLayersPtr);
		}
		return DefaultStereoLayers.Get();
	}
} // namespace WindowsMixedReality

IStereoLayers::FLayerDesc FWindowsMixedRealityStereoLayers::GetDebugCanvasLayerDesc(FTextureRHIRef Texture)
{
	IStereoLayers::FLayerDesc StereoLayerDesc;
	StereoLayerDesc.Transform = FTransform(FVector(100.f, 0, 0));
#if PLATFORM_HOLOLENS
	StereoLayerDesc.QuadSize = FVector2D(56.f, 56.f);
#else
	StereoLayerDesc.QuadSize = FVector2D(120.f, 120.f);
#endif
	StereoLayerDesc.PositionType = IStereoLayers::ELayerType::FaceLocked;
	StereoLayerDesc.ShapeType = IStereoLayers::ELayerShape::QuadLayer;
	StereoLayerDesc.Texture = Texture;
	StereoLayerDesc.Flags = IStereoLayers::ELayerFlags::LAYER_FLAG_TEX_CONTINUOUS_UPDATE;
	StereoLayerDesc.Flags |= IStereoLayers::ELayerFlags::LAYER_FLAG_QUAD_PRESERVE_TEX_RATIO;
	return StereoLayerDesc;
}
