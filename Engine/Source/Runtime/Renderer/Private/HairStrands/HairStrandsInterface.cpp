// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: Hair manager implementation.
=============================================================================*/

#include "HairStrandsInterface.h"
#include "HairStrandsRendering.h"
#include "HairStrandsMeshProjection.h"

#include "SkeletalRenderPublic.h"
#include "SceneRendering.h"
#include "SystemTextures.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairRendering, Log, All);

static int32 GHairStrandsRenderingEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsRenderingEnable(TEXT("r.HairStrands.Enable"), GHairStrandsRenderingEnable, TEXT("Enable/Disable hair strands rendering"));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FHairGroupPublicData::FHairGroupPublicData(uint32 InGroupIndex, uint32 InGroupInstanceVertexCount, uint32 InClusterCount, uint32 InVertexCount)
{
	GroupIndex = InGroupIndex;
	GroupInstanceVertexCount = InGroupInstanceVertexCount;
	ClusterCount = InClusterCount;
	VertexCount = InVertexCount;
}

void FHairGroupPublicData::InitRHI()
{
	DrawIndirectBuffer.Initialize(sizeof(uint32), 4, PF_R32_UINT, BUF_DrawIndirect);
	uint32* BufferData = (uint32*)RHILockVertexBuffer(DrawIndirectBuffer.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly);
	BufferData[0] = GroupInstanceVertexCount;
	BufferData[1] = 1;
	BufferData[2] = 0;
	BufferData[3] = 0;
	RHIUnlockVertexBuffer(DrawIndirectBuffer.Buffer);

	ClusterAABBBuffer.Initialize(sizeof(int32), ClusterCount * 6, EPixelFormat::PF_R32_SINT, BUF_Static);
	GroupAABBBuffer.Initialize(sizeof(int32), 6, EPixelFormat::PF_R32_SINT, BUF_Static);

	CulledVertexIdBuffer.Initialize(sizeof(int32), VertexCount, EPixelFormat::PF_R32_UINT, BUF_Static);
	CulledVertexRadiusScaleBuffer.Initialize(sizeof(float), VertexCount, EPixelFormat::PF_R32_FLOAT, BUF_Static);
}

void FHairGroupPublicData::ReleaseRHI()
{
	DrawIndirectBuffer.Release();
	ClusterAABBBuffer.Release();
	GroupAABBBuffer.Release();
	CulledVertexIdBuffer.Release();
	CulledVertexRadiusScaleBuffer.Release();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FHairStrandsProjectionMeshData::Section ConvertMeshSection(const FCachedGeometrySection& In, const FTransform& InTransform)
{
	FHairStrandsProjectionMeshData::Section Out;
	Out.IndexBuffer = In.IndexBuffer;
	Out.PositionBuffer = In.PositionBuffer;
	Out.UVsBuffer = In.UVsBuffer;
	Out.UVsChannelOffset = In.UVsChannelOffset;
	Out.UVsChannelCount = In.UVsChannelCount;
	Out.TotalVertexCount = In.TotalVertexCount;
	Out.TotalIndexCount = In.TotalIndexCount;
	Out.VertexBaseIndex = In.VertexBaseIndex;
	Out.IndexBaseIndex = In.IndexBaseIndex;
	Out.NumPrimitives = In.NumPrimitives;
	Out.SectionIndex = In.SectionIndex;
	Out.LODIndex = In.LODIndex;
	Out.LocalToWorld = InTransform;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime execution order (on the render thread):
//  * Register
//  * For each frame
//		* Update
//		* Update triangles information for dynamic meshes
//		* RunHairStrandsInterpolation (Interpolation callback)
//  * UnRegister
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairStrandsManager
{
	struct Element
	{
		uint64 Id = 0;
		EWorldType::Type WorldType = EWorldType::None;
		FHairStrandsDebugInfo DebugInfo;
		FHairStrandsPrimitiveResources PrimitiveResources;
		FHairStrandsInterpolationData InterpolationData;
		FHairStrandsProjectionHairData RenProjectionHairDatas;
		FHairStrandsProjectionHairData SimProjectionHairDatas;
		FCachedGeometry CachedGeometry;
		FTransform SkeletalLocalToWorld;
		const FSkeletalMeshObject* MeshObject = nullptr;
		FHairStrandsProjectionDebugInfo DebugProjectionInfo;
		int32 FrameLODIndex = -1;
	};
	TArray<Element> Elements;
};

FHairStrandsManager GHairManager;

void RegisterHairStrands(
	uint64 Id, 
	EWorldType::Type WorldType,
	const FHairStrandsInterpolationData& InterpolationData, 
	const FHairStrandsProjectionHairData& RenProjectionDatas,
 	const FHairStrandsProjectionHairData& SimProjectionDatas,
	const FHairStrandsPrimitiveResources& PrimitiveResources,
	const FHairStrandsDebugInfo& DebugInfo,
	const FHairStrandsProjectionDebugInfo& DebugProjectionInfo)
{
	for (int32 Index = 0; Index < GHairManager.Elements.Num(); ++Index)
	{
		if (GHairManager.Elements[Index].Id == Id && GHairManager.Elements[Index].WorldType == WorldType)
		{
			// Component already registered. This should not happen. 
			UE_LOG(LogHairRendering, Warning, TEXT("Component already register. This should't happen. Please report this to a rendering engineer."))
			return;
		}
	}

	FHairStrandsManager::Element& E =  GHairManager.Elements.AddDefaulted_GetRef();
	E.Id = Id;
	E.WorldType = WorldType;
	E.InterpolationData = InterpolationData;
	E.RenProjectionHairDatas = RenProjectionDatas;
	E.SimProjectionHairDatas = SimProjectionDatas;
	E.SkeletalLocalToWorld = FTransform::Identity;
	E.PrimitiveResources = PrimitiveResources;
	E.DebugInfo = DebugInfo;
	E.DebugProjectionInfo = DebugProjectionInfo;
}

bool UpdateHairStrands(
	uint64 Id, 
	EWorldType::Type WorldType, 
	const FTransform& HairLocalToWorld, 
	const FTransform& SkeletalLocalToWorld)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id || E.WorldType != WorldType)
			continue;

		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.RenProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.SimProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		E.SkeletalLocalToWorld = SkeletalLocalToWorld;
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint64 Id, 
	EWorldType::Type NewWorldType)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id)
			continue;

		E.WorldType = NewWorldType;
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FHairStrandsProjectionHairData& RenProjectionDatas,
	const FHairStrandsProjectionHairData& SimProjectionDatas)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id || E.WorldType != WorldType)
			continue;

		E.RenProjectionHairDatas = RenProjectionDatas;
		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.RenProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		E.SimProjectionHairDatas = SimProjectionDatas;
		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.SimProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FSkeletalMeshObject* MeshObject)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id || E.WorldType != WorldType)
			continue;

		E.MeshObject = MeshObject;
		return true;
	}

	return false;
}

void UnregisterHairStrands(uint64 Id)
{
	for (int32 Index=0;Index< GHairManager.Elements.Num();++Index)
	{
		if (GHairManager.Elements[Index].Id == Id)
		{
			GHairManager.Elements[Index] = GHairManager.Elements[GHairManager.Elements.Num()-1];
			GHairManager.Elements.SetNum(GHairManager.Elements.Num() - 1);
		}
	}
}

void RunMeshTransfer(
	FRHICommandListImmediate& RHICmdList,
	const FHairStrandsProjectionMeshData& SourceMeshData,
	const FHairStrandsProjectionMeshData& TargetMeshData, 
	TArray<FRWBuffer>& OutTransferedPositions)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	FRDGBuilder GraphBuilder(RHICmdList);

	const uint32 LODCount = TargetMeshData.LODs.Num();
	OutTransferedPositions.SetNum(LODCount);
	for (uint32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		check(TargetMeshData.LODs[LODIndex].Sections.Num() > 0);

		OutTransferedPositions[LODIndex].Initialize(sizeof(float), TargetMeshData.LODs[LODIndex].Sections[0].TotalVertexCount * 3, PF_R32_FLOAT);
		TransferMesh(GraphBuilder, ShaderMap, LODIndex, SourceMeshData, TargetMeshData, OutTransferedPositions[LODIndex]);
	}
	
	GraphBuilder.Execute();	
}

void RunProjection(
	FRHICommandListImmediate& RHICmdList,
	const FTransform& LocalToWorld,
	const FHairStrandsProjectionMeshData& TargetMeshData,
	FHairStrandsProjectionHairData& RenProjectionHairData,
	FHairStrandsProjectionHairData& SimProjectionHairData)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	auto Project = [&RHICmdList, ShaderMap, &TargetMeshData, LocalToWorld](FHairStrandsProjectionHairData& ProjectionHairData)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		for (FHairStrandsProjectionHairData::HairGroup& HairGroup : ProjectionHairData.HairGroups)
		{
			for (FHairStrandsProjectionHairData::LODData& LODData : HairGroup.LODDatas)
			{
				const uint32 LODIndex = LODData.LODIndex;
				HairGroup.LocalToWorld = LocalToWorld;
				ProjectHairStrandsOntoMesh(GraphBuilder, ShaderMap, LODIndex, TargetMeshData, HairGroup);
				UpdateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, LODIndex, HairStrandsTriangleType::RestPose, TargetMeshData.LODs[LODIndex], HairGroup);
			}
		}
		GraphBuilder.Execute();
	};

	Project(RenProjectionHairData);
	Project(SimProjectionHairData);
}

void RunHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList, 
	EWorldType::Type WorldType, 
	const FShaderDrawDebugData* ShaderDrawData,
	FGlobalShaderMap* ShaderMap, 
	EHairStrandsInterpolationType Type,
	FHairStrandClusterData* ClusterData)
{
	check(IsInRenderingThread());

	// Update geometry cached based on GPU Skin output
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		E.CachedGeometry = E.MeshObject ? E.MeshObject->GetCachedGeometry() : FCachedGeometry();
	}
	
	// Update dynamic mesh triangles
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		E.FrameLODIndex = -1;
		if (E.WorldType != WorldType || E.CachedGeometry.Sections.Num() == 0)
			continue;
	
		FHairStrandsProjectionMeshData::LOD MeshDataLOD;
		for (FCachedGeometrySection Section : E.CachedGeometry.Sections)
		{
			// Ensure all mesh's sections have the same LOD index
			if (E.FrameLODIndex < 0) E.FrameLODIndex = Section.LODIndex;
			check(E.FrameLODIndex == Section.LODIndex);

			MeshDataLOD.Sections.Add(ConvertMeshSection(Section, E.SkeletalLocalToWorld));
		}

		FRDGBuilder GraphBuilder(RHICmdList);

		for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
		{
			if (EHairStrandsInterpolationType::RenderStrands == Type && 0 <= E.FrameLODIndex && E.FrameLODIndex < ProjectionHairData.LODDatas.Num() && ProjectionHairData.LODDatas[E.FrameLODIndex].bIsValid)
			{
				UpdateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshDataLOD, ProjectionHairData);
			}
		}

		for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
		{
			if (EHairStrandsInterpolationType::SimulationStrands == Type && 0 <= E.FrameLODIndex && E.FrameLODIndex < ProjectionHairData.LODDatas.Num() && ProjectionHairData.LODDatas[E.FrameLODIndex].bIsValid)
			{
				UpdateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshDataLOD, ProjectionHairData);
			}
		}
		GraphBuilder.Execute();
	}

	// Reset deformation
	if (EHairStrandsInterpolationType::SimulationStrands == Type)
	{
		for (FHairStrandsManager::Element& E : GHairManager.Elements)
		{
			if (E.WorldType != WorldType)
				continue;

			if (E.InterpolationData.Input && E.InterpolationData.Output && E.InterpolationData.ResetFunction)
			{
				E.InterpolationData.ResetFunction(RHICmdList, E.InterpolationData.Input, E.InterpolationData.Output, E.SimProjectionHairDatas, E.FrameLODIndex);
			}
		}
	}

	// Hair interpolation
	if (EHairStrandsInterpolationType::RenderStrands == Type)
	{
		for (FHairStrandsManager::Element& E : GHairManager.Elements)
		{
			if (E.WorldType != WorldType)
				continue;

			if (E.InterpolationData.Input && E.InterpolationData.Output && E.InterpolationData.Function)
			{
				E.InterpolationData.Function(RHICmdList, ShaderDrawData, E.InterpolationData.Input, E.InterpolationData.Output, E.RenProjectionHairDatas, E.SimProjectionHairDatas, E.FrameLODIndex, ClusterData);
			}
		}
	}
}

void GetGroomInterpolationData(const EWorldType::Type WorldType, 
	const EHairStrandsProjectionMeshType MeshtType,
	FHairStrandsProjectionMeshData::LOD& OutGeometries)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		if (MeshtType == EHairStrandsProjectionMeshType::DeformedMesh || MeshtType == EHairStrandsProjectionMeshType::RestMesh)
		{
			for (FCachedGeometrySection Section : E.CachedGeometry.Sections)
			{			
				FHairStrandsProjectionMeshData::Section OutSection = ConvertMeshSection(Section, E.SkeletalLocalToWorld);
				if (MeshtType == EHairStrandsProjectionMeshType::RestMesh)
				{
					// If the mesh has some mesh-tranferred data, we display that otherwise we use the rest data
					const bool bHasTransferData = E.FrameLODIndex < E.DebugProjectionInfo.TransferredPositions.Num();
					if (bHasTransferData)
					{
						OutSection.PositionBuffer = E.DebugProjectionInfo.TransferredPositions[E.FrameLODIndex].SRV;
					}
					else if (E.DebugProjectionInfo.TargetMeshData.LODs.Num() > 0)
					{
						OutGeometries = E.DebugProjectionInfo.TargetMeshData.LODs[0];
					}
				}
				OutGeometries.Sections.Add(OutSection);
			}
		}

		if (MeshtType == EHairStrandsProjectionMeshType::TargetMesh && E.DebugProjectionInfo.TargetMeshData.LODs.Num() > 0)
		{
			OutGeometries = E.DebugProjectionInfo.TargetMeshData.LODs[0];
		}

		if (MeshtType == EHairStrandsProjectionMeshType::SourceMesh && E.DebugProjectionInfo.SourceMeshData.LODs.Num() > 0)
		{
			OutGeometries = E.DebugProjectionInfo.SourceMeshData.LODs[0];
		}
	}
}

void GetGroomInterpolationData(const EWorldType::Type WorldType, const bool bRenderData, FHairStrandsProjectionHairData& Out, TArray<int32>& OutLODIndices)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		const bool bHasDynamicMesh = E.CachedGeometry.Sections.Num() > 0;
		if (bHasDynamicMesh)
		{
			if (bRenderData)
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
				{
					Out.HairGroups.Add(ProjectionHairData);
					OutLODIndices.Add(E.FrameLODIndex);
				}
			}
			else
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
				{
					Out.HairGroups.Add(ProjectionHairData);
					OutLODIndices.Add(E.FrameLODIndex);
				}
			}
		}
	}
}

FHairStrandsDebugInfos GetHairStandsDebugInfos()
{
	FHairStrandsDebugInfos Infos;
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		FHairStrandsDebugInfo& Info = Infos.AddDefaulted_GetRef();
		Info = E.DebugInfo;
		Info.Id = E.Id;
		Info.WorldType = E.WorldType;

		const uint32 GroupCount = Info.HairGroups.Num();
		for (uint32 GroupIt=0; GroupIt < GroupCount; ++GroupIt)
		{
			FHairStrandsDebugInfo::HairGroup& GroupInfo = Info.HairGroups[GroupIt];
			if (GroupIt < uint32(E.RenProjectionHairDatas.HairGroups.Num()))
			{
				FHairStrandsProjectionHairData::HairGroup& ProjectionHair = E.RenProjectionHairDatas.HairGroups[GroupIt];
				GroupInfo.LODCount = ProjectionHair.LODDatas.Num();
				GroupInfo.bHasSkinInterpolation = ProjectionHair.LODDatas.Num() > 0;
			}
			else
			{
				GroupInfo.LODCount = 0;
				GroupInfo.bHasSkinInterpolation = false;
			}
		}
	}

	return Infos;
}

FHairStrandsPrimitiveResources GetHairStandsPrimitiveResources(uint64 Id)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id)
			continue;

		return E.PrimitiveResources;
	}
	return FHairStrandsPrimitiveResources();
}


bool IsHairStrandsEnable(EShaderPlatform Platform) 
{ 
	return 
		IsHairStrandsSupported(Platform) && 
		GHairStrandsRenderingEnable == 1 && 
		GHairManager.Elements.Num() > 0; 
}
bool IsHairRayTracingEnabled()
{
	return IsRayTracingEnabled();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FBindingQuery
{
	void* Asset = nullptr;
	TBindingProcess BindingProcess;
};
TQueue<FBindingQuery> GBindingQueries;

void EnqueueGroomBindingQuery(void* Asset, TBindingProcess BindingProcess)
{
	GBindingQueries.Enqueue({ Asset, BindingProcess });
}

bool HasHairStrandsProjectionQuery(EShaderPlatform Platform)
{
	return
		IsHairStrandsSupported(Platform) &&
		GHairStrandsRenderingEnable == 1 &&
		!GBindingQueries.IsEmpty();
}

void RunHairStrandsBindingQueries(FRHICommandListImmediate& RHICmdList, FGlobalShaderMap* ShaderMap)
{
	FBindingQuery Q;
	while (GBindingQueries.Dequeue(Q))
	{
		if (Q.BindingProcess && Q.Asset)
		{
			Q.BindingProcess(RHICmdList, Q.Asset);
		}
	}
}