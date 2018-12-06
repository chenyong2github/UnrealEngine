// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
//

#include "MagicLeapStereoLayers.h"
#include "MagicLeapHMD.h"
#include "SceneViewExtension.h"
#include "RHI.h"
#include "RHIDefinitions.h"

#include "CoreMinimal.h"

IStereoLayers* FMagicLeapHMD::GetStereoLayers()
{
	if (!DefaultStereoLayers.IsValid())
	{
		TSharedPtr<FMagicLeapStereoLayers, ESPMode::ThreadSafe> NewLayersPtr = FSceneViewExtensions::NewExtension<FMagicLeapStereoLayers>(this);
		DefaultStereoLayers = StaticCastSharedPtr<FDefaultStereoLayers>(NewLayersPtr);
	}
	return DefaultStereoLayers.Get();
}

FMagicLeapStereoLayers::FMagicLeapStereoLayers(const class FAutoRegister& AutoRegister, class FHeadMountedDisplayBase* InHmd)
: FDefaultStereoLayers(AutoRegister, InHmd)
, DefaultX(11.0f)
, DefaultY(-18.0f)
, DefaultZ(82.0f)
, DefaultWidth(75.0f)
, DefaultHeight(40.0f)
, CVarX(nullptr)
, CVarY(nullptr)
, CVarZ(nullptr)
, CVarWidth(nullptr)
, CVarHeight(nullptr)
{
	CVarX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LuminDebugCanvasX"));
	CVarY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LuminDebugCanvasY"));
	CVarZ = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LuminDebugCanvasZ"));
	CVarWidth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LuminDebugCanvasWidth"));
	CVarHeight = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LuminDebugCanvasHeight"));
}

IStereoLayers::FLayerDesc FMagicLeapStereoLayers::GetDebugCanvasLayerDesc(FTextureRHIRef Texture)
{
	IStereoLayers::FLayerDesc StereoLayerDesc;
	const float DebugCanvasX = CVarX ? CVarX->GetFloat() : DefaultX;
	const float DebugCanvasY = CVarY ? CVarY->GetFloat() : DefaultY;
	const float DebugCanvasZ = CVarZ ? CVarZ->GetFloat() : DefaultZ;
	StereoLayerDesc.Transform = FTransform(FVector(DebugCanvasZ, DebugCanvasX, DebugCanvasY));
	if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
	{
		StereoLayerDesc.Transform.SetScale3D(FVector(1.f, 1.f, -1.f));
	}
	const float DebugCanvasWidth = CVarWidth ? CVarWidth->GetFloat() : DefaultWidth;
	const float DebugCanvasHeight = CVarHeight ? CVarHeight->GetFloat() : DefaultHeight;
	StereoLayerDesc.QuadSize = FVector2D(DebugCanvasWidth, DebugCanvasHeight);
	StereoLayerDesc.PositionType = IStereoLayers::ELayerType::FaceLocked;
	StereoLayerDesc.ShapeType = IStereoLayers::ELayerShape::QuadLayer;
	StereoLayerDesc.Texture = Texture;
	StereoLayerDesc.Flags = IStereoLayers::ELayerFlags::LAYER_FLAG_TEX_CONTINUOUS_UPDATE;
	StereoLayerDesc.Flags |= IStereoLayers::ELayerFlags::LAYER_FLAG_QUAD_PRESERVE_TEX_RATIO;
	return StereoLayerDesc;
}
