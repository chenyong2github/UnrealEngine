// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneRendering.h"
#include "RenderGraph.h"

struct FSortedLightSceneInfo;
struct FCloudShadowAOData;

class FLightSceneInfo;
class FProjectedShadowInfo;

struct FTranslucencyLightingVolumeTextures
{
	static int32 GetIndex(int32 ViewIndex, int32 CascadeIndex)
	{
		return (ViewIndex * TVC_MAX) + CascadeIndex;
	}

	bool IsValid() const
	{
		check(!VolumeDim || (Ambient.Num() == Directional.Num() && Ambient.Num() > 0));
		return VolumeDim != 0;
	}

	TArray<FRDGTextureRef, TInlineAllocator<TVC_MAX>> Ambient;
	TArray<FRDGTextureRef, TInlineAllocator<TVC_MAX>> Directional;
	int32 VolumeDim = 0;
};

/** Initializes the translucent volume textures and clears them using a compute pass. PassFlags should be ERDGPassFlags::Compute or ERDGPassFlags::AsyncCompute. */
void InitTranslucencyLightingVolumeTextures(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, ERDGPassFlags PassFlags, FTranslucencyLightingVolumeTextures& OutTextures);

BEGIN_SHADER_PARAMETER_STRUCT(FTranslucencyLightingVolumeParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyLightingVolumeAmbientInner)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyLightingVolumeAmbientOuter)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyLightingVolumeDirectionalInner)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyLightingVolumeDirectionalOuter)
END_SHADER_PARAMETER_STRUCT()

/** Initializes translucency volume lighting shader parameters from an optional textures struct. If null or uninitialized, fallback textures are used. */
FTranslucencyLightingVolumeParameters GetTranslucencyLightingVolumeParameters(FRDGBuilder& GraphBuilder, const FTranslucencyLightingVolumeTextures& Textures, uint32 ViewIndex);

void InjectTranslucencyLightingVolume(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ViewIndex,
	const FScene* Scene,
	const FSceneRenderer& Renderer,
	const FTranslucencyLightingVolumeTextures& Textures,
	const TArrayView<const FVisibleLightInfo> VisibleLightInfos,
	const FLightSceneInfo& LightSceneInfo,
	const FProjectedShadowInfo* ProjectedShadowInfo);

void InjectTranslucencyLightingVolumeArray(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	const FScene* Scene,
	const FSceneRenderer& Renderer,
	const FTranslucencyLightingVolumeTextures& Textures,
	const TArrayView<const FVisibleLightInfo> VisibleLightInfos,
	const TArrayView<const FSortedLightSceneInfo> SortedLights,
	const TInterval<int32> SortedLightInterval);

void InjectSimpleTranslucencyLightingVolumeArray(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ViewIndex,
	const uint32 ViewCount,
	const FTranslucencyLightingVolumeTextures& Textures,
	const FSimpleLightArray& SimpleLights);

void FinalizeTranslucencyLightingVolume(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	FTranslucencyLightingVolumeTextures& Textures);

void InjectTranslucencyLightingVolumeAmbientCubemap(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	const FTranslucencyLightingVolumeTextures& Textures);

void FilterTranslucencyLightingVolume(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	FTranslucencyLightingVolumeTextures& Textures);