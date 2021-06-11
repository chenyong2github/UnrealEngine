// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsMeshProjection.h"
#include "MeshMaterialShader.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "CommonRenderResources.h"
#include "SkeletalRenderPublic.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RenderGraphUtils.h"
#include "GroomResources.h"

static int32 GHairProjectionMaxTrianglePerProjectionIteration = 8;
static FAutoConsoleVariableRef CVarHairProjectionMaxTrianglePerProjectionIteration(TEXT("r.HairStrands.Projection.MaxTrianglePerIteration"), GHairProjectionMaxTrianglePerProjectionIteration, TEXT("Change the number of triangles which are iterated over during one projection iteration step. In kilo triangle (e.g., 8 == 8000 triangles). Default is 8."));

static int32 GHairStrandsUseGPUPositionOffset = 1;
static FAutoConsoleVariableRef CVarHairStrandsUseGPUPositionOffset(TEXT("r.HairStrands.UseGPUPositionOffset"), GHairStrandsUseGPUPositionOffset, TEXT("Use GPU position offset to improve hair strands position precision."));

///////////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_HAIRSTRANDS_SECTION_COUNT 255
#define MAX_HAIRSTRANDS_SECTION_BITOFFSET 24

uint32 GetHairStrandsMaxSectionCount()
{
	return MAX_HAIRSTRANDS_SECTION_COUNT;
}

uint32 GetHairStrandsMaxTriangleCount()
{
	return (1 << MAX_HAIRSTRANDS_SECTION_BITOFFSET) - 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsProjectionMeshData ExtractMeshData(FSkeletalMeshRenderData* RenderData)
{
	FHairStrandsProjectionMeshData MeshData;
	uint32 LODIndex = 0;
	for (FSkeletalMeshLODRenderData& LODRenderData : RenderData->LODRenderData)
	{
		FHairStrandsProjectionMeshData::LOD& LOD = MeshData.LODs.AddDefaulted_GetRef();
		uint32 SectionIndex = 0;
		for (FSkelMeshRenderSection& InSection : LODRenderData.RenderSections)
		{
			// Pick between float and halt
			const uint32 UVSizeInByte = (LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs() ? 4 : 2) * 2;

			FHairStrandsProjectionMeshData::Section& OutSection = LOD.Sections.AddDefaulted_GetRef();
			OutSection.UVsChannelOffset = 0; // Assume that we needs to pair meshes based on UVs 0
			OutSection.UVsChannelCount = LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
			OutSection.UVsBuffer = LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
			OutSection.PositionBuffer = LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetSRV();
			OutSection.IndexBuffer = LODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
			OutSection.TotalVertexCount = LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
			OutSection.TotalIndexCount = LODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
			OutSection.NumPrimitives = InSection.NumTriangles;
			OutSection.NumVertices = InSection.NumVertices;
			OutSection.VertexBaseIndex = InSection.BaseVertexIndex;
			OutSection.IndexBaseIndex = InSection.BaseIndex;
			OutSection.SectionIndex = SectionIndex;
			OutSection.LODIndex = LODIndex;

			++SectionIndex;
		}
		++LODIndex;
	}

	return MeshData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef GPU_BINDING
#define GPU_BINDING 0
#endif
#if GPU_BINDING
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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
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
#endif // GPU_BINDING

///////////////////////////////////////////////////////////////////////////////////////////////////
class FSkinUpdateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSkinUpdateCS);
	SHADER_USE_PARAMETER_STRUCT(FSkinUpdateCS, FGlobalShader);

	class FUnlimitedBoneInfluence : SHADER_PERMUTATION_INT("GPUSKIN_UNLIMITED_BONE_INFLUENCE", 2);
	class FUseExtraInfluence : SHADER_PERMUTATION_INT("GPUSKIN_USE_EXTRA_INFLUENCES", 2);
	class FIndexUint16 : SHADER_PERMUTATION_INT("GPUSKIN_BONE_INDEX_UINT16", 2);
	using FPermutationDomain = TShaderPermutationDomain<FUnlimitedBoneInfluence, FUseExtraInfluence, FIndexUint16>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, IndexSize)
		SHADER_PARAMETER(uint32, NumVertices)
		SHADER_PARAMETER(uint32, WeightStride)
		SHADER_PARAMETER_SRV(Buffer<uint>, WeightLookup)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BoneMatrices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MatrixOffsets)
		SHADER_PARAMETER_SRV(Buffer<uint>, VertexWeights)
		SHADER_PARAMETER_SRV(Buffer<float>, RestPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, DeformedPositions)

		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FSkinUpdateCS, "/Engine/Private/HairStrands/HairStrandsSkinUpdate.usf", "UpdateSkinPositionCS", SF_Compute);

void AddSkinUpdatePass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FSkinWeightVertexBuffer* SkinWeight,
	FSkeletalMeshLODRenderData& RenderData,
	FRDGBufferRef BoneMatrices,
	FRDGBufferRef MatrixOffsets,
	FRDGBufferRef OutDeformedosition)
{
	FSkinUpdateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSkinUpdateCS::FParameters>();

	Parameters->IndexSize = SkinWeight->GetBoneIndexByteSize();
	Parameters->NumVertices = RenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
	Parameters->WeightStride = SkinWeight->GetConstantInfluencesVertexStride();
	Parameters->WeightLookup = SkinWeight->GetLookupVertexBuffer()->GetSRV();
	Parameters->BoneMatrices = GraphBuilder.CreateSRV(BoneMatrices);//BoneBuffer.VertexBufferSRV;
	Parameters->MatrixOffsets = GraphBuilder.CreateSRV(MatrixOffsets);//BoneBuffer.VertexBufferSRV;
	Parameters->VertexWeights = SkinWeight->GetDataVertexBuffer()->GetSRV();
	Parameters->RestPositions = RenderData.StaticVertexBuffers.PositionVertexBuffer.GetSRV();
	Parameters->DeformedPositions = GraphBuilder.CreateUAV(OutDeformedosition, PF_R32_FLOAT);

	FSkinUpdateCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FSkinUpdateCS::FUnlimitedBoneInfluence>(SkinWeight->GetBoneInfluenceType() == GPUSkinBoneInfluenceType::UnlimitedBoneInfluence);
	PermutationVector.Set<FSkinUpdateCS::FUseExtraInfluence>(SkinWeight->GetMaxBoneInfluences() > MAX_INFLUENCES_PER_STREAM);
	PermutationVector.Set<FSkinUpdateCS::FIndexUint16>(SkinWeight->Use16BitBoneIndex());

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(), 64);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FSkinUpdateCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("UpdateSkinPosition"),
		ComputeShader,
		Parameters,
		DispatchGroupCount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
#if GPU_BINDING
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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
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
	FHairStrandsRestRootResource* RestResources,
	FRDGBufferRef RootDistanceBuffer)
{
	const uint32 RootCount = RestResources->RootData.RootCount;
	if (!RestResources->RootPositionBuffer.SRV ||
		!RestResources->RootNormalBuffer.SRV ||
		LODIndex < 0 || LODIndex >= RestResources->LODs.Num() ||
		!MeshSectionData.IndexBuffer ||
		!MeshSectionData.PositionBuffer ||
		MeshSectionData.TotalIndexCount == 0 ||
		MeshSectionData.TotalVertexCount == 0)
	{
		return;
	}

	FHairStrandsRestRootResource::FLOD& RestLODDatas = RestResources->LODs[LODIndex];
	if (!RestLODDatas.RootTriangleIndexBuffer.UAV || !RestLODDatas.RootTriangleBarycentricBuffer.UAV)
	{
		return;
	}

	// The current shader code HairStrandsMeshProjection.usf encode the section ID onto the highest 8bits of a 32bits uint. 
	// This limits the number of section to 64. See EncodeTriangleIndex & DecodeTriangleIndex functions in 
	// HairStarndsMeshProjectionCommon.ush for mode details.
	// This means that the mesh needs to have less than 67M triangles (since triangle ID is stored onto 26bits).
	//
	// This could be increase if necessary.
	check(MeshSectionData.SectionIndex < GetHairStrandsMaxSectionCount());
	check(MeshSectionData.NumPrimitives < GetHairStrandsMaxTriangleCount());

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
		Parameters->MaxRootCount		= RootCount;
		Parameters->RootPositionBuffer	= RestResources->RootPositionBuffer.SRV;
		Parameters->RootNormalBuffer	= RestResources->RootNormalBuffer.SRV;
		Parameters->MeshSectionIndex	= MeshSectionData.SectionIndex;
		Parameters->MeshMaxIndexCount	= MeshSectionData.TotalIndexCount;
		Parameters->MeshMaxVertexCount	= MeshSectionData.TotalVertexCount;
		Parameters->MeshIndexOffset		= MeshSectionData.IndexBaseIndex + (MeshPassNumPrimitive * MeshPassIt * 3);
		Parameters->MeshIndexBuffer		= MeshSectionData.IndexBuffer;
		Parameters->MeshPositionBuffer	= MeshSectionData.PositionBuffer;
		Parameters->MeshPrimitiveOffset_Iteration	= MeshPassNumPrimitive * MeshPassIt;
		Parameters->MeshPrimitiveCount_Iteration	= (MeshPassIt < MeshPassCount-1) ? MeshPassNumPrimitive : (MeshSectionData.NumPrimitives - MeshPassNumPrimitive * MeshPassIt);

		// The projection is always done onto the source/rest mesh
		Parameters->OutRootTriangleIndex		= RestLODDatas.RootTriangleIndexBuffer.UAV;
		Parameters->OutRootTriangleBarycentrics = RestLODDatas.RootTriangleBarycentricBuffer.UAV;
		Parameters->OutRootTriangleDistance		= DistanceUAV;

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootCount, 128);
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
	FHairStrandsRestRootResource* RestResources)
{
	if (LODIndex < 0 || LODIndex >= RestResources->LODs.Num())
		return;

	const uint32 RootCount = RestResources->RootData.RootCount;
	FHairStrandsRestRootResource::FLOD& RestLODDatas = RestResources->LODs[LODIndex];
	FRDGBufferRef RootDistanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), RootCount), TEXT("HairStrandsTriangleDistance"));

	bool ClearDistance = true;
	for (const FHairStrandsProjectionMeshData::Section& MeshSection : ProjectionMeshData.LODs[LODIndex].Sections)
	{
		check(RestLODDatas.LODIndex == LODIndex);
		AddHairStrandMeshProjectionPass(GraphBuilder, ShaderMap, ClearDistance, LODIndex, MeshSection, RestResources, RootDistanceBuffer);
		RestLODDatas.Status = FHairStrandsRestRootResource::FLOD::EStatus::Completed;
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
	AddMeshTransferPass(GraphBuilder, ShaderMap, true, SourceMeshSection, TargetMeshSection, VertexSectionId, OutPositionBuffer);
}
#endif // GPU_BINDING

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairUpdateMeshTriangleCS : public FGlobalShader
{
public:
	const static uint32 SectionArrayCount = 16; // This defines the number of sections managed for each iteration pass
private:
	DECLARE_GLOBAL_SHADER(FHairUpdateMeshTriangleCS);
	SHADER_USE_PARAMETER_STRUCT(FHairUpdateMeshTriangleCS, FGlobalShader);

	class FUpdateUVs : SHADER_PERMUTATION_INT("PERMUTATION_WITHUV", 2);
	class FPositionType : SHADER_PERMUTATION_INT("PERMUTATION_POSITION_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FUpdateUVs, FPositionType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxRootCount)
		SHADER_PARAMETER(uint32, MaxSectionCount)
		SHADER_PARAMETER(uint32, Pass_SectionStart)
		SHADER_PARAMETER(uint32, Pass_SectionCount)
		
		SHADER_PARAMETER_ARRAY(uint32, MeshSectionIndices, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshMaxIndexCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshMaxVertexCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshIndexOffset, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshUVsChannelOffset, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshUVsChannelCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, MeshSectionBufferIndex, [SectionArrayCount])

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer0)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer1)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer2)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer3)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer4)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer5)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer6)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGMeshPositionBuffer7)

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

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RootTriangleIndex)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutRootTrianglePosition0)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutRootTrianglePosition1)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutRootTrianglePosition2)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_SECTION_COUNT"), SectionArrayCount);
		OutEnvironment.SetDefine(TEXT("SHADER_MESH_UPDATE"), SectionArrayCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairUpdateMeshTriangleCS, "/Engine/Private/HairStrands/HairStrandsMeshUpdate.usf", "MainCS", SF_Compute);

void AddHairStrandUpdateMeshTrianglesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsRestRootResource* RestResources,
	FHairStrandsDeformedRootResource* DeformedResources)
{
	const uint32 RootCount = RestResources->RootData.RootCount;
	if (RootCount == 0 || LODIndex < 0)
	{
		return;
	}

	if (Type == HairStrandsTriangleType::RestPose && LODIndex >= RestResources->LODs.Num())
	{
		return;
	}

	if (Type == HairStrandsTriangleType::DeformedPose && (LODIndex >= RestResources->LODs.Num() || LODIndex >= DeformedResources->LODs.Num()))
	{
		return;
	}

	FHairStrandsRestRootResource::FLOD& RestLODData = RestResources->LODs[LODIndex];
	check(RestLODData.LODIndex == LODIndex);

	const uint32 SectionCount = MeshData.Sections.Num();
	const uint32 MaxSupportedSectionCount = GetHairStrandsMaxSectionCount();
	check(SectionCount < MaxSupportedSectionCount);
	if (SectionCount == 0 || SectionCount >= MaxSupportedSectionCount)
	{
		return;
	}

	// Update the last known mesh LOD for which the root resources has been updated
	DeformedResources->MeshLODIndex = LODIndex;

	// When the number of section of a mesh is above FHairUpdateMeshTriangleCS::SectionArrayCount, the update is split into several passes
	const TArray<uint32>& ValidSectionIndices = RestResources->RootData.MeshProjectionLODs[LODIndex].ValidSectionIndices;
	const uint32 ValidSectionCount = ValidSectionIndices.Num();
	const uint32 PassCount = FMath::DivideAndRoundUp(ValidSectionCount, FHairUpdateMeshTriangleCS::SectionArrayCount);

	FHairUpdateMeshTriangleCS::FParameters CommonParameters;

	FRDGImportedBuffer OutputBuffers[3];
	const bool bEnableUAVOverlap = true;
	const ERDGUnorderedAccessViewFlags UAVFlags = bEnableUAVOverlap ? ERDGUnorderedAccessViewFlags::SkipBarrier : ERDGUnorderedAccessViewFlags::None;
	CommonParameters.RootTriangleIndex = RegisterAsSRV(GraphBuilder, RestLODData.RootTriangleIndexBuffer);
	if (Type == HairStrandsTriangleType::RestPose)
	{
		OutputBuffers[0] = Register(GraphBuilder, RestLODData.RestRootTrianglePosition0Buffer, ERDGImportedBufferFlags::CreateUAV, UAVFlags);
		OutputBuffers[1] = Register(GraphBuilder, RestLODData.RestRootTrianglePosition1Buffer, ERDGImportedBufferFlags::CreateUAV, UAVFlags);
		OutputBuffers[2] = Register(GraphBuilder, RestLODData.RestRootTrianglePosition2Buffer, ERDGImportedBufferFlags::CreateUAV, UAVFlags);
	}
	else if (Type == HairStrandsTriangleType::DeformedPose)
	{
		FHairStrandsDeformedRootResource::FLOD& DeformedLODData = DeformedResources->LODs[LODIndex];
		OutputBuffers[0] = Register(GraphBuilder, DeformedLODData.DeformedRootTrianglePosition0Buffer, ERDGImportedBufferFlags::CreateUAV, UAVFlags);
		OutputBuffers[1] = Register(GraphBuilder, DeformedLODData.DeformedRootTrianglePosition1Buffer, ERDGImportedBufferFlags::CreateUAV, UAVFlags);
		OutputBuffers[2] = Register(GraphBuilder, DeformedLODData.DeformedRootTrianglePosition2Buffer, ERDGImportedBufferFlags::CreateUAV, UAVFlags);
		DeformedLODData.Status = FHairStrandsDeformedRootResource::FLOD::EStatus::Completed;
	}
	else
	{
		// error
		return;
	}

	CommonParameters.OutRootTrianglePosition0 = OutputBuffers[0].UAV;
	CommonParameters.OutRootTrianglePosition1 = OutputBuffers[1].UAV;
	CommonParameters.OutRootTrianglePosition2 = OutputBuffers[2].UAV;

	for (uint32 PassIt = 0; PassIt < PassCount; ++PassIt)
	{
		FHairUpdateMeshTriangleCS::FParameters* Parameters = &CommonParameters;
		Parameters->MaxRootCount = RootCount;
		Parameters->MaxSectionCount = MeshData.Sections.Num();
		Parameters->Pass_SectionStart = PassIt * FHairUpdateMeshTriangleCS::SectionArrayCount;
		Parameters->Pass_SectionCount = FMath::Min(ValidSectionCount - Parameters->Pass_SectionStart, FHairUpdateMeshTriangleCS::SectionArrayCount);

		// Most often, a skeletal mesh will have many sections, but *most* of will share the same vertex buffers. 
		// So NumSection >> NumBuffer, and the limitation of MaxSectionBufferCount = 8;
		const uint32 MaxSectionBufferCount = 8;
		struct FMeshSectionBuffers
		{
			uint32 MeshSectionBufferIndex = 0;
			FRDGBufferSRVRef RDGPositionBuffer = nullptr;
			FRHIShaderResourceView* PositionBuffer = nullptr;
			FRHIShaderResourceView* IndexBuffer = nullptr;
			FRHIShaderResourceView* UVsBuffer = nullptr;
		};
		TMap<FRHIShaderResourceView*, FMeshSectionBuffers>	UniqueMeshSectionBuffers;
		TMap<FRDGBufferSRVRef, FMeshSectionBuffers>		UniqueMeshSectionBuffersRDG;
		uint32 UniqueMeshSectionBufferIndex = 0;

		auto SetMeshSectionBuffers = [Parameters](uint32 UniqueIndex, const FHairStrandsProjectionMeshData::Section& MeshSectionData)
		{
			switch (UniqueIndex)
			{
			case 0:
			{
				Parameters->RDGMeshPositionBuffer0 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer0 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer0 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer0 = MeshSectionData.UVsBuffer;
				break;
			}
			case 1:
			{
				Parameters->RDGMeshPositionBuffer1 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer1 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer1 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer1 = MeshSectionData.UVsBuffer;
				break;
			}
			case 2:
			{
				Parameters->RDGMeshPositionBuffer2 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer2 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer2 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer2 = MeshSectionData.UVsBuffer;
				break;
			}
			case 3:
			{
				Parameters->RDGMeshPositionBuffer3 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer3 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer3 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer3 = MeshSectionData.UVsBuffer;
				break;
			}
			case 4:
			{
				Parameters->RDGMeshPositionBuffer4 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer4 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer4 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer4 = MeshSectionData.UVsBuffer;
				break;
			}
			case 5:
			{
				Parameters->RDGMeshPositionBuffer5 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer5 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer5 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer5 = MeshSectionData.UVsBuffer;
				break;
			}
			case 6:
			{
				Parameters->RDGMeshPositionBuffer6 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer6 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer6 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer6 = MeshSectionData.UVsBuffer;
				break;
			}
			case 7:
			{
				Parameters->RDGMeshPositionBuffer7 = MeshSectionData.RDGPositionBuffer;
				Parameters->MeshPositionBuffer7 = MeshSectionData.PositionBuffer;
				Parameters->MeshIndexBuffer7 = MeshSectionData.IndexBuffer;
				Parameters->MeshUVsBuffer7 = MeshSectionData.UVsBuffer;
				break;
			}
			}
		};

		bool bUseRDGPositionBuffer = false;
		for (int32 SectionStartIt = Parameters->Pass_SectionStart, SectionItEnd = Parameters->Pass_SectionStart + Parameters->Pass_SectionCount; SectionStartIt < SectionItEnd; ++SectionStartIt)
		{
			const int32 SectionIt = SectionStartIt - Parameters->Pass_SectionStart;
			const int32 SectionIndex = ValidSectionIndices[SectionStartIt];

			if (SectionIndex < 0 || SectionIndex >= MeshData.Sections.Num())
			{
				continue;
			}

			const FHairStrandsProjectionMeshData::Section& MeshSectionData = MeshData.Sections[SectionIndex];

			const FMeshSectionBuffers* Buffers = nullptr;
			if (MeshSectionData.PositionBuffer)
			{
				Buffers = UniqueMeshSectionBuffers.Find(MeshSectionData.PositionBuffer);
			}
			else if (MeshSectionData.RDGPositionBuffer)
			{
				Buffers = UniqueMeshSectionBuffersRDG.Find(MeshSectionData.RDGPositionBuffer);
			}
			else
			{
				check(false); // Should never happen
				continue;
			}

			if (Buffers != nullptr)
			{
				// Insure that all buffers actually match
				check(Buffers->RDGPositionBuffer == MeshSectionData.RDGPositionBuffer);
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

				FMeshSectionBuffers Entry;
				Entry.MeshSectionBufferIndex = UniqueMeshSectionBufferIndex;
				Entry.RDGPositionBuffer = MeshSectionData.RDGPositionBuffer;
				Entry.PositionBuffer = MeshSectionData.PositionBuffer;
				Entry.IndexBuffer = MeshSectionData.IndexBuffer;
				Entry.UVsBuffer = MeshSectionData.UVsBuffer;

				if (MeshSectionData.PositionBuffer)
				{
					UniqueMeshSectionBuffers.Add(MeshSectionData.PositionBuffer, Entry);
				}
				else if (MeshSectionData.RDGPositionBuffer)
				{
					UniqueMeshSectionBuffersRDG.Add(MeshSectionData.RDGPositionBuffer, Entry);
				}

				Parameters->MeshSectionBufferIndex[SectionIt] = UniqueMeshSectionBufferIndex;
				++UniqueMeshSectionBufferIndex;
			}

			Parameters->MeshSectionIndices[SectionIt] = MeshSectionData.SectionIndex;
			Parameters->MeshMaxIndexCount[SectionIt] = MeshSectionData.TotalIndexCount;
			Parameters->MeshMaxVertexCount[SectionIt] = MeshSectionData.TotalVertexCount;
			Parameters->MeshIndexOffset[SectionIt] = MeshSectionData.IndexBaseIndex;
			Parameters->MeshUVsChannelOffset[SectionIt] = MeshSectionData.UVsChannelOffset;
			Parameters->MeshUVsChannelCount[SectionIt] = MeshSectionData.UVsChannelCount;

			// Sanity check
			// If one of the input is using RDG position, we expect all mesh sections to use RDG input
			if (bUseRDGPositionBuffer)
			{
				check(MeshSectionData.RDGPositionBuffer != nullptr);
			}
			else if (MeshSectionData.RDGPositionBuffer != nullptr)
			{
				bUseRDGPositionBuffer = true;
			}
		}

		if (MeshData.Sections.Num() > 0)
		{
			for (uint32 Index = UniqueMeshSectionBufferIndex; Index < MaxSectionBufferCount; ++Index)
			{
				SetMeshSectionBuffers(Index, MeshData.Sections[0]);
			}
		}

		if (UniqueMeshSectionBufferIndex == 0 || Parameters->MeshUVsBuffer0 == nullptr)
		{
			return;
		}

		FHairUpdateMeshTriangleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairUpdateMeshTriangleCS::FParameters>();
		*PassParameters = *Parameters;

		FHairUpdateMeshTriangleCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FHairUpdateMeshTriangleCS::FUpdateUVs>(1);
		PermutationVector.Set<FHairUpdateMeshTriangleCS::FPositionType>(bUseRDGPositionBuffer ? 1 : 0);

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootCount, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FHairUpdateMeshTriangleCS> ComputeShader(ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsTriangleMeshUpdate"),
			ComputeShader,
			PassParameters,
			DispatchGroupCount);
	}

	GraphBuilder.SetBufferAccessFinal(OutputBuffers[0].Buffer, ERHIAccess::SRVMask);
	GraphBuilder.SetBufferAccessFinal(OutputBuffers[1].Buffer, ERHIAccess::SRVMask);
	GraphBuilder.SetBufferAccessFinal(OutputBuffers[2].Buffer, ERHIAccess::SRVMask);
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

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RestSamplePositionsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MeshSampleWeightsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestRootTrianglePosition0)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestRootTrianglePosition1)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestRootTrianglePosition2)

		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutDeformedRootTrianglePosition0)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutDeformedRootTrianglePosition1)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutDeformedRootTrianglePosition2)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ROOTTRIANGLE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairInterpolateMeshTriangleCS, "/Engine/Private/HairStrands/HairStrandsMeshInterpolate.usf", "MainCS", SF_Compute);

void AddHairStrandInterpolateMeshTrianglesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsRestRootResource* RestResources,
	FHairStrandsDeformedRootResource* DeformedResources)
{
	const uint32 RootCount = RestResources->RootData.RootCount;
	if (RootCount == 0 || LODIndex < 0 || LODIndex >= RestResources->LODs.Num() || LODIndex >= DeformedResources->LODs.Num())
	{
		return;
	}
	FHairStrandsRestRootResource::FLOD& RestLODData = RestResources->LODs[LODIndex];
	FHairStrandsDeformedRootResource::FLOD& DeformedLODData = DeformedResources->LODs[LODIndex];
	check(RestLODData.LODIndex == LODIndex);
	check(DeformedLODData.LODIndex == LODIndex);

	FHairInterpolateMeshTriangleCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairInterpolateMeshTriangleCS::FParameters>();
	Parameters->MaxRootCount = RootCount;
	Parameters->MaxSampleCount = RestLODData.SampleCount;

	FRDGImportedBuffer OutDeformedRootTrianglePosition0 = Register(GraphBuilder, DeformedLODData.DeformedRootTrianglePosition0Buffer, ERDGImportedBufferFlags::CreateUAV);
	FRDGImportedBuffer OutDeformedRootTrianglePosition1 = Register(GraphBuilder, DeformedLODData.DeformedRootTrianglePosition1Buffer, ERDGImportedBufferFlags::CreateUAV);
	FRDGImportedBuffer OutDeformedRootTrianglePosition2 = Register(GraphBuilder, DeformedLODData.DeformedRootTrianglePosition2Buffer, ERDGImportedBufferFlags::CreateUAV);

	Parameters->RestRootTrianglePosition0 = RegisterAsSRV(GraphBuilder, RestLODData.RestRootTrianglePosition0Buffer);
	Parameters->RestRootTrianglePosition1 = RegisterAsSRV(GraphBuilder, RestLODData.RestRootTrianglePosition1Buffer);
	Parameters->RestRootTrianglePosition2 = RegisterAsSRV(GraphBuilder, RestLODData.RestRootTrianglePosition2Buffer);

	Parameters->OutDeformedRootTrianglePosition0 = OutDeformedRootTrianglePosition0.UAV;
	Parameters->OutDeformedRootTrianglePosition1 = OutDeformedRootTrianglePosition1.UAV;
	Parameters->OutDeformedRootTrianglePosition2 = OutDeformedRootTrianglePosition2.UAV;

	Parameters->MeshSampleWeightsBuffer		= RegisterAsSRV(GraphBuilder, DeformedLODData.MeshSampleWeightsBuffer);
	Parameters->RestSamplePositionsBuffer	= RegisterAsSRV(GraphBuilder, RestLODData.RestSamplePositionsBuffer);

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootCount, 128);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FHairInterpolateMeshTriangleCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTriangleMeshInterpolate"),
		ComputeShader,
		Parameters,
		DispatchGroupCount);

	GraphBuilder.SetBufferAccessFinal(OutDeformedRootTrianglePosition0.Buffer, ERHIAccess::SRVMask);
	GraphBuilder.SetBufferAccessFinal(OutDeformedRootTrianglePosition1.Buffer, ERHIAccess::SRVMask);
	GraphBuilder.SetBufferAccessFinal(OutDeformedRootTrianglePosition2.Buffer, ERHIAccess::SRVMask);
}


///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairMeshesInterpolateCS : public FGlobalShader
{
private:
	DECLARE_GLOBAL_SHADER(FHairMeshesInterpolateCS);
	SHADER_USE_PARAMETER_STRUCT(FHairMeshesInterpolateCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, MaxSampleCount)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RestSamplePositionsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MeshSampleWeightsBuffer)

		SHADER_PARAMETER_SRV(Buffer, RestPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, OutDeformedPositionBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_HAIRMESHES"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairMeshesInterpolateCS, "/Engine/Private/HairStrands/HairStrandsMeshInterpolate.usf", "MainHairMeshesCS", SF_Compute);

template<typename TRestResource, typename TDeformedResource>
void InternalAddHairRBFInterpolationPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 MeshLODIndex,
	TRestResource* RestResources,
	TDeformedResource* DeformedResources,
	FHairStrandsRestRootResource* RestRootResources,
	FHairStrandsDeformedRootResource* DeformedRootResources)
{
	const uint32 VertexCount = RestResources ? RestResources->VertexCount : 0;
	if (!RestResources || !DeformedResources || !RestRootResources || !DeformedRootResources || VertexCount == 0 || MeshLODIndex >= RestRootResources->LODs.Num())
	{
		return;
	}

	FHairStrandsRestRootResource::FLOD& RestLODData = RestRootResources->LODs[MeshLODIndex];
	FHairStrandsDeformedRootResource::FLOD& DeformedLODData = DeformedRootResources->LODs[MeshLODIndex];
	FHairMeshesInterpolateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairMeshesInterpolateCS::FParameters>();
	Parameters->VertexCount = VertexCount;
	Parameters->MaxSampleCount = RestLODData.SampleCount;

	FRDGImportedBuffer OutDeformedPositionBuffer = Register(GraphBuilder, DeformedResources->GetBuffer(TDeformedResource::EFrameType::Current), ERDGImportedBufferFlags::CreateUAV);
	Parameters->RestPositionBuffer			= RestResources->RestPositionBuffer.ShaderResourceViewRHI;
	Parameters->OutDeformedPositionBuffer	= OutDeformedPositionBuffer.UAV;

	Parameters->RestSamplePositionsBuffer	= RegisterAsSRV(GraphBuilder, RestLODData.RestSamplePositionsBuffer);
	Parameters->MeshSampleWeightsBuffer		= RegisterAsSRV(GraphBuilder, DeformedLODData.MeshSampleWeightsBuffer);

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(VertexCount, 128);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FHairMeshesInterpolateCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairInterpolationRBF"),
		ComputeShader,
		Parameters,
		DispatchGroupCount);

	GraphBuilder.SetBufferAccessFinal(OutDeformedPositionBuffer.Buffer, ERHIAccess::SRVMask);
}

void AddHairMeshesRBFInterpolationPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 MeshLODIndex,
	FHairMeshesRestResource* RestResources,
	FHairMeshesDeformedResource* DeformedResources,
	FHairStrandsRestRootResource* RestRootResources,
	FHairStrandsDeformedRootResource* DeformedRootResources)
{
	InternalAddHairRBFInterpolationPass(
		GraphBuilder,
		ShaderMap,
		MeshLODIndex,
		RestResources,
		DeformedResources,
		RestRootResources,
		DeformedRootResources);
}

void AddHairCardsRBFInterpolationPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 MeshLODIndex,
	FHairCardsRestResource* RestResources,
	FHairCardsDeformedResource* DeformedResources,
	FHairStrandsRestRootResource* RestRootResources,
	FHairStrandsDeformedRootResource* DeformedRootResources)
{
	InternalAddHairRBFInterpolationPass(
		GraphBuilder,
		ShaderMap,
		MeshLODIndex,
		RestResources,
		DeformedResources,
		RestRootResources,
		DeformedRootResources);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//
class FHairInitMeshSamplesCS : public FGlobalShader
{
public:
	const static uint32 SectionArrayCount = 16; // This defines the number of sections managed for each iteration pass
private:
	DECLARE_GLOBAL_SHADER(FHairInitMeshSamplesCS);
	SHADER_USE_PARAMETER_STRUCT(FHairInitMeshSamplesCS, FGlobalShader);

	class FPositionType : SHADER_PERMUTATION_INT("PERMUTATION_POSITION_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FPositionType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxSampleCount)
		SHADER_PARAMETER(uint32, MaxVertexCount)
		SHADER_PARAMETER(uint32, PassSectionCount)

		SHADER_PARAMETER_ARRAY(uint32, SectionVertexOffset, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, SectionVertexCount, [SectionArrayCount])
		SHADER_PARAMETER_ARRAY(uint32, SectionBufferIndex, [SectionArrayCount])

		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer0)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer1)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer2)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer3)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer4)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer5)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer6)
		SHADER_PARAMETER_SRV(Buffer, VertexPositionsBuffer7)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer0)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer1)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer2)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer3)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer4)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer5)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer6)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RDGVertexPositionsBuffer7)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SampleIndicesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutSamplePositionsBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_SECTION_COUNT"), SectionArrayCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairInitMeshSamplesCS, "/Engine/Private/HairStrands/HairStrandsSamplesInit.usf", "MainCS", SF_Compute);

void AddHairStrandInitMeshSamplesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsRestRootResource* RestResources,
	FHairStrandsDeformedRootResource* DeformedResources)
{
	if (LODIndex < 0)
	{
		return;
	}

	if (Type == HairStrandsTriangleType::RestPose && LODIndex >= RestResources->LODs.Num())
	{
		return;
	}

	if (Type == HairStrandsTriangleType::DeformedPose && (LODIndex >= RestResources->LODs.Num() || LODIndex >= DeformedResources->LODs.Num()))
	{
		return;
	}

	FHairStrandsRestRootResource::FLOD& RestLODData = RestResources->LODs[LODIndex];
	check(RestLODData.LODIndex == LODIndex);

	const uint32 SectionCount = MeshData.Sections.Num();
	const uint32 MaxSupportedSectionCount = GetHairStrandsMaxSectionCount();
	check(SectionCount < MaxSupportedSectionCount);
	if (SectionCount == 0 || SectionCount >= MaxSupportedSectionCount)
	{
		return;
	}

	// When the number of section of a mesh is above FHairUpdateMeshTriangleCS::SectionArrayCount, the update is split into several passes
	const TArray<uint32>& ValidSectionIndices = RestResources->RootData.MeshProjectionLODs[LODIndex].ValidSectionIndices;
	const uint32 ValidSectionCount = ValidSectionIndices.Num();
	const uint32 PassCount = FMath::DivideAndRoundUp(ValidSectionCount, FHairInitMeshSamplesCS::SectionArrayCount);

	if (SectionCount > 0 && RestLODData.SampleCount > 0)
	{
		FRDGImportedBuffer OutBuffer;
		if (Type == HairStrandsTriangleType::RestPose)
		{
			OutBuffer = Register(GraphBuilder, RestLODData.RestSamplePositionsBuffer, ERDGImportedBufferFlags::CreateUAV);
		}
		else if (Type == HairStrandsTriangleType::DeformedPose)
		{
			FHairStrandsDeformedRootResource::FLOD& DeformedLODData = DeformedResources->LODs[LODIndex];
			check(DeformedLODData.LODIndex == LODIndex);

			OutBuffer = Register(GraphBuilder, DeformedLODData.DeformedSamplePositionsBuffer, ERDGImportedBufferFlags::CreateUAV);
		}
		else
		{
			return;
		}
		for (uint32 PassIt = 0; PassIt < PassCount; ++PassIt)
		{
			FHairInitMeshSamplesCS::FParameters Parameters;

			for (uint32 DataIndex = 0; DataIndex < FHairInitMeshSamplesCS::SectionArrayCount; ++DataIndex)
			{
				Parameters.SectionBufferIndex[DataIndex] = 0;
				Parameters.SectionVertexOffset[DataIndex] = 0;
				Parameters.SectionVertexCount[DataIndex] = 0;
			}

			const int32 PassSectionStart = PassIt * FHairInitMeshSamplesCS::SectionArrayCount;
			const int32 PassSectionCount = FMath::Min(ValidSectionCount - PassSectionStart, FHairInitMeshSamplesCS::SectionArrayCount);
			Parameters.PassSectionCount = PassSectionCount;

			struct FMeshSectionBuffers
			{
				uint32 SectionBufferIndex = 0;
				FRDGBufferSRVRef RDGPositionBuffer = nullptr;
				FRHIShaderResourceView* PositionBuffer = nullptr;
			};
			TMap<FRHIShaderResourceView*, FMeshSectionBuffers>	UniqueMeshSectionBuffers;
			TMap<FRDGBufferSRVRef, FMeshSectionBuffers>		UniqueMeshSectionBuffersRDG;

			auto SetMeshSectionBuffers = [&Parameters](uint32 UniqueIndex, const FHairStrandsProjectionMeshData::Section& MeshSectionData)
			{
				switch (UniqueIndex)
				{
				case 0:
				{
					Parameters.RDGVertexPositionsBuffer0 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer0 = MeshSectionData.PositionBuffer;
					break;
				}
				case 1:
				{
					Parameters.RDGVertexPositionsBuffer1 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer1 = MeshSectionData.PositionBuffer;
					break;
				}
				case 2:
				{
					Parameters.RDGVertexPositionsBuffer2 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer2 = MeshSectionData.PositionBuffer;
					break;
				}
				case 3:
				{
					Parameters.RDGVertexPositionsBuffer3 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer3 = MeshSectionData.PositionBuffer;
					break;
				}
				case 4:
				{
					Parameters.RDGVertexPositionsBuffer4 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer4 = MeshSectionData.PositionBuffer;
					break;
				}
				case 5:
				{
					Parameters.RDGVertexPositionsBuffer5 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer5 = MeshSectionData.PositionBuffer;
					break;
				}
				case 6:
				{
					Parameters.RDGVertexPositionsBuffer6 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer6 = MeshSectionData.PositionBuffer;
					break;
				}
				case 7:
				{
					Parameters.RDGVertexPositionsBuffer7 = MeshSectionData.RDGPositionBuffer;
					Parameters.VertexPositionsBuffer7 = MeshSectionData.PositionBuffer;
					break;
				}
				}
			};
			uint32 UniqueMeshSectionBufferIndex = 0;
			bool bUseRDGPositionBuffer = false;
			for (int32 SectionStartIt = PassSectionStart, SectionItEnd = PassSectionStart + PassSectionCount; SectionStartIt < SectionItEnd; ++SectionStartIt)
			{
				const int32 SectionIt = SectionStartIt - PassSectionStart;
				const int32 SectionIndex = ValidSectionIndices[SectionStartIt];

				const FHairStrandsProjectionMeshData::Section& MeshSectionData = MeshData.Sections[SectionIndex];

				Parameters.SectionVertexOffset[SectionIt] = MeshSectionData.VertexBaseIndex;
				Parameters.SectionVertexCount[SectionIt] = MeshSectionData.NumVertices;

				const FMeshSectionBuffers* Buffers = nullptr;
				if (MeshSectionData.PositionBuffer)
				{
					Buffers = UniqueMeshSectionBuffers.Find(MeshSectionData.PositionBuffer);
				}
				else if (MeshSectionData.RDGPositionBuffer)
				{
					Buffers = UniqueMeshSectionBuffersRDG.Find(MeshSectionData.RDGPositionBuffer);
				}
				else
				{
					check(false); // Should never happen
					continue;
				}
				if (Buffers != nullptr)
				{
					Parameters.SectionBufferIndex[SectionIt] = Buffers->SectionBufferIndex;
				}
				else
				{
					// Only support 8 unique different buffer at the moment
					check(UniqueMeshSectionBufferIndex < 8);
					SetMeshSectionBuffers(UniqueMeshSectionBufferIndex, MeshSectionData);

					FMeshSectionBuffers Entry;
					Entry.SectionBufferIndex = UniqueMeshSectionBufferIndex;
					Entry.RDGPositionBuffer = MeshSectionData.RDGPositionBuffer;
					Entry.PositionBuffer = MeshSectionData.PositionBuffer;

					if (MeshSectionData.PositionBuffer)
					{
						UniqueMeshSectionBuffers.Add(MeshSectionData.PositionBuffer, Entry);
					}
					else if (MeshSectionData.RDGPositionBuffer)
					{
						UniqueMeshSectionBuffersRDG.Add(MeshSectionData.RDGPositionBuffer, Entry);
					}

					Parameters.SectionBufferIndex[SectionIt] = UniqueMeshSectionBufferIndex;
					++UniqueMeshSectionBufferIndex;
				}

				// Sanity check
				// If one of the input is using RDG position, we expect all mesh sections to use RDG input
				if (bUseRDGPositionBuffer)
				{
					check(MeshSectionData.RDGPositionBuffer != nullptr);
				}
				else if (MeshSectionData.RDGPositionBuffer != nullptr)
				{
					bUseRDGPositionBuffer = true;
				}
			}

			if (MeshData.Sections.Num() > 0)
			{
				for (uint32 Index = UniqueMeshSectionBufferIndex; Index < 8; ++Index)
				{
					SetMeshSectionBuffers(Index, MeshData.Sections[0]);
				}
			}

			if (UniqueMeshSectionBufferIndex == 0)
			{
				return;
			}
			Parameters.MaxVertexCount = MeshData.Sections[0].TotalVertexCount;
			Parameters.MaxSampleCount = RestLODData.SampleCount;
			Parameters.SampleIndicesBuffer = RegisterAsSRV(GraphBuilder, RestLODData.MeshSampleIndicesBuffer);
			Parameters.OutSamplePositionsBuffer = OutBuffer.UAV;

			FHairInitMeshSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairInitMeshSamplesCS::FParameters>();
			*PassParameters = Parameters;

			FHairInitMeshSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FHairInitMeshSamplesCS::FPositionType>(bUseRDGPositionBuffer ? 1 : 0);

			const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RestLODData.SampleCount, 128);
			check(DispatchGroupCount.X < 65536);
			TShaderMapRef<FHairInitMeshSamplesCS> ComputeShader(ShaderMap, PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("HairStrandsInitMeshSamples"),
				ComputeShader,
				PassParameters,
				DispatchGroupCount);
		}
		GraphBuilder.SetBufferAccessFinal(OutBuffer.Buffer, ERHIAccess::SRVMask);
	}
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

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SampleIndicesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InterpolationWeightsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, SampleRestPositionsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, SampleDeformedPositionsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutSampleDeformationsBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairUpdateMeshSamplesCS, "/Engine/Private/HairStrands/HairStrandsSamplesUpdate.usf", "MainCS", SF_Compute);

void AddHairStrandUpdateMeshSamplesPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& MeshData,
	FHairStrandsRestRootResource* RestResources,
	FHairStrandsDeformedRootResource* DeformedResources)
{
	if (LODIndex < 0 || LODIndex >= RestResources->LODs.Num() || LODIndex >= DeformedResources->LODs.Num())
	{
		return;
	}

	FHairStrandsRestRootResource::FLOD& RestLODData = RestResources->LODs[LODIndex];
	FHairStrandsDeformedRootResource::FLOD& DeformedLODData = DeformedResources->LODs[LODIndex];
	check(RestLODData.LODIndex == LODIndex);
	check(DeformedLODData.LODIndex == LODIndex);

	const uint32 SectionCount = MeshData.Sections.Num();
	if (SectionCount > 0 && RestLODData.SampleCount > 0)
	{
		FHairUpdateMeshSamplesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairUpdateMeshSamplesCS::FParameters>();

		FRDGImportedBuffer OutWeightsBuffer = Register(GraphBuilder, DeformedLODData.MeshSampleWeightsBuffer, ERDGImportedBufferFlags::CreateUAV);

		Parameters->MaxSampleCount					= RestLODData.SampleCount;
		Parameters->SampleIndicesBuffer				= RegisterAsSRV(GraphBuilder, RestLODData.MeshSampleIndicesBuffer);
		Parameters->InterpolationWeightsBuffer		= RegisterAsSRV(GraphBuilder, RestLODData.MeshInterpolationWeightsBuffer);
		Parameters->SampleRestPositionsBuffer		= RegisterAsSRV(GraphBuilder, RestLODData.RestSamplePositionsBuffer);
		Parameters->SampleDeformedPositionsBuffer	= RegisterAsSRV(GraphBuilder, DeformedLODData.DeformedSamplePositionsBuffer);
		Parameters->OutSampleDeformationsBuffer		= OutWeightsBuffer.UAV;

		const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RestLODData.SampleCount+4, 128);
		check(DispatchGroupCount.X < 65536);
		TShaderMapRef<FHairUpdateMeshSamplesCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsUpdateMeshSamples"),
			ComputeShader,
			Parameters,
			DispatchGroupCount);

		GraphBuilder.SetBufferAccessFinal(OutWeightsBuffer.Buffer, ERHIAccess::SRVMask);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////

// Generate follicle mask texture
BEGIN_SHADER_PARAMETER_STRUCT(FHairFollicleMaskParameters, )
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER(uint32, MaxRootCount)
	SHADER_PARAMETER(uint32, Channel)
	SHADER_PARAMETER(uint32, KernelSizeInPixels)

	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TrianglePosition0Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TrianglePosition1Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TrianglePosition2Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RootBarycentricBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RootUVsBuffer)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairFollicleMask : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
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

	class FUVType : SHADER_PERMUTATION_INT("PERMUTATION_UV_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FUVType>;

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
	FHairStrandsRestRootResource* RestResources,
	FRDGTextureRef OutTexture)
{
	const uint32 RootCount = RestResources->RootData.RootCount;
	if (LODIndex >= uint32(RestResources->LODs.Num()) || RootCount == 0)
		return;

	FHairStrandsRestRootResource::FLOD& LODData = RestResources->LODs[LODIndex];
	if (!LODData.RootTriangleBarycentricBuffer.Buffer ||
		!LODData.RestRootTrianglePosition0Buffer.Buffer ||
		!LODData.RestRootTrianglePosition1Buffer.Buffer ||
		!LODData.RestRootTrianglePosition2Buffer.Buffer)
		return;

	const FIntPoint OutputResolution = OutTexture->Desc.Extent;
	FHairFollicleMaskParameters* Parameters = GraphBuilder.AllocParameters<FHairFollicleMaskParameters>();
	Parameters->TrianglePosition0Buffer = RegisterAsSRV(GraphBuilder, LODData.RestRootTrianglePosition0Buffer);
	Parameters->TrianglePosition1Buffer = RegisterAsSRV(GraphBuilder, LODData.RestRootTrianglePosition1Buffer);
	Parameters->TrianglePosition2Buffer = RegisterAsSRV(GraphBuilder, LODData.RestRootTrianglePosition2Buffer);
	Parameters->RootBarycentricBuffer   = RegisterAsSRV(GraphBuilder, LODData.RootTriangleBarycentricBuffer);
	Parameters->RootUVsBuffer = nullptr;
	Parameters->OutputResolution = OutputResolution;
	Parameters->MaxRootCount = RootCount;
	Parameters->Channel = FMath::Min(Channel, 3u);
	Parameters->KernelSizeInPixels = FMath::Clamp(KernelSizeInPixels, 2u, 200u);
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTexture, bNeedClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, 0);

	FHairFollicleMaskVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairFollicleMaskVS::FUVType>(0); // Mesh UVs

	TShaderMapRef<FHairFollicleMaskVS> VertexShader(ShaderMap, PermutationVector);
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

static void AddFollicleMaskPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const bool bNeedClear,
	const uint32 KernelSizeInPixels,
	const uint32 Channel,
	const uint32 RootCount,
	const FRDGBufferRef RootUVBuffer,
	FRDGTextureRef OutTexture)
{
	const FIntPoint OutputResolution = OutTexture->Desc.Extent;
	FHairFollicleMaskParameters* Parameters = GraphBuilder.AllocParameters<FHairFollicleMaskParameters>();
	Parameters->TrianglePosition0Buffer = nullptr;
	Parameters->TrianglePosition1Buffer = nullptr;
	Parameters->TrianglePosition2Buffer = nullptr;
	Parameters->RootBarycentricBuffer = nullptr;
	Parameters->RootUVsBuffer = GraphBuilder.CreateSRV(RootUVBuffer, PF_G32R32F);
	Parameters->OutputResolution = OutputResolution;
	Parameters->MaxRootCount = RootCount;
	Parameters->Channel = FMath::Min(Channel, 3u);
	Parameters->KernelSizeInPixels = FMath::Clamp(KernelSizeInPixels, 2u, 200u);
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTexture, bNeedClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, 0);

	FHairFollicleMaskVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairFollicleMaskVS::FUVType>(1); // Groom root's UV

	TShaderMapRef<FHairFollicleMaskVS> VertexShader(ShaderMap, PermutationVector);
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
	const EPixelFormat Format,
	const FIntPoint Resolution,
	const uint32 MipCount,
	const uint32 KernelSizeInPixels,
	const uint32 Channel,
	const int32 LODIndex,
	FHairStrandsRestRootResource* RestResources,
	FRDGTextureRef& OutTexture)
{
	const FLinearColor ClearColor(0.0f, 0.f, 0.f, 0.f);

	bool bClear = OutTexture == nullptr;
	if (OutTexture == nullptr)
	{
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(Resolution, Format, FClearValueBinding(ClearColor), TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, MipCount);
		OutTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("FollicleMask"));
	}

	AddFollicleMaskPass(GraphBuilder, ShaderMap, bClear, KernelSizeInPixels, Channel, LODIndex, RestResources, OutTexture);
}

void GenerateFolliculeMask(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const EPixelFormat Format,
	const FIntPoint Resolution,
	const uint32 MipCount,
	const uint32 KernelSizeInPixels,
	const uint32 Channel,
	const TArray<FRDGBufferRef>& RootUVBuffers,
	FRDGTextureRef& OutTexture)
{
	const FLinearColor ClearColor(0.0f, 0.f, 0.f, 0.f);

	bool bClear = OutTexture == nullptr;
	if (OutTexture == nullptr)
	{
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(Resolution, Format, FClearValueBinding(ClearColor), TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, MipCount);
		OutTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("FollicleMask"));
	}

	for (const FRDGBufferRef& RootUVBuffer : RootUVBuffers)
	{
		const uint32 RootCount = RootUVBuffer->Desc.NumElements;
		AddFollicleMaskPass(GraphBuilder, ShaderMap, bClear, KernelSizeInPixels, Channel, RootCount, RootUVBuffer, OutTexture);
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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
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
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader, TargetResolution](FRHICommandList& RHICmdList)
		{
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntPoint(TargetResolution, TargetResolution), FIntPoint(8, 8));
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, GroupCount);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairUpdatePositionOffsetCS : public FGlobalShader
{
public:
private:
	DECLARE_GLOBAL_SHADER(FHairUpdatePositionOffsetCS);
	SHADER_USE_PARAMETER_STRUCT(FHairUpdatePositionOffsetCS, FGlobalShader);

	class FUseGPUOffset : SHADER_PERMUTATION_BOOL("PERMUTATION_USE_GPU_OFFSET");
	using FPermutationDomain = TShaderPermutationDomain<FUseGPUOffset>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector, CPUPositionOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RootTrianglePosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutOffsetBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_OFFSET_UPDATE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairUpdatePositionOffsetCS, "/Engine/Private/HairStrands/HairStrandsMeshUpdate.usf", "MainCS", SF_Compute);

void AddHairStrandUpdatePositionOffsetPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	FHairStrandsDeformedRootResource* DeformedRootResources,
	FHairStrandsDeformedResource* DeformedResources)
{
	if ((DeformedRootResources && LODIndex < 0) || DeformedResources == nullptr)
	{
		return;
	}

	FRDGImportedBuffer OutPositionOffsetBuffer    = Register(GraphBuilder, DeformedResources->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current), ERDGImportedBufferFlags::CreateUAV);
	FRDGImportedBuffer RootTrianglePositionBuffer;
	if (DeformedRootResources)
	{
		RootTrianglePositionBuffer = Register(GraphBuilder, DeformedRootResources->LODs[LODIndex].DeformedRootTrianglePosition0Buffer, ERDGImportedBufferFlags::CreateSRV);
	}

	const bool bUseGPUOffset = DeformedRootResources != nullptr && GHairStrandsUseGPUPositionOffset > 0;
	const uint32 OffsetIndex = DeformedResources->GetIndex(FHairStrandsDeformedResource::Current);
	FHairUpdatePositionOffsetCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairUpdatePositionOffsetCS::FParameters>();
	PassParameters->CPUPositionOffset = DeformedResources->PositionOffset[OffsetIndex];
	PassParameters->RootTrianglePosition0Buffer = RootTrianglePositionBuffer.SRV;
	PassParameters->OutOffsetBuffer = OutPositionOffsetBuffer.UAV;

	FHairUpdatePositionOffsetCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairUpdatePositionOffsetCS::FUseGPUOffset>(bUseGPUOffset);
	TShaderMapRef<FHairUpdatePositionOffsetCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsUpdatePositionOffset"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));

	GraphBuilder.SetBufferAccessFinal(OutPositionOffsetBuffer.Buffer, ERHIAccess::SRVMask);
}