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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
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

	FRDGBufferRef VertexSectionIdBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MeshData.Sections[0].TotalVertexCount), TEXT("SectionId"));
	for (const FHairStrandsProjectionMeshData::Section& MeshSection : MeshData.Sections)
	{	
		FMarkMeshSectionIdCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMarkMeshSectionIdCS::FParameters>();
		Parameters->MeshSectionId				= MeshSection.SectionIndex;
		Parameters->MeshSectionPrimitiveCount	= MeshSection.NumPrimitives;
		Parameters->MeshMaxIndexCount			= MeshSection.TotalIndexCount;
		Parameters->MeshMaxVertexCount			= MeshSection.TotalVertexCount;
		Parameters->MeshIndexOffset				= MeshSection.IndexBaseIndex;
		Parameters->MeshIndexBuffer				= MeshSection.IndexBuffer;
		Parameters->OutVertexSectionId			= GraphBuilder.CreateUAV(VertexSectionIdBuffer, PF_R32_UINT);

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(MeshSection.TotalVertexCount, 128);
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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
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
	FRWBuffer& OutTargetRestPosition)
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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
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
	FRDGBufferRef RootDistanceBuffer)
{
	if (!RootData.RootPositionBuffer ||
		!RootData.RootNormalBuffer ||
		LODIndex < 0 || LODIndex >= RootData.LODDatas.Num() ||
		!RootData.LODDatas[LODIndex].RootTriangleIndexBuffer ||
		!RootData.LODDatas[LODIndex].RootTriangleBarycentricBuffer ||
		!MeshSectionData.IndexBuffer ||
		!MeshSectionData.PositionBuffer ||
		MeshSectionData.TotalIndexCount == 0 ||
		MeshSectionData.TotalVertexCount == 0)
	{
		return;
	}

	// The current shader code HairStrandsMeshProjection.usf encode the section ID onto the highest 3bits of a 32bits uint. 
	// This limits the number of section to 8. See EncodeTriangleIndex & DecodeTriangleIndex functions in 
	// HairStarndsMeshProjectionCommon.ush for mode details.
	//
	// This could be increase if necessary.
	check(MeshSectionData.SectionIndex < 8);

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
		Parameters->OutRootTriangleIndex		= RootData.LODDatas[LODIndex].RootTriangleIndexBuffer->UAV;
		Parameters->OutRootTriangleBarycentrics = RootData.LODDatas[LODIndex].RootTriangleBarycentricBuffer->UAV;
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
	}
}

void ProjectHairStrandsOntoMesh(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData)
{
	if (LODIndex < 0 || LODIndex >= ProjectionHairData.LODDatas.Num())
		return;

	FRDGBufferRef RootDistanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), ProjectionHairData.RootCount), TEXT("HairStrandsTriangleDistance"));

	bool ClearDistance = true;
	for (const FHairStrandsProjectionMeshData::Section& MeshSection : ProjectionMeshData.LODs[LODIndex].Sections)
	{
		check(ProjectionHairData.LODDatas[LODIndex].LODIndex == LODIndex);
		AddHairStrandMeshProjectionPass(GraphBuilder, ShaderMap, ClearDistance, LODIndex, MeshSection, ProjectionHairData, RootDistanceBuffer);
		ProjectionHairData.LODDatas[LODIndex].bIsValid = true;
		ClearDistance = false;
	}
}

void TransferMesh(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& SourceMeshData,
	const FHairStrandsProjectionMeshData& TargetMeshData,
	FRWBuffer& OutPositionBuffer)
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
	AddMeshTransferPass(GraphBuilder, ShaderMap, true, SourceMeshSection, TargetMeshSection, VertexSectionId, OutPositionBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairUpdateMeshTriangleCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairUpdateMeshTriangleCS);
	SHADER_USE_PARAMETER_STRUCT(FHairUpdateMeshTriangleCS, FGlobalShader);

	class FUpdateUVs : SHADER_PERMUTATION_INT("PERMUTATION_WITHUV", 2);
	using FPermutationDomain = TShaderPermutationDomain<FUpdateUVs>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxRootCount)

		SHADER_PARAMETER(uint32, MeshSectionIndex)
		SHADER_PARAMETER(uint32, MeshMaxIndexCount)
		SHADER_PARAMETER(uint32, MeshMaxVertexCount)
		SHADER_PARAMETER(uint32, MeshIndexOffset)
		SHADER_PARAMETER(uint32, MeshUVsChannelOffset)
		SHADER_PARAMETER(uint32, MeshUVsChannelCount)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, MeshUVsBuffer)

		SHADER_PARAMETER_SRV(Buffer, RootTriangleIndex)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutRootTrianglePosition0)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutRootTrianglePosition1)
		SHADER_PARAMETER_UAV(StructuredBuffer, OutRootTrianglePosition2)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
};

IMPLEMENT_GLOBAL_SHADER(FHairUpdateMeshTriangleCS, "/Engine/Private/HairStrands/HairStrandsMeshUpdate.usf", "MainCS", SF_Compute);

static void AddHairStrandUpdateMeshTrianglesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::Section& MeshSectionData,
	FHairStrandsProjectionHairData::HairGroup& RootData)
{
	if (RootData.RootCount == 0 || LODIndex < 0 || LODIndex >= RootData.LODDatas.Num())
	{
		return;
	}
	FHairStrandsProjectionHairData::LODData& LODData = RootData.LODDatas[LODIndex];
	check(LODData.LODIndex == LODIndex);

	FHairUpdateMeshTriangleCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairUpdateMeshTriangleCS::FParameters>();
	Parameters->MaxRootCount		= RootData.RootCount;
	Parameters->MeshSectionIndex	= MeshSectionData.SectionIndex;
	Parameters->MeshMaxIndexCount	= MeshSectionData.TotalIndexCount;
	Parameters->MeshMaxVertexCount	= MeshSectionData.TotalVertexCount;
	Parameters->MeshIndexOffset		= MeshSectionData.IndexBaseIndex;
	Parameters->MeshIndexBuffer		= MeshSectionData.IndexBuffer;
	Parameters->MeshPositionBuffer	= MeshSectionData.PositionBuffer;
	Parameters->MeshUVsBuffer		= MeshSectionData.UVsBuffer;
	Parameters->MeshUVsChannelOffset= MeshSectionData.UVsChannelOffset;
	Parameters->MeshUVsChannelCount	= MeshSectionData.UVsChannelCount;

	Parameters->RootTriangleIndex = LODData.RootTriangleIndexBuffer->SRV;
	if (Type == HairStrandsTriangleType::RestPose)
	{
		Parameters->OutRootTrianglePosition0 = LODData.RestRootTrianglePosition0Buffer->UAV;
		Parameters->OutRootTrianglePosition1 = LODData.RestRootTrianglePosition1Buffer->UAV;
		Parameters->OutRootTrianglePosition2 = LODData.RestRootTrianglePosition2Buffer->UAV;
	}
	else if (Type == HairStrandsTriangleType::DeformedPose)
	{
		Parameters->OutRootTrianglePosition0 = LODData.DeformedRootTrianglePosition0Buffer->UAV;
		Parameters->OutRootTrianglePosition1 = LODData.DeformedRootTrianglePosition1Buffer->UAV;
		Parameters->OutRootTrianglePosition2 = LODData.DeformedRootTrianglePosition2Buffer->UAV;
		if (LODData.Status) (*LODData.Status) = FHairStrandsProjectionHairData::LODData::EStatus::Completed;
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
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData)
{
	for (const FHairStrandsProjectionMeshData::Section& MeshSection : ProjectionMeshData.Sections)
	{
		AddHairStrandUpdateMeshTrianglesPass(GraphBuilder, ShaderMap, LODIndex, Type, MeshSection, ProjectionHairData);
	}
}