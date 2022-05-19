// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodySceneProxy.h"
#include "WaterSplineMetadata.h"
#include "WaterModule.h"
#include "WaterUtils.h"
#include "WaterBodyComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneInterface.h"


static TAutoConsoleVariable<int32> CVarWaterShowProxies(
	TEXT("r.Water.ShowWaterSceneProxies"),
	0,
	TEXT("Allows editor visualization of water scene proxies. If the mode is set to 1 we will show only the selected water body in wireframe, if it is set to 2 we will show all in wireframe, and if it is 3 we will show all as opaque meshes"),
	ECVF_Default);

// ----------------------------------------------------------------------------------

class FWaterBodyMeshSection
{
public:
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory VertexFactory;

	FWaterBodyMeshSection(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FWaterBodyMeshSection")
	{}
};

void FWaterBodySceneProxy::InitResources(FWaterBodyMeshSection* Section)
{
	check(Section != nullptr);
	BeginInitResource(&Section->VertexBuffers.PositionVertexBuffer);
	BeginInitResource(&Section->VertexBuffers.StaticMeshVertexBuffer);
	BeginInitResource(&Section->VertexBuffers.ColorVertexBuffer);
	BeginInitResource(&Section->IndexBuffer);
	BeginInitResource(&Section->VertexFactory);
}

FWaterBodySceneProxy::FWaterBodySceneProxy(UWaterBodyComponent* Component, const TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices)
	: FPrimitiveSceneProxy(Component)
{
	if (Vertices.Num() > 0 && Indices.Num() > 0)
	{
		FWaterBodyMeshSection* NewSection = new FWaterBodyMeshSection(GetScene().GetFeatureLevel());
		NewSection->IndexBuffer.Indices = Indices;

		TArray<FDynamicMeshVertex> SectionVertices = Vertices;
		NewSection->VertexBuffers.InitFromDynamicVertex(&NewSection->VertexFactory, SectionVertices);

		InitResources(NewSection);

		Sections.Add(NewSection);
	}

	if (UMaterialInstance* WaterInfoMaterial = Component->GetWaterInfoMaterialInstance())
	{
		Material = WaterInfoMaterial->GetRenderProxy();
	}
}

FWaterBodySceneProxy::~FWaterBodySceneProxy()
{
	for (FWaterBodyMeshSection* Section : Sections)
	{
		Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
		Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
		Section->IndexBuffer.ReleaseResource();
		Section->VertexFactory.ReleaseResource();

		delete Section;
	}
}

void FWaterBodySceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	FMaterialRenderProxy* MaterialToUse = Material;
	if (!MaterialToUse)
	{
		FColoredMaterialRenderProxy* FallbackMaterial = new FColoredMaterialRenderProxy(
			GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial->GetRenderProxy() : nullptr,
			FLinearColor(1.f, 1.f, 1.f));

		Collector.RegisterOneFrameMaterialProxy(FallbackMaterial);

		MaterialToUse = FallbackMaterial;
	}

	// If we are not in the waterinfo pass and the cvar is not set to show opaque bodies, we should be in wireframe
	const bool bWireframe = AllowDebugViewmodes() && !(bWithinWaterInfoPass) && (CVarWaterShowProxies.GetValueOnRenderThread() != 3);

	if (bWireframe)
	{
		FColoredMaterialRenderProxy* WireframeMaterialInstance= new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
			FLinearColor(0, 0.5f, 1.f)
			);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		MaterialToUse = WireframeMaterialInstance;
	}
	
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		for (const FWaterBodyMeshSection* Section : Sections)
		{
			FMeshBatch& Mesh = Collector.AllocateMesh();
			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			BatchElement.IndexBuffer = &Section->IndexBuffer;
			Mesh.bWireframe = bWireframe;
			Mesh.VertexFactory = &Section->VertexFactory;
			Mesh.MaterialRenderProxy = MaterialToUse;

			bool bHasPrecomputedVolumetricLightmap;
			FMatrix PreviousLocalToWorld;
			int32 SingleCaptureIndex;
			bool bOutputVelocity;
			GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
			bOutputVelocity |= AlwaysHasVelocity();

			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity); BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
			check(BatchElement.NumPrimitives != 0);
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = bWireframe ? SDPG_Foreground : SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Collector.AddMesh(ViewIndex, Mesh);
		}
	}
}

FPrimitiveViewRelevance FWaterBodySceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	Result.bVelocityRelevance = IsMovable();
	return Result;
}

SIZE_T FWaterBodySceneProxy::GetTypeHash() const
{
	static size_t UniquePtr;
	return reinterpret_cast<size_t>(&UniquePtr);
}

uint32 FWaterBodySceneProxy::GetMemoryFootprint() const 
{
	return (sizeof(*this) + GetAllocatedSize());
}

uint32 FWaterBodySceneProxy::GetAllocatedSize() const
{
	return FPrimitiveSceneProxy::GetAllocatedSize();
}

bool FWaterBodySceneProxy::IsShown(const FSceneView* View) const
{
	if (!bWithinWaterInfoPass)
	{
		const int32 ShowProxiesCVar = CVarWaterShowProxies.GetValueOnRenderThread();
		if ((ShowProxiesCVar == 1 && IsSelected()) || ShowProxiesCVar >= 2)
		{
			return true;
		}

		return false;
	}

	return FPrimitiveSceneProxy::IsShown(View);
}

void FWaterBodySceneProxy::SetWithinWaterInfoPass(bool bInWithinWaterInfoPass)
{
	bWithinWaterInfoPass = bInWithinWaterInfoPass;
}
