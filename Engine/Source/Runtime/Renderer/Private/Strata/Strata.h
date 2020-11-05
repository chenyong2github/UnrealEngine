// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "MeshPassProcessor.h"
#include "UnifiedBuffer.h"
#include "RHIUtilities.h"

// Forward declarations.
class FScene;
class FRDGBuilder;



BEGIN_SHADER_PARAMETER_STRUCT(FStrataOpaquePassUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER_UAV(RWByteAddressBuffer, MaterialLobesBufferUAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialLobesBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()



struct FStrataSceneData
{
	uint32 MaxBytesPerPixel;
	TRefCountPtr<IPooledRenderTarget> MaterialLobesTexture; // This should be a RDG resource when the refactoring gets in
	FRWByteAddressBuffer MaterialLobesBuffer;				// This should be a RDG resource	"		"		"

	TUniformBufferRef<FStrataGlobalUniformParameters> StrataGlobalUniformParameters;

	FStrataSceneData()
	{
	}
};




namespace Strata
{

bool IsStrataEnabled();

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder);

void BindStrataBasePassUniformParameters(const FViewInfo& View, FStrataOpaquePassUniformParameters& OutStrataUniformParameters);

TUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(const FViewInfo& View);

void AddVisualizeMaterialPasses(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, FRDGTextureRef SceneColorTexture);

};


