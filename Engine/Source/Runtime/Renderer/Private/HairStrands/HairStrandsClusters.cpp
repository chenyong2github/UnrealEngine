// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsClusters.h"
#include "HairStrandsUtils.h"
#include "SceneRendering.h"
#include "SceneManagement.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairStrandsClusterCullingUsesHzb = 1;
static FAutoConsoleVariableRef CVarHairCullingUseHzb(TEXT("r.HairStrands.Cluster.CullingUsesHzb"), GHairStrandsClusterCullingUsesHzb, TEXT("Enable/disable the use of HZB to help cull more hair clusters."));

static int32 GHairStrandsClusterForceLOD = -1;
static FAutoConsoleVariableRef CVarHairClusterCullingLodMode(TEXT("r.HairStrands.Cluster.ForceLOD"), GHairStrandsClusterForceLOD, TEXT("Force a specific hair LOD."));

static int32 GHairStrandsClusterCullingFreezeCamera = 0;
static FAutoConsoleVariableRef CVarHairStrandsClusterCullingFreezeCamera(TEXT("r.HairStrands.Cluster.CullingFreezeCamera"), GHairStrandsClusterCullingFreezeCamera, TEXT("Freeze camera when enabled. It will disable HZB culling because hzb buffer is not frozen."));

bool IsHairStrandsClusterCullingEnable()
{
	// At the moment it is not possible to disable cluster culling, as this pass is in charge of LOD selection, 
	// and preparing the buffer which will be need for the cluster AABB pass (used later on by the voxelization pass)
	return true;
}

bool IsHairStrandsClusterCullingUseHzb()
{
	return GHairStrandsClusterCullingUsesHzb > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairIndBufferClearCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairIndBufferClearCS);
	SHADER_USE_PARAMETER_STRUCT(FHairIndBufferClearCS, FGlobalShader);

	class FSetIndirectDraw : SHADER_PERMUTATION_BOOL("PERMUTATION_SETINDIRECTDRAW");
	using FPermutationDomain = TShaderPermutationDomain<FSetIndirectDraw>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, DispatchIndirectParametersClusterCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, DrawIndirectParameters)
		SHADER_PARAMETER(uint32, VertexCountPerInstance)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLUSTERCULLINGINDCLEAR"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairIndBufferClearCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClusterCullingIndClearCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairClusterCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairClusterCullingCS);
	SHADER_USE_PARAMETER_STRUCT(FHairClusterCullingCS, FGlobalShader);

	class FHZBCulling : SHADER_PERMUTATION_INT("PERMUTATION_HZBCULLING", 2);
	class FDebugAABBBuffer : SHADER_PERMUTATION_INT("PERMUTATION_DEBUGAABBBUFFER", 2);
	using FPermutationDomain = TShaderPermutationDomain<FHZBCulling, FDebugAABBBuffer>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector, CameraWorldPos)
		SHADER_PARAMETER(FMatrix, WorldToClipMatrix)
		SHADER_PARAMETER(FMatrix, ProjectionMatrix)
		SHADER_PARAMETER(uint32, ClusterCount)
		SHADER_PARAMETER(float, LODForcedIndex)
		SHADER_PARAMETER(int32, bIsHairGroupVisible)
		SHADER_PARAMETER(uint32, NumConvexHullPlanes)
		SHADER_PARAMETER(float, LODBias)
		SHADER_PARAMETER_ARRAY(FVector4, ViewFrustumConvexHull, [6])
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ClusterInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ClusterLODInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, GlobalClusterIdBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, GlobalIndexStartBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, GlobalIndexCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, GlobalRadiusScaleBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, ClusterDebugInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, DispatchIndirectParametersClusterCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, DrawIndirectParameters)
		SHADER_PARAMETER(FVector, HZBUvFactor)
		SHADER_PARAMETER(FVector4, HZBSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLUSTERCULLING"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterCullingCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClusterCullingCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FMainClusterCullingPrepareIndirectDrawsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMainClusterCullingPrepareIndirectDrawsCS);
	SHADER_USE_PARAMETER_STRUCT(FMainClusterCullingPrepareIndirectDrawsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DispatchIndirectParametersClusterCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, DispatchIndirectParametersClusterCount2D)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, DispatchIndirectParametersClusterCountDiv512)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, DispatchIndirectParametersClusterCountDiv512Div512)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PREPAREINDIRECTDRAW"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMainClusterCullingPrepareIndirectDrawsCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClusterCullingPrepareIndirectDrawsCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FMainClusterCullingPrepareIndirectDispatchCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMainClusterCullingPrepareIndirectDispatchCS);
	SHADER_USE_PARAMETER_STRUCT(FMainClusterCullingPrepareIndirectDispatchCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, DrawIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, DispatchIndirectBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PREPAREINDIRECTDISPATCH"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMainClusterCullingPrepareIndirectDispatchCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClusterCullingPrepareIndirectDispatchCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairClusterCullingLocalBlockPreFixSumCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairClusterCullingLocalBlockPreFixSumCS);
	SHADER_USE_PARAMETER_STRUCT(FHairClusterCullingLocalBlockPreFixSumCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER(Buffer, DispatchIndirectParametersClusterCountDiv512)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DispatchIndirectParametersClusterCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GlobalIndexCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, PerBlocklIndexCountPreFixSumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, PerBlocklTotalIndexCountBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PERBLOCKPREFIXSUM"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterCullingLocalBlockPreFixSumCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainPerBlockPreFixSumCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairClusterCullingCompactVertexIdsLocalBlockCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairClusterCullingCompactVertexIdsLocalBlockCS);
	SHADER_USE_PARAMETER_STRUCT(FHairClusterCullingCompactVertexIdsLocalBlockCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER(Buffer, DispatchIndirectParametersBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DispatchIndirectParametersClusterCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DispatchIndirectParametersClusterCount2D)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DispatchIndirectParametersClusterCountDiv512)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PerBlocklIndexCountPreFixSumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PerBlocklTotalIndexCountPreFixSumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GlobalIndexStartBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GlobalIndexCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GlobalRadiusScaleBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterVertexIdBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, CulledCompactedIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, CulledCompactedRadiusScaleBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLUSTERCULLINGCOMPACTVERTEXIDLOCALBLOCK"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterCullingCompactVertexIdsLocalBlockCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClusterCullingCompactVertexIdsLocalBlockCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairClusterCullingPreFixSumCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairClusterCullingPreFixSumCS);
	SHADER_USE_PARAMETER_STRUCT(FHairClusterCullingPreFixSumCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER(Buffer, DispatchIndirectParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DispatchIndirectParametersClusterCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GlobalIndexCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, GlobalIndexCountPreFixSumBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLUSTERCULLINGPREFIXSUM"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterCullingPreFixSumCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClusterCullingPreFixSumCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

static FVector CapturedCameraWorldPos;
static FMatrix CapturedWorldToClipMatrix;
static FMatrix CapturedProjMatrix;

struct FHairHZBParameters
{
	// Set from renderer for view culling
	FVector HZBUvFactorValue;
	FVector4 HZBSizeValue;
	TRefCountPtr<IPooledRenderTarget> HZB;
};

bool IsHairStrandsClusterDebugEnable();
bool IsHairStrandsClusterDebugAABBEnable();

static void AddClusterCullingPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FViewInfo& View,
	const FHairCullingParams& CullingParameters,
	const FHairHZBParameters& HZBParameters,
	FHairStrandClusterData::FHairGroup& ClusterData)
{
	FRDGBufferRef DispatchIndirectParametersClusterCount = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairDispatchIndirectParametersClusterCount"));
	FRDGBufferRef DispatchIndirectParametersClusterCount2D = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairDispatchIndirectParametersClusterCount2D"));
	FRDGBufferRef DispatchIndirectParametersClusterCountDiv512 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairDispatchIndirectParametersClusterCountDiv512"));
	FRDGBufferRef DispatchIndirectParametersClusterCountDiv512Div512 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairDispatchIndirectParametersClusterCountDiv512Div512"));
	
	FRDGBufferRef GlobalClusterIdBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ClusterData.ClusterCount), TEXT("HairGlobalClusterIdBuffer"));
	FRDGBufferRef GlobalIndexStartBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ClusterData.ClusterCount), TEXT("HairGlobalIndexStartBuffer"));
	FRDGBufferRef GlobalIndexCountBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ClusterData.ClusterCount), TEXT("HairGlobalIndexCountBuffer"));
	FRDGBufferRef GlobalRadiusScaleBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(float), ClusterData.ClusterCount), TEXT("HairGlobalRadiusScaleBuffer"));

	FRDGBufferRef PerBlocklTotalIndexCountBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ClusterData.ClusterCount), TEXT("PerBlocklTotalIndexCountBuffer"));
	FRDGBufferRef PerBlocklTotalIndexCountPreFixSumBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, ClusterData.ClusterCount), TEXT("PerBlocklTotalIndexCountPreFixSumBuffer"));
	FRDGBufferRef PerBlocklIndexCountPreFixSumBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, ClusterData.ClusterCount), TEXT("PerBlocklIndexCountPreFixSumBuffer"));

	FRDGImportedBuffer DrawIndirectParametersBuffer = Register(GraphBuilder, ClusterData.HairGroupPublicPtr->GetDrawIndirectBuffer(), ERDGImportedBufferFlags::CreateUAV);
	FRDGImportedBuffer DrawIndirectParametersRasterComputeBuffer = Register(GraphBuilder, ClusterData.HairGroupPublicPtr->GetDrawIndirectRasterComputeBuffer(), ERDGImportedBufferFlags::CreateUAV);
	
	bool bClusterDebugAABBBuffer = false;
	bool bClusterDebug = false;
#if WITH_EDITOR
	FRDGBufferRef ClusterDebugInfoBuffer = nullptr;
	{
		// Defined in HairStrandsClusterCommon.ush
		struct FHairClusterDebugInfo
		{
			uint32 GroupIndex;
			float LOD;
			float VertexCount;
			float CurveCount;
		};

		static IConsoleVariable* CVarClusterDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Cluster.Debug"));
		bClusterDebugAABBBuffer = IsHairStrandsClusterDebugAABBEnable();
		bClusterDebug = IsHairStrandsClusterDebugEnable();
		ClusterDebugInfoBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FHairClusterDebugInfo), ClusterData.ClusterCount), TEXT("CulledCompactedIndexBuffer"));
	}
#endif

	/// Initialise indirect buffers to be setup during the culling process
	{
		FHairIndBufferClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairIndBufferClearCS::FParameters>();
		Parameters->DispatchIndirectParametersClusterCount = GraphBuilder.CreateUAV(DispatchIndirectParametersClusterCount);
		Parameters->DrawIndirectParameters = DrawIndirectParametersBuffer.UAV;

		FHairIndBufferClearCS::FPermutationDomain Permutation;
		Permutation.Set<FHairIndBufferClearCS::FSetIndirectDraw>(false);
		TShaderMapRef<FHairIndBufferClearCS> ComputeShader(ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BufferClearCS"),
			ComputeShader,
			Parameters,
			FIntVector(1, 1, 1));
	}

	/// Cull cluster, generate indirect dispatch and prepare data to expand index buffer
	{
		const bool bClusterCullingFrozenCamera = GHairStrandsClusterCullingFreezeCamera > 0;
		if (!bClusterCullingFrozenCamera)
		{
			CapturedCameraWorldPos = View.ViewMatrices.GetViewOrigin();
			CapturedWorldToClipMatrix = View.ViewMatrices.GetViewProjectionMatrix();
			CapturedProjMatrix = View.ViewMatrices.GetProjectionMatrix();
		}

		float ForceLOD = -1;
		bool bIsVisible = true;
		if (GHairStrandsClusterForceLOD >= 0)
		{
			ForceLOD = GHairStrandsClusterForceLOD;
		}
		else if (ClusterData.LODIndex >= 0) // CPU-driven LOD selection
		{
			ForceLOD = ClusterData.LODIndex;
			bIsVisible = ClusterData.bVisible;
		}

		FHairClusterCullingCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairClusterCullingCS::FParameters>();
		Parameters->ProjectionMatrix = CapturedProjMatrix;
		Parameters->CameraWorldPos = CapturedCameraWorldPos;
		Parameters->WorldToClipMatrix = CapturedWorldToClipMatrix;
		Parameters->ClusterCount = ClusterData.ClusterCount;
		Parameters->LODForcedIndex = ForceLOD;
		Parameters->LODBias = ClusterData.LODBias;
		Parameters->bIsHairGroupVisible = bIsVisible ? 1 : 0;

		Parameters->NumConvexHullPlanes = View.ViewFrustum.Planes.Num();
		check(Parameters->NumConvexHullPlanes <= 6);
		for (uint32 i = 0; i < Parameters->NumConvexHullPlanes; ++i)
		{
			Parameters->ViewFrustumConvexHull[i] = View.ViewFrustum.Planes[i];
		}

		Parameters->ClusterAABBBuffer = RegisterAsSRV(GraphBuilder, *ClusterData.ClusterAABBBuffer);
		Parameters->ClusterInfoBuffer = RegisterAsSRV(GraphBuilder, *ClusterData.ClusterInfoBuffer);
		Parameters->ClusterLODInfoBuffer = RegisterAsSRV(GraphBuilder, *ClusterData.ClusterLODInfoBuffer);

		Parameters->GlobalClusterIdBuffer = GraphBuilder.CreateUAV(GlobalClusterIdBuffer, PF_R32_UINT);
		Parameters->GlobalIndexStartBuffer = GraphBuilder.CreateUAV(GlobalIndexStartBuffer, PF_R32_UINT);
		Parameters->GlobalIndexCountBuffer = GraphBuilder.CreateUAV(GlobalIndexCountBuffer, PF_R32_UINT);
		Parameters->GlobalRadiusScaleBuffer = GraphBuilder.CreateUAV(GlobalRadiusScaleBuffer, PF_R32_FLOAT);

		Parameters->DispatchIndirectParametersClusterCount = GraphBuilder.CreateUAV(DispatchIndirectParametersClusterCount);
		Parameters->DrawIndirectParameters = DrawIndirectParametersBuffer.UAV;

#if WITH_EDITOR
		Parameters->ClusterDebugInfoBuffer = GraphBuilder.CreateUAV(ClusterDebugInfoBuffer, PF_R32_SINT);
#endif

		Parameters->HZBUvFactor = HZBParameters.HZBUvFactorValue;
		Parameters->HZBSize = HZBParameters.HZBSizeValue;
		Parameters->HZBTexture = HZBParameters.HZB.IsValid() ? GraphBuilder.RegisterExternalTexture(HZBParameters.HZB, TEXT("HairClusterCullingHZB")) : nullptr;
		Parameters->HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		FHairClusterCullingCS::FPermutationDomain Permutation;
		Permutation.Set<FHairClusterCullingCS::FHZBCulling>((HZBParameters.HZB.IsValid() && !bClusterCullingFrozenCamera && GHairStrandsClusterCullingUsesHzb) ? 1 : 0);
		Permutation.Set<FHairClusterCullingCS::FDebugAABBBuffer>(bClusterDebugAABBBuffer ? 1 : 0);
		TShaderMapRef<FHairClusterCullingCS> ComputeShader(ShaderMap, Permutation);
		const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(ClusterData.ClusterCount, 1, 1), FIntVector(64, 1, 1));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClusterCullingCS"),
			ComputeShader,
			Parameters,
			DispatchCount);
	}

	/// Prepare some indirect draw buffers for specific compute group size
	{
		FMainClusterCullingPrepareIndirectDrawsCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMainClusterCullingPrepareIndirectDrawsCS::FParameters>();
		Parameters->DispatchIndirectParametersClusterCount = GraphBuilder.CreateSRV(DispatchIndirectParametersClusterCount, PF_R32_UINT);
		Parameters->DispatchIndirectParametersClusterCount2D = GraphBuilder.CreateUAV(DispatchIndirectParametersClusterCount2D, PF_R32_UINT);
		Parameters->DispatchIndirectParametersClusterCountDiv512 = GraphBuilder.CreateUAV(DispatchIndirectParametersClusterCountDiv512, PF_R32_UINT);
		Parameters->DispatchIndirectParametersClusterCountDiv512Div512 = GraphBuilder.CreateUAV(DispatchIndirectParametersClusterCountDiv512Div512, PF_R32_UINT);

		TShaderMapRef<FMainClusterCullingPrepareIndirectDrawsCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PrepareIndirectDrawsCS"),
			ComputeShader,
			Parameters,
			FIntVector(2, 1, 1));
	}

	/// local prefix sum per 512 block
	{
		FHairClusterCullingLocalBlockPreFixSumCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairClusterCullingLocalBlockPreFixSumCS::FParameters>();
		Parameters->DispatchIndirectParametersClusterCountDiv512 = DispatchIndirectParametersClusterCountDiv512;
		Parameters->DispatchIndirectParametersClusterCount = GraphBuilder.CreateSRV(DispatchIndirectParametersClusterCount);
		Parameters->GlobalIndexCountBuffer = GraphBuilder.CreateSRV(GlobalIndexCountBuffer, PF_R32_UINT);
		Parameters->PerBlocklIndexCountPreFixSumBuffer = GraphBuilder.CreateUAV(PerBlocklIndexCountPreFixSumBuffer, PF_R32G32_UINT);
		Parameters->PerBlocklTotalIndexCountBuffer = GraphBuilder.CreateUAV(PerBlocklTotalIndexCountBuffer, PF_R32_UINT);

		TShaderMapRef<FHairClusterCullingLocalBlockPreFixSumCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("WithinBlockIndexCountPreFixSumCS"),
			ComputeShader,
			Parameters,
			DispatchIndirectParametersClusterCountDiv512, 0); // FIX ME, this could get over 65535
		check(ClusterData.ClusterCount / 512 <= 65535);
	}

	/// Prefix sum on the total index count per block of 512
	{
		FHairClusterCullingPreFixSumCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairClusterCullingPreFixSumCS::FParameters>();
		Parameters->DispatchIndirectParameters = DispatchIndirectParametersClusterCountDiv512Div512;
		Parameters->DispatchIndirectParametersClusterCount = GraphBuilder.CreateSRV(DispatchIndirectParametersClusterCountDiv512, PF_R32_UINT);
		Parameters->GlobalIndexCountBuffer = GraphBuilder.CreateSRV(PerBlocklTotalIndexCountBuffer, PF_R32_UINT);
		Parameters->GlobalIndexCountPreFixSumBuffer = GraphBuilder.CreateUAV(PerBlocklTotalIndexCountPreFixSumBuffer, PF_R32G32_UINT);

		TShaderMapRef<FHairClusterCullingPreFixSumCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BlockIndexCountPreFixSumCS"),
			ComputeShader,
			Parameters,
			DispatchIndirectParametersClusterCountDiv512Div512, 0); // FIX ME, this could get over 65535
		check(ClusterData.ClusterCount / (512*512) <= 65535);
	}

	/// Compact to VertexId buffer using hierarchical binary search or splatting
	{
		check(ClusterData.GetCulledVertexIdBuffer());
		check(ClusterData.GetCulledVertexRadiusScaleBuffer());
		
		FRDGImportedBuffer ClusterVertexIdBuffer			= Register(GraphBuilder, *ClusterData.ClusterVertexIdBuffer, ERDGImportedBufferFlags::CreateSRV);
		FRDGImportedBuffer CulledCompactedIndexBuffer		= Register(GraphBuilder, *ClusterData.GetCulledVertexIdBuffer(), ERDGImportedBufferFlags::CreateUAV);
		FRDGImportedBuffer CulledCompactedRadiusScaleBuffer	= Register(GraphBuilder, *ClusterData.GetCulledVertexRadiusScaleBuffer(), ERDGImportedBufferFlags::CreateUAV);

		FHairClusterCullingCompactVertexIdsLocalBlockCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairClusterCullingCompactVertexIdsLocalBlockCS::FParameters>();

		Parameters->DispatchIndirectParametersClusterCount = GraphBuilder.CreateSRV(DispatchIndirectParametersClusterCount, PF_R32_UINT);
		Parameters->DispatchIndirectParametersClusterCount2D = GraphBuilder.CreateSRV(DispatchIndirectParametersClusterCount2D, PF_R32_UINT);
		Parameters->DispatchIndirectParametersClusterCountDiv512 = GraphBuilder.CreateSRV(DispatchIndirectParametersClusterCountDiv512, PF_R32_UINT);

		Parameters->PerBlocklIndexCountPreFixSumBuffer = GraphBuilder.CreateSRV(PerBlocklIndexCountPreFixSumBuffer, PF_R32G32_UINT);
		Parameters->PerBlocklTotalIndexCountPreFixSumBuffer = GraphBuilder.CreateSRV(PerBlocklTotalIndexCountPreFixSumBuffer, PF_R32G32_UINT);

		Parameters->GlobalIndexStartBuffer = GraphBuilder.CreateSRV(GlobalIndexStartBuffer, PF_R32_UINT);
		Parameters->GlobalIndexCountBuffer = GraphBuilder.CreateSRV(GlobalIndexCountBuffer, PF_R32_UINT);
		Parameters->GlobalRadiusScaleBuffer = GraphBuilder.CreateSRV(GlobalRadiusScaleBuffer, PF_R32_FLOAT);
		Parameters->ClusterVertexIdBuffer = ClusterVertexIdBuffer.SRV;

		
		Parameters->CulledCompactedIndexBuffer = CulledCompactedIndexBuffer.UAV;
		Parameters->CulledCompactedRadiusScaleBuffer = CulledCompactedRadiusScaleBuffer.UAV;

		Parameters->DispatchIndirectParametersBuffer = DispatchIndirectParametersClusterCount2D;
		TShaderMapRef<FHairClusterCullingCompactVertexIdsLocalBlockCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SplatCompactVertexIdsCS"),
			ComputeShader,
			Parameters,
			DispatchIndirectParametersClusterCount2D, 0); // DispatchIndirectParametersClusterCount2D is used to avoid having any dispatch dimension going above 65535.

		GraphBuilder.SetBufferAccessFinal(CulledCompactedIndexBuffer.Buffer, ERHIAccess::SRVMask);
		GraphBuilder.SetBufferAccessFinal(CulledCompactedRadiusScaleBuffer.Buffer, ERHIAccess::SRVMask);
	}

	{
		ConvertToExternalBuffer(GraphBuilder, GlobalClusterIdBuffer, ClusterData.ClusterIdBuffer);
		ConvertToExternalBuffer(GraphBuilder, GlobalIndexStartBuffer, ClusterData.ClusterIndexOffsetBuffer);
		ConvertToExternalBuffer(GraphBuilder, GlobalIndexCountBuffer, ClusterData.ClusterIndexCountBuffer);
	}

	/// Prepare some indirect dispatch for compute raster visibility buffers
	{
		FMainClusterCullingPrepareIndirectDispatchCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMainClusterCullingPrepareIndirectDispatchCS::FParameters>();
		Parameters->DrawIndirectBuffer = DrawIndirectParametersBuffer.UAV;
		Parameters->DispatchIndirectBuffer = DrawIndirectParametersRasterComputeBuffer.UAV;
		
		TShaderMapRef<FMainClusterCullingPrepareIndirectDispatchCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PrepareIndirectDispatchCS"),
			ComputeShader,
			Parameters,
			FIntVector(1, 1, 1));
	}

	// Should this be move onto the culling result?
#if WITH_EDITOR
	if (bClusterDebugAABBBuffer)
	{
		ConvertToExternalBuffer(GraphBuilder, ClusterDebugInfoBuffer, ClusterData.ClusterDebugInfoBuffer);
	}
	if (bClusterDebug)
	{
		ConvertToExternalBuffer(GraphBuilder, DispatchIndirectParametersClusterCount, ClusterData.CulledDispatchIndirectParametersClusterCount);
	}
#endif

	GraphBuilder.SetBufferAccessFinal(DrawIndirectParametersBuffer.Buffer, ERHIAccess::IndirectArgs | ERHIAccess::SRVMask);
	GraphBuilder.SetBufferAccessFinal(DrawIndirectParametersRasterComputeBuffer.Buffer, ERHIAccess::IndirectArgs | ERHIAccess::SRVMask);

	ClusterData.SetCullingResultAvailable(true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void AddClusterResetLod0(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FHairStrandClusterData::FHairGroup& ClusterData)
{
	// Set as culling result not available
	ClusterData.SetCullingResultAvailable(false);

	FRDGImportedBuffer IndirectBuffer = Register(GraphBuilder, ClusterData.HairGroupPublicPtr->GetDrawIndirectBuffer(), ERDGImportedBufferFlags::CreateUAV);

	// Initialise indirect buffers to entire lod 0 dispatch
	FHairIndBufferClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairIndBufferClearCS::FParameters>();
	Parameters->DrawIndirectParameters = IndirectBuffer.UAV;
	Parameters->VertexCountPerInstance = ClusterData.HairGroupPublicPtr->GetGroupInstanceVertexCount();

	FHairIndBufferClearCS::FPermutationDomain Permutation;
	Permutation.Set<FHairIndBufferClearCS::FSetIndirectDraw>(true);
	TShaderMapRef<FHairIndBufferClearCS> ComputeShader(ShaderMap, Permutation);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BufferClearCS"),
		ComputeShader,
		Parameters,
		FIntVector(1, 1, 1));

	GraphBuilder.SetBufferAccessFinal(IndirectBuffer.Buffer, ERHIAccess::IndirectArgs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void ComputeHairStrandsClustersCulling(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap& ShaderMap,
	const TArray<FViewInfo>& Views,
	const FHairCullingParams& CullingParameters,
	FHairStrandClusterData& ClusterDatas)
{
	DECLARE_GPU_STAT(HairStrandsClusterCulling);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsClusterCulling");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsClusterCulling);

	uint32 ViewCount = Views.Num();
	TArray<const FSceneView*> SceneViews;
	SceneViews.SetNumUninitialized(ViewCount);
	for (uint32 ViewId = 0; ViewId < ViewCount; ++ViewId)
	{
		SceneViews[ViewId] = &Views[ViewId];
	}

	FHairHZBParameters HZBParameters;
	if (ViewCount > 0) // only handling one view for now
	{
		const FViewInfo& ViewInfo = Views[0];
		HZBParameters.HZB = ViewInfo.HZB.IsValid() ? ViewInfo.HZB : nullptr;

		const float kHZBTestMaxMipmap = 9.0f;
		const float HZBMipmapCounts = FMath::Log2(FMath::Max(ViewInfo.HZBMipmap0Size.X, ViewInfo.HZBMipmap0Size.Y));
		const FVector HZBUvFactorValue(
			float(ViewInfo.ViewRect.Width()) / float(2 * ViewInfo.HZBMipmap0Size.X),
			float(ViewInfo.ViewRect.Height()) / float(2 * ViewInfo.HZBMipmap0Size.Y),
			FMath::Max(HZBMipmapCounts - kHZBTestMaxMipmap, 0.0f)
		);
		const FVector4 HZBSizeValue(
			ViewInfo.HZBMipmap0Size.X,
			ViewInfo.HZBMipmap0Size.Y,
			1.0f / float(ViewInfo.HZBMipmap0Size.X),
			1.0f / float(ViewInfo.HZBMipmap0Size.Y)
		);
		HZBParameters.HZBUvFactorValue = HZBUvFactorValue;
		HZBParameters.HZBSizeValue = HZBSizeValue;
	}

	const bool bClusterCulling = IsHairStrandsClusterCullingEnable();
	for (const FViewInfo& View : Views)
	{
		// TODO use compute overlap (will need to split AddClusterCullingPass)
		for (FHairStrandClusterData::FHairGroup& ClusterData : ClusterDatas.HairGroups)
		{
			AddClusterResetLod0(GraphBuilder, &ShaderMap, ClusterData);
		}

		if (bClusterCulling)
		{
			for (FHairStrandClusterData::FHairGroup& ClusterData : ClusterDatas.HairGroups)		
			{
				AddClusterCullingPass(
					GraphBuilder, 
					&ShaderMap, 
					View, 
					CullingParameters,
					HZBParameters,
					ClusterData);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
