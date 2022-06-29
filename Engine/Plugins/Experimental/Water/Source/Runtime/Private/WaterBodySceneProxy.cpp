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

void FWaterBodySceneProxy::InitResources(FWaterBodyMeshSection& Section)
{
	BeginInitResource(&Section.VertexBuffers.PositionVertexBuffer);
	BeginInitResource(&Section.VertexBuffers.StaticMeshVertexBuffer);
	BeginInitResource(&Section.VertexBuffers.ColorVertexBuffer);
	BeginInitResource(&Section.IndexBuffer);
	BeginInitResource(&Section.VertexFactory);
}

FWaterBodySceneProxy::FWaterBodySceneProxy(UWaterBodyComponent* Component)
	: FPrimitiveSceneProxy(Component)
{
	if (Component->WaterBodyMeshVertices.Num() > 0 && Component->WaterBodyMeshIndices.Num() > 0)
	{
		FWaterBodyMeshSection& NewSection = Sections.Emplace_GetRef(GetScene().GetFeatureLevel());
		NewSection.IndexBuffer.Indices = Component->WaterBodyMeshIndices;

		NewSection.VertexBuffers.InitFromDynamicVertex(&NewSection.VertexFactory, Component->WaterBodyMeshVertices);

		InitResources(NewSection);
	}

	if (Component->DilatedWaterBodyMeshVertices.Num() > 0 && Component->DilatedWaterBodyMeshIndices.Num() > 0)
	{
		FWaterBodyMeshSection& NewSection = DilatedSections.Emplace_GetRef(GetScene().GetFeatureLevel());
		NewSection.IndexBuffer.Indices = Component->DilatedWaterBodyMeshIndices;

		NewSection.VertexBuffers.InitFromDynamicVertex(&NewSection.VertexFactory, Component->DilatedWaterBodyMeshVertices);

		InitResources(NewSection);
	}

	if (UMaterialInstance* WaterInfoMaterial = Component->GetWaterInfoMaterialInstance())
	{
		Material = WaterInfoMaterial->GetRenderProxy();
	}
}

FWaterBodySceneProxy::~FWaterBodySceneProxy()
{
	for (FWaterBodyMeshSection& Section : Sections)
	{
		Section.VertexBuffers.PositionVertexBuffer.ReleaseResource();
		Section.VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		Section.VertexBuffers.ColorVertexBuffer.ReleaseResource();
		Section.IndexBuffer.ReleaseResource();
		Section.VertexFactory.ReleaseResource();
	}

	for (FWaterBodyMeshSection& Section : DilatedSections)
	{
		Section.VertexBuffers.PositionVertexBuffer.ReleaseResource();
		Section.VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		Section.VertexBuffers.ColorVertexBuffer.ReleaseResource();
		Section.IndexBuffer.ReleaseResource();
		Section.VertexFactory.ReleaseResource();
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

	const bool bWithinWaterInfoPasses = CurrentWaterInfoPass != EWaterInfoPass::None;

	const int32 ShowProxiesCVar = CVarWaterShowProxies.GetValueOnRenderThread();
	const bool bDebugShowWaterSceneProxiesEnabled = !bWithinWaterInfoPasses && ((ShowProxiesCVar == 1 && IsSelected()) || ShowProxiesCVar >= 2);

	// If we are not in the waterinfo pass and the cvar is not set to show opaque bodies, we should be in wireframe
	const bool bWireframe = AllowDebugViewmodes() && !(bWithinWaterInfoPasses) && (CVarWaterShowProxies.GetValueOnRenderThread() != 3);

	if (bWireframe)
	{
		FColoredMaterialRenderProxy* WireframeMaterialInstance= new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
			FLinearColor(0, 0.5f, 1.f)
			);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		MaterialToUse = WireframeMaterialInstance;
	}

	auto AddWaterBodyMeshSection = [&](const FWaterBodyMeshSection& Section, int32 ViewIndex)
	{
		FMeshBatch& Mesh = Collector.AllocateMesh();
		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = &Section.IndexBuffer;
		Mesh.bWireframe = bWireframe;
		Mesh.VertexFactory = &Section.VertexFactory;
		Mesh.MaterialRenderProxy = MaterialToUse;

		bool bHasPrecomputedVolumetricLightmap;
		FMatrix PreviousLocalToWorld;
		int32 SingleCaptureIndex;
		bool bOutputVelocity;
		GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
		bOutputVelocity |= AlwaysHasVelocity();

		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
		DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
		BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = Section.IndexBuffer.Indices.Num() / 3;
		check(BatchElement.NumPrimitives != 0);
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = Section.VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = bWireframe ? SDPG_Foreground : SDPG_World;
		Mesh.bCanApplyViewModeOverrides = false;
		Collector.AddMesh(ViewIndex, Mesh);
	};

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if ((CurrentWaterInfoPass == EWaterInfoPass::Color) || (bDebugShowWaterSceneProxiesEnabled))
		{
			for (const FWaterBodyMeshSection& Section : Sections)
			{
				AddWaterBodyMeshSection(Section, ViewIndex);
			}
		}

		if ((CurrentWaterInfoPass == EWaterInfoPass::Dilation) || (bDebugShowWaterSceneProxiesEnabled))
		{
			for (const FWaterBodyMeshSection& Section : DilatedSections)
			{
				AddWaterBodyMeshSection(Section, ViewIndex);
			}
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

	return FPrimitiveSceneProxy::GetAllocatedSize() + Sections.GetAllocatedSize() + DilatedSections.GetAllocatedSize();
}

bool FWaterBodySceneProxy::IsShown(const FSceneView* View) const
{
	if (CurrentWaterInfoPass == EWaterInfoPass::None)
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
