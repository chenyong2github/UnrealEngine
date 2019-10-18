// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsMeshProjection.h"
#include "MeshMaterialShader.h"
#include "ScenePrivate.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "MeshPassProcessor.h"
#include "RenderGraphUtils.h"

static int32 GHairProjectionMaxSpinLockCount = 4096;
static FAutoConsoleVariableRef CVarHairProjectionMaxSpinLockCount(TEXT("r.HairStrands.Projection.MaxSpinLockCount"), GHairProjectionMaxSpinLockCount, TEXT("Change the spin lock count for writing hair projection data"));

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairMeshProjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairMeshProjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FHairMeshProjectionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, bClear)
		SHADER_PARAMETER(uint32, MaxRootCount)
		SHADER_PARAMETER(uint32, MaxSpinLockCount)

		SHADER_PARAMETER(uint32, MeshNumPrimitives)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutRootTriangleDistance)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
};

IMPLEMENT_GLOBAL_SHADER(FHairMeshProjectionCS, "/Engine/Private/HairStrands/HairStrandsMeshProjection.usf", "MainCS", SF_Compute);

static void AddHairStrandMeshProjectionPass(
	FRDGBuilder& GraphBuilder,
	TShaderMap<FGlobalShaderType>* ShaderMap,
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

	FHairMeshProjectionCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairMeshProjectionCS::FParameters>();
	Parameters->bClear				= bClear ? 1 : 0;
	Parameters->MaxRootCount		= RootData.RootCount;
	Parameters->MaxSpinLockCount	= FMath::Clamp(GHairProjectionMaxSpinLockCount, 0, 100000);
	Parameters->RootPositionBuffer	= RootData.RootPositionBuffer;
	Parameters->RootNormalBuffer	= RootData.RootNormalBuffer;
	Parameters->MeshNumPrimitives	= MeshSectionData.NumPrimitives;
	Parameters->MeshSectionIndex	= MeshSectionData.SectionIndex;
	Parameters->MeshMaxIndexCount	= MeshSectionData.TotalIndexCount;
	Parameters->MeshMaxVertexCount	= MeshSectionData.TotalVertexCount;
	Parameters->MeshIndexOffset		= MeshSectionData.IndexBaseIndex;
	Parameters->MeshIndexBuffer		= MeshSectionData.IndexBuffer;
	Parameters->MeshPositionBuffer	= MeshSectionData.PositionBuffer;

	Parameters->OutRootTriangleIndex		= RootData.LODDatas[LODIndex].RootTriangleIndexBuffer->UAV;
	Parameters->OutRootTriangleBarycentrics = RootData.LODDatas[LODIndex].RootTriangleBarycentricBuffer->UAV;
	Parameters->OutRootTriangleDistance		= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RootDistanceBuffer, PF_R32_FLOAT));

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootData.RootCount, 32);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FHairMeshProjectionCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsMeshProjection"),
		*ComputeShader,
		Parameters,
		DispatchGroupCount);
}

void ProjectHairStrandsOntoMesh(
	FRHICommandListImmediate& RHICmdList, 
	TShaderMap<FGlobalShaderType>* ShaderMap,
	const int32 LODIndex,
	FHairStrandsProjectionMeshData& ProjectionMeshData, 
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData)
{
	if (LODIndex < 0 || LODIndex >= ProjectionHairData.LODDatas.Num())
		return;

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGBufferRef RootDistanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float),  ProjectionHairData.RootCount),	TEXT("HairStrandsTriangleDistance"));

	bool ClearDistance = true;
	for (FHairStrandsProjectionMeshData::Section& MeshSection : ProjectionMeshData.Sections)
	{
		check(ProjectionHairData.LODDatas[LODIndex].LODIndex == LODIndex);
		AddHairStrandMeshProjectionPass(GraphBuilder, ShaderMap, ClearDistance, LODIndex, MeshSection, ProjectionHairData, RootDistanceBuffer);
		ProjectionHairData.LODDatas[LODIndex].bIsValid = true;
		ClearDistance = false;
	}
	GraphBuilder.Execute();
}



///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairUpdateMeshTriangleCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairUpdateMeshTriangleCS);
	SHADER_USE_PARAMETER_STRUCT(FHairUpdateMeshTriangleCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector, RootTrianglePositionOffset)
		SHADER_PARAMETER(uint32, MaxRootCount)
		
		SHADER_PARAMETER(uint32, MeshSectionIndex)
		SHADER_PARAMETER(uint32, MeshMaxIndexCount)
		SHADER_PARAMETER(uint32, MeshMaxVertexCount)
		SHADER_PARAMETER(uint32, MeshIndexOffset)
		SHADER_PARAMETER_SRV(Buffer, MeshIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer, MeshPositionBuffer)

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
	TShaderMap<FGlobalShaderType>* ShaderMap,
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
	Parameters->RootTriangleIndex	= LODData.RootTriangleIndexBuffer->SRV;
	Parameters->MeshSectionIndex	= MeshSectionData.SectionIndex;
	Parameters->MeshMaxIndexCount	= MeshSectionData.TotalIndexCount;
	Parameters->MeshMaxVertexCount	= MeshSectionData.TotalVertexCount;
	Parameters->MeshIndexOffset		= MeshSectionData.IndexBaseIndex;
	Parameters->MeshIndexBuffer		= MeshSectionData.IndexBuffer;
	Parameters->MeshPositionBuffer	= MeshSectionData.PositionBuffer;

	if (Type == HairStrandsTriangleType::RestPose)
	{
		Parameters->RootTrianglePositionOffset = LODData.RestPositionOffset;
		Parameters->OutRootTrianglePosition0 = LODData.RestRootTrianglePosition0Buffer->UAV;
		Parameters->OutRootTrianglePosition1 = LODData.RestRootTrianglePosition1Buffer->UAV;
		Parameters->OutRootTrianglePosition2 = LODData.RestRootTrianglePosition2Buffer->UAV;
	}
	else if (Type == HairStrandsTriangleType::DeformedPose)
	{
		Parameters->RootTrianglePositionOffset = LODData.DeformedPositionOffset;
		Parameters->OutRootTrianglePosition0 = LODData.DeformedRootTrianglePosition0Buffer->UAV;
		Parameters->OutRootTrianglePosition1 = LODData.DeformedRootTrianglePosition1Buffer->UAV;
		Parameters->OutRootTrianglePosition2 = LODData.DeformedRootTrianglePosition2Buffer->UAV;
	}

	const FIntVector DispatchGroupCount = FComputeShaderUtils::GetGroupCount(RootData.RootCount, 32);
	check(DispatchGroupCount.X < 65536);
	TShaderMapRef<FHairUpdateMeshTriangleCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTriangleMeshUpdate"),
		*ComputeShader,
		Parameters,
		DispatchGroupCount);
}

void UpdateHairStrandsMeshTriangles(
	FRHICommandListImmediate& RHICmdList,
	TShaderMap<FGlobalShaderType>* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	FHairStrandsProjectionMeshData& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData)
{
	FRDGBuilder GraphBuilder(RHICmdList);

	for (FHairStrandsProjectionMeshData::Section& MeshSection : ProjectionMeshData.Sections)
	{
		AddHairStrandUpdateMeshTrianglesPass(GraphBuilder, ShaderMap, LODIndex, Type, MeshSection, ProjectionHairData);
	}
	
	GraphBuilder.Execute();
}