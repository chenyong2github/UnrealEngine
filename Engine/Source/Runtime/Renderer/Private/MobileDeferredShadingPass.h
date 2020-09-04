// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRHICommandListImmediate;
class FScene;
class FViewInfo;
struct FSortedLightSetSceneInfo;

void MobileDeferredShadingPass(FRHICommandListImmediate& RHICmdList, const FScene& Scene, const FViewInfo& View, const FSortedLightSetSceneInfo &SortedLightSet);