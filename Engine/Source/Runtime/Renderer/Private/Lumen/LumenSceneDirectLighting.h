// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneUtils.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "Lumen.h"
#include "LumenSceneUtils.h"
#include "LumenSceneRendering.h"

class FLumenLight
{
public:
	FString Name;
	ELumenLightType Type;
	const FLightSceneInfo* LightSceneInfo = nullptr;
	FRDGBufferRef ShadowMaskTiles = nullptr;
};

namespace Lumen
{
	void SetDirectLightingDeferredLightUniformBuffer(
		const FViewInfo& View,
		const FLightSceneInfo* LightSceneInfo,
		TUniformBufferBinding<FDeferredLightUniformStruct>& UniformBuffer);
};