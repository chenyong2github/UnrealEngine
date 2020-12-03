// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricRenderTarget.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "RendererInterface.h"
#include "RenderResource.h"
#include "VolumetricRenderTargetViewStateData.h"



class FScene;
class FViewInfo;



bool ShouldViewRenderVolumetricCloudRenderTarget(const FViewInfo& ViewInfo);
bool IsVolumetricRenderTargetEnabled();
bool IsVolumetricRenderTargetAsyncCompute();


