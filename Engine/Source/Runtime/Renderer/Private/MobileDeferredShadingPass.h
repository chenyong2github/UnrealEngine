// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRHICommandListImmediate;
class FScene;
class FViewInfo;
struct FSortedLightSetSceneInfo;
class FMobileSceneTextureUniformParameters;

extern int32 GMobileUseClusteredDeferredShading;

void MobileDeferredShadingPass(FRDGBuilder& GraphBuilder, FRenderTargetBindingSlots& BasePassRenderTargets, TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> MobileSceneTextures, const FScene& Scene, const FViewInfo& View, const FSortedLightSetSceneInfo &SortedLightSet);