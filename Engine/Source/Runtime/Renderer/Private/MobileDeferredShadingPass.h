// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRHICommandListImmediate;
class FScene;
class FViewInfo;
struct FSortedLightSetSceneInfo;

extern int32 GMobileUseClusteredDeferredShading;

void MobileDeferredShadingPass(
	FRHICommandListImmediate& RHICmdList, 
	const FScene& Scene, 
	const TArrayView<const FViewInfo*> PassViews, 
	const FSortedLightSetSceneInfo &SortedLightSet);