// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneTexturesConfig.h"
#include "RenderGraphResources.h"

FSceneTexturesConfig FSceneTexturesConfig::GlobalInstance;

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FSceneTextureUniformParameters, "SceneTexturesStruct", SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FMobileSceneTextureUniformParameters, "MobileSceneTextures", SceneTextures);

FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.SceneTextures = UniformBuffer;
	return Parameters;
}

FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.MobileSceneTextures = UniformBuffer;
	return Parameters;
}