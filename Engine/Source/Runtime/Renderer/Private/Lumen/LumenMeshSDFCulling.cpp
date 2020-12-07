// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenMeshSDFCulling.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "DistanceFieldLightingShared.h"
#include "LumenCubeMapTree.h"

int32 GMeshSDFAverageCulledCount = 512;
FAutoConsoleVariableRef CVarMeshSDFAverageCulledCount(
	TEXT("r.Lumen.DiffuseIndirect.MeshSDFAverageCulledCount"),
	GMeshSDFAverageCulledCount,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GMeshSDFRadiusThreshold = 85;
FAutoConsoleVariableRef CVarMeshSDFRadiusThreshold(
	TEXT("r.Lumen.DiffuseIndirect.MeshSDFRadiusThreshold"),
	GMeshSDFRadiusThreshold,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

uint32 CullMeshSDFObjectsForViewGroupSize = 64;

class FCullMeshSDFObjectsForViewCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullMeshSDFObjectsForViewCS)
	SHADER_USE_PARAMETER_STRUCT(FCullMeshSDFObjectsForViewCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectIndirectArguments)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, SceneObjectData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, NumConvexHullPlanes)
		SHADER_PARAMETER_ARRAY(FVector4, ViewFrustumConvexHull, [6])
		SHADER_PARAMETER(uint32, NumSceneObjects)
		SHADER_PARAMETER(uint32, ObjectBoundingGeometryIndexCount)
		SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
		SHADER_PARAMETER(float, MaxMeshSDFInfluenceRadius)
		SHADER_PARAMETER(float, MeshSDFRadiusThreshold)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), CullMeshSDFObjectsForViewGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullMeshSDFObjectsForViewCS, "/Engine/Private/Lumen/LumenMeshSDFCulling.usf", "CullMeshSDFObjectsForViewCS", SF_Compute);


class FMeshSDFObjectCullVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMeshSDFObjectCullVS);
	SHADER_USE_PARAMETER_STRUCT(FMeshSDFObjectCullVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint32>, ObjectIndexBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, ConservativeRadiusScale)
		SHADER_PARAMETER(float, MaxMeshSDFInfluenceRadius)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshSDFObjectCullVS, "/Engine/Private/Lumen/LumenMeshSDFCulling.usf", "MeshSDFObjectCullVS", SF_Vertex);

class FMeshSDFObjectCullPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMeshSDFObjectCullPS);
	SHADER_USE_PARAMETER_STRUCT(FMeshSDFObjectCullPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumGridCulledMeshSDFObjects)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumCulledObjectsToCompact)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledObjectsToCompactArray)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectStartOffsetArray)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, MaxMeshSDFInfluenceRadius)
		SHADER_PARAMETER(FVector, CardGridZParams)
		SHADER_PARAMETER(uint32, CardGridPixelSizeShift)
		SHADER_PARAMETER(FIntVector, CullGridSize)
		SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
		SHADER_PARAMETER_TEXTURE(Texture3D, DistanceFieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DistanceFieldSampler)
		SHADER_PARAMETER(FVector, DistanceFieldAtlasTexelSize)
		SHADER_PARAMETER(uint32, MaxNumberOfCulledObjects)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestHZBTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZBTexture)
		SHADER_PARAMETER(float, HZBMipLevel)
		SHADER_PARAMETER(uint32, HaveClosestHZB)
		SHADER_PARAMETER(FVector2D, ViewportUVToHZBBufferUV)
	END_SHADER_PARAMETER_STRUCT()

	class FCullToFroxelGrid : SHADER_PERMUTATION_BOOL("CULL_TO_FROXEL_GRID");
	
	using FPermutationDomain = TShaderPermutationDomain<FCullToFroxelGrid>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshSDFObjectCullPS, "/Engine/Private/Lumen/LumenMeshSDFCulling.usf", "MeshSDFObjectCullPS", SF_Pixel);

class FMeshSDFObjectCullForProbesPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMeshSDFObjectCullForProbesPS);
	SHADER_USE_PARAMETER_STRUCT(FMeshSDFObjectCullForProbesPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumGridCulledMeshSDFObjects)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumCulledObjectsToCompact)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledObjectsToCompactArray)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectStartOffsetArray)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ProbeListPerEmitTile)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
		SHADER_PARAMETER(float, MaxMeshSDFInfluenceRadius)
		SHADER_PARAMETER(uint32, ProbeHierarchyLevelIndex)
		SHADER_PARAMETER(FIntPoint, EmitTileStorageExtent)
		SHADER_PARAMETER_TEXTURE(Texture3D, DistanceFieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DistanceFieldSampler)
		SHADER_PARAMETER(FVector, DistanceFieldAtlasTexelSize)
		SHADER_PARAMETER(uint32, MaxNumberOfCulledObjects)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshSDFObjectCullForProbesPS, "/Engine/Private/Lumen/LumenMeshSDFCulling.usf", "MeshSDFObjectCullForProbesPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FMeshSDFObjectCull, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FMeshSDFObjectCullVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FMeshSDFObjectCullPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, MeshSDFIndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMeshSDFObjectCullForProbes, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FMeshSDFObjectCullVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FMeshSDFObjectCullForProbesPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, MeshSDFIndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FMeshSDFObjectCompactCulledObjectsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMeshSDFObjectCompactCulledObjectsCS);
	SHADER_USE_PARAMETER_STRUCT(FMeshSDFObjectCompactCulledObjectsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumGridCulledMeshSDFObjects)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWGridCulledMeshSDFObjectIndicesArray)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectStartOffsetArray)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumCulledObjectsToCompact)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledObjectsToCompactArray)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, CompactCulledObjectsIndirectArguments)
		SHADER_PARAMETER(uint32, MaxNumberOfCulledObjects)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

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

IMPLEMENT_GLOBAL_SHADER(FMeshSDFObjectCompactCulledObjectsCS, "/Engine/Private/Lumen/LumenMeshSDFCulling.usf", "MeshSDFObjectCompactCulledObjectsCS", SF_Compute);


uint32 ComputeCulledMeshSDFObjectsStartOffsetGroupSize = 64;

class FComputeCulledMeshSDFObjectsStartOffsetCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeCulledMeshSDFObjectsStartOffsetCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeCulledMeshSDFObjectsStartOffsetCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWGridCulledMeshSDFObjectStartOffsetArray)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledMeshSDFObjectAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactCulledObjectsIndirectArguments)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumGridCulledMeshSDFObjects)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumCulledObjectsToCompact)
		SHADER_PARAMETER(uint32, NumCullGridCells)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ComputeCulledMeshSDFObjectsStartOffsetGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FComputeCulledMeshSDFObjectsStartOffsetCS, "/Engine/Private/Lumen/LumenMeshSDFCulling.usf", "ComputeCulledMeshSDFObjectsStartOffsetCS", SF_Compute);

class FMeshSDFCullingContext
{
public:
	uint32 NumCullGridCells = 0;
	uint32 MaxNumberOfCulledObjects = 0;
	FVector DistanceFieldAtlasTexelSize = FVector(0.0f, 0.0f, 0.0f);

	FRDGBufferRef ObjectIndirectArguments = nullptr;

	// View culled object index buffer
	FRDGBufferRef ObjectIndexBuffer = nullptr;

	FRDGBufferRef NumGridCulledMeshSDFObjects = nullptr;
	FRDGBufferRef GridCulledMeshSDFObjectIndicesArray = nullptr;
	FRDGBufferRef NumCulledObjectsToCompact = nullptr;
	FRDGBufferRef CulledObjectsToCompactArray = nullptr;

	FRDGBufferRef GridCulledMeshSDFObjectStartOffsetArray = nullptr;
};

void InitMeshSDFCullingContext(
	FRDGBuilder& GraphBuilder,
	uint32 NumCullGridCells,
	FMeshSDFCullingContext& Context)
{
	Context.MaxNumberOfCulledObjects = NumCullGridCells * GMeshSDFAverageCulledCount;

	const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
	const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
	const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
	const FVector DistanceFieldAtlasTexelSize(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);

	Context.NumCullGridCells = NumCullGridCells;
	Context.DistanceFieldAtlasTexelSize = DistanceFieldAtlasTexelSize;

	Context.NumGridCulledMeshSDFObjects = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCullGridCells), TEXT("NumGridCulledMeshSDFObjects"));
	Context.GridCulledMeshSDFObjectIndicesArray = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Context.MaxNumberOfCulledObjects), TEXT("GridCulledMeshSDFObjectIndicesArray"));
	Context.NumCulledObjectsToCompact = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("NumCulledObjectsToCompact"));
	Context.CulledObjectsToCompactArray = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2 * Context.MaxNumberOfCulledObjects), TEXT("CulledObjectsToCompactArray"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Context.NumGridCulledMeshSDFObjects, PF_R32_UINT), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Context.NumCulledObjectsToCompact, PF_R32_UINT), 0);
}

void FillGridParameters(
	FRDGBuilder& GraphBuilder, 
	const FScene* Scene,
	const FMeshSDFCullingContext* Context,
	FLumenMeshSDFGridParameters& OutGridParameters)
{
	if (Context)
	{
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

		OutGridParameters.NumGridCulledMeshSDFObjects = GraphBuilder.CreateSRV(Context->NumGridCulledMeshSDFObjects, PF_R32_UINT);
		OutGridParameters.GridCulledMeshSDFObjectStartOffsetArray = GraphBuilder.CreateSRV(Context->GridCulledMeshSDFObjectStartOffsetArray, PF_R32_UINT);
		OutGridParameters.GridCulledMeshSDFObjectIndicesArray = GraphBuilder.CreateSRV(Context->GridCulledMeshSDFObjectIndicesArray, PF_R32_UINT);

		OutGridParameters.TracingParameters.SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
		OutGridParameters.TracingParameters.SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
		OutGridParameters.TracingParameters.NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;

		OutGridParameters.TracingParameters.DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
		OutGridParameters.TracingParameters.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		OutGridParameters.TracingParameters.DistanceFieldAtlasTexelSize = Context->DistanceFieldAtlasTexelSize;
	}
	else
	{
		OutGridParameters.NumGridCulledMeshSDFObjects = nullptr;
		OutGridParameters.GridCulledMeshSDFObjectStartOffsetArray = nullptr;
		OutGridParameters.GridCulledMeshSDFObjectIndicesArray = nullptr;

		OutGridParameters.TracingParameters.SceneObjectBounds = nullptr;
		OutGridParameters.TracingParameters.SceneObjectData = nullptr;
		OutGridParameters.TracingParameters.NumSceneObjects = 0;

		OutGridParameters.TracingParameters.DistanceFieldTexture = nullptr;
		OutGridParameters.TracingParameters.DistanceFieldSampler = nullptr;
		OutGridParameters.TracingParameters.DistanceFieldAtlasTexelSize = FVector::ZeroVector;
	}
}

void CullMeshSDFObjectsForView(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	FMeshSDFCullingContext& Context)
{
	const FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	int32 MaxSDFMeshObjects = FMath::RoundUpToPowerOfTwo(DistanceFieldSceneData.NumObjectsInBuffer);
	MaxSDFMeshObjects = FMath::DivideAndRoundUp(MaxSDFMeshObjects, 128) * 128;
	int32 MaxCubeMapTrees = FMath::RoundUpToPowerOfTwo(LumenSceneData.CubeMapTrees.Num());
	MaxCubeMapTrees = FMath::DivideAndRoundUp(MaxCubeMapTrees, 128) * 128;

	Context.ObjectIndirectArguments = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndexedIndirectParameters>(1), TEXT("CulledObjectIndirectArguments"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Context.ObjectIndirectArguments), 0);

	Context.ObjectIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxSDFMeshObjects), TEXT("ObjectIndices"));

	{
		FCullMeshSDFObjectsForViewCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullMeshSDFObjectsForViewCS::FParameters>();
		PassParameters->RWObjectIndexBuffer = GraphBuilder.CreateUAV(Context.ObjectIndexBuffer, PF_R32_UINT);
		PassParameters->RWObjectIndirectArguments = GraphBuilder.CreateUAV(Context.ObjectIndirectArguments, PF_R32_UINT);
		PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
		PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->NumConvexHullPlanes = View.ViewFrustum.Planes.Num();

		for (int32 i = 0; i < View.ViewFrustum.Planes.Num(); i++)
		{
			PassParameters->ViewFrustumConvexHull[i] = FVector4(View.ViewFrustum.Planes[i], View.ViewFrustum.Planes[i].W);
		}

		PassParameters->NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;
		PassParameters->ObjectBoundingGeometryIndexCount = StencilingGeometry::GLowPolyStencilSphereIndexBuffer.GetIndexCount();
		PassParameters->CardTraceEndDistanceFromCamera = CardTraceEndDistanceFromCamera;
		PassParameters->MaxMeshSDFInfluenceRadius = MaxMeshSDFInfluenceRadius;
		PassParameters->MeshSDFRadiusThreshold = GMeshSDFRadiusThreshold;

		auto ComputeShader = View.ShaderMap->GetShader<FCullMeshSDFObjectsForViewCS>();

		const int32 GroupSize = FMath::DivideAndRoundUp<int32>(DistanceFieldSceneData.NumObjectsInBuffer, CullMeshSDFObjectsForViewGroupSize);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CullMeshSDFObjectsForView"),
			ComputeShader,
			PassParameters,
			FIntVector(GroupSize, 1, 1));
	}
}

// Compact list of {ObjectIndex, GridCellIndex} into a continuos array
void CompactCulledMeshSDFObjectArray(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FMeshSDFCullingContext& Context)
{
	Context.GridCulledMeshSDFObjectStartOffsetArray = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Context.NumCullGridCells), TEXT("GridCulledMeshSDFObjectStartOffsetArray"));

	FRDGBufferRef CulledMeshSDFObjectAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("CulledMeshSDFObjectAllocator"));
	FRDGBufferRef CompactCulledObjectsIndirectArguments = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("CompactCulledObjectsIndirectArguments"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CulledMeshSDFObjectAllocator, PF_R32_UINT), 0);

	{
		FComputeCulledMeshSDFObjectsStartOffsetCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeCulledMeshSDFObjectsStartOffsetCS::FParameters>();
		PassParameters->RWGridCulledMeshSDFObjectStartOffsetArray = GraphBuilder.CreateUAV(Context.GridCulledMeshSDFObjectStartOffsetArray, PF_R32_UINT);
		PassParameters->RWCulledMeshSDFObjectAllocator = GraphBuilder.CreateUAV(CulledMeshSDFObjectAllocator, PF_R32_UINT);
		PassParameters->RWCompactCulledObjectsIndirectArguments = GraphBuilder.CreateUAV(CompactCulledObjectsIndirectArguments, PF_R32_UINT);
		PassParameters->NumGridCulledMeshSDFObjects = GraphBuilder.CreateSRV(Context.NumGridCulledMeshSDFObjects, PF_R32_UINT);
		PassParameters->NumCulledObjectsToCompact = GraphBuilder.CreateSRV(Context.NumCulledObjectsToCompact, PF_R32_UINT);
		PassParameters->NumCullGridCells = Context.NumCullGridCells;

		auto ComputeShader = View.ShaderMap->GetShader<FComputeCulledMeshSDFObjectsStartOffsetCS>();

		int32 GroupSize = FMath::DivideAndRoundUp(Context.NumCullGridCells, ComputeCulledMeshSDFObjectsStartOffsetGroupSize);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComputeCulledMeshSDFObjectsStartOffsetCS"),
			ComputeShader,
			PassParameters,
			FIntVector(GroupSize, 1, 1));
	}

	FRDGBufferUAVRef NumGridCulledMeshSDFObjectsUAV = GraphBuilder.CreateUAV(Context.NumGridCulledMeshSDFObjects, PF_R32_UINT);

	AddClearUAVPass(GraphBuilder, NumGridCulledMeshSDFObjectsUAV, 0);

	{
		FMeshSDFObjectCompactCulledObjectsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMeshSDFObjectCompactCulledObjectsCS::FParameters>();

		PassParameters->RWNumGridCulledMeshSDFObjects = NumGridCulledMeshSDFObjectsUAV;
		PassParameters->RWGridCulledMeshSDFObjectIndicesArray = GraphBuilder.CreateUAV(Context.GridCulledMeshSDFObjectIndicesArray, PF_R32_UINT);
		PassParameters->GridCulledMeshSDFObjectStartOffsetArray = GraphBuilder.CreateSRV(Context.GridCulledMeshSDFObjectStartOffsetArray, PF_R32_UINT);
		PassParameters->NumCulledObjectsToCompact = GraphBuilder.CreateSRV(Context.NumCulledObjectsToCompact, PF_R32_UINT);
		PassParameters->CulledObjectsToCompactArray = GraphBuilder.CreateSRV(Context.CulledObjectsToCompactArray, PF_R32_UINT);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->CompactCulledObjectsIndirectArguments = CompactCulledObjectsIndirectArguments;
		PassParameters->MaxNumberOfCulledObjects = Context.MaxNumberOfCulledObjects;

		auto ComputeShader = View.ShaderMap->GetShader<FMeshSDFObjectCompactCulledObjectsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CompactCulledObjects"),
			ComputeShader,
			PassParameters,
			CompactCulledObjectsIndirectArguments,
			0);
	}
}

void CullMeshSDFObjectsToProbes(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	const LumenProbeHierarchy::FHierarchyParameters& ProbeHierarchyParameters,
	const LumenProbeHierarchy::FEmitProbeParameters& EmitProbeParameters,
	FLumenMeshSDFGridParameters& OutGridParameters)
{
	RDG_EVENT_SCOPE(GraphBuilder, "MeshSDFCullingToProbes");

	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	FMeshSDFCullingContext Context;

	InitMeshSDFCullingContext(
		GraphBuilder,
		EmitProbeParameters.MaxProbeCount,
		Context);

	CullMeshSDFObjectsForView(
		GraphBuilder,
		Scene,
		View,
		MaxMeshSDFInfluenceRadius,
		CardTraceEndDistanceFromCamera,
		Context);

	// Scatter mesh SDF objects into a temporary array of {ObjectIndex, ProbeIndex}
	{
		FRDGBufferUAVRef NumGridCulledMeshSDFObjectsUAV = GraphBuilder.CreateUAV(Context.NumGridCulledMeshSDFObjects, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef NumCulledObjectsToCompactUAV = GraphBuilder.CreateUAV(Context.NumCulledObjectsToCompact, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef CulledObjectsToCompactArrayUAV = GraphBuilder.CreateUAV(Context.CulledObjectsToCompactArray, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);

		for (int32 ProbeHierarchyLevelIndex = 0; ProbeHierarchyLevelIndex < ProbeHierarchyParameters.HierarchyDepth; ++ProbeHierarchyLevelIndex)
		{
			FIntPoint ProbeTileCount = EmitProbeParameters.ProbeTileCount[ProbeHierarchyLevelIndex];

			FMeshSDFObjectCullForProbes* PassParameters = GraphBuilder.AllocParameters<FMeshSDFObjectCullForProbes>();

			PassParameters->VS.SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
			PassParameters->VS.SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
			PassParameters->VS.ObjectIndexBuffer = GraphBuilder.CreateSRV(Context.ObjectIndexBuffer, PF_R32_UINT);
			PassParameters->VS.View = GetShaderBinding(View.ViewUniformBuffer);

			// Boost the effective radius so that the edges of the sphere approximation lie on the sphere, instead of the vertices
			const int32 NumRings = StencilingGeometry::GLowPolyStencilSphereVertexBuffer.GetNumRings();
			const float RadiansPerRingSegment = PI / (float)NumRings;
			PassParameters->VS.ConservativeRadiusScale = 1.0f / FMath::Cos(RadiansPerRingSegment);
			PassParameters->VS.MaxMeshSDFInfluenceRadius = MaxMeshSDFInfluenceRadius;

			PassParameters->PS.RWNumGridCulledMeshSDFObjects = NumGridCulledMeshSDFObjectsUAV;
			PassParameters->PS.RWNumCulledObjectsToCompact = NumCulledObjectsToCompactUAV;
			PassParameters->PS.RWCulledObjectsToCompactArray = CulledObjectsToCompactArrayUAV;
			PassParameters->PS.SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
			PassParameters->PS.View = GetShaderBinding(View.ViewUniformBuffer);
			PassParameters->PS.MaxMeshSDFInfluenceRadius = MaxMeshSDFInfluenceRadius;
			PassParameters->PS.CardTraceEndDistanceFromCamera = CardTraceEndDistanceFromCamera;
			PassParameters->PS.DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			PassParameters->PS.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->PS.DistanceFieldAtlasTexelSize = Context.DistanceFieldAtlasTexelSize;
			PassParameters->PS.HierarchyParameters = ProbeHierarchyParameters;
			PassParameters->PS.ProbeHierarchyLevelIndex = ProbeHierarchyLevelIndex;
			PassParameters->PS.EmitTileStorageExtent = EmitProbeParameters.EmitTileStorageExtent;
			PassParameters->PS.ProbeListPerEmitTile = EmitProbeParameters.ProbeListsPerEmitTile[ProbeHierarchyLevelIndex];
			PassParameters->PS.MaxNumberOfCulledObjects = Context.MaxNumberOfCulledObjects;

			PassParameters->MeshSDFIndirectArgs = Context.ObjectIndirectArguments;

			auto VertexShader = View.ShaderMap->GetShader<FMeshSDFObjectCullVS>();
			auto PixelShader = View.ShaderMap->GetShader<FMeshSDFObjectCullForProbesPS>();
			const bool bReverseCulling = View.bReverseCulling;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ScatterSDFObjectsToProbes (level=%d)", ProbeHierarchyLevelIndex),
				PassParameters,
				ERDGPassFlags::Raster,
				[ProbeTileCount, bReverseCulling, VertexShader, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					RHICmdList.SetViewport(0, 0, 0.0f, ProbeTileCount.X, ProbeTileCount.Y, 1.0f);

					// Render backfaces since camera may intersect
					GraphicsPSOInit.RasterizerState = bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

					RHICmdList.SetStreamSource(0, StencilingGeometry::GLowPolyStencilSphereVertexBuffer.VertexBufferRHI, 0);

					RHICmdList.DrawIndexedPrimitiveIndirect(
						StencilingGeometry::GLowPolyStencilSphereIndexBuffer.IndexBufferRHI,
						PassParameters->MeshSDFIndirectArgs->GetIndirectRHICallBuffer(),
						0);
				});
		}
	}

	CompactCulledMeshSDFObjectArray(
		GraphBuilder,
		View,
		Context);

	FillGridParameters(
		GraphBuilder,
		Scene,
		&Context,
		OutGridParameters);
}

void CullMeshSDFObjectsToViewGrid(
	const FViewInfo& View,
	const FScene* Scene,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	int32 GridPixelsPerCellXY,
	int32 GridSizeZ,
	FVector ZParams,
	FRDGBuilder& GraphBuilder,
	FLumenMeshSDFGridParameters& OutGridParameters)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (DistanceFieldSceneData.NumObjectsInBuffer > 0)
	{
		const FIntPoint CardGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GridPixelsPerCellXY);
		const FIntVector CullGridSize(CardGridSizeXY.X, CardGridSizeXY.Y, GridSizeZ);
		const uint32 NumCullGridCells = CullGridSize.X * CullGridSize.Y * CullGridSize.Z;

		uint32 MaxCullGridCells;

		{
			// Allocate buffers using scene render targets size so we won't reallocate every frame with dynamic resolution
			const FIntPoint BufferSize = FSceneRenderTargets::Get().GetBufferSizeXY();
			const FIntPoint MaxCardGridSizeXY = FIntPoint::DivideAndRoundUp(BufferSize, GridPixelsPerCellXY);
			MaxCullGridCells = MaxCardGridSizeXY.X * MaxCardGridSizeXY.Y * GridSizeZ;
			ensure(MaxCullGridCells >= NumCullGridCells);
		}

		RDG_EVENT_SCOPE(GraphBuilder, "MeshSDFCulling %ux%ux%u cells", CullGridSize.X, CullGridSize.Y, CullGridSize.Z);

		FMeshSDFCullingContext Context;

		InitMeshSDFCullingContext(
			GraphBuilder,
			MaxCullGridCells,
			Context);

		CullMeshSDFObjectsForView(
			GraphBuilder,
			Scene,
			View,
			MaxMeshSDFInfluenceRadius,
			CardTraceEndDistanceFromCamera,
			Context);

		// Scatter mesh SDF objects into a temporary array of {ObjectIndex, GridCellIndex}
		{
			FMeshSDFObjectCull* PassParameters = GraphBuilder.AllocParameters<FMeshSDFObjectCull>();

			PassParameters->VS.SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
			PassParameters->VS.SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
			PassParameters->VS.ObjectIndexBuffer = GraphBuilder.CreateSRV(Context.ObjectIndexBuffer, PF_R32_UINT);
			PassParameters->VS.View = GetShaderBinding(View.ViewUniformBuffer);

			// Boost the effective radius so that the edges of the sphere approximation lie on the sphere, instead of the vertices
			const int32 NumRings = StencilingGeometry::GLowPolyStencilSphereVertexBuffer.GetNumRings();
			const float RadiansPerRingSegment = PI / (float)NumRings;
			PassParameters->VS.ConservativeRadiusScale = 1.0f / FMath::Cos(RadiansPerRingSegment);
			PassParameters->VS.MaxMeshSDFInfluenceRadius = MaxMeshSDFInfluenceRadius;

			PassParameters->PS.RWNumGridCulledMeshSDFObjects = GraphBuilder.CreateUAV(Context.NumGridCulledMeshSDFObjects, PF_R32_UINT);
			PassParameters->PS.RWNumCulledObjectsToCompact = GraphBuilder.CreateUAV(Context.NumCulledObjectsToCompact, PF_R32_UINT);
			PassParameters->PS.RWCulledObjectsToCompactArray = GraphBuilder.CreateUAV(Context.CulledObjectsToCompactArray, PF_R32_UINT);
			PassParameters->PS.SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
			PassParameters->PS.View = GetShaderBinding(View.ViewUniformBuffer);
			PassParameters->PS.MaxMeshSDFInfluenceRadius = MaxMeshSDFInfluenceRadius;
			PassParameters->PS.CardGridZParams = ZParams;
			PassParameters->PS.CardGridPixelSizeShift = FMath::FloorLog2(GridPixelsPerCellXY);
			PassParameters->PS.CullGridSize = CullGridSize;
			PassParameters->PS.CardTraceEndDistanceFromCamera = CardTraceEndDistanceFromCamera;
			PassParameters->PS.DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			PassParameters->PS.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->PS.DistanceFieldAtlasTexelSize = Context.DistanceFieldAtlasTexelSize;
			PassParameters->PS.MaxNumberOfCulledObjects = Context.MaxNumberOfCulledObjects;
			PassParameters->PS.ClosestHZBTexture = View.ClosestHZB ? View.ClosestHZB : GSystemTextures.GetBlackDummy(GraphBuilder);
			PassParameters->PS.FurthestHZBTexture = View.HZB;
			PassParameters->PS.HZBMipLevel = FMath::Max<float>((int32)FMath::FloorLog2(GridPixelsPerCellXY) - 1, 0.0f);
			PassParameters->PS.HaveClosestHZB = View.ClosestHZB ? 1 : 0;
			PassParameters->PS.ViewportUVToHZBBufferUV = FVector2D(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y)
			);

			PassParameters->MeshSDFIndirectArgs = Context.ObjectIndirectArguments;

			auto VertexShader = View.ShaderMap->GetShader< FMeshSDFObjectCullVS >();
			FMeshSDFObjectCullPS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FMeshSDFObjectCullPS::FCullToFroxelGrid >(GridSizeZ > 1);
			auto PixelShader = View.ShaderMap->GetShader<FMeshSDFObjectCullPS>(PermutationVector);
			const bool bReverseCulling = View.bReverseCulling;

			ClearUnusedGraphResources(VertexShader, &PassParameters->VS);
			ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ScatterMeshSDFsToGrid"),
				PassParameters,
				ERDGPassFlags::Raster,
				[CullGridSize, bReverseCulling, VertexShader, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				RHICmdList.SetViewport(0, 0, 0.0f, CullGridSize.X, CullGridSize.Y, 1.0f);

				// Render backfaces since camera may intersect
				GraphicsPSOInit.RasterizerState = bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

				RHICmdList.SetStreamSource(0, StencilingGeometry::GLowPolyStencilSphereVertexBuffer.VertexBufferRHI, 0);

				RHICmdList.DrawIndexedPrimitiveIndirect(
					StencilingGeometry::GLowPolyStencilSphereIndexBuffer.IndexBufferRHI,
					PassParameters->MeshSDFIndirectArgs->GetIndirectRHICallBuffer(),
					0);
			});
		}

		CompactCulledMeshSDFObjectArray(
			GraphBuilder,
			View,
			Context);

		FillGridParameters(
			GraphBuilder,
			Scene,
			&Context,
			OutGridParameters);
	}
	else
	{
		FillGridParameters(
			GraphBuilder,
			Scene,
			nullptr,
			OutGridParameters);
	}
}
