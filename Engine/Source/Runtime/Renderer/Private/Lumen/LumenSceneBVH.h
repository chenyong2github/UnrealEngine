// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneBVH.h
=============================================================================*/

#pragma once

#include "LumenSceneRendering.h"
#include "ShaderParameterStruct.h"

BEGIN_SHADER_PARAMETER_STRUCT(FBVHCullingParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBVHQueryArray)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBVHQueryNum)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardLinkHeadGrid)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardLinkData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardLinkNext)
	SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer<uint>, IndirectDispatchArgsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InputBVHQueryArray)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InputBVHQueryNum)
	SHADER_PARAMETER(FIntVector, CullGridSize)
	SHADER_PARAMETER(uint32, NumCullGridCells)
	SHADER_PARAMETER(uint32, MaxCulledCardLinks)
	SHADER_PARAMETER(uint32, MaxBVHQueries)
END_SHADER_PARAMETER_STRUCT()

class FBVHCullingBaseCS : public FGlobalShader
{
public:
	class FFirstPass : SHADER_PERMUTATION_BOOL("FIRST_PASS");
	using FPermutationDomain = TShaderPermutationDomain<FFirstPass>;

	FBVHCullingBaseCS() {}
	FBVHCullingBaseCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{ }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

class FBVHCulling
{
public:
	// Must match shader.
	static constexpr int32 CulledCardGridHeaderStride = 2;
	static constexpr int32 CulledCardLinkStride = 2;

	int32 MaxCulledNodesPerCell;
	int32 MaxCulledCardsPerCell;

	FIntVector CullGridSize;
	int32 NumCullGridCells;

	// Temporary buffers for the BVH traversal.
	FRDGBufferRef BVHQueryArray[2];
	FRDGBufferRef BVHQueryNum[2];
	FRDGBufferUAVRef BVHQueryArrayUAV[2];
	FRDGBufferUAVRef BVHQueryNumUAV[2];
	FRDGBufferSRVRef BVHQueryArraySRV[2];
	FRDGBufferSRVRef BVHQueryNumSRV[2];

	// Linked list for temporary culled cards.
	FRDGBufferRef CulledCardLinkData = nullptr;
	FRDGBufferRef CulledCardLinkNext = nullptr;
	FRDGBufferRef CulledCardLinkHeadGrid = nullptr;
	FRDGBufferUAVRef CulledCardLinkDataUAV = nullptr;
	FRDGBufferUAVRef CulledCardLinkNextUAV = nullptr;
	FRDGBufferUAVRef CulledCardLinkHeadGridUAV = nullptr;
	FRDGBufferSRVRef CulledCardLinkDataSRV = nullptr;
	FRDGBufferSRVRef CulledCardLinkHeadGridSRV = nullptr;

	// Compacted grid of culled cards.
	FRDGBufferRef CulledCardGridHeader = nullptr;
	FRDGBufferRef CulledCardGridData = nullptr;
	FRDGBufferRef CulledCardGridNext = nullptr;
	FRDGBufferUAVRef CulledCardGridHeaderUAV = nullptr;
	FRDGBufferUAVRef CulledCardGridDataUAV = nullptr;
	FRDGBufferUAVRef CulledCardGridNextUAV = nullptr;
	FRDGBufferSRVRef CulledCardGridHeaderSRV = nullptr;
	FRDGBufferSRVRef CulledCardGridDataSRV = nullptr;

	FBVHCullingParameters BVHCullingParameters;

	void Init(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FIntVector InCullGridSize, int32 InMaxCulledNodesPerCell = -1, int32 InMaxCulledCardsPerCell = -1);
	void InitNextPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, int32 BVHLevel);
	void CompactListIntoGrid(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UsedCardDataUAV = nullptr);

	template <typename ShaderType, typename PassParameterType>
	void NextPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, int32 BVHLevel, PassParameterType PassParameters)
	{
		const bool bFirstPass = BVHLevel == 0;

		typename ShaderType::FPermutationDomain PermutationVector;
		PermutationVector.template Set<typename ShaderType::FFirstPass>(bFirstPass);
		auto ComputeShader = ShaderMap->GetShader<ShaderType>(PermutationVector);

		if (bFirstPass)
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BVHCullingFirstPass"),
				ComputeShader,
				PassParameters,
				FIntVector(NumCullGridCells, 1, 1));
		}
		else
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BVHCulling"),
				ComputeShader,
				PassParameters,
				BVHCullingParameters.IndirectDispatchArgsBuffer,
				0);
		}
	};
};