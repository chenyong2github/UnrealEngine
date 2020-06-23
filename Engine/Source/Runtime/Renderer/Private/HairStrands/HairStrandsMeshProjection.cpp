// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsMeshProjection.h"
#include "MeshMaterialShader.h"
#include "ScenePrivate.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "MeshPassProcessor.h"
#include "RenderGraphUtils.h"

static int32 GHairProjectionMaxTrianglePerProjectionIteration = 8;
static FAutoConsoleVariableRef CVarHairProjectionMaxTrianglePerProjectionIteration(TEXT("r.HairStrands.Projection.MaxTrianglePerIteration"), GHairProjectionMaxTrianglePerProjectionIteration, TEXT("Change the number of triangles which are iterated over during one projection iteration step. In kilo triangle (e.g., 8 == 8000 triangles). Default is 8."));

///////////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_HAIRSTRANDS_SECTION_COUNT 20
#define MAX_HAIRSTRANDS_SECTION_BITOFFSET 26

uint32 GetHairStrandsMaxSectionCount()
{
	return MAX_HAIRSTRANDS_SECTION_COUNT;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FMarkMeshSectionIdCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkMeshSectionIdCS);
	SHADER_USE_PARAMETER_STRUCT(FMarkMeshSectionIdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MeshSectionId)
		SHADER_PARAMETER(uint32, MeshSectionPrimitiveCount)
		SHADER_PARAMETER(uint32, MeshMaxIndexCount)
		SHADER_PARAMETER(uint32, MeshMaxVertexCount)
		SHADER_PARAMETER(uint32, MeshIndexOffset)
		SHADER_PARAMETER_SRV(Buffer<uint>, MeshIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint32>, OutVertexSectionId)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_SECTIONID"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMarkMeshSectionIdCS, "/Engine/Private/HairStrands/HairStrandsMeshProjection.usf", "MainMarkSectionIdCS", SF_Compute);

static FRDGBufferRef AddMeshSectionId(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FHairStrandsProjectionMeshData::LOD& MeshData)
{
	const int32 SectionCount = MeshData.Sections.Num();
	if (SectionCount < 0)
		return nullptr;

	// Initialized the section ID to a large number, as the shader will do an atomic min on the section ID.
	FRDGBufferRef VertexSectionIdBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MeshData.Sections[0].TotalVertexCount), TEXT("SectionId"));
	FRDGBufferUAVRef VertexSectionIdBufferUAV = GraphBuilder.CreateUAV(VertexSectionIdBuffer, PF_R32_UINT);
	AddClearUAVPass(GraphBuilder, VertexSectionIdBufferUAV, ~0u);
	for (const FHairStrandsProjectionMeshData::Section& MeshSection : MeshData.Sections)
	{	
		FMarkMeshSectionIdCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMarkMeshSectionIdCS::FParameters>();
		Parameters->MeshSectionId				= MeshSection.SectionIndex;
		Parameters->MeshSectionPrimitiveCount	= MeshSection.NumPrimitives;
		Parameters->MeshMaxIndexCount			= MeshSection.TotalIndexCount;
		Parameters->MeshMaxVertexCount			= MeshSection.TotalVertexCount;
		Parameters->MeshIndexOffset				= MeshSection.IndexBaseIndex;
		Parameters->MeshIndexBuffer				= MeshSection.IndexBuffer;
		Parameters->OutVertexSectionId			= VertexSectionIdBufferUAV;

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(MeshSection.NumPrimitives*3, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FMarkMeshSectionIdCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsMarkVertexSectionId"),
			ComputeShader,
			Parameters,
			DispatchGroupCount);
	}

	return VertexSectionIdBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FMeshTransferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMeshTransferCS);
	SHADER_USE_PARAMETER_STRUCT(FMeshTransferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, bNeedClear)
		SHADER_PARAMETER(uint32, Source_MeshPrimitiveCount_Iteration)
		SHADER_PARAMETER(uint32, Source_MeshMaxIndexCount)
		SHADER_PARAMETER(uint32, Source_MeshMaxVertexCount)
		SHADER_PARAMETER(uint32, Source_MeshIndexOffset)
		SHADER_PARAMETER(uint32, Source_MeshUVsChannelOffset)
		SHADER_PARAMETER(uint32, Source_MeshUVsChannelCount)
		SHADER_PARAMETER(uint32, Target_MeshMaxVertexCount)
		SHADER_PARAMETER(uint32, Target_MeshUVsChannelOffset)
		SHADER_PARAMETER(uint32, Target_MeshUVsChannelCount)
		SHADER_PARAMETER(uint32, Target_SectionId)
		SHADER_PARAMETER_SRV(Buffer<uint>, Source_MeshIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer<float>, Source_MeshPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer<float2>, Source_MeshUVsBuffer)
		SHADER_PARAMETER_SRV(Buffer<float2>, Target_MeshUVsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Target_VertexSectionId)
		SHADER_PARAMETER_UAV(RWBuffer<float>, Target_MeshPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint32>, OutDistanceBuffer)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MESHTRANSFER"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshTransferCS, "/Engine/Private/HairStrands/HairStrandsMeshProjection.usf", "MainMeshTransferCS", SF_Compute);

static void AddMeshTransferPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	bool bClear,
	const FHairStrandsProjectionMeshData::Section& SourceSectionData,
	const FHairStrandsProjectionMeshData::Section& TargetSectionData,
	FRDGBufferRef VertexSectionId, 
	FRWBuffer& OutTargetRestPosition,
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (!SourceSectionData.IndexBuffer ||
		!SourceSectionData.PositionBuffer ||
		 SourceSectionData.TotalIndexCount == 0 ||
		 SourceSectionData.TotalVertexCount == 0||

		!TargetSectionData.IndexBuffer ||
		!TargetSectionData.PositionBuffer ||
		 TargetSectionData.TotalIndexCount == 0 ||
		 TargetSectionData.TotalVertexCount == 0)
	{
		return;
	}
	
	FRDGBufferRef PositionDistanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TargetSectionData.TotalVertexCount), TEXT("DistanceBuffer"));
	FRDGBufferUAVRef PositionDistanceBufferUAV = GraphBuilder.CreateUAV(PositionDistanceBuffer, PF_R32_UINT);

	// For projecting hair onto a skeletal mesh, 1 thread is spawn for each hair which iterates over all triangles.
	// To avoid TDR, we split projection into multiple passes when the mesh is too large.
	uint32 MeshPassNumPrimitive = 1024 * FMath::Clamp(GHairProjectionMaxTrianglePerProjectionIteration, 1, 256);
	uint32 MeshPassCount = 1;
	if (SourceSectionData.NumPrimitives < MeshPassNumPrimitive)
	{
		MeshPassNumPrimitive = SourceSectionData.NumPrimitives;
	}
	else
	{
		MeshPassCount = FMath::CeilToInt(SourceSectionData.NumPrimitives / float(MeshPassNumPrimitive));
	}

	FRDGBufferSRVRef VertexSectionIdSRV = GraphBuilder.CreateSRV(VertexSectionId, PF_R32_UINT);
	for (uint32 MeshPassIt = 0; MeshPassIt < MeshPassCount; ++MeshPassIt)
	{
		FMeshTransferCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMeshTransferCS::FParameters>();
		Parameters->bNeedClear = bClear ? 1 : 0;

		Parameters->Source_MeshPrimitiveCount_Iteration = (MeshPassIt < MeshPassCount - 1) ? MeshPassNumPrimitive : (SourceSectionData.NumPrimitives - MeshPassNumPrimitive * MeshPassIt);
		Parameters->Source_MeshMaxIndexCount		= SourceSectionData.TotalIndexCount;
		Parameters->Source_MeshMaxVertexCount		= SourceSectionData.TotalVertexCount;
		Parameters->Source_MeshIndexOffset			= SourceSectionData.IndexBaseIndex + (MeshPassNumPrimitive * MeshPassIt * 3);
		Parameters->Source_MeshUVsChannelOffset		= SourceSectionData.UVsChannelOffset;
		Parameters->Source_MeshUVsChannelCount		= SourceSectionData.UVsChannelCount;
		Parameters->Source_MeshIndexBuffer			= SourceSectionData.IndexBuffer;
		Parameters->Source_MeshPositionBuffer		= SourceSectionData.PositionBuffer;
		Parameters->Source_MeshUVsBuffer			= SourceSectionData.UVsBuffer;

		Parameters->Target_MeshMaxVertexCount		= TargetSectionData.TotalVertexCount;
		Parameters->Target_MeshUVsChannelOffset		= TargetSectionData.UVsChannelOffset;
		Parameters->Target_MeshUVsChannelCount		= TargetSectionData.UVsChannelCount;
		Parameters->Target_MeshUVsBuffer			= TargetSectionData.UVsBuffer;
		Parameters->Target_MeshPositionBuffer		= OutTargetRestPosition.UAV;
		Parameters->Target_VertexSectionId			= VertexSectionIdSRV;
		Parameters->Target_SectionId				= TargetSectionData.SectionIndex;

		Parameters->OutDistanceBuffer				= PositionDistanceBufferUAV;

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(TargetSectionData.TotalVertexCount, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FMeshTransferCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsTransferMesh"),
			ComputeShader,
			Parameters,
			DispatchGroupCount);
		bClear = false;

		OutTransitionQueue.Add(OutTargetRestPosition.UAV);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairMeshProjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairMeshProjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FHairMeshProjectionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, bClear)
		SHADER_PARAMETER(uint32, MaxRootCount)

		SHADER_PARAMETER(uint32, MeshPrimitiveOffset_Iteration)
		SHADER_PARAMETER(uint32, MeshPrimitiveCount_Iteration)
		SHADER_PARAMETER(uint32, MeshSectionIndex)
		SHADER_PARAMETER(uint32, MeshMaxIndexCount)
		SHADER_PARAMETER(uint32, MeshMaxVertexCount)
		SHADER_PARAMETER(uint32, MeshIndexOffset)

		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer)

		SHADER_PARAMETER_SRV(Buffer, RootPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, RootNormalBuffer)

		SHADER_PARAMETER_UAV(RWBuffer, OutRootTriangleIndex)
		SHADER_PARAMETER_UAV(RWBuffer, OutRootTriangleBarycentrics)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutRootTriangleDistance)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PROJECTION"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairMeshProjectionCS, "/Engine/Private/HairStrands/HairStrandsMeshProjection.usf", "MainCS", SF_Compute);

static void AddHairStrandMeshProjectionPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const bool bClear,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::Section& MeshSectionData,
	const FHairStrandsProjectionHairData::HairGroup& RootData,
	FRDGBufferRef RootDistanceBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (!RootData.RootPositionBuffer ||
		!RootData.RootNormalBuffer ||
		LODIndex < 0 || LODIndex >= RootData.RestLODDatas.Num() ||
		!RootData.RestLODDatas[LODIndex].RootTriangleIndexBuffer ||
		!RootData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer ||
		!MeshSectionData.IndexBuffer ||
		!MeshSectionData.PositionBuffer ||
		MeshSectionData.TotalIndexCount == 0 ||
		MeshSectionData.TotalVertexCount == 0)
	{
		return;
	}

	// The current shader code HairStrandsMeshProjection.usf encode the section ID onto the highest 8bits of a 32bits uint. 
	// This limits the number of section to 64. See EncodeTriangleIndex & DecodeTriangleIndex functions in 
	// HairStarndsMeshProjectionCommon.ush for mode details.
	// This means that the mesh needs to have less than 67M triangles (since triangle ID is stored onto 26bits).
	//
	// This could be increase if necessary.
	check(MeshSectionData.SectionIndex < MAX_HAIRSTRANDS_SECTION_COUNT);
	check(MeshSectionData.NumPrimitives < ((1<<MAX_HAIRSTRANDS_SECTION_BITOFFSET)-1))

	// For projecting hair onto a skeletal mesh, 1 thread is spawn for each hair which iterates over all triangles.
	// To avoid TDR, we split projection into multiple passes when the mesh is too large.
	uint32 MeshPassNumPrimitive = 1024 * FMath::Clamp(GHairProjectionMaxTrianglePerProjectionIteration, 1, 256);
	uint32 MeshPassCount = 1;
	if (MeshSectionData.NumPrimitives < MeshPassNumPrimitive)
	{
		MeshPassNumPrimitive = MeshSectionData.NumPrimitives;
	}
	else
	{
		MeshPassCount = FMath::CeilToInt(MeshSectionData.NumPrimitives / float(MeshPassNumPrimitive));
	}

	FRDGBufferUAVRef DistanceUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RootDistanceBuffer, PF_R32_FLOAT));
	for (uint32 MeshPassIt=0;MeshPassIt<MeshPassCount;++MeshPassIt)
	{
		FHairMeshProjectionCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairMeshProjectionCS::FParameters>();
		Parameters->bClear				= bClear && MeshPassIt == 0 ? 1 : 0;
		Parameters->MaxRootCount		= RootData.RootCount;
		Parameters->RootPositionBuffer	= RootData.RootPositionBuffer;
		Parameters->RootNormalBuffer	= RootData.RootNormalBuffer;
		Parameters->MeshSectionIndex	= MeshSectionData.SectionIndex;
		Parameters->MeshMaxIndexCount	= MeshSectionData.TotalIndexCount;
		Parameters->MeshMaxVertexCount	= MeshSectionData.TotalVertexCount;
		Parameters->MeshIndexOffset		= MeshSectionData.IndexBaseIndex + (MeshPassNumPrimitive * MeshPassIt * 3);
		Parameters->MeshIndexBuffer		= MeshSectionData.IndexBuffer;
		Parameters->MeshPositionBuffer	= MeshSectionData.PositionBuffer;
		Parameters->MeshPrimitiveOffset_Iteration	= MeshPassNumPrimitive * MeshPassIt;
		Parameters->MeshPrimitiveCount_Iteration	= (MeshPassIt < MeshPassCount-1) ? MeshPassNumPrimitive : (MeshSectionData.NumPrimitives - MeshPassNumPrimitive * MeshPassIt);

		// The projection is always done onto the source/rest mesh
		Parameters->OutRootTriangleIndex		= RootData.RestLODDatas[LODIndex].RootTriangleIndexBuffer->UAV;
		Parameters->OutRootTriangleBarycentrics = RootData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer->UAV;
		Parameters->OutRootTriangleDistance		= DistanceUAV;

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootData.RootCount, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FHairMeshProjectionCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsMeshProjection"),
			ComputeShader,
			Parameters,
			DispatchGroupCount);

		OutTransitionQueue.Add(RootData.RestLODDatas[LODIndex].RootTriangleIndexBuffer->UAV);
		OutTransitionQueue.Add(RootData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer->UAV);
	}
}

void ProjectHairStrandsOntoMesh(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData, 
	FBufferTransitionQueue& TransitionQueue)
{
	if (LODIndex < 0 || LODIndex >= ProjectionHairData.RestLODDatas.Num())
		return;

	FRDGBufferRef RootDistanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), ProjectionHairData.RootCount), TEXT("HairStrandsTriangleDistance"));

	bool ClearDistance = true;
	for (const FHairStrandsProjectionMeshData::Section& MeshSection : ProjectionMeshData.LODs[LODIndex].Sections)
	{
		check(ProjectionHairData.RestLODDatas[LODIndex].LODIndex == LODIndex);
		AddHairStrandMeshProjectionPass(GraphBuilder, ShaderMap, ClearDistance, LODIndex, MeshSection, ProjectionHairData, RootDistanceBuffer, TransitionQueue);
		check(ProjectionHairData.RestLODDatas[LODIndex].Status);
		*ProjectionHairData.RestLODDatas[LODIndex].Status = FHairStrandsProjectionHairData::EStatus::Completed;
		ClearDistance = false;
	}
}

void TransferMesh(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& SourceMeshData,
	const FHairStrandsProjectionMeshData& TargetMeshData,
	FRWBuffer& OutPositionBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (LODIndex < 0)
		return;

	// LODs are transfered using the LOD0 of the source mesh, as the LOD count can mismatch between source and target meshes.
	const int32 SourceLODIndex = 0;
	const int32 TargetLODIndex = LODIndex;

	// Assume that the section 0 contains the head section, which is where the hair/facial hair should be projected on
	const uint32 SourceSectionIndex = 0;
	const uint32 TargetSectionIndex = 0;

	const int32 SectionCount = TargetMeshData.LODs[TargetLODIndex].Sections.Num();
	if (SectionCount < 0)
		return;

	FRDGBufferRef VertexSectionId = AddMeshSectionId(GraphBuilder, ShaderMap, TargetMeshData.LODs[TargetLODIndex]);
	const FHairStrandsProjectionMeshData::Section& SourceMeshSection = SourceMeshData.LODs[SourceLODIndex].Sections[SourceSectionIndex];
	const FHairStrandsProjectionMeshData::Section& TargetMeshSection = TargetMeshData.LODs[TargetLODIndex].Sections[TargetSectionIndex];
	AddMeshTransferPass(GraphBuilder, ShaderMap, true, SourceMeshSection, TargetMeshSection, VertexSectionId, OutPositionBuffer, OutTransitionQueue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairUpdateMeshTriangleCS : public FGlobalShader
{
public:
	const static uint32 SectionArrayCount = MAX_HAIRSTRANDS_SECTION_COUNT;
private:
	DECLARE_GLOBAL_SHADER(FHairUpdateMeshTriangleCS);
	SHADER_USE_PARAMETER_STRUCT(FHairUpdateMeshTriangleCS, FGlobalShader);

	class FUpdateUVs : SHADER_PERMUTATION_INT("PERMUTATION_WITHUV", 2);
	using FPermutationDomain = TShaderPermutationDomain<FUpdateUVs>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxRootCount)
		SHADER_PARAMETER(uint32, MaxSectionCount)
		
		SHADER_PARAMETER_ARRAY(uint32, MeshSectionIndex, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshMaxIndexCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshMaxVertexCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshIndexOffset, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshUVsChannelOffset, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshUVsChannelCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshSectionBufferIndex, [SectionArrayCount])

		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer0)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer1)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer2)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer3)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer4)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer5)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer6)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer7)

		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer0)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer1)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer2)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer3)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer4)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer5)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer6)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer7)
		
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer0)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer1)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer2)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer3)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer4)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer5)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer6)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer7)

		SHADER_PARAMETER_SRV(Buffer, RootTriangleIndex)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutRootTrianglePosition0)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutRootTrianglePosition1)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutRootTrianglePosition2)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_SECTION_COUNT"), SectionArrayCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairUpdateMeshTriangleCS, "/Engine/Private/HairStrands/HairStrandsMeshUpdate.usf", "MainCS", SF_Compute);

static void AddHairStrandUpdateMeshTrianglesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsProjectionHairData::HairGroup& RootData, 
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (RootData.RootCount == 0 || LODIndex < 0)
	{
		return;
	}

	if (Type == HairStrandsTriangleType::RestPose && LODIndex >= RootData.RestLODDatas.Num())
	{
		return;
	}

	if (Type == HairStrandsTriangleType::DeformedPose && (LODIndex >= RootData.DeformedLODDatas.Num() || LODIndex >= RootData.RestLODDatas.Num()))
	{
		return;
	}

	FHairStrandsProjectionHairData::RestLODData& RestLODData = RootData.RestLODDatas[LODIndex];
	check(RestLODData.LODIndex == LODIndex);

	const int32 SectionCount = MeshData.Sections.Num();
	FHairUpdateMeshTriangleCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairUpdateMeshTriangleCS::FParameters>();
	Parameters->MaxRootCount		= RootData.RootCount;
	Parameters->MaxSectionCount		= SectionCount;

	const uint32 MaxSectionBufferCount = 8;
	struct FMeshSectionBuffers
	{
		uint32 MeshSectionBufferIndex = 0;
		FRHIShaderResourceView* PositionBuffer = nullptr;
		FRHIShaderResourceView* IndexBuffer = nullptr;
		FRHIShaderResourceView* UVsBuffer = nullptr;
	};
	TMap<FRHIShaderResourceView*, FMeshSectionBuffers> UniqueMeshSectionBuffers;
	uint32 UniqueMeshSectionBufferIndex = 0;

	auto SetMeshSectionBuffers = [Parameters](uint32 UniqueIndex, const FHairStrandsProjectionMeshData::Section& MeshSectionData)
	{
		switch (UniqueIndex)
		{
		case 0:
		{
			Parameters->MeshPositionBuffer0 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer0 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer0 = MeshSectionData.UVsBuffer;
			break;
		}
		case 1:
		{
			Parameters->MeshPositionBuffer1 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer1 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer1 = MeshSectionData.UVsBuffer;
			break;
		}
		case 2:
		{
			Parameters->MeshPositionBuffer2 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer2 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer2 = MeshSectionData.UVsBuffer;
			break;
		}
		case 3:
		{
			Parameters->MeshPositionBuffer3 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer3 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer3 = MeshSectionData.UVsBuffer;
			break;
		}
		case 4:
		{
			Parameters->MeshPositionBuffer4 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer4 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer4 = MeshSectionData.UVsBuffer;
			break;
		}
		case 5:
		{
			Parameters->MeshPositionBuffer5 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer5 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer5 = MeshSectionData.UVsBuffer;
			break;
		}
		case 6:
		{
			Parameters->MeshPositionBuffer6 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer6 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer6 = MeshSectionData.UVsBuffer;
			break;
		}
		case 7:
		{
			Parameters->MeshPositionBuffer7 = MeshSectionData.PositionBuffer;
			Parameters->MeshIndexBuffer7 = MeshSectionData.IndexBuffer;
			Parameters->MeshUVsBuffer7 = MeshSectionData.UVsBuffer;
			break;
		}
		}
	};

	check(SectionCount < FHairUpdateMeshTriangleCS::SectionArrayCount);
	for (int32 SectionIt = 0; SectionIt < SectionCount; ++SectionIt)
	{
		const FHairStrandsProjectionMeshData::Section& MeshSectionData = MeshData.Sections[SectionIt];
		
		if (const FMeshSectionBuffers* Buffers = UniqueMeshSectionBuffers.Find(MeshSectionData.PositionBuffer))
		{
			// Insure that all buffers actually match
			check(Buffers->PositionBuffer == MeshSectionData.PositionBuffer);
			check(Buffers->IndexBuffer == MeshSectionData.IndexBuffer);
			check(Buffers->UVsBuffer == MeshSectionData.UVsBuffer);

			Parameters->MeshSectionBufferIndex[SectionIt] = Buffers->MeshSectionBufferIndex;
		}
		else
		{
			// Only support 8 unique different buffer at the moment
			check(UniqueMeshSectionBufferIndex < MaxSectionBufferCount);
			SetMeshSectionBuffers(UniqueMeshSectionBufferIndex, MeshSectionData);

			FMeshSectionBuffers& Entry = UniqueMeshSectionBuffers.Add(MeshSectionData.PositionBuffer);
			Entry.MeshSectionBufferIndex = UniqueMeshSectionBufferIndex;
			Entry.PositionBuffer = MeshSectionData.PositionBuffer;
			Entry.IndexBuffer = MeshSectionData.IndexBuffer;
			Entry.UVsBuffer = MeshSectionData.UVsBuffer;

			Parameters->MeshSectionBufferIndex[SectionIt] = UniqueMeshSectionBufferIndex;
			++UniqueMeshSectionBufferIndex;
		}

		Parameters->MeshSectionIndex[SectionIt]		= MeshSectionData.SectionIndex;
		Parameters->MeshMaxIndexCount[SectionIt]	= MeshSectionData.TotalIndexCount;
		Parameters->MeshMaxVertexCount[SectionIt]	= MeshSectionData.TotalVertexCount;
		Parameters->MeshIndexOffset[SectionIt]		= MeshSectionData.IndexBaseIndex;
		Parameters->MeshUVsChannelOffset[SectionIt] = MeshSectionData.UVsChannelOffset;
		Parameters->MeshUVsChannelCount[SectionIt]	= MeshSectionData.UVsChannelCount;
	}

	if (MeshData.Sections.Num() > 0)
	{
		for (uint32 Index = UniqueMeshSectionBufferIndex; Index < MaxSectionBufferCount; ++Index)
		{
			SetMeshSectionBuffers(Index, MeshData.Sections[0]);
		}
	}

	Parameters->RootTriangleIndex = RestLODData.RootTriangleIndexBuffer->SRV;
	if (Type == HairStrandsTriangleType::RestPose)
	{
		Parameters->OutRootTrianglePosition0 = RestLODData.RestRootTrianglePosition0Buffer->UAV;
		Parameters->OutRootTrianglePosition1 = RestLODData.RestRootTrianglePosition1Buffer->UAV;
		Parameters->OutRootTrianglePosition2 = RestLODData.RestRootTrianglePosition2Buffer->UAV;

		OutTransitionQueue.Add(RestLODData.RestRootTrianglePosition0Buffer->UAV);
		OutTransitionQueue.Add(RestLODData.RestRootTrianglePosition1Buffer->UAV);
		OutTransitionQueue.Add(RestLODData.RestRootTrianglePosition2Buffer->UAV);
	}
	else if (Type == HairStrandsTriangleType::DeformedPose)
	{
		FHairStrandsProjectionHairData::DeformedLODData& DeformedLODData = RootData.DeformedLODDatas[LODIndex];
		check(DeformedLODData.LODIndex == LODIndex);

		Parameters->OutRootTrianglePosition0 = DeformedLODData.DeformedRootTrianglePosition0Buffer->UAV;
		Parameters->OutRootTrianglePosition1 = DeformedLODData.DeformedRootTrianglePosition1Buffer->UAV;
		Parameters->OutRootTrianglePosition2 = DeformedLODData.DeformedRootTrianglePosition2Buffer->UAV;
		check(DeformedLODData.Status);
		(*DeformedLODData.Status) = FHairStrandsProjectionHairData::EStatus::Completed;

		OutTransitionQueue.Add(DeformedLODData.DeformedRootTrianglePosition0Buffer->UAV);
		OutTransitionQueue.Add(DeformedLODData.DeformedRootTrianglePosition1Buffer->UAV);
		OutTransitionQueue.Add(DeformedLODData.DeformedRootTrianglePosition2Buffer->UAV);
	}
	else
	{
		// error
		return;
	}

	FHairUpdateMeshTriangleCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairUpdateMeshTriangleCS::FUpdateUVs>(1);

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootData.RootCount, 128);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FHairUpdateMeshTriangleCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTriangleMeshUpdate"),
		ComputeShader,
		Parameters,
		DispatchGroupCount);
}

void UpdateHairStrandsMeshTriangles(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	AddHairStrandUpdateMeshTrianglesPass(GraphBuilder, ShaderMap, LODIndex, Type, ProjectionMeshData, ProjectionHairData,OutTransitionQueue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairInterpolateMeshTriangleCS : public FGlobalShader
{
private:
	DECLARE_GLOBAL_SHADER(FHairInterpolateMeshTriangleCS);
	SHADER_USE_PARAMETER_STRUCT(FHairInterpolateMeshTriangleCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxRootCount)
		SHADER_PARAMETER(uint32, MaxSampleCount)

		SHADER_PARAMETER_SRV(Buffer, RestSamplePositionsBuffer)
		SHADER_PARAMETER_SRV(Buffer, MeshSampleWeightsBuffer)

		SHADER_PARAMETER_SRV(StructuredBuffer, RestRootTrianglePosition0)
		SHADER_PARAMETER_SRV(StructuredBuffer, RestRootTrianglePosition1)
		SHADER_PARAMETER_SRV(StructuredBuffer, RestRootTrianglePosition2)

		SHADER_PARAMETER_UAV(StructuredBuffer, OutDeformedRootTrianglePosition0)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutDeformedRootTrianglePosition1)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutDeformedRootTrianglePosition2)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairInterpolateMeshTriangleCS, "/Engine/Private/HairStrands/HairStrandsMeshInterpolate.usf", "MainCS", SF_Compute);

void TransitBufferToReadable(FRHICommandList& RHICmdList, FBufferTransitionQueue& BuffersToTransit)
{
	if (BuffersToTransit.Num())
	{
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, BuffersToTransit.GetData(), BuffersToTransit.Num());
		BuffersToTransit.SetNum(0, false);
	}
}

static void AddHairStrandInterpolateMeshTrianglesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsProjectionHairData::HairGroup& RootData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (RootData.RootCount == 0 || LODIndex < 0 || LODIndex >= RootData.RestLODDatas.Num() || LODIndex >= RootData.DeformedLODDatas.Num())
	{
		return;
	}
	FHairStrandsProjectionHairData::RestLODData& RestLODData = RootData.RestLODDatas[LODIndex];
	FHairStrandsProjectionHairData::DeformedLODData& DeformedLODData = RootData.DeformedLODDatas[LODIndex];
	check(RestLODData.LODIndex == LODIndex);
	check(DeformedLODData.LODIndex == LODIndex);

	FHairInterpolateMeshTriangleCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairInterpolateMeshTriangleCS::FParameters>();
	Parameters->MaxRootCount = RootData.RootCount;
	Parameters->MaxSampleCount = RestLODData.SampleCount;

	Parameters->RestRootTrianglePosition0 = RestLODData.RestRootTrianglePosition0Buffer->SRV;
	Parameters->RestRootTrianglePosition1 = RestLODData.RestRootTrianglePosition1Buffer->SRV;
	Parameters->RestRootTrianglePosition2 = RestLODData.RestRootTrianglePosition2Buffer->SRV;

	Parameters->OutDeformedRootTrianglePosition0 = DeformedLODData.DeformedRootTrianglePosition0Buffer->UAV;
	Parameters->OutDeformedRootTrianglePosition1 = DeformedLODData.DeformedRootTrianglePosition1Buffer->UAV;
	Parameters->OutDeformedRootTrianglePosition2 = DeformedLODData.DeformedRootTrianglePosition2Buffer->UAV;

	Parameters->MeshSampleWeightsBuffer = DeformedLODData.MeshSampleWeightsBuffer->SRV;
	Parameters->RestSamplePositionsBuffer = RestLODData.RestSamplePositionsBuffer->SRV;

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootData.RootCount, 128);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FHairInterpolateMeshTriangleCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTriangleMeshInterpolate"),
		ComputeShader,
		Parameters,
		DispatchGroupCount);

	OutTransitionQueue.Add(DeformedLODData.DeformedRootTrianglePosition0Buffer->UAV);
	OutTransitionQueue.Add(DeformedLODData.DeformedRootTrianglePosition1Buffer->UAV);
	OutTransitionQueue.Add(DeformedLODData.DeformedRootTrianglePosition2Buffer->UAV);
}

void InterpolateHairStrandsMeshTriangles(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	AddHairStrandInterpolateMeshTrianglesPass(GraphBuilder, ShaderMap, LODIndex, ProjectionMeshData, ProjectionHairData, OutTransitionQueue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//
class FHairInitMeshSamplesCS : public FGlobalShader
{
private:
	DECLARE_GLOBAL_SHADER(FHairInitMeshSamplesCS);
	SHADER_USE_PARAMETER_STRUCT(FHairInitMeshSamplesCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxSampleCount)
		SHADER_PARAMETER(uint32, MaxVertexCount)

		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer)

		SHADER_PARAMETER_SRV(Buffer, SampleIndicesBuffer)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutSamplePositionsBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairInitMeshSamplesCS, "/Engine/Private/HairStrands/HairStrandsSamplesInit.usf", "MainCS", SF_Compute);

static void AddHairStrandInitMeshSamplesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsProjectionHairData::HairGroup& RootData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (LODIndex < 0)
	{
		return;
	}

	if (Type == HairStrandsTriangleType::RestPose && LODIndex >= RootData.RestLODDatas.Num())
	{
		return;
	}

	if (Type == HairStrandsTriangleType::DeformedPose && (LODIndex >= RootData.RestLODDatas.Num() || LODIndex >= RootData.DeformedLODDatas.Num()))
	{
		return;
	}

	FHairStrandsProjectionHairData::RestLODData& RestLODData = RootData.RestLODDatas[LODIndex];
	check(RestLODData.LODIndex == LODIndex);

	const uint32 SectionCount = MeshData.Sections.Num();
	if (SectionCount > 0 && RestLODData.SampleCount > 0)
	{
		FHairInitMeshSamplesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairInitMeshSamplesCS::FParameters>();

		Parameters->MaxVertexCount = MeshData.Sections[0].TotalVertexCount;
		Parameters->VertexPositionsBuffer = MeshData.Sections[0].PositionBuffer;

		Parameters->MaxSampleCount = RestLODData.SampleCount;
		Parameters->SampleIndicesBuffer = RestLODData.MeshSampleIndicesBuffer->SRV;
		if (Type == HairStrandsTriangleType::RestPose)
		{
			Parameters->OutSamplePositionsBuffer = RestLODData.RestSamplePositionsBuffer->UAV;
			OutTransitionQueue.Add(RestLODData.RestSamplePositionsBuffer->UAV);
		}
		else if (Type == HairStrandsTriangleType::DeformedPose)
		{
			FHairStrandsProjectionHairData::DeformedLODData& DeformedLODData = RootData.DeformedLODDatas[LODIndex];
			check(DeformedLODData.LODIndex == LODIndex);

			Parameters->OutSamplePositionsBuffer = DeformedLODData.DeformedSamplePositionsBuffer->UAV;
			OutTransitionQueue.Add(DeformedLODData.DeformedSamplePositionsBuffer->UAV);
		}
		else
		{
			return;
		}

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RestLODData.SampleCount, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FHairInitMeshSamplesCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsInitMeshSamples"),
			ComputeShader,
			Parameters,
			DispatchGroupCount);
	}
}

void InitHairStrandsMeshSamples(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	AddHairStrandInitMeshSamplesPass(GraphBuilder, ShaderMap, LODIndex, Type, ProjectionMeshData, ProjectionHairData, OutTransitionQueue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//
class FHairUpdateMeshSamplesCS : public FGlobalShader
{
private:
	DECLARE_GLOBAL_SHADER(FHairUpdateMeshSamplesCS);
	SHADER_USE_PARAMETER_STRUCT(FHairUpdateMeshSamplesCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxSampleCount)

		SHADER_PARAMETER_SRV(Buffer, SampleIndicesBuffer)
		SHADER_PARAMETER_SRV(Buffer, InterpolationWeightsBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer, SampleRestPositionsBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer, SampleDeformedPositionsBuffer)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutSampleDeformationsBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairUpdateMeshSamplesCS, "/Engine/Private/HairStrands/HairStrandsSamplesUpdate.usf", "MainCS", SF_Compute);

static void AddHairStrandUpdateMeshSamplesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsProjectionHairData::HairGroup& RootData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	if (LODIndex < 0 || LODIndex >= RootData.RestLODDatas.Num() || LODIndex >= RootData.DeformedLODDatas.Num())
	{
		return;
	}

	FHairStrandsProjectionHairData::RestLODData& RestLODData = RootData.RestLODDatas[LODIndex];
	FHairStrandsProjectionHairData::DeformedLODData& DeformedLODData = RootData.DeformedLODDatas[LODIndex];
	check(RestLODData.LODIndex == LODIndex);
	check(DeformedLODData.LODIndex == LODIndex);

	const uint32 SectionCount = MeshData.Sections.Num();
	if (SectionCount > 0 && RestLODData.SampleCount > 0)
	{
		FHairUpdateMeshSamplesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairUpdateMeshSamplesCS::FParameters>();

		Parameters->MaxSampleCount = RestLODData.SampleCount;
		Parameters->SampleIndicesBuffer = RestLODData.MeshSampleIndicesBuffer->SRV;
		Parameters->InterpolationWeightsBuffer = RestLODData.MeshInterpolationWeightsBuffer->SRV;
		Parameters->SampleRestPositionsBuffer = RestLODData.RestSamplePositionsBuffer->SRV;
		Parameters->SampleDeformedPositionsBuffer = DeformedLODData.DeformedSamplePositionsBuffer->SRV;
		Parameters->OutSampleDeformationsBuffer = DeformedLODData.MeshSampleWeightsBuffer->UAV;

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RestLODData.SampleCount+4, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FHairUpdateMeshSamplesCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsUpdateMeshSamples"),
			ComputeShader,
			Parameters,
			DispatchGroupCount);

		OutTransitionQueue.Add(DeformedLODData.MeshSampleWeightsBuffer->UAV);
	}
}

void UpdateHairStrandsMeshSamples(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& OutTransitionQueue)
{
	AddHairStrandUpdateMeshSamplesPass(GraphBuilder, ShaderMap, LODIndex, ProjectionMeshData, ProjectionHairData, OutTransitionQueue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

// Generate follicle mask texture
BEGIN_SHADER_PARAMETER_STRUCT(FHairFollicleMaskParameters, )
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER(uint32, MaxRootCount)
	SHADER_PARAMETER(uint32, Channel)
	SHADER_PARAMETER(uint32, KernelSizeInPixels)

	SHADER_PARAMETER_SRV(Buffer, TrianglePosition0Buffer)
	SHADER_PARAMETER_SRV(Buffer, TrianglePosition1Buffer)
	SHADER_PARAMETER_SRV(Buffer, TrianglePosition2Buffer)
	SHADER_PARAMETER_SRV(Buffer, RootBarycentricBuffer)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairFollicleMask : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_FOLLICLE_MASK"), 1);
	}

	FHairFollicleMask() = default;
	FHairFollicleMask(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};

class FHairFollicleMaskVS : public FHairFollicleMask
{
	DECLARE_GLOBAL_SHADER(FHairFollicleMaskVS);
	SHADER_USE_PARAMETER_STRUCT(FHairFollicleMaskVS, FHairFollicleMask);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairFollicleMaskParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()
};

class FHairFollicleMaskPS : public FHairFollicleMask
{
	DECLARE_GLOBAL_SHADER(FHairFollicleMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FHairFollicleMaskPS, FHairFollicleMask);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairFollicleMaskParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairFollicleMaskPS, "/Engine/Private/HairStrands/HairStrandsFollicleMask.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FHairFollicleMaskVS, "/Engine/Private/HairStrands/HairStrandsFollicleMask.usf", "MainVS", SF_Vertex);

static void AddFollicleMaskPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const bool bNeedClear,
	const uint32 KernelSizeInPixels,
	const uint32 Channel,
	const uint32 LODIndex,
	const FHairStrandsProjectionHairData::HairGroup& HairData,
	FRDGTextureRef OutTexture)
{
	if (LODIndex >= uint32(HairData.RestLODDatas.Num()) || HairData.RootCount == 0)
		return;

	const FHairStrandsProjectionHairData::RestLODData& LODData = HairData.RestLODDatas[LODIndex];
	if (!LODData.RootTriangleBarycentricBuffer ||
		!LODData.RestRootTrianglePosition0Buffer ||
		!LODData.RestRootTrianglePosition1Buffer ||
		!LODData.RestRootTrianglePosition2Buffer)
		return;

	const FIntPoint OutputResolution = OutTexture->Desc.Extent;
	FHairFollicleMaskParameters* Parameters = GraphBuilder.AllocParameters<FHairFollicleMaskParameters>();
	Parameters->TrianglePosition0Buffer = LODData.RestRootTrianglePosition0Buffer->SRV;
	Parameters->TrianglePosition1Buffer = LODData.RestRootTrianglePosition1Buffer->SRV;
	Parameters->TrianglePosition2Buffer = LODData.RestRootTrianglePosition2Buffer->SRV;
	Parameters->RootBarycentricBuffer = LODData.RootTriangleBarycentricBuffer->SRV;
	Parameters->OutputResolution = OutputResolution;
	Parameters->MaxRootCount = HairData.RootCount;
	Parameters->Channel = FMath::Min(Channel, 3u);
	Parameters->KernelSizeInPixels = FMath::Clamp(KernelSizeInPixels, 2u, 200u);
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTexture, bNeedClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, 0);

	TShaderMapRef<FHairFollicleMaskVS> VertexShader(ShaderMap);
	TShaderMapRef<FHairFollicleMaskPS> PixelShader(ShaderMap);
	FHairFollicleMaskVS::FParameters ParametersVS;
	FHairFollicleMaskPS::FParameters ParametersPS;
	ParametersVS.Pass = *Parameters;
	ParametersPS.Pass = *Parameters;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsFollicleMask"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, ParametersVS, ParametersPS, VertexShader, PixelShader, OutputResolution](FRHICommandList& RHICmdList)
	{

		RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);

		// Apply additive blending pipeline state.
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Max, BF_SourceColor, BF_DestColor, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), ParametersPS);

		// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
		RHICmdList.DrawPrimitive(0, Parameters->MaxRootCount, 1);
	});
}

void GenerateFolliculeMask(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FIntPoint Resolution,
	const uint32 MipCount,
	const uint32 KernelSizeInPixels,
	const uint32 Channel,
	const int32 LODIndex,
	const FHairStrandsProjectionHairData& HairData, 
	FRDGTextureRef& OutTexture)
{
	const FLinearColor ClearColor(0.0f, 0.f, 0.f, 0.f);

	bool bClear = OutTexture == nullptr;
	if (OutTexture == nullptr)
	{
		FRDGTextureDesc OutputDesc;
		OutputDesc.ClearValue = FClearValueBinding(ClearColor);
		OutputDesc.Extent.X = Resolution.X;
		OutputDesc.Extent.Y = Resolution.Y;
		OutputDesc.Depth = 0;
		OutputDesc.Format = PF_R8G8B8A8;
		OutputDesc.NumMips = MipCount;
		OutputDesc.Flags = 0;
		OutputDesc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV;
		OutTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("FollicleMask"));
	}

	for (const FHairStrandsProjectionHairData::HairGroup& HairGroup : HairData.HairGroups)
	{
		AddFollicleMaskPass(GraphBuilder, ShaderMap, bClear, KernelSizeInPixels, Channel, LODIndex, HairGroup, OutTexture);
		bClear = false;
	}
}

class FGenerateMipCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateMipCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Resolution)
		SHADER_PARAMETER(uint32, SourceMip)
		SHADER_PARAMETER(uint32, TargetMip)

		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_GENERATE_MIPS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateMipCS, "/Engine/Private/HairStrands/HairStrandsFollicleMask.usf", "MainCS", SF_Compute);

void AddComputeMipsPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FRDGTextureRef& OutTexture)
{
	check(OutTexture->Desc.Extent.X == OutTexture->Desc.Extent.Y);
	const uint32 Resolution = OutTexture->Desc.Extent.X;
	const uint32 MipCount = OutTexture->Desc.NumMips;
	for (uint32 MipIt = 0; MipIt < MipCount - 1; ++MipIt)
	{
		const uint32 SourceMipIndex = MipIt;
		const uint32 TargetMipIndex = MipIt + 1;
		const uint32 TargetResolution = Resolution << TargetMipIndex;

		FGenerateMipCS::FParameters* Parameters = GraphBuilder.AllocParameters<FGenerateMipCS::FParameters>();
		Parameters->InTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(OutTexture, SourceMipIndex));
		Parameters->OutTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutTexture, TargetMipIndex));
		Parameters->Resolution = Resolution;
		Parameters->SourceMip = SourceMipIndex;
		Parameters->TargetMip = TargetMipIndex;
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		TShaderMapRef<FGenerateMipCS> ComputeShader(ShaderMap);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsComputeVoxelMip"),
			Parameters,
			ERDGPassFlags::Compute | ERDGPassFlags::GenerateMips,
			[Parameters, ComputeShader, TargetResolution](FRHICommandList& RHICmdList)
		{
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntPoint(TargetResolution, TargetResolution), FIntPoint(8, 8));
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, GroupCount);
		});
	}
}