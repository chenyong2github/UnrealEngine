// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"

BEGIN_UNIFORM_BUFFER_STRUCT(FIrradianceCachingParameters, )
	SHADER_PARAMETER(uint32, HashTableSize)
	SHADER_PARAMETER(uint32, CacheSize)
	SHADER_PARAMETER(int32, Quality)
	SHADER_PARAMETER(float, Spacing)
	SHADER_PARAMETER(float, CornerRejection)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<FIrradianceCacheRecord>, IrradianceCacheRecords)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, RWHashTable)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, RWHashToIndex)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, RWIndexToHash)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, RecordAllocator)
END_UNIFORM_BUFFER_STRUCT()

struct FIrradianceCache
{
	struct FIrradianceCacheRecord
	{
		// When used as a cache entry, 
		// WorldPosition.w == FrameLastTouched
		// WorldNormal.w == NumAccumulatedSamples
		FVector4 WorldPosition;
		FVector4 WorldNormal;
		FVector4 Irradiance;
	};

	const int32 IrradianceCacheMaxSize = 1048576;

	FStructuredBufferRHIRef IrradianceCacheRecords;
	FUnorderedAccessViewRHIRef IrradianceCacheRecordsUAV;

	TUniformBufferRef<FIrradianceCachingParameters> IrradianceCachingParametersUniformBuffer;

	FIrradianceCache(int32 Quality, float Spacing, float CornerRejection);

	FRWBuffer HashTable;
	FRWBuffer HashToIndex;
	FRWBuffer IndexToHash;
	FRWBuffer RecordAllocator;

	int32 CurrentRevision = 0;
};

class FVisualizeIrradianceCachePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisualizeIrradianceCachePS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeIrradianceCachePS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FIrradianceCachingParameters, IrradianceCachingParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
