// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsDebug.h: Hair strands debug display.
=============================================================================*/

#pragma once

#include "RenderGraph.h"
#include "Renderer/Private/SceneRendering.h"

struct FHairStrandsDebugData
{
	BEGIN_SHADER_PARAMETER_STRUCT(FWriteParameters, )
		SHADER_PARAMETER(uint32,							Debug_MaxShadingPointCount)
		SHADER_PARAMETER(uint32,							Debug_MaxSampleCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, Debug_ShadingPointBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer,			Debug_ShadingPointCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, Debug_SampleBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer,			Debug_SampleCounter)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FReadParameters, )
		SHADER_PARAMETER(uint32,							Debug_MaxShadingPointCount)
		SHADER_PARAMETER(uint32,							Debug_MaxSampleCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RWStructuredBuffer, Debug_ShadingPointBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RWBuffer,			Debug_ShadingPointCounter)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RWStructuredBuffer, Debug_SampleBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RWBuffer,			Debug_SampleCounter)
	END_SHADER_PARAMETER_STRUCT()

	struct ShadingInfo
	{
		FVector BaseColor;
		float	Roughness;
		FVector T;
		uint32	SampleCount;
		FVector V;
		float	SampleOffset;
	};

	struct Sample
	{
		FVector Direction;
		float	Pdf;
		FVector Weights;
		float	Pad;
	};
	static const uint32 MaxShadingPointCount = 32;
	static const uint32 MaxSampleCount = 1024 * 32;

	struct Data
	{
		FRDGBufferRef ShadingPointBuffer = nullptr;
		FRDGBufferRef ShadingPointCounter = nullptr;
		FRDGBufferRef SampleBuffer = nullptr;
		FRDGBufferRef SampleCounter = nullptr;
	} Resources;

	bool IsValid() const
	{
		return Resources.ShadingPointBuffer && Resources.ShadingPointCounter && Resources.SampleBuffer && Resources.SampleCounter;
	}

	static Data CreateData(FRDGBuilder& GraphBuilder);
	static void SetParameters(FRDGBuilder& GraphBuilder, Data& In, FWriteParameters& Out);
	static void SetParameters(FRDGBuilder& GraphBuilder, const Data& In, FReadParameters& Out);
};

void RenderHairStrandsDebugInfo(
	FRDGBuilder& GraphBuilder,
	TArrayView<FViewInfo> Views,
	const struct FHairStrandsRenderingData* HairDatas,
	const struct FHairStrandClusterData& HairClusterData,
	FRDGTextureRef SceneColorTexture);