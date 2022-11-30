// Copyright Epic Games, Inc. All Rights Reserved.

#include "SplineMeshSceneProxy.h"
#include "Materials/Material.h"
#include "MeshMaterialShader.h"

IMPLEMENT_TYPE_LAYOUT(FSplineMeshVertexFactoryShaderParameters);

bool FSplineMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.bIsUsedWithSplineMeshes || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
		&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
}

/** Modify compile environment to enable spline deformation */
void FSplineMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	const bool ContainsManualVertexFetch = OutEnvironment.GetDefinitions().Contains("MANUAL_VERTEX_FETCH");
	if (!ContainsManualVertexFetch)
	{
		OutEnvironment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), TEXT("0"));
	}

	// We don't call this because we don't actually support speed tree wind, and this advertises support for that
	//FLocalVertexFactory::ModifyCompilationEnvironment(Type, Platform, Material, OutEnvironment);

	FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	OutEnvironment.SetDefine(TEXT("USE_SPLINEDEFORM"), TEXT("1"));
}

FSplineMeshSceneProxy::FSplineMeshSceneProxy(USplineMeshComponent* InComponent) :
	FStaticMeshSceneProxy(InComponent, false)
{
	bSupportsDistanceFieldRepresentation = false;
	bVFRequiresPrimitiveUniformBuffer = !UseGPUScene(GMaxRHIShaderPlatform, GetScene().GetFeatureLevel());

	// make sure all the materials are okay to be rendered as a spline mesh
	for (FStaticMeshSceneProxy::FLODInfo& LODInfo : LODs)
	{
		for (FStaticMeshSceneProxy::FLODInfo::FSectionInfo& Section : LODInfo.Sections)
		{
			if (!Section.Material->CheckMaterialUsage_Concurrent(MATUSAGE_SplineMesh))
			{
				Section.Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}
		}
	}

	// Copy spline params from component
	SplineParams = InComponent->SplineParams;
	SplineUpDir = InComponent->SplineUpDir;
	bSmoothInterpRollScale = InComponent->bSmoothInterpRollScale;
	ForwardAxis = InComponent->ForwardAxis;

	// Fill in info about the mesh
	InComponent->CalculateScaleZAndMinZ(SplineMeshScaleZ, SplineMeshMinZ);

	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		InitVertexFactory(InComponent, LODIndex, nullptr); // we always need this one for shadows etc
		if (InComponent->LODData.IsValidIndex(LODIndex) && InComponent->LODData[LODIndex].OverrideVertexColors)
		{
			InitVertexFactory(InComponent, LODIndex, InComponent->LODData[LODIndex].OverrideVertexColors);
		}
	}
}

SIZE_T FSplineMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

bool FSplineMeshSceneProxy::GetShadowMeshElement(int32 LODIndex, int32 BatchIndex, uint8 InDepthPriorityGroup, FMeshBatch& OutMeshBatch, bool bDitheredLODTransition) const
{
	//checkf(LODIndex == 0, TEXT("Getting spline static mesh element with invalid LOD [%d]"), LODIndex);

	if (FStaticMeshSceneProxy::GetShadowMeshElement(LODIndex, BatchIndex, InDepthPriorityGroup, OutMeshBatch, bDitheredLODTransition))
	{
		const FStaticMeshVertexFactories& VFs = RenderData->LODVertexFactories[LODIndex];
		check(OutMeshBatch.Elements.Num() == 1);
		OutMeshBatch.VertexFactory = OutMeshBatch.Elements[0].bUserDataIsColorVertexBuffer ? VFs.SplineVertexFactoryOverrideColorVertexBuffer : VFs.SplineVertexFactory;
		check(OutMeshBatch.VertexFactory);
		OutMeshBatch.Elements[0].SplineMeshSceneProxy = const_cast<FSplineMeshSceneProxy*>(this);
		OutMeshBatch.Elements[0].bIsSplineProxy = true;
		OutMeshBatch.Elements[0].PrimitiveUniformBuffer = GetUniformBuffer();
		OutMeshBatch.ReverseCulling ^= (SplineParams.StartScale.X < 0) ^ (SplineParams.StartScale.Y < 0);
		return true;
	}
	return false;
}

bool FSplineMeshSceneProxy::GetMeshElement(int32 LODIndex, int32 BatchIndex, int32 SectionIndex, uint8 InDepthPriorityGroup, bool bUseSelectionOutline, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const
{
	//checkf(LODIndex == 0 /*&& SectionIndex == 0*/, TEXT("Getting spline static mesh element with invalid params [%d, %d]"), LODIndex, SectionIndex);

	if (FStaticMeshSceneProxy::GetMeshElement(LODIndex, BatchIndex, SectionIndex, InDepthPriorityGroup, bUseSelectionOutline, bAllowPreCulledIndices, OutMeshBatch))
	{
		const FStaticMeshVertexFactories& VFs = RenderData->LODVertexFactories[LODIndex];
		check(OutMeshBatch.Elements.Num() == 1);
		OutMeshBatch.VertexFactory = OutMeshBatch.Elements[0].bUserDataIsColorVertexBuffer ? VFs.SplineVertexFactoryOverrideColorVertexBuffer : VFs.SplineVertexFactory;
		check(OutMeshBatch.VertexFactory);
		OutMeshBatch.Elements[0].SplineMeshSceneProxy = const_cast<FSplineMeshSceneProxy*>(this);
		OutMeshBatch.Elements[0].bIsSplineProxy = true;
		OutMeshBatch.Elements[0].PrimitiveUniformBuffer = GetUniformBuffer();
		OutMeshBatch.ReverseCulling ^= (SplineParams.StartScale.X < 0) ^ (SplineParams.StartScale.Y < 0);
		return true;
	}
	return false;
}

bool FSplineMeshSceneProxy::GetWireframeMeshElement(int32 LODIndex, int32 BatchIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const
{
	//checkf(LODIndex == 0, TEXT("Getting spline static mesh element with invalid LOD [%d]"), LODIndex);

	if (FStaticMeshSceneProxy::GetWireframeMeshElement(LODIndex, BatchIndex, WireframeRenderProxy, InDepthPriorityGroup, bAllowPreCulledIndices, OutMeshBatch))
	{
		const FStaticMeshVertexFactories& VFs = RenderData->LODVertexFactories[LODIndex];
		check(OutMeshBatch.Elements.Num() == 1);
		OutMeshBatch.VertexFactory = OutMeshBatch.Elements[0].bUserDataIsColorVertexBuffer ? VFs.SplineVertexFactoryOverrideColorVertexBuffer : VFs.SplineVertexFactory;
		check(OutMeshBatch.VertexFactory);
		OutMeshBatch.Elements[0].SplineMeshSceneProxy = const_cast<FSplineMeshSceneProxy*>(this);
		OutMeshBatch.Elements[0].bIsSplineProxy = true;
		OutMeshBatch.Elements[0].PrimitiveUniformBuffer = GetUniformBuffer();
		OutMeshBatch.ReverseCulling ^= (SplineParams.StartScale.X < 0) ^ (SplineParams.StartScale.Y < 0);
		return true;
	}
	return false;
}
