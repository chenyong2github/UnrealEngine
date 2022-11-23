// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowEngineSceneProxy.h"

#include "Async/ParallelFor.h"
#include "Dataflow/DataflowComponent.h"
#include "Dataflow/DataflowEnginePlugin.h"
#include "Dataflow/DataflowEngineSceneHitProxies.h"
#include "DynamicMeshBuilder.h"
#include "Engine/CollisionProfile.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "Materials/Material.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"

FDataflowEngineSceneProxy::FDataflowEngineSceneProxy(UDataflowComponent* Component)
	: FPrimitiveSceneProxy(Component)
	, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	, VertexFactory(GetScene().GetFeatureLevel(), "FTriangleSetSceneProxy")
	, DefaultHitProxy(new HDataflowActor(Component->GetOwner(), Component, 3, 7))
	, RenderMaterial(Component->GetMaterial(0))
	, ConstantData(Component->GetRenderingCollection().NewCopy())
{}

FDataflowEngineSceneProxy::~FDataflowEngineSceneProxy() 
{
	if (ConstantData) delete ConstantData;
}


void FDataflowEngineSceneProxy::CreateRenderThreadResources()
{
	check(ConstantData);
	check(IsInRenderingThread());

	GeometryCollection::Facades::FRenderingFacade Facade(*ConstantData);
	check(Facade.CanRenderSurface());

	const int32 NumTriangleVertices = Facade.NumTriangles();
	const int32 NumTriangleIndices = Facade.NumTriangles();
	const int32 TotalNumVertices = NumTriangleVertices * 3;
	const int32 TotalNumIndices = NumTriangleIndices * 3;
	constexpr int32 NumTextureCoordinates = 1;

	VertexBuffers.PositionVertexBuffer.Init(TotalNumVertices);
	VertexBuffers.StaticMeshVertexBuffer.Init(TotalNumVertices, NumTextureCoordinates);
	VertexBuffers.ColorVertexBuffer.Init(TotalNumVertices);
	IndexBuffer.Indices.SetNumUninitialized(TotalNumIndices);
#if WITH_EDITOR
	HitProxyIdBuffer.Init(TotalNumVertices);
#endif

	// Initialize points.
	// Triangles are represented as two tris, all of whose vertices are
	// coincident. The material then offsets them according to the signs of the
	// vertex normals in a camera facing orientation. Size of the point is given
	// by U0.
	if (Facade.NumTriangles() > 0)
	{
		MeshBatchDatas.Emplace();
		FDataflowTriangleSetMeshBatchData& MeshBatchData = MeshBatchDatas.Last();
		MeshBatchData.MinVertexIndex = 0;
		MeshBatchData.MaxVertexIndex = NumTriangleVertices - 1;
		MeshBatchData.StartIndex = 0;
		MeshBatchData.NumPrimitives = Facade.NumTriangles();
		if (!RenderMaterial)
		{
			RenderMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		MeshBatchData.MaterialProxy = RenderMaterial->GetRenderProxy();

		const TManagedArray<FIntVector>& Indices = Facade.GetIndices();
		const TManagedArray<FVector3f>& Vertex = Facade.GetVertices();

		// The color stored in the vertices actually gets interpreted as a linear
		// color by the material, whereas it is more convenient for the user of the
		// TriangleSet to specify colors as sRGB. So we actually have to convert it
		// back to linear. The ToFColor(false) call just scales back into 0-255
		// space.
		ParallelFor(Facade.NumTriangles(), [&](int32 i)
		{
		const int32 VertexBufferIndex = 3 * i;
		const int32 IndexBufferIndex = 3 * i;

		const auto& P1 = Vertex[Indices[i][0]];
		const auto& P2 = Vertex[Indices[i][1]];
		const auto& P3 = Vertex[Indices[i][2]];

		VertexBuffers.PositionVertexBuffer.VertexPosition(VertexBufferIndex + 0) = P1;
		VertexBuffers.PositionVertexBuffer.VertexPosition(VertexBufferIndex + 1) = P2;
		VertexBuffers.PositionVertexBuffer.VertexPosition(VertexBufferIndex + 2) = P3;

		FVector3f Tangent1 = (P2 - P1).GetSafeNormal();
		FVector3f Tangent2 = (P3 - P2).GetSafeNormal();
		FVector3f Normal = (Tangent2 ^ Tangent1).GetSafeNormal();

		VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexBufferIndex + 0, FVector3f(1, 0, 0), FVector3f(0, 1, 0), Normal);
		VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexBufferIndex + 1, FVector3f(1, 0, 0), FVector3f(0, 1, 0), Normal);
		VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexBufferIndex + 2, FVector3f(1, 0, 0), FVector3f(0, 1, 0), Normal);

		VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(VertexBufferIndex + 0, 0, FVector2f(0, 0));
		VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(VertexBufferIndex + 1, 0, FVector2f(0, 0));
		VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(VertexBufferIndex + 2, 0, FVector2f(0, 0));

		FColor FaceColor = IsSelected() ? IDataflowEnginePlugin::PrimarySelectionColor : IDataflowEnginePlugin::SurfaceColor;
		VertexBuffers.ColorVertexBuffer.VertexColor(VertexBufferIndex + 0) = FaceColor;
		VertexBuffers.ColorVertexBuffer.VertexColor(VertexBufferIndex + 1) = FaceColor;
		VertexBuffers.ColorVertexBuffer.VertexColor(VertexBufferIndex + 2) = FaceColor;

		IndexBuffer.Indices[IndexBufferIndex + 0] = VertexBufferIndex + 0;
		IndexBuffer.Indices[IndexBufferIndex + 1] = VertexBufferIndex + 1;
		IndexBuffer.Indices[IndexBufferIndex + 2] = VertexBufferIndex + 2;

#if WITH_EDITOR
		// @todo(Dataflow) Question : Does this even matter? 
		// FMeshBatch::BatchHitProxyId has this also and 
		// when GetCustomHitProxyIdBuffer returns non-null
		// the hit buffer seems to be ignored in the render.
		HitProxyIdBuffer.VertexColor(i) = DefaultHitProxy->Id.GetColor();
#endif
		});
	}
#if WITH_EDITOR
	SetUsedMaterialForVerification({RenderMaterial});
#endif

	VertexBuffers.PositionVertexBuffer.InitResource();
	VertexBuffers.StaticMeshVertexBuffer.InitResource();
	VertexBuffers.ColorVertexBuffer.InitResource();

	FLocalVertexFactory::FDataType Data;
	VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, Data);
	VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, Data);
	VertexBuffers.StaticMeshVertexBuffer.BindTexCoordVertexBuffer(&VertexFactory, Data);
	VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, Data);
	VertexFactory.SetData(Data);

	VertexFactory.InitResource();
	IndexBuffer.InitResource();
#if WITH_EDITOR
	HitProxyIdBuffer.InitResource();
#endif
}

void FDataflowEngineSceneProxy::DestroyRenderThreadResources()
{
	check(IsInRenderingThread());
	VertexBuffers.PositionVertexBuffer.ReleaseResource();
	VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
#if WITH_EDITOR
	HitProxyIdBuffer.ReleaseResource();
#endif
	delete ConstantData;
	ConstantData = nullptr;
}


#if WITH_EDITOR
HHitProxy* FDataflowEngineSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies)
{
	check(!IsInRenderingThread());
	OutHitProxies.Add(DefaultHitProxy);
	return FPrimitiveSceneProxy::CreateHitProxies(Component, OutHitProxies);
}


const FColorVertexBuffer* FDataflowEngineSceneProxy::GetCustomHitProxyIdBuffer() const
{
	// returning my own HitProxyIdBuffer causes the Hit to fail. 
	return nullptr;// &HitProxyIdBuffer;
}
#endif // WITH_EDITOR


void FDataflowEngineSceneProxy::GetDynamicMeshElements( 
	const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_OverlaySceneProxy_GetDynamicMeshElements);
	check(IsInRenderingThread());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			for (const FDataflowTriangleSetMeshBatchData& MeshBatchData : MeshBatchDatas)
			{
				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];
				BatchElement.IndexBuffer = &IndexBuffer;
				Mesh.bWireframe = false;
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MeshBatchData.MaterialProxy;

				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), GetLocalToWorld(),GetBounds(), GetLocalBounds(), true, false, AlwaysHasVelocity());
				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
				//BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

				BatchElement.FirstIndex = MeshBatchData.StartIndex;
				BatchElement.NumPrimitives = MeshBatchData.NumPrimitives;
				BatchElement.MinVertexIndex = MeshBatchData.MinVertexIndex;
				BatchElement.MaxVertexIndex = MeshBatchData.MaxVertexIndex;
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = true;
#if WITH_EDITOR
				Mesh.BatchHitProxyId = DefaultHitProxy->Id;
#endif // WITH_EDITOR

				Collector.AddMesh(ViewIndex, Mesh);
			}
		}
	}
}


FPrimitiveViewRelevance FDataflowEngineSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels =GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = false; // was ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = false;//was DrawsVelocity() && Result.bOpaque&& Result.bRenderInMainPass;
	return Result;
}


bool FDataflowEngineSceneProxy::CanBeOccluded() const
{
	return false;//was !MaterialRelevance.bDisableDepthTest;
}

SIZE_T FDataflowEngineSceneProxy::GetTypeHash() const
{
	static SIZE_T UniquePointer;
	return reinterpret_cast<SIZE_T>(&UniquePointer);
}
