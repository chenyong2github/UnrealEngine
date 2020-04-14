// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/LidarPointCloudRendering.h"
#include "LidarPointCloudRenderBuffers.h"
#include "LidarPointCloudComponent.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudOctree.h"
#include "LidarPointCloudLODManager.h"
#include "PrimitiveSceneProxy.h"
#include "MeshBatch.h"
#include "Engine/Engine.h"
#include "Misc/ScopeTryLock.h"
#include "SceneManagement.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("Draw Calls"), STAT_DrawCallCount, STATGROUP_LidarPointCloud)

FLidarPointCloudProxyUpdateData::FLidarPointCloudProxyUpdateData()
	: FirstElementIndex(0)
	, NumElements(0)
	, VDMultiplier(1)
	, RootCellSize(1)
{
}

class FLidarPointCloudCollisionRendering
{
public:
	FLidarPointCloudCollisionRendering()
		: NumPrimitives(0)
		, MaxVertexIndex(0)
	{
	}
	~FLidarPointCloudCollisionRendering()
	{
		VertexFactory.ReleaseResource();
		VertexBuffer.ReleaseResource();
		IndexBuffer.ReleaseResource();
	}

	void Initialize(FLidarPointCloudOctree* Octree)
	{
		// Create collision visualization buffers
		if (Octree->HasCollisionData())
		{
			const FTriMeshCollisionData* CollisionData = Octree->GetCollisionData();
			
			// Release the existing resources, if present
			VertexBuffer.ReleaseResource();
			VertexFactory.ReleaseResource();
			IndexBuffer.ReleaseResource();

			// Initialize the buffers
			VertexBuffer.Initialize(CollisionData->Vertices.GetData(), CollisionData->Vertices.Num());
			VertexFactory.Initialize(&VertexBuffer);
			IndexBuffer.Initialize((int32*)CollisionData->Indices.GetData(), CollisionData->Indices.Num() * 3);

			NumPrimitives = CollisionData->Indices.Num();
			MaxVertexIndex = CollisionData->Vertices.Num() - 1;
		}
	}

	const FVertexFactory* GetVertexFactory() const { return &VertexFactory; }
	const FIndexBuffer* GetIndexBuffer() const { return &IndexBuffer; }
	const int32 GetNumPrimitives() const { return NumPrimitives; }
	const int32 GetMaxVertexIndex() const { return MaxVertexIndex; }

private:
	class FLidarPointCloudCollisionVertexFactory : public FLocalVertexFactory
	{
	public:
		FLidarPointCloudCollisionVertexFactory() : FLocalVertexFactory(ERHIFeatureLevel::SM5, "") { }

		void Initialize(FVertexBuffer* InVertexBuffer)
		{
			FDataType NewData;
			NewData.PositionComponent = FVertexStreamComponent(InVertexBuffer, 0, 12, VET_Float3);
			NewData.TangentBasisComponents[0] = FVertexStreamComponent(InVertexBuffer, 0, 12, VET_PackedNormal);
			NewData.TangentBasisComponents[1] = FVertexStreamComponent(InVertexBuffer, 0, 12, VET_PackedNormal);
			NewData.ColorComponent = FVertexStreamComponent(InVertexBuffer, 0, 12, VET_Color);

			NewData.PositionComponentSRV = nullptr;
			NewData.ColorComponentsSRV = nullptr;
			NewData.TangentsSRV = nullptr;
			NewData.TextureCoordinatesSRV = nullptr;

			if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
			{
				NewData.PositionComponentSRV = RHICreateShaderResourceView(InVertexBuffer->VertexBufferRHI, 4, PF_R32_FLOAT);
				NewData.ColorComponentsSRV = RHICreateShaderResourceView(InVertexBuffer->VertexBufferRHI, 4, PF_R8G8B8A8);
				NewData.TangentsSRV = RHICreateShaderResourceView(InVertexBuffer->VertexBufferRHI, 4, PF_R8G8B8A8);
				NewData.TextureCoordinatesSRV = RHICreateShaderResourceView(InVertexBuffer->VertexBufferRHI, 4, PF_G16R16F);
			}

			Data = NewData;
			InitResource();
		}
	} VertexFactory;
	class FLidarPointCloudCollisionVertexBuffer : public FVertexBuffer
	{
	private:
		const void* Data;
		int32 DataLength;

	public:
		void Initialize(const FVector* InData, int32 InDataLength)
		{
			Data = InData;
			DataLength = InDataLength;

			InitResource();
		}

		virtual void InitRHI() override
		{
			FRHIResourceCreateInfo CreateInfo;
			void* Buffer = nullptr;
			VertexBufferRHI = RHICreateAndLockVertexBuffer(DataLength * sizeof(FVector), BUF_Static | BUF_ShaderResource, CreateInfo, Buffer);
			FMemory::Memcpy(Buffer, Data, DataLength * sizeof(FVector));
			RHIUnlockVertexBuffer(VertexBufferRHI);
			Buffer = nullptr;
		}
	} VertexBuffer;
	class FLidarPointCloudCollisionIndexBuffer : public FIndexBuffer
	{
	private:
		const int32* Data;
		int32 DataLength;

	public:
		void Initialize(const int32* InData, int32 InDataLength)
		{
			Data = InData;
			DataLength = InDataLength;

			InitResource();
		}

		virtual void InitRHI() override
		{
			FRHIResourceCreateInfo CreateInfo;
			void* Buffer = nullptr;
			IndexBufferRHI = RHICreateAndLockIndexBuffer(sizeof(uint32), DataLength * sizeof(uint32), BUF_Static, CreateInfo, Buffer);
			FMemory::Memcpy(Buffer, Data, DataLength * sizeof(uint32));
			RHIUnlockIndexBuffer(IndexBufferRHI);
			Buffer = nullptr;
		}
	} IndexBuffer;

	int32 NumPrimitives;
	int32 MaxVertexIndex;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS

class FLidarPointCloudSceneProxy : public ILidarPointCloudSceneProxy, public FPrimitiveSceneProxy
{
public:
	FLidarPointCloudSceneProxy(ULidarPointCloudComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, ProxyWrapper(MakeShared<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe>(this))
		, Component(Component)
		, Owner(Component->GetOwner())
		, CollisionRendering(Component->GetPointCloud()->CollisionRendering)
	{
		// Skip material verification - async update could occasionally cause it to crash
		bVerifyUsedMaterials = false;

		MaterialRelevance = Component->GetMaterialRelevance(GetScene().GetFeatureLevel());
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_PointCloudSceneProxy_GetDynamicMeshElements);

		if (!Component->GetMaterial(0))
		{
			return;
		}

		const bool bUsesSprites = Component->PointSize > 0;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FSceneView* View = Views[ViewIndex];

			if (IsShown(View) && (VisibilityMap & (1 << ViewIndex)))
			{
				// Prepare the draw call
				if (RenderData.NumElements)
				{
					FMeshBatch& MeshBatch = Collector.AllocateMesh();

					MeshBatch.Type = bUsesSprites ? PT_TriangleList : PT_PointList;
					MeshBatch.LODIndex = 0;
					MeshBatch.VertexFactory = &GLidarPointCloudVertexFactory;
					MeshBatch.bWireframe = false;
					MeshBatch.MaterialRenderProxy = Component->GetMaterial(0)->GetRenderProxy();
					MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
					MeshBatch.DepthPriorityGroup = SDPG_World;

					FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
					BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
					BatchElement.IndexBuffer = &GLidarPointCloudIndexBuffer;
					BatchElement.FirstIndex = bUsesSprites ? 0 : GLidarPointCloudIndexBuffer.PointOffset;
					BatchElement.MinVertexIndex = 0;
					BatchElement.NumPrimitives = RenderData.NumElements * (bUsesSprites ? 2 : 1);
					BatchElement.UserData = &UserData[UserData.Add(BuildUserDataElement(View))];

					Collector.AddMesh(ViewIndex, MeshBatch);

					INC_DWORD_STAT(STAT_DrawCallCount);
				}

#if !(UE_BUILD_SHIPPING)
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				// Draw selected nodes' bounds
				if (Component->bDrawNodeBounds)
				{
					for (const FBox& Node : RenderData.Bounds)
					{
						DrawWireBox(PDI, Node, FColor(72, 72, 255), SDPG_World);
					}
				}

				// Render bounds
				if (ViewFamily.EngineShowFlags.Bounds)
				{
					RenderBounds(PDI, ViewFamily.EngineShowFlags, GetBounds(), !Owner || IsSelected());
				}

				// Render collision wireframe
				if (ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled() && CollisionRendering && CollisionRendering->GetNumPrimitives() > 0)
				{
					// Create colored proxy
					FColoredMaterialRenderProxy* CollisionMaterialInstance;
					CollisionMaterialInstance = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial->GetRenderProxy(), FColor(0, 255, 255, 255));
					Collector.RegisterOneFrameMaterialProxy(CollisionMaterialInstance);

					FMeshBatch& MeshBatch = Collector.AllocateMesh();
					if (PrepareCollisionWireframe(MeshBatch, CollisionMaterialInstance))
					{
						Collector.AddMesh(ViewIndex, MeshBatch);
					}
				}
#endif // !(UE_BUILD_SHIPPING)
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;

		Result.bUseCustomViewData = true;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		Result.bStaticRelevance = false;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		MaterialRelevance.SetPrimitiveViewRelevance(Result);

		return Result;
	}

	virtual void* InitViewCustomData(const FSceneView& InView, float InViewLODScale, FMemStackBase& InCustomDataMemStack, bool InIsStaticRelevant, bool InIsShadowOnly, const FLODMask* InVisiblePrimitiveLODMask = nullptr, float InMeshScreenSizeSquared = -1.0f) override
	{
		// Don't process for shadow views
		if (!InIsShadowOnly)
		{
			UserData.Reset(10);
		}

		return nullptr;
	}

	bool PrepareCollisionWireframe(FMeshBatch& MeshBatch, FColoredMaterialRenderProxy* CollisionMaterialInstance) const
	{
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.VertexFactory = CollisionRendering->GetVertexFactory();
		MeshBatch.bWireframe = true;
		MeshBatch.MaterialRenderProxy = CollisionMaterialInstance;
		MeshBatch.ReverseCulling = !IsLocalToWorldDeterminantNegative();
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.CastShadow = false;

		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		BatchElement.IndexBuffer = CollisionRendering->GetIndexBuffer();
		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = CollisionRendering->GetNumPrimitives();
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = CollisionRendering->GetMaxVertexIndex();

		return true;
	}

	/** UserData is used to pass rendering information to the VertexFactory */
	FLidarPointCloudBatchElementUserData BuildUserDataElement(const FSceneView* InView) const
	{	
		FLidarPointCloudBatchElementUserData UserDataElement(RenderData.VDMultiplier, RenderData.RootCellSize);

		const bool bUsesSprites = Component->PointSize > 0;

		FVector BoundsSize = Component->GetPointCloud()->GetBounds().GetSize();

		// Make sure to apply minimum bounds size
		BoundsSize.X = FMath::Max(BoundsSize.X, 0.001f);
		BoundsSize.Y = FMath::Max(BoundsSize.Y, 0.001f);
		BoundsSize.Z = FMath::Max(BoundsSize.Z, 0.001f);

		// Update shader parameters
		UserDataElement.IndexDivisor = bUsesSprites ? 4 : 1;
		UserDataElement.FirstElementIndex = RenderData.FirstElementIndex;

		UserDataElement.LocationOffset = Component->GetPointCloud()->GetLocationOffset().ToVector();
		
		UserDataElement.SizeOffset = GLidarPointCloudRenderBuffer.PointCount * 4;
		UserDataElement.SpriteSizeMultiplier = Component->PointSize * Component->GetComponentScale().GetAbsMax();

		UserDataElement.ViewRightVector = InView->GetViewRight();
		UserDataElement.ViewUpVector = InView->GetViewUp();
		UserDataElement.BoundsSize = BoundsSize;
		UserDataElement.ElevationColorBottom = FVector(Component->ColorSource == ELidarPointCloudColorationMode::None ? FColor::White : Component->ElevationColorBottom);
		UserDataElement.ElevationColorTop = FVector(Component->ColorSource == ELidarPointCloudColorationMode::None ? FColor::White : Component->ElevationColorTop);
		UserDataElement.bUseCircle = bUsesSprites && Component->PointShape == ELidarPointCloudSpriteShape::Circle;
		UserDataElement.bUseColorOverride = Component->ColorSource != ELidarPointCloudColorationMode::Data;
		UserDataElement.bUseElevationColor = Component->ColorSource == ELidarPointCloudColorationMode::Elevation || Component->ColorSource == ELidarPointCloudColorationMode::None;
		UserDataElement.Offset = Component->Offset;
		UserDataElement.Contrast = Component->Contrast;
		UserDataElement.Saturation = Component->Saturation;
		UserDataElement.Gamma = Component->Gamma;
		UserDataElement.Tint = FVector(Component->ColorTint);
		UserDataElement.IntensityInfluence = Component->IntensityInfluence;

		UserDataElement.bUseClassification = Component->ColorSource == ELidarPointCloudColorationMode::Classification;
		UserDataElement.SetClassificationColors(Component->ClassificationColors);

		// Make sure to update the reference to the resource, in case it was re-initialized
		UserDataElement.DataBuffer = GLidarPointCloudRenderBuffer.SRV;

		return UserDataElement;
	}

	virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
	virtual uint32 GetMemoryFootprint() const override { return(sizeof(*this) + GetAllocatedSize()); }
	uint32 GetAllocatedSize() const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual void UpdateRenderData(FLidarPointCloudProxyUpdateData InRenderData) override { RenderData = InRenderData; }

public:
	TSharedPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> ProxyWrapper;

private:
	FLidarPointCloudProxyUpdateData RenderData;
	mutable TArray<FLidarPointCloudBatchElementUserData> UserData;

	ULidarPointCloudComponent* Component;
	FMaterialRelevance MaterialRelevance;
	AActor* Owner;

	FLidarPointCloudCollisionRendering* CollisionRendering;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

FPrimitiveSceneProxy* ULidarPointCloudComponent::CreateSceneProxy()
{
	FLidarPointCloudSceneProxy* Proxy = nullptr;
	if (PointCloud)
	{
		Proxy = new FLidarPointCloudSceneProxy(this);
		FLidarPointCloudLODManager::RegisterProxy(this, Proxy->ProxyWrapper);
	}
	return Proxy;
}

void ULidarPointCloud::InitializeCollisionRendering()
{
	// Do not process, if the app in incapable of rendering
	if (!FApp::CanEverRender())
	{
		return;
	}

	if (IsInRenderingThread())
	{
		FScopeLock Lock(&Octree.DataLock);

		if (!CollisionRendering)
		{
			CollisionRendering = new FLidarPointCloudCollisionRendering();
		}

		CollisionRendering->Initialize(&Octree);
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(InitializeCollisionRendering)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				InitializeCollisionRendering();
			});
	}
}

void ULidarPointCloud::ReleaseCollisionRendering()
{
	// Do not process, if the app in incapable of rendering
	if (!FApp::CanEverRender())
	{
		return;
	}

	if (IsInRenderingThread())
	{
		if (CollisionRendering)
		{
			delete CollisionRendering;
			CollisionRendering = nullptr;
		}
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(ReleaseCollisionRendering)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				ReleaseCollisionRendering();
			});
	}
}
