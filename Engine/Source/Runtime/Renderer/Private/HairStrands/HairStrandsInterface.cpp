// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: Hair manager implementation.
=============================================================================*/

#include "HairStrandsInterface.h"
#include "HairStrandsRendering.h"
#include "HairStrandsMeshProjection.h"

#include "GPUSkinCache.h"
#include "SkeletalRenderPublic.h"
#include "SceneRendering.h"
#include "SystemTextures.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairRendering, Log, All);

static int32 GHairStrandsRenderingEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsRenderingEnable(TEXT("r.HairStrands.Enable"), GHairStrandsRenderingEnable, TEXT("Enable/Disable hair strands rendering"));

static int32 GHairStrandsRaytracingEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsRaytracingEnable(TEXT("r.HairStrands.Raytracing"), GHairStrandsRaytracingEnable, TEXT("Enable/Disable hair strands raytracing (fallback onto raster techniques"));

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

inline FHairStrandsProjectionMeshData::Section ConvertMeshSection(const FCachedGeometry::Section& In, const FTransform& InTransform)
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
		uint32 ComponentId = 0;
		uint32 SkeletalComponentId = 0;
		EWorldType::Type WorldType = EWorldType::None;
		FHairStrandsDebugInfo DebugInfo;
		FHairStrandsPrimitiveResources PrimitiveResources;
		FHairStrandsInterpolationData InterpolationData;
		FHairStrandsProjectionHairData RenProjectionHairDatas;
		FHairStrandsProjectionHairData SimProjectionHairDatas;
		FTransform SkeletalLocalToWorld;
		FTransform LocalToWorld;
		FHairStrandsProjectionDebugInfo DebugProjectionInfo;
		int32 FrameLODIndex = -1;
	};

	// TODO change this array to a queue update, in order make processing/update thread safe.
	TArray<Element> Elements;

	FHairStrandsManager()
	{
		// Reserve a large a mount of object to avoid any potential memory reallocation, which 
		// could cause some thread safety issue. This is a workaround against the non-thread-safe array
		Elements.Reserve(64);
	}
};

FHairStrandsManager GHairManager;

void RegisterHairStrands(
	uint32 ComponentId,
	uint32 SkeletalComponentId, 
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
		if (GHairManager.Elements[Index].ComponentId == ComponentId && GHairManager.Elements[Index].WorldType == WorldType)
		{
			// Component already registered. This should not happen. 
			UE_LOG(LogHairRendering, Warning, TEXT("Component already register. This should't happen. Please report this to a rendering engineer."))
			return;
		}
	}

	FHairStrandsManager::Element& E =  GHairManager.Elements.AddDefaulted_GetRef();
	E.ComponentId = ComponentId;
	E.SkeletalComponentId = SkeletalComponentId;
	E.WorldType = WorldType;
	E.InterpolationData = InterpolationData;
	E.RenProjectionHairDatas = RenProjectionDatas;
	E.SimProjectionHairDatas = SimProjectionDatas;
	E.SkeletalLocalToWorld = FTransform::Identity;
	E.LocalToWorld = FTransform::Identity;
	E.PrimitiveResources = PrimitiveResources;
	E.DebugInfo = DebugInfo;
	E.DebugProjectionInfo = DebugProjectionInfo;
}

bool UpdateHairStrandsDebugInfo(
	uint32 ComponentId,
	EWorldType::Type WorldType,
	const uint32 GroupIt,
	const bool bSimulationEnable)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.ComponentId != ComponentId || E.WorldType != WorldType || GroupIt >= uint32(E.DebugInfo.HairGroups.Num()))
			continue;

		E.DebugInfo.HairGroups[GroupIt].bHasSimulation = bSimulationEnable;
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint32 ComponentId,
	EWorldType::Type WorldType, 
	const FTransform& HairLocalToWorld, 
	const FTransform& SkeletalLocalToWorld)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.ComponentId != ComponentId || E.WorldType != WorldType)
			continue;

		E.LocalToWorld = HairLocalToWorld;

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
	uint32 ComponentId,
	EWorldType::Type NewWorldType)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.ComponentId != ComponentId)
			continue;

		E.WorldType = NewWorldType;
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint32 ComponentId,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FHairStrandsProjectionHairData& RenProjectionDatas,
	const FHairStrandsProjectionHairData& SimProjectionDatas)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.ComponentId != ComponentId || E.WorldType != WorldType)
			continue;

		E.LocalToWorld = HairLocalToWorld;

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

void UnregisterHairStrands(uint32 ComponentId)
{
	for (int32 Index=0;Index< GHairManager.Elements.Num();++Index)
	{
		if (GHairManager.Elements[Index].ComponentId == ComponentId)
		{
			GHairManager.Elements[Index] = GHairManager.Elements[GHairManager.Elements.Num()-1];
			GHairManager.Elements.SetNum(GHairManager.Elements.Num() - 1, false);
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

	FBufferTransitionQueue TransitionQueue;
	FRDGBuilder GraphBuilder(RHICmdList);

	const uint32 LODCount = TargetMeshData.LODs.Num();
	OutTransferedPositions.SetNum(LODCount);
	for (uint32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		check(TargetMeshData.LODs[LODIndex].Sections.Num() > 0);

		OutTransferedPositions[LODIndex].Initialize(sizeof(float), TargetMeshData.LODs[LODIndex].Sections[0].TotalVertexCount * 3, PF_R32_FLOAT);
		TransferMesh(GraphBuilder, ShaderMap, LODIndex, SourceMeshData, TargetMeshData, OutTransferedPositions[LODIndex], TransitionQueue);
	}
	
	GraphBuilder.Execute();	
	TransitBufferToReadable(RHICmdList, TransitionQueue);
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

	FBufferTransitionQueue TransitionQueue;

	auto Project = [&RHICmdList, ShaderMap, &TargetMeshData, LocalToWorld, &TransitionQueue](FHairStrandsProjectionHairData& ProjectionHairData)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		for (FHairStrandsProjectionHairData::HairGroup& HairGroup : ProjectionHairData.HairGroups)
		{
			for (FHairStrandsProjectionHairData::RestLODData& LODData : HairGroup.RestLODDatas)
			{
				const uint32 LODIndex = LODData.LODIndex;
				HairGroup.LocalToWorld = LocalToWorld;
				ProjectHairStrandsOntoMesh(GraphBuilder, ShaderMap, LODIndex, TargetMeshData, HairGroup, TransitionQueue);
				UpdateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, LODIndex, HairStrandsTriangleType::RestPose, TargetMeshData.LODs[LODIndex], HairGroup, TransitionQueue);
			}
		}
		GraphBuilder.Execute();
	};

	Project(RenProjectionHairData);
	Project(SimProjectionHairData);

	TransitBufferToReadable(RHICmdList, TransitionQueue);
}

void RunHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList, 
	EWorldType::Type WorldType, 
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	FGlobalShaderMap* ShaderMap, 
	EHairStrandsInterpolationType Type,
	FHairStrandClusterData* ClusterData)
{
	check(IsInRenderingThread());

	DECLARE_GPU_STAT(HairStrandsInterpolationGrouped);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsInterpolationGrouped);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsInterpolationGrouped);

	// Update dynamic mesh triangles
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		E.FrameLODIndex = -1;
		if (E.WorldType != WorldType)
			continue;
	
		FCachedGeometry CachedGeometry;
		if (SkinCache)
		{
			CachedGeometry = SkinCache->GetCachedGeometry(E.SkeletalComponentId);
		}
		if (CachedGeometry.Sections.Num() == 0)
			continue;

		FHairStrandsProjectionMeshData::LOD MeshDataLOD;
		for (FCachedGeometry::Section& Section : CachedGeometry.Sections)
		{
			// Ensure all mesh's sections have the same LOD index
			if (E.FrameLODIndex < 0) E.FrameLODIndex = Section.LODIndex;
			check(E.FrameLODIndex == Section.LODIndex);

			MeshDataLOD.Sections.Add(ConvertMeshSection(Section, E.SkeletalLocalToWorld));
		}

		FBufferTransitionQueue TransitionQueue;

		FRDGBuilder GraphBuilder(RHICmdList);

		if (0 <= E.FrameLODIndex)
		{
			if (EHairStrandsInterpolationType::RenderStrands == Type)
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
				{
					if (E.FrameLODIndex < ProjectionHairData.DeformedLODDatas.Num() && ProjectionHairData.DeformedLODDatas[E.FrameLODIndex].IsValid())
					{
						UpdateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshDataLOD, ProjectionHairData, TransitionQueue);
					}
				}
			}

			if (EHairStrandsInterpolationType::SimulationStrands == Type)
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
				{
					if (E.FrameLODIndex < ProjectionHairData.DeformedLODDatas.Num() && ProjectionHairData.DeformedLODDatas[E.FrameLODIndex].IsValid())
					{
						UpdateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshDataLOD, ProjectionHairData, TransitionQueue);
					}
				}

				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
				{
					if (E.FrameLODIndex < ProjectionHairData.DeformedLODDatas.Num() && ProjectionHairData.DeformedLODDatas[E.FrameLODIndex].IsValid())
					{
						InitHairStrandsMeshSamples(GraphBuilder, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshDataLOD, ProjectionHairData, TransitionQueue);
						UpdateHairStrandsMeshSamples(GraphBuilder, ShaderMap, E.FrameLODIndex, MeshDataLOD, ProjectionHairData, TransitionQueue);
						//InterpolateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, E.FrameLODIndex, MeshDataLOD, ProjectionHairData);
					}
				}
			}
		}

		/*for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
		{
			if (EHairStrandsInterpolationType::RenderStrands == Type && 0 <= E.FrameLODIndex && E.FrameLODIndex < ProjectionHairData.LODDatas.Num() && ProjectionHairData.LODDatas[E.FrameLODIndex].bIsValid)
			{
				InitHairStrandsMeshSamples(GraphBuilder, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshDataLOD, ProjectionHairData);
				UpdateHairStrandsMeshSamples(GraphBuilder, ShaderMap, E.FrameLODIndex, MeshDataLOD, ProjectionHairData);
				InterpolateHairStrandsMeshTriangles(GraphBuilder, ShaderMap, E.FrameLODIndex, MeshDataLOD, ProjectionHairData);
			}
		}*/
		GraphBuilder.Execute();
		TransitBufferToReadable(RHICmdList, TransitionQueue);
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
				E.InterpolationData.Function(RHICmdList, ShaderDrawData, E.LocalToWorld, E.InterpolationData.Input, E.InterpolationData.Output, E.RenProjectionHairDatas, E.SimProjectionHairDatas, E.FrameLODIndex, ClusterData);
			}
		}
	}
}

void GetGroomInterpolationData(
	const EWorldType::Type WorldType, 
	const EHairStrandsProjectionMeshType MeshType,
	const FGPUSkinCache* SkinCache,
	FHairStrandsProjectionMeshData::LOD& OutGeometries)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		FCachedGeometry CachedGeometry = SkinCache->GetCachedGeometry(E.SkeletalComponentId);
		if (CachedGeometry.Sections.Num() == 0)
			continue;

		if (MeshType == EHairStrandsProjectionMeshType::DeformedMesh || MeshType == EHairStrandsProjectionMeshType::RestMesh)
		{
			for (FCachedGeometry::Section Section : CachedGeometry.Sections)
			{			
				FHairStrandsProjectionMeshData::Section OutSection = ConvertMeshSection(Section, E.SkeletalLocalToWorld);
				if (MeshType == EHairStrandsProjectionMeshType::RestMesh)
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

		if (MeshType == EHairStrandsProjectionMeshType::TargetMesh && E.DebugProjectionInfo.TargetMeshData.LODs.Num() > 0)
		{
			OutGeometries = E.DebugProjectionInfo.TargetMeshData.LODs[0];
		}

		if (MeshType == EHairStrandsProjectionMeshType::SourceMesh && E.DebugProjectionInfo.SourceMeshData.LODs.Num() > 0)
		{
			OutGeometries = E.DebugProjectionInfo.SourceMeshData.LODs[0];
		}
	}
}

void GetGroomInterpolationData(
	const EWorldType::Type WorldType, 
	EHairStrandsInterpolationType StrandsType,
	const FGPUSkinCache* SkinCache,
	FHairStrandsProjectionHairData& Out, TArray<int32>& OutLODIndices)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		FCachedGeometry CachedGeometry = SkinCache->GetCachedGeometry(E.SkeletalComponentId);
		if (CachedGeometry.Sections.Num() == 0)
			continue;
		const bool bHasDynamicMesh = CachedGeometry.Sections.Num() > 0;
		if (bHasDynamicMesh)
		{
			if (StrandsType == EHairStrandsInterpolationType::RenderStrands)
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
				{
					Out.HairGroups.Add(ProjectionHairData);
					OutLODIndices.Add(E.FrameLODIndex);
				}
			}
			else if (StrandsType == EHairStrandsInterpolationType::SimulationStrands)
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
		Info.ComponentId = E.ComponentId;
		Info.WorldType = E.WorldType;
		Info.GroomAssetName = E.DebugProjectionInfo.GroomAssetName;
		Info.SkeletalComponentName = E.DebugProjectionInfo.SkeletalComponentName;

		const uint32 GroupCount = Info.HairGroups.Num();
		for (uint32 GroupIt=0; GroupIt < GroupCount; ++GroupIt)
		{		
			FHairStrandsDebugInfo::HairGroup& GroupInfo = Info.HairGroups[GroupIt];
			if (GroupIt < uint32(E.RenProjectionHairDatas.HairGroups.Num()))
			{
				FHairStrandsProjectionHairData::HairGroup& ProjectionHair = E.RenProjectionHairDatas.HairGroups[GroupIt];
				GroupInfo.LODCount = ProjectionHair.DeformedLODDatas.Num();
				GroupInfo.bHasSkinInterpolation = ProjectionHair.DeformedLODDatas.Num() > 0;
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

FHairStrandsPrimitiveResources GetHairStandsPrimitiveResources(uint32 ComponentId)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.ComponentId != ComponentId)
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
	return IsRayTracingEnabled() && GHairStrandsRaytracingEnable;
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

static void RunHairStrandsBindingQueries(FRHICommandListImmediate& RHICmdList, FGlobalShaderMap* ShaderMap)
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

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FFollicleQuery
{
	static const uint32 MaxInfoCount = 16;
	FFollicleInfo Infos[MaxInfoCount];
	uint32 InfoCount = 0;
	UTexture2D* OutTexture = nullptr;
};
TQueue<FFollicleQuery> GFollicleQueries;

void EnqueueFollicleMaskUpdateQuery(const TArray<FFollicleInfo>& Infos, UTexture2D* OutTexture)
{
	if (!OutTexture)
	{
		return;
	}

	FFollicleQuery Q;
	Q.OutTexture = OutTexture;
	for (const FFollicleInfo& Info : Infos)
	{
		if (Q.InfoCount >= FFollicleQuery::MaxInfoCount)
		{
			return;
		}
		Q.Infos[Q.InfoCount++] = Info;
	}
	GFollicleQueries.Enqueue(Q);
}

static void RunFolliculeMaskGeneration(FRHICommandListImmediate& RHICmdList, FGlobalShaderMap* ShaderMap)
{
	struct FFollicleElement
	{
		FHairStrandsManager::Element* E = nullptr;
		FFollicleInfo::EChannel Channel = FFollicleInfo::R;
		uint32 KernelSizeInPixels = 0;
	};

	FFollicleQuery Q;
	while (GFollicleQueries.Dequeue(Q))
	{
		// TODO deduplicate queries (compute hash?)

		// Filter valid/ready groom
		TArray<FFollicleElement> Elements;
		Elements.Reserve(Q.InfoCount);
		for (uint32 InfoIt=0; InfoIt <Q.InfoCount;++InfoIt)
		{
			for (FHairStrandsManager::Element& E : GHairManager.Elements)
			{
				if (E.ComponentId == Q.Infos[InfoIt].GroomId && E.FrameLODIndex >= 0)
				{
					Elements.Add({ &E, Q.Infos[InfoIt].Channel, Q.Infos[InfoIt].KernelSizeInPixels });
				}
			}
		}

		// Generate texture
		if (Elements.Num() > 0)
		{
			TRefCountPtr<IPooledRenderTarget> OutMaskTexture;

			const uint32 MipCount = Q.OutTexture->GetNumMips();
			const FIntPoint Resolution(Q.OutTexture->Resource->GetSizeX(), Q.OutTexture->Resource->GetSizeY());
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef FollicleMaskTexture = nullptr;
			for (FFollicleElement& E : Elements)
			{
				GenerateFolliculeMask(
					GraphBuilder,
					ShaderMap,
					Resolution,
					MipCount,
					E.KernelSizeInPixels,
					uint32(E.Channel),
					E.E->FrameLODIndex,
					E.E->RenProjectionHairDatas,
					FollicleMaskTexture);
			}

			AddComputeMipsPass(GraphBuilder, ShaderMap, FollicleMaskTexture);

			GraphBuilder.QueueTextureExtraction(FollicleMaskTexture, &OutMaskTexture);
			GraphBuilder.Execute();

			check(FollicleMaskTexture->Desc.Format == Q.OutTexture->GetPixelFormat());

			FRHICopyTextureInfo CopyInfo;
			CopyInfo.NumMips = MipCount;
			RHICmdList.CopyTexture(
				OutMaskTexture->GetRenderTargetItem().ShaderResourceTexture,
				Q.OutTexture->Resource->TextureRHI,
				CopyInfo);
		}
	}
}

void RunHairStrandsProcess(FRHICommandListImmediate& RHICmdList, FGlobalShaderMap* ShaderMap)
{
	if (!GFollicleQueries.IsEmpty())
	{
		RunFolliculeMaskGeneration(RHICmdList, ShaderMap);
	}

	if (!GBindingQueries.IsEmpty())
	{
		RunHairStrandsBindingQueries(RHICmdList, ShaderMap);
	}
}

bool HasHairStrandsProcess(EShaderPlatform Platform)
{
	return
		IsHairStrandsSupported(Platform) &&
		GHairStrandsRenderingEnable == 1 &&
		(!GBindingQueries.IsEmpty() || !GFollicleQueries.IsEmpty());
}

void TransitBufferToReadable(FRHICommandListImmediate& RHICmdList, FBufferTransitionQueue& BuffersToTransit)
{
	if (BuffersToTransit.Num())
	{
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, BuffersToTransit.GetData(), BuffersToTransit.Num());
		BuffersToTransit.SetNum(0, false);
	}
}