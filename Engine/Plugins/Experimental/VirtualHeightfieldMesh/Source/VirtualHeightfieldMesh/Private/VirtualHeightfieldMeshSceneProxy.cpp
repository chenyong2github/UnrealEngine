// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshSceneProxy.h"

#include "CommonRenderResources.h"
#include "EngineModule.h"
#include "Engine/Engine.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "HeightfieldMinMaxTexture.h"
#include "Materials/Material.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "VirtualHeightfieldMeshComponent.h"
#include "VirtualHeightfieldMeshVertexFactory.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/VirtualTextureFeedbackBuffer.h"

DECLARE_STATS_GROUP(TEXT("Virtual Heightfield Mesh"), STATGROUP_VirtualHeightfieldMesh, STATCAT_Advanced);

DECLARE_LOG_CATEGORY_EXTERN(LogVirtualHeightfieldMesh, Warning, All);
DEFINE_LOG_CATEGORY(LogVirtualHeightfieldMesh);

static TAutoConsoleVariable<float> CVarVHMLodScale(
	TEXT("r.VHM.LodScale"),
	1.f,
	TEXT("Global LOD scale applied for Virtual Heightfield Mesh."),
	ECVF_RenderThreadSafe
);

// We disable View.LODDistanceFactor by default.
// When it is set according to GCalcLocalPlayerCachedLODDistanceFactor in ULocalPlayer we end up with double couting of the FOV scale.
// Ideally we would remove the calculation in ULocalPlayer and View.LODDistanceFactor would be only for view specific adjustments (screen captures etc.)
// However the removal of the code in ULocalPlayer could have a big impact on any preexisting data in any project.
static TAutoConsoleVariable<int32> CVarVHMEnableViewLodFactor(
	TEXT("r.VHM.EnableViewLodFactor"),
	0,
	TEXT("Enable the View.LODDistanceFactor.")
	TEXT("This is disabled by default to avoid an issue where FOV is double counted when calculating Lods.")
	TEXT("See comment in code for more information."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMOcclusion(
	TEXT("r.VHM.Occlusion"),
	1,
	TEXT("Enable occlusion queries."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMMaxRenderItems(
	TEXT("r.VHM.MaxRenderInstances"),
	1024 * 4,
	TEXT("Size of buffers used to collect render instances."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMMaxFeedbackItems(
	TEXT("r.VHM.MaxFeedbackItems"),
	1024,
	TEXT("Size of buffer used by virtual texture feedback."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMMaxPersistentQueueItems(
	TEXT("r.VHM.MaxPersistentQueueItems"),
	1024 * 4,
	TEXT("Size of queue used in the collect pass. This is rounded to the nearest power of 2."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMCollectPassWavefronts(
	TEXT("r.VHM.CollectPassWavefronts"),
	16,
	TEXT("Number of wavefronts to use for collect pass."),
	ECVF_RenderThreadSafe
);

namespace VirtualHeightfieldMesh
{
	/** Buffers filled by GPU culling used by the Virtual Heightfield Mesh final draw call. */
	struct FDrawInstanceBuffers
	{
		/* Culled instance buffer. */
		TRefCountPtr<FRDGPooledBuffer> InstanceBuffer;
		FShaderResourceViewRHIRef InstanceBufferSRV;

		/* IndirectArgs buffer for final DrawInstancedIndirect. */
		TRefCountPtr<FRDGPooledBuffer> IndirectArgsBuffer;
	};

	/** Initialize the FDrawInstanceBuffers objects. */
	void InitializeInstanceBuffers(FRHICommandListImmediate& InRHICmdList, FDrawInstanceBuffers& InBuffers);

	/** Release the FDrawInstanceBuffers objects. */
	void ReleaseInstanceBuffers(FDrawInstanceBuffers& InBuffers)
	{
		InBuffers.InstanceBuffer.SafeRelease();
		InBuffers.InstanceBufferSRV.SafeRelease();
		InBuffers.IndirectArgsBuffer.SafeRelease();
	}
}

struct FOcclusionResults
{
	FTexture2DRHIRef OcclusionTexture;
	FIntPoint TextureSize;
	int32 NumTextureMips;
};

struct FOcclusionResultsKey
{
	FVirtualHeightfieldMeshSceneProxy const* Proxy;
	FSceneView const* View;

	FOcclusionResultsKey(FVirtualHeightfieldMeshSceneProxy const* InProxy, FSceneView const* InView)
		: Proxy(InProxy)
		, View(InView)
	{
	}

	friend inline uint32 GetTypeHash(const FOcclusionResultsKey& InKey)
	{
		return HashCombine(GetTypeHash(InKey.View), GetTypeHash(InKey.Proxy));
	}

	friend bool operator==(const FOcclusionResultsKey& A, const FOcclusionResultsKey& B)
	{
		return A.View == B.View && A.Proxy == B.Proxy;
	}
};

/** */
TMap< FOcclusionResultsKey, FOcclusionResults > GOcclusionResults;

namespace VirtualHeightfieldMesh
{
	/** Calculate distances used for LODs in a given view for a given scene proxy. */
	FVector4 CalculateLodRanges(FSceneView const* InView, FVirtualHeightfieldMeshSceneProxy const* InProxy)
	{
		const uint32 MaxLevel = InProxy->AllocatedVirtualTexture->GetMaxLevel();
		const float Lod0UVSize = 1.f / (float)(1 << MaxLevel);
		const FVector2D Lod0WorldSize = FVector2D(InProxy->UVToWorldScale.X, InProxy->UVToWorldScale.Y) * Lod0UVSize;
		const float Lod0WorldRadius = Lod0WorldSize.Size();
		const float ScreenMultiple = FMath::Max(0.5f * InView->ViewMatrices.GetProjectionMatrix().M[0][0], 0.5f * InView->ViewMatrices.GetProjectionMatrix().M[1][1]);
		const float Lod0Distance = Lod0WorldRadius * ScreenMultiple / InProxy->Lod0ScreenSize;
		const float ViewLodDistanceFactor = CVarVHMEnableViewLodFactor.GetValueOnRenderThread() == 0 ? 1.f : InView->LODDistanceFactor;
		const float LodScale = ViewLodDistanceFactor * CVarVHMLodScale.GetValueOnRenderThread();
		
		return FVector4(Lod0Distance, InProxy->Lod0Distribution, InProxy->LodDistribution, LodScale);
	}
}

/** Renderer extension to manage the buffer pool and add hooks for GPU culling passes. */
class FVirtualHeightfieldMeshRendererExtension : public FRenderResource
{
public:
	FVirtualHeightfieldMeshRendererExtension()
		: bInFrame(false)
		, DiscardId(0)
	{}

	virtual ~FVirtualHeightfieldMeshRendererExtension()
	{}

	/** Call once to register this extension. */
	void RegisterExtension();

	/** Call once per frame for each mesh/view that has relevance. This allocates the buffers to use for the frame and adds the work to fill the buffers to the queue. */
	VirtualHeightfieldMesh::FDrawInstanceBuffers& AddWork(FVirtualHeightfieldMeshSceneProxy const* InProxy, FSceneView const* InMainView, FSceneView const* InCullView);
	/** Submit all the work added by AddWork(). The work fills all of the buffers ready for use by the referencing mesh batches. */
	void SubmitWork(FRHICommandListImmediate& InRHICmdList);

protected:
	//~ Begin FRenderResource Interface
	virtual void ReleaseRHI() override;
	//~ End FRenderResource Interface

private:
	/** Called by renderer at start of render frame. */
	void BeginFrame();
	/** Called by renderer at end of render frame. */
	void EndFrame();

	/** Flag for frame validation. */
	bool bInFrame;

	/** Buffers to fill. Resources can persist between frames to reduce allocation cost, but contents don't persist. */
	TArray<VirtualHeightfieldMesh::FDrawInstanceBuffers> Buffers;
	/** Per buffer frame time stamp of last usage. */
	TArray<uint32> DiscardIds;
	/** Current frame time stamp. */
	uint32 DiscardId;

	/** Arrary of uniqe scene proxies to render this frame. */
	TArray<FVirtualHeightfieldMeshSceneProxy const*> SceneProxies;
	/** Arrary of unique main views to render this frame. */
	TArray<FSceneView const*> MainViews;
	/** Arrary of unique culling views to render this frame. */
	TArray<FSceneView const*> CullViews;

	/** Key for each buffer we need to generate. */
	struct FWorkDesc
	{
		int32 ProxyIndex;
		int32 MainViewIndex;
		int32 CullViewIndex;
		int32 BufferIndex;
	};

	/** Keys specifying what to render. */
	TArray<FWorkDesc> WorkDescs;

	/** Sort predicate for FWorkDesc. When rendering we want to batch work by proxy, then by main view. */
	struct FWorkDescSort
	{
		uint32 SortKey(FWorkDesc const& WorkDesc) const
		{
			return (WorkDesc.ProxyIndex << 24) | (WorkDesc.MainViewIndex << 16) | (WorkDesc.CullViewIndex << 8) | WorkDesc.BufferIndex;
		}

		bool operator()(FWorkDesc const& A, FWorkDesc const& B) const
		{
			return SortKey(A) < SortKey(B);
		}
	};
};

/** Single global instance of the VirtualHeightfieldMesh renderer extension. */
TGlobalResource< FVirtualHeightfieldMeshRendererExtension > GVirtualHeightfieldMeshViewRendererExtension;

void FVirtualHeightfieldMeshRendererExtension::RegisterExtension()
{
	static bool bInit = false;
	if (!bInit)
	{
		GEngine->GetPreRenderDelegate().AddRaw(this, &FVirtualHeightfieldMeshRendererExtension::BeginFrame);
		GEngine->GetPostRenderDelegate().AddRaw(this, &FVirtualHeightfieldMeshRendererExtension::EndFrame);
		bInit = true;
	}
}

void FVirtualHeightfieldMeshRendererExtension::ReleaseRHI()
{
	Buffers.Empty();
}

VirtualHeightfieldMesh::FDrawInstanceBuffers& FVirtualHeightfieldMeshRendererExtension::AddWork(FVirtualHeightfieldMeshSceneProxy const* InProxy, FSceneView const* InMainView, FSceneView const* InCullView)
{
	// If we hit this then BegineFrame()/EndFrame() logic needs fixing in the Scene Renderer.
	if (!ensure(!bInFrame))
	{
		EndFrame();
	}

	// Create workload
	FWorkDesc WorkDesc;
	WorkDesc.ProxyIndex = SceneProxies.AddUnique(InProxy);
	WorkDesc.MainViewIndex = MainViews.AddUnique(InMainView);
	WorkDesc.CullViewIndex = CullViews.AddUnique(InCullView);
	WorkDesc.BufferIndex = -1;

	// Check for an existing duplicate
	for (FWorkDesc& It : WorkDescs)
	{
		if (It.ProxyIndex == WorkDesc.ProxyIndex && It.MainViewIndex == WorkDesc.MainViewIndex && It.CullViewIndex == WorkDesc.CullViewIndex && It.BufferIndex != -1)
		{
			WorkDesc.BufferIndex = It.BufferIndex;
			break;
		}
	}

	// Try to recycle a buffer
	if (WorkDesc.BufferIndex == -1)
	{
		for (int32 BufferIndex = 0; BufferIndex < Buffers.Num(); BufferIndex++)
		{
			if (DiscardIds[BufferIndex] < DiscardId)
			{
				DiscardIds[BufferIndex] = DiscardId;
				WorkDesc.BufferIndex = BufferIndex;
				WorkDescs.Add(WorkDesc);
				break;
			}
		}
	}

	// Allocate new buffer if necessary
	if (WorkDesc.BufferIndex == -1)
	{
		DiscardIds.Add(DiscardId);
		WorkDesc.BufferIndex = Buffers.AddDefaulted();
		WorkDescs.Add(WorkDesc);
		VirtualHeightfieldMesh::InitializeInstanceBuffers(GetImmediateCommandList_ForRenderCommand(), Buffers[WorkDesc.BufferIndex]);
	}

	return Buffers[WorkDesc.BufferIndex];
}

void FVirtualHeightfieldMeshRendererExtension::BeginFrame()
{
	// If we hit this then BegineFrame()/EndFrame() logic needs fixing in the Scene Renderer.
	if (!ensure(!bInFrame))
	{
		EndFrame();
	}
	bInFrame = true;

	if (WorkDescs.Num() > 0)
	{
		SubmitWork(GetImmediateCommandList_ForRenderCommand());
	}
}

void FVirtualHeightfieldMeshRendererExtension::EndFrame()
{
	ensure(bInFrame);
	bInFrame = false;

	SceneProxies.Reset();
	MainViews.Reset();
	CullViews.Reset();
	WorkDescs.Reset();

	// Clean the buffer pool
	DiscardId++;

	for (int32 Index = 0; Index < DiscardIds.Num();)
	{
		if (DiscardId - DiscardIds[Index] > 4u)
		{
			VirtualHeightfieldMesh::ReleaseInstanceBuffers(Buffers[Index]);
			Buffers.RemoveAtSwap(Index);
			DiscardIds.RemoveAtSwap(Index);
		}
		else
		{
			++Index;
		}
	}

	GOcclusionResults.Reset();
}

const static FName NAME_VirtualHeightfieldMesh(TEXT("VirtualHeightfieldMesh"));

FVirtualHeightfieldMeshSceneProxy::FVirtualHeightfieldMeshSceneProxy(UVirtualHeightfieldMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent, NAME_VirtualHeightfieldMesh)
	, bHiddenInEditor(InComponent->GetHiddenInEditor())
	, RuntimeVirtualTexture(InComponent->GetVirtualTexture())
	, MinMaxTexture(nullptr)
	, AllocatedVirtualTexture(nullptr)
	, bCallbackRegistered(false)
	, NumQuadsPerTileSide(0)
	, VertexFactory(nullptr)
	, Lod0ScreenSize(InComponent->GetLod0ScreenSize())
	, Lod0Distribution(InComponent->GetLod0Distribution())
	, LodDistribution(InComponent->GetLodDistribution())
	, NumSubdivisionLODs(InComponent->GetNumSubdivisionLods())
	, NumTailLods(InComponent->GetNumTailLods())
	, NumOcclusionLods(0)
	, OcclusionGridSize(0, 0)
{
	GVirtualHeightfieldMeshViewRendererExtension.RegisterExtension();

	UMaterialInterface* ComponentMaterial = InComponent->GetMaterial();
	const bool bValidMaterial = ComponentMaterial != nullptr && ComponentMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_VirtualHeightfieldMesh);
	Material = bValidMaterial ? ComponentMaterial->GetRenderProxy() : UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();

	const FTransform VirtualTextureTransform = InComponent->GetVirtualTextureTransform();

	UVToWorldScale = VirtualTextureTransform.GetScale3D();
	UVToWorld = VirtualTextureTransform.ToMatrixWithScale();

	WorldToUV = UVToWorld.Inverse();
	WorldToUVTransposeAdjoint = WorldToUV.TransposeAdjoint();

	UVToLocal = UVToWorld * GetLocalToWorld().Inverse();

	UHeightfieldMinMaxTexture* HeightfieldMinMaxTexture = InComponent->GetMinMaxTexture();
	if (HeightfieldMinMaxTexture != nullptr)
	{
		MinMaxTexture = HeightfieldMinMaxTexture->Texture;
		BuildOcclusionVolumes(HeightfieldMinMaxTexture->TextureData, HeightfieldMinMaxTexture->TextureDataSize, HeightfieldMinMaxTexture->TextureDataMips, InComponent->GetNumOcclusionLods());
	}
}

SIZE_T FVirtualHeightfieldMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FVirtualHeightfieldMeshSceneProxy::GetMemoryFootprint() const
{
	return(sizeof(*this) + FPrimitiveSceneProxy::GetAllocatedSize());
}

void FVirtualHeightfieldMeshSceneProxy::OnTransformChanged()
{
	UVToLocal = UVToWorld * GetLocalToWorld().Inverse();

	// Setup a default occlusion volume array containing just the primitive bounds.
	// We use this if disabling the full set of occlusion volumes.
	DefaultOcclusionVolumes.Reset();
	DefaultOcclusionVolumes.Add(GetBounds());
}

void FVirtualHeightfieldMeshSceneProxy::CreateRenderThreadResources()
{
	if (RuntimeVirtualTexture != nullptr)
	{
		if (!bCallbackRegistered)
		{
			GetRendererModule().AddVirtualTextureProducerDestroyedCallback(RuntimeVirtualTexture->GetProducerHandle(), &OnVirtualTextureDestroyedCB, this);
			bCallbackRegistered = true;
		}

		if (RuntimeVirtualTexture->GetMaterialType() == ERuntimeVirtualTextureMaterialType::WorldHeight)
		{
			AllocatedVirtualTexture = RuntimeVirtualTexture->GetAllocatedVirtualTexture();

			NumQuadsPerTileSide = RuntimeVirtualTexture->GetTileSize();
			VertexFactory = new FVirtualHeightfieldMeshVertexFactory(GetScene().GetFeatureLevel(), NumQuadsPerTileSide);
			VertexFactory->InitResource();
		}
	}
}

void FVirtualHeightfieldMeshSceneProxy::DestroyRenderThreadResources()
{
	if (VertexFactory != nullptr)
	{
		VertexFactory->ReleaseResource();
		delete VertexFactory;
		VertexFactory = nullptr;
	}

	if (bCallbackRegistered)
	{
		GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(this);
		bCallbackRegistered = false;
	}
}

void FVirtualHeightfieldMeshSceneProxy::OnVirtualTextureDestroyedCB(const FVirtualTextureProducerHandle& InHandle, void* Baton)
{
	FVirtualHeightfieldMeshSceneProxy* SceneProxy = (FVirtualHeightfieldMeshSceneProxy*)Baton;
	SceneProxy->DestroyRenderThreadResources();
	SceneProxy->CreateRenderThreadResources();
}

FPrimitiveViewRelevance FVirtualHeightfieldMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	const bool bValid = AllocatedVirtualTexture != nullptr;
	const bool bIsHiddenInEditor = bHiddenInEditor && View->Family->EngineShowFlags.Editor;

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = bValid && IsShown(View) && !bIsHiddenInEditor;
	Result.bShadowRelevance = bValid && IsShadowCast(View) && ShouldRenderInMainPass() &&!bIsHiddenInEditor;
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = false;
	Result.bVelocityRelevance = false;
	return Result;
}

void FVirtualHeightfieldMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	check(IsInRenderingThread());
	check(AllocatedVirtualTexture != nullptr);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			VirtualHeightfieldMesh::FDrawInstanceBuffers& Buffers = GVirtualHeightfieldMeshViewRendererExtension.AddWork(this, ViewFamily.Views[0], Views[ViewIndex]);

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
			Mesh.bUseWireframeSelectionColoring = IsSelected();
			Mesh.VertexFactory = VertexFactory;
			Mesh.MaterialRenderProxy = Material;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.bUseForMaterial = true;
			Mesh.CastShadow = true;
			Mesh.bUseForDepthPass = true;

			Mesh.Elements.SetNumZeroed(1);
			{
				FMeshBatchElement& BatchElement = Mesh.Elements[0];

				BatchElement.IndexBuffer = VertexFactory->IndexBuffer;
				BatchElement.IndirectArgsBuffer = Buffers.IndirectArgsBuffer->GetVertexBufferRHI();
				BatchElement.IndirectArgsOffset = 0;

				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = 0;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = 0;

				FVirtualHeightfieldMeshUserData* UserData = &Collector.AllocateOneFrameResource<FVirtualHeightfieldMeshUserData>();
				BatchElement.UserData = (void*)UserData;

				UserData->InstanceBufferSRV = Buffers.InstanceBufferSRV;
				UserData->HeightPhysicalTexture = AllocatedVirtualTexture->GetPhysicalTexture(0);

				const float PageTableSizeX = AllocatedVirtualTexture->GetWidthInTiles();
				const float PageTableSizeY = AllocatedVirtualTexture->GetHeightInTiles();
				UserData->PageTableSize = FVector4(PageTableSizeX, PageTableSizeY, 1.f / PageTableSizeX, 1.f / PageTableSizeY);

				UserData->MaxLod = AllocatedVirtualTexture->GetMaxLevel() + NumTailLods;
				UserData->VirtualHeightfieldToLocal = UVToLocal;
				UserData->VirtualHeightfieldToWorld = UVToWorld;

				FSceneView const* MainView = ViewFamily.Views[0];
				
				UserData->LodViewOrigin = MainView->ViewMatrices.GetViewOrigin();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				// Support the freezerendering mode. Use any frozen view state for culling.
				const FViewMatrices* FrozenViewMatrices = MainView->State != nullptr ? MainView->State->GetFrozenViewMatrices() : nullptr;
				if (FrozenViewMatrices != nullptr)
				{
					UserData->LodViewOrigin = FrozenViewMatrices->GetViewOrigin();
				}
#endif
				
				UserData->LodDistances = VirtualHeightfieldMesh::CalculateLodRanges(MainView, this);
				
				BatchElement.PrimitiveIdMode = PrimID_ForceZero;
				BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
			}
			Collector.AddMesh(ViewIndex, Mesh);
		}
	}
}

bool FVirtualHeightfieldMeshSceneProxy::HasSubprimitiveOcclusionQueries() const
{
	return CVarVHMOcclusion.GetValueOnAnyThread() != 0;
}

const TArray<FBoxSphereBounds>* FVirtualHeightfieldMeshSceneProxy::GetOcclusionQueries(const FSceneView* View) const
{
	return (CVarVHMOcclusion.GetValueOnAnyThread() == 0 || OcclusionVolumes.Num() == 0) ? &DefaultOcclusionVolumes : &OcclusionVolumes;
}

void FVirtualHeightfieldMeshSceneProxy::BuildOcclusionVolumes(TArrayView<FVector2D> const& InMinMaxData, FIntPoint const& InMinMaxSize, TArrayView<int32> const& InMinMaxMips, int32 InNumLods)
{
	NumOcclusionLods = 0;
	OcclusionGridSize = FIntPoint::ZeroValue;
	OcclusionVolumes.Reset();

	if (InNumLods > 0 && InMinMaxMips.Num() > 0)
	{
		NumOcclusionLods = FMath::Min(InNumLods, InMinMaxMips.Num());

		const int32 BaseLod = InMinMaxMips.Num() - NumOcclusionLods;
		OcclusionGridSize.X = FMath::Max(InMinMaxSize.X >> BaseLod, 1);
		OcclusionGridSize.Y = FMath::Max(InMinMaxSize.Y >> BaseLod, 1);

		OcclusionVolumes.Reserve(InMinMaxData.Num() - InMinMaxMips[BaseLod]);

		for (int32 LodIndex = BaseLod; LodIndex < InMinMaxMips.Num(); ++LodIndex)
		{
			int32 SizeX = FMath::Max(InMinMaxSize.X >> LodIndex, 1);
			int32 SizeY = FMath::Max(InMinMaxSize.Y >> LodIndex, 1);
			int32 MinMaxDataIndex = InMinMaxMips[LodIndex];

			for (int Y = 0; Y < SizeY; ++Y)
			{
				for (int X = 0; X < SizeX; ++X)
				{
					FVector2D MinMaxU = FVector2D((float)X / (float)SizeX, (float)(X + 1) / (float)SizeX);
					FVector2D MinMaxV = FVector2D((float)Y / (float)SizeY, (float)(Y + 1) / (float)SizeY);
					FVector2D MinMaxZ = InMinMaxData[MinMaxDataIndex++];

					FVector Pos[8];
					Pos[0] = UVToWorld.TransformPosition(FVector(MinMaxU.X, MinMaxV.X, MinMaxZ.X));
					Pos[1] = UVToWorld.TransformPosition(FVector(MinMaxU.Y, MinMaxV.X, MinMaxZ.X));
					Pos[2] = UVToWorld.TransformPosition(FVector(MinMaxU.X, MinMaxV.Y, MinMaxZ.X));
					Pos[3] = UVToWorld.TransformPosition(FVector(MinMaxU.Y, MinMaxV.Y, MinMaxZ.X));
					Pos[4] = UVToWorld.TransformPosition(FVector(MinMaxU.X, MinMaxV.X, MinMaxZ.Y));
					Pos[5] = UVToWorld.TransformPosition(FVector(MinMaxU.Y, MinMaxV.X, MinMaxZ.Y));
					Pos[6] = UVToWorld.TransformPosition(FVector(MinMaxU.X, MinMaxV.Y, MinMaxZ.Y));
					Pos[7] = UVToWorld.TransformPosition(FVector(MinMaxU.Y, MinMaxV.Y, MinMaxZ.Y));

					const float ExpandOcclusion = 3.f;
					OcclusionVolumes.Add(FBoxSphereBounds(FBox(Pos, 8).ExpandBy(ExpandOcclusion)));
				}
			}
		}
	}
}

void FVirtualHeightfieldMeshSceneProxy::AcceptOcclusionResults(FSceneView const* View, TArray<bool>* Results, int32 ResultsStart, int32 NumResults)
{
	check(IsInRenderingThread());

	if (CVarVHMOcclusion.GetValueOnAnyThread() != 0 && Results != nullptr && NumResults > 1)
	{
		FOcclusionResults& OcclusionResults = GOcclusionResults.Emplace(FOcclusionResultsKey(this, View));
		OcclusionResults.TextureSize = OcclusionGridSize;
		OcclusionResults.NumTextureMips = NumOcclusionLods;
		
		FRHIResourceCreateInfo CreateInfo;
		OcclusionResults.OcclusionTexture = RHICreateTexture2D(OcclusionGridSize.X, OcclusionGridSize.Y, PF_G8, NumOcclusionLods, 1, TexCreate_ShaderResource, CreateInfo);
		
		bool const* Src = Results->GetData() + ResultsStart;
		FIntPoint Size = OcclusionGridSize;
		for (int32 MipIndex = 0; MipIndex < NumOcclusionLods; ++MipIndex)
		{
			uint32 Stride;
			uint8* Dst = (uint8*)RHILockTexture2D(OcclusionResults.OcclusionTexture, MipIndex, RLM_WriteOnly, Stride, false);
		
			for (int Y = 0; Y < Size.Y; ++Y)
			{
				for (int X = 0; X < Size.X; ++X)
				{
					Dst[Y * Stride + X] = *(Src++) ? 255 : 0;
				}
			}
			
			RHIUnlockTexture2D(OcclusionResults.OcclusionTexture, MipIndex, false);

			Size.X = FMath::Max(Size.X / 2, 1);
			Size.Y = FMath::Max(Size.Y / 2, 1);
		}
	}		
}

namespace VirtualHeightfieldMesh
{
	/* Keep indirect args offsets in sync with VirtualHeightfieldMesh.usf. */
	static const int32 IndirectArgsByteOffset_RenderLodMap = 0;
	static const int32 IndirectArgsByteOffset_FetchNeighborLod = 5 * sizeof(uint32);
	static const int32 IndirectArgsByteOffset_FinalCull = 5 * sizeof(uint32);
	static const int32 IndirectArgsByteSize = 9 * sizeof(uint32);

	/** Shader structure used for tracking work queues in persistent wave style shaders. Keep in sync with VirtualHeightfieldMesh.ush. */
	struct WorkerQueueInfo
	{
		uint32 Read;
		uint32 Write;
		int32 NumActive;
	};

	/** Final render instance description used by the DrawInstancedIndirect(). Keep in sync with VirtualHeightfieldMesh.ush. */
	struct QuadRenderInstance
	{
		uint32 AddressLevelPacked;
		float UVTransform[3];
		float NeigborUVTransform[4][3];
	};

	/** Compute shader to initialize all buffers, including adding the lowest mip page(s) to the QuadBuffer. */
	class FInitBuffersCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FInitBuffersCS);
		SHADER_USE_PARAMETER_STRUCT(FInitBuffersCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, MaxLevel)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<WorkerQueueInfo>, RWQueueInfo)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQueueBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWQuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFeedbackBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FInitBuffersCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "InitBuffersCS", SF_Compute);

	/** Compute shader to traverse the virtual texture page table for a view and generate an array of quads to potentially render. */
	class FCollectQuadsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FCollectQuadsCS);
		SHADER_USE_PARAMETER_STRUCT(FCollectQuadsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_TEXTURE(Texture2D, MinMaxTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, MinMaxTextureSampler)
			SHADER_PARAMETER(int32, MinMaxLevelOffset)
			SHADER_PARAMETER_TEXTURE(Texture2D<float>, OcclusionTexture)
			SHADER_PARAMETER(int32, OcclusionLevelOffset)
			SHADER_PARAMETER_TEXTURE(Texture2D<uint>, PageTableTexture)
			SHADER_PARAMETER(FVector4, PageTableSize)
			SHADER_PARAMETER(FVector4, LodDistances)
			SHADER_PARAMETER(FVector, ViewOrigin)
			SHADER_PARAMETER_ARRAY(FVector4, FrustumPlanes, [5])
			SHADER_PARAMETER(FMatrix, UVToWorld)
			SHADER_PARAMETER(FVector, UVToWorldScale)
			SHADER_PARAMETER(uint32, QueueBufferSizeMask)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<WorkerQueueInfo>, RWQueueInfo)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQueueBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWQuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FCollectQuadsCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "CollectQuadsCS", SF_Compute);

	/** Shader that draws to a render target the Lod info for the quads output by the Collect pass. */
	class FRenderLodMap : public FGlobalShader
	{
	public:
		SHADER_USE_PARAMETER_STRUCT(FRenderLodMap, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			RENDER_TARGET_BINDING_SLOTS()
			SHADER_PARAMETER(FVector4, PageTableSize)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, QuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	/** RenderLodMap vertex shader. */
	class FRenderLodMapVS : public FRenderLodMap
	{
	public:
		DECLARE_GLOBAL_SHADER(FRenderLodMapVS);

		FRenderLodMapVS()
		{}

		FRenderLodMapVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FRenderLodMap(Initializer)
		{}
	};

	IMPLEMENT_GLOBAL_SHADER(FRenderLodMapVS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "RenderLodMapVS", SF_Vertex);

	/** RenderLodMap pixel shader. */
	class FRenderLodMapPS : public FRenderLodMap
	{
	public:
		DECLARE_GLOBAL_SHADER(FRenderLodMapPS);

		FRenderLodMapPS()
		{}

		FRenderLodMapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FRenderLodMap(Initializer)
		{}
	};

	IMPLEMENT_GLOBAL_SHADER(FRenderLodMapPS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "RenderLodMapPS", SF_Pixel);

	/** */
	class FResolveNeighborLodsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FResolveNeighborLodsCS);
		SHADER_USE_PARAMETER_STRUCT(FResolveNeighborLodsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector4, PageTableSize)
			SHADER_PARAMETER_TEXTURE(Texture2D, PageTableTexture)
			SHADER_PARAMETER(uint32, PageTableFeedbackId)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, QuadBuffer)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, LodTexture)
			SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgsBuffer)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndirectArgsBufferSRV)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadNeighborBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFeedbackBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FResolveNeighborLodsCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "ResolveNeighborLodsCS", SF_Compute);

	/** */
	class FInitInstanceBufferCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FInitInstanceBufferCS);
		SHADER_USE_PARAMETER_STRUCT(FInitInstanceBufferCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(int32, NumIndices)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FInitInstanceBufferCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "InitInstanceBufferCS", SF_Compute);

	/** */
	class FCullInstances : public FGlobalShader
	{
	public:
		SHADER_USE_PARAMETER_STRUCT(FCullInstances, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_TEXTURE(Texture2D, MinMaxTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, MinMaxTextureSampler)
			SHADER_PARAMETER_TEXTURE(Texture2D, PageTableTexture)
			SHADER_PARAMETER(FVector4, PageTableSize)
			SHADER_PARAMETER_ARRAY(FVector4, FrustumPlanes, [5])
			SHADER_PARAMETER(FVector4, PhysicalPageTransform)
			SHADER_PARAMETER(uint32, NumPhysicalAddressBits)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, QuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadNeighborBuffer)
			SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgsBuffer)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndirectArgsBufferSRV)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadRenderInstance>, RWInstanceBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()
	};

	template< bool bReuseCull >
	class TCullInstancesCS : public FCullInstances
	{
	public:
		typedef TCullInstancesCS< bReuseCull > ClassName;
		DECLARE_GLOBAL_SHADER(ClassName);

		TCullInstancesCS()
		{}

		TCullInstancesCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FCullInstances(Initializer)
		{}

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("REUSE_CULL"), bReuseCull ? 1 : 0);
		}
	};

	IMPLEMENT_SHADER_TYPE(template<>, TCullInstancesCS<true>, TEXT("/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf"), TEXT("CullInstancesCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, TCullInstancesCS<false>, TEXT("/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf"), TEXT("CullInstancesCS"), SF_Compute);


	/** Default Min/Max texture has the fixed maximum [0,1]. */
	class FMinMaxDefaultTexture : public FTexture
	{
	public:
		virtual void InitRHI() override
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("MinMaxDefaultTexture"));
			FTexture2DRHIRef Texture2D = RHICreateTexture2D(1, 1, PF_B8G8R8A8, 1, 1, TexCreate_ShaderResource, CreateInfo);
			TextureRHI = Texture2D;

			// Write the contents of the texture.
			uint32 DestStride;
			FColor* DestBuffer = (FColor*)RHILockTexture2D(Texture2D, 0, RLM_WriteOnly, DestStride, false);
			*DestBuffer = FColor(0, 0, 255, 255);
			RHIUnlockTexture2D(Texture2D, 0, false);

			// Create the sampler state RHI resource.
			FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point, AM_Clamp, AM_Clamp, AM_Clamp);
			SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);
		}

		virtual uint32 GetSizeX() const override { return 1; }
		virtual uint32 GetSizeY() const override { return 1; }
	};

	/** Single global instance of default Min/Max texture. */
	FTexture* GMinMaxDefaultTexture = new TGlobalResource<FMinMaxDefaultTexture>;

	/** */
	struct FViewData
	{
		FVector ViewOrigin;
		FMatrix ProjectionMatrix;
		FConvexVolume ViewFrustum;
		bool bViewFrozen;
	};

	/** Fill the FViewData from an FSceneView respecting the freezerendering mode. */
	void GetViewData(FSceneView const* InSceneView, FViewData& OutViewData)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const FViewMatrices* FrozenViewMatrices = InSceneView->State != nullptr ? InSceneView->State->GetFrozenViewMatrices() : nullptr;
		if (FrozenViewMatrices != nullptr)
		{
			OutViewData.ViewOrigin = FrozenViewMatrices->GetViewOrigin();
			OutViewData.ProjectionMatrix = FrozenViewMatrices->GetProjectionMatrix();
			GetViewFrustumBounds(OutViewData.ViewFrustum, FrozenViewMatrices->GetViewProjectionMatrix(), true);
			OutViewData.bViewFrozen = true;
		}
		else
#endif
		{
			OutViewData.ViewOrigin = InSceneView->ViewMatrices.GetViewOrigin();
			OutViewData.ProjectionMatrix = InSceneView->ViewMatrices.GetProjectionMatrix();
			OutViewData.ViewFrustum = InSceneView->ViewFrustum;
			OutViewData.bViewFrozen = false;
		}
	}

	/** Convert FPlane to Xx+Yy+Zz+W=0 form for simpler use in shader. */
	FVector4 ConvertPlane(FPlane const& Plane)
	{
		return FVector4(-Plane.X, -Plane.Y, -Plane.Z, Plane.W);
	}

	/** Translate a plane. This is a simpler case than the full TransformPlane(). */
	FPlane TranslatePlane(FPlane const& Plane, FVector const& Translation)
	{
		FPlane OutPlane = Plane / Plane.Size();
		OutPlane.W -= FVector::DotProduct(FVector(OutPlane),  Translation);
		return OutPlane;
	}

	/** Transform a plane using a transform matrix. Precalculate and pass in transpose adjoint to avoid work when transforming multiple planes.  */
	FPlane TransformPlane(FPlane const& Plane, FMatrix const& Matrix, FMatrix const& TransposeAdjoint)
	{
		FVector N(Plane.X, Plane.Y, Plane.Z);
		N = TransposeAdjoint.TransformVector(N).GetUnsafeNormal3();

		FVector P(Plane.X * Plane.W, Plane.Y * Plane.W, Plane.Z * Plane.W);
		P = Matrix.TransformPosition(P);

		return FPlane(N, FVector::DotProduct(N, P));
	}

	/** Structure describing GPU culling setup for a single Proxy. */
	struct FProxyDesc
	{
		FRHITexture* PageTableTexture;
		FRHITexture* MinMaxTexture;
		int32 MinMaxLevelOffset;

		uint32 MaxLevel;
		uint32 PageTableFeedbackId;
		uint32 NumPhysicalAddressBits;
		FVector4 PageTableSize;
		FVector4 PhysicalPageTransform;
		FMatrix UVToWorld;
		FVector UVToWorldScale;
		uint32 NumQuadsPerTileSide;

		int32 MaxPersistentQueueItems;
		int32 MaxRenderItems;
		int32 MaxFeedbackItems;
		int32 NumCollectPassWavefronts;
	};

	/** View description used for LOD calculation in the main view. */
	struct FMainViewDesc
	{
		FSceneView const* ViewDebug;
		FVector ViewOrigin;
		FVector4 LodDistances;
		FVector4 Planes[5];
		FTextureRHIRef OcclusionTexture;
		int32 OcclusionLevelOffset;
	};

	/** View description used for culling in the child view. */
	struct FChildViewDesc
	{
		FSceneView const* ViewDebug;
		bool bIsMainView;
		FVector4 Planes[5];
	};

	/** Structure to carry RDG resources. */
	struct FVolatileResources
	{
		FRDGBufferRef QueueInfo;
		FRDGBufferUAVRef QueueInfoUAV;
		FRDGBufferRef QueueBuffer;
		FRDGBufferUAVRef QueueBufferUAV;

		FRDGBufferRef QuadBuffer;
		FRDGBufferUAVRef QuadBufferUAV;
		FRDGBufferSRVRef QuadBufferSRV;

		FRDGBufferRef FeedbackBuffer;
		FRDGBufferUAVRef FeedbackBufferUAV;

		FRDGBufferRef IndirectArgsBuffer;
		FRDGBufferUAVRef IndirectArgsBufferUAV;
		FRDGBufferSRVRef IndirectArgsBufferSRV;

		FRDGTextureRef LodTexture;

		FRDGBufferRef QuadNeighborBuffer;
		FRDGBufferUAVRef QuadNeighborBufferUAV;
		FRDGBufferSRVRef QuadNeighborBufferSRV;
	};

	/** Structure to carry the RDG wrapping for our output FDrawInstanceBuffers. */
	struct FOutputResources
	{
		FRDGBufferRef InstanceBuffer;
		FRDGBufferUAVRef InstanceBufferUAV;
		FRDGBufferSRVRef InstanceBufferSRV;

		FRDGBufferRef IndirectArgsBuffer;
		FRDGBufferUAVRef IndirectArgsBufferUAV;
	};

	/* Dummy parameter struct used to allocate FPooledRDGBuffer objects using a fake RDG pass. */
	BEGIN_SHADER_PARAMETER_STRUCT(FCreateBufferParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UPLOAD(InstanceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UPLOAD(IndirectArgsBuffer)
	END_SHADER_PARAMETER_STRUCT()

	void InitializeInstanceBuffers(FRHICommandListImmediate& InRHICmdList, FDrawInstanceBuffers& InBuffers)
	{
		// We use a fake RDG pass for allocation. Is there a more direct way to do this for PooledRDGBuffer objects?
		// An alternative is use standard RHI allocation, but then we need to be manage resource transitions.
		FRDGBuilder GraphBuilder(InRHICmdList);

		int32 InstanceBufferSize = CVarVHMMaxRenderItems.GetValueOnRenderThread();
		FRDGBufferRef InstanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(VirtualHeightfieldMesh::QuadRenderInstance), InstanceBufferSize), TEXT("InstanceBuffer"));
		FRDGBufferRef IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(5), TEXT("IndirectArgsBuffer"));

		FCreateBufferParameters* Parameters = GraphBuilder.AllocParameters<FCreateBufferParameters>();
		Parameters->InstanceBuffer = InstanceBuffer;
		Parameters->IndirectArgsBuffer = IndirectArgsBuffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CreateBuffers"),
			Parameters,
			ERDGPassFlags::Copy,
			[InstanceBuffer, IndirectArgsBuffer](FRHICommandList& RHICmdList)
			{
				//InstanceBuffer->MarkResourceAsUsed();
				//IndirectArgsBuffer->MarkResourceAsUsed();
			});

		GraphBuilder.QueueBufferExtraction(InstanceBuffer, &InBuffers.InstanceBuffer, ERHIAccess::UAVCompute);
		GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer, &InBuffers.IndirectArgsBuffer, ERHIAccess::UAVCompute);

		GraphBuilder.Execute();

		// The SRV objects referenced by final rendering are managed outside of RDG.
		InBuffers.InstanceBufferSRV = RHICreateShaderResourceView(InBuffers.InstanceBuffer->GetStructuredBufferRHI());
	}

	/** Initialize the volatie resources used in the render graph. */
	void InitializeResources(FRDGBuilder& GraphBuilder, FProxyDesc const& InDesc, FMainViewDesc const& InMainViewDesc, FVolatileResources& OutResources)
	{
		OutResources.QueueInfo = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(WorkerQueueInfo), 1), TEXT("QueueInfo"));
		OutResources.QueueInfoUAV = GraphBuilder.CreateUAV(OutResources.QueueInfo);
		OutResources.QueueBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InDesc.MaxPersistentQueueItems), TEXT("QuadQueue"));
		OutResources.QueueBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutResources.QueueBuffer, PF_R32_UINT));

		OutResources.QuadBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, InDesc.MaxRenderItems), TEXT("QuadBuffer"));
		OutResources.QuadBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutResources.QuadBuffer, PF_R32G32_UINT));
		OutResources.QuadBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OutResources.QuadBuffer, PF_R32G32_UINT));

		FRDGBufferDesc FeedbackBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InDesc.MaxFeedbackItems + 1);
		FeedbackBufferDesc.Usage = EBufferUsageFlags(FeedbackBufferDesc.Usage | BUF_SourceCopy);
		OutResources.FeedbackBuffer = GraphBuilder.CreateBuffer(FeedbackBufferDesc, TEXT("FeedbackBuffer"));
		OutResources.FeedbackBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutResources.FeedbackBuffer, PF_R32_UINT));

		OutResources.IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsByteSize), TEXT("IndirectArgsBuffer"));
		OutResources.IndirectArgsBufferUAV = GraphBuilder.CreateUAV(OutResources.IndirectArgsBuffer);
		OutResources.IndirectArgsBufferSRV = GraphBuilder.CreateSRV(OutResources.IndirectArgsBuffer);

		FRDGTextureDesc LodTextureDesc = FRDGTextureDesc::Create2D(
			FIntPoint(InDesc.PageTableSize.X, InDesc.PageTableSize.Y),
			PF_R8G8,
			FClearValueBinding::None,
			TexCreate_RenderTargetable | TexCreate_ShaderResource);
		OutResources.LodTexture = GraphBuilder.CreateTexture(LodTextureDesc, TEXT("LodTexture"));

		OutResources.QuadNeighborBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InDesc.MaxRenderItems * 4), TEXT("QuadNeighborBuffer"));
		OutResources.QuadNeighborBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutResources.QuadNeighborBuffer, PF_R32_UINT));
		OutResources.QuadNeighborBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OutResources.QuadNeighborBuffer, PF_R32_UINT));
	}

	/** Initialize the output resources used in the render graph. */
	void InitializeResources(FRDGBuilder& GraphBuilder, FDrawInstanceBuffers const& InBuffers, FOutputResources& OutResources)
	{
		OutResources.InstanceBuffer = GraphBuilder.RegisterExternalBuffer(InBuffers.InstanceBuffer);
		OutResources.InstanceBufferUAV = GraphBuilder.CreateUAV(OutResources.InstanceBuffer);
		OutResources.InstanceBufferSRV = GraphBuilder.CreateSRV(OutResources.InstanceBuffer);

		OutResources.IndirectArgsBuffer = GraphBuilder.RegisterExternalBuffer(InBuffers.IndirectArgsBuffer);
		OutResources.IndirectArgsBufferUAV = GraphBuilder.CreateUAV(OutResources.IndirectArgsBuffer);
	}

	/** */
	void AddPass_InitBuffers(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources)
	{
		TShaderMapRef<FInitBuffersCS> ComputeShader(InGlobalShaderMap);

		FInitBuffersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitBuffersCS::FParameters>();
		PassParameters->MaxLevel = InDesc.MaxLevel;
		PassParameters->RWQueueInfo = InVolatileResources.QueueInfoUAV;
		PassParameters->RWQueueBuffer = InVolatileResources.QueueBufferUAV;
		PassParameters->RWQuadBuffer = InVolatileResources.QuadBufferUAV;
		PassParameters->RWIndirectArgsBuffer = InVolatileResources.IndirectArgsBufferUAV;
		PassParameters->RWFeedbackBuffer = InVolatileResources.FeedbackBufferUAV;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("InitBuffers"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader](FRHICommandList& RHICmdList)
			{
				//todo: If feedback parsing understands append counter we don't need to fully clear
				RHICmdList.ClearUAVUint(PassParameters->RWFeedbackBuffer->GetRHI(), FUintVector4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff));
				RHICmdList.Transition(FRHITransitionInfo(PassParameters->RWFeedbackBuffer->GetRHI(), ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));

				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, FIntVector(1, 1, 1));
			});
	}

	/** */
	void AddPass_CollectQuads(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FMainViewDesc const& InViewDesc)
	{
		TShaderMapRef<FCollectQuadsCS> ComputeShader(InGlobalShaderMap);

		FCollectQuadsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCollectQuadsCS::FParameters>();
		PassParameters->MinMaxTexture = InDesc.MinMaxTexture;
		PassParameters->MinMaxTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->MinMaxLevelOffset = InDesc.MinMaxLevelOffset;
		PassParameters->OcclusionTexture = InViewDesc.OcclusionTexture;
		PassParameters->OcclusionLevelOffset = InViewDesc.OcclusionLevelOffset;
		PassParameters->PageTableTexture = InDesc.PageTableTexture;
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->UVToWorld = InDesc.UVToWorld;
		PassParameters->UVToWorldScale = InDesc.UVToWorldScale;
		PassParameters->ViewOrigin = InViewDesc.ViewOrigin;
		PassParameters->LodDistances = InViewDesc.LodDistances;
		for (int32 PlaneIndex = 0; PlaneIndex < 5; ++PlaneIndex)
		{
			PassParameters->FrustumPlanes[PlaneIndex] = InViewDesc.Planes[PlaneIndex];
		}
		PassParameters->QueueBufferSizeMask = InDesc.MaxPersistentQueueItems - 1; // Assumes MaxPersistentQueueItems is a power of 2 so that we can wrap with a mask.
		PassParameters->RWQueueInfo = InVolatileResources.QueueInfoUAV;
		PassParameters->RWQueueBuffer = InVolatileResources.QueueBufferUAV;
		PassParameters->RWQuadBuffer = InVolatileResources.QuadBufferUAV;
		PassParameters->RWIndirectArgsBuffer = InVolatileResources.IndirectArgsBufferUAV;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CollectQuads"),
			ComputeShader, PassParameters, FIntVector(InDesc.NumCollectPassWavefronts, 1, 1));
	}

	/**  */
	void AddPass_RenderLodMap(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources)
	{
		TShaderMapRef< FRenderLodMapVS > VertexShader(InGlobalShaderMap);
		TShaderMapRef< FRenderLodMapPS > PixelShader(InGlobalShaderMap);

		FRenderLodMap::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderLodMap::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(InVolatileResources.LodTexture, ERenderTargetLoadAction::ENoAction);
		PassParameters->QuadBuffer = InVolatileResources.QuadBufferSRV;
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RenderLodMap"),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, PassParameters, IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer](FRHICommandListImmediate& RHICmdListImmediate)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdListImmediate.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdListImmediate, GraphicsPSOInit);

				SetShaderParameters(RHICmdListImmediate, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
				SetShaderParameters(RHICmdListImmediate, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				IndirectArgsBuffer->MarkResourceAsUsed();

				int32 IndirectArgOffset = VirtualHeightfieldMesh::IndirectArgsByteOffset_RenderLodMap;
				RHICmdListImmediate.DrawIndexedPrimitiveIndirect(GTwoTrianglesIndexBuffer.IndexBufferRHI, IndirectArgsBuffer->GetIndirectRHICallBuffer(), IndirectArgOffset);
			});
	}

	/** */
	void AddPass_ResolveNeighborLods(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FMainViewDesc const& InViewDesc)
	{
		TShaderMapRef<FResolveNeighborLodsCS> ComputeShader(InGlobalShaderMap);

		FResolveNeighborLodsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FResolveNeighborLodsCS::FParameters>();
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->PageTableFeedbackId = InDesc.PageTableFeedbackId;
		PassParameters->PageTableTexture = InDesc.PageTableTexture;
		PassParameters->QuadBuffer = InVolatileResources.QuadBufferSRV;
		PassParameters->LodTexture = InVolatileResources.LodTexture;
		PassParameters->IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer;
		PassParameters->IndirectArgsBufferSRV = InVolatileResources.IndirectArgsBufferSRV;
		PassParameters->RWQuadNeighborBuffer = InVolatileResources.QuadNeighborBufferUAV;
		PassParameters->RWFeedbackBuffer = InVolatileResources.FeedbackBufferUAV;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ResolveNeighborLods"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader, IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer](FRHICommandList& RHICmdList)
			{
				IndirectArgsBuffer->MarkResourceAsUsed();
				int32 IndirectArgOffset = VirtualHeightfieldMesh::IndirectArgsByteOffset_FetchNeighborLod;
				FComputeShaderUtils::DispatchIndirect(RHICmdList, ComputeShader, *PassParameters, IndirectArgsBuffer->GetIndirectRHICallBuffer(), IndirectArgOffset);
			});
	}

	/** */
	void AddPass_InitInstanceBuffer(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FOutputResources& InOutputResources)
	{
		TShaderMapRef<FInitInstanceBufferCS> ComputeShader(InGlobalShaderMap);

		FInitInstanceBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitInstanceBufferCS::FParameters>();
		PassParameters->NumIndices = InDesc.NumQuadsPerTileSide * InDesc.NumQuadsPerTileSide * 6;
		PassParameters->RWIndirectArgsBuffer = InOutputResources.IndirectArgsBufferUAV;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitInstanceBuffer"),
			ComputeShader, PassParameters, FIntVector(1, 1, 1));
	}

	/** */
	void AddPass_CullInstances(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FOutputResources& InOutputResources, FChildViewDesc const& InViewDesc)
	{
		FCullInstances::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullInstances::FParameters>();
		PassParameters->MinMaxTexture = InDesc.MinMaxTexture;
		PassParameters->MinMaxTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->PageTableTexture = InDesc.PageTableTexture;
		PassParameters->PageTableSize = InDesc.PageTableSize;
		for (int32 PlaneIndex = 0; PlaneIndex < 5; ++PlaneIndex)
		{
			PassParameters->FrustumPlanes[PlaneIndex] = InViewDesc.Planes[PlaneIndex];
		}
		PassParameters->PhysicalPageTransform = InDesc.PhysicalPageTransform;
		PassParameters->NumPhysicalAddressBits = InDesc.NumPhysicalAddressBits;
		PassParameters->QuadBuffer = InVolatileResources.QuadBufferSRV;
		PassParameters->QuadNeighborBuffer = InVolatileResources.QuadNeighborBufferSRV;
		PassParameters->IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer;
		PassParameters->IndirectArgsBufferSRV = InVolatileResources.IndirectArgsBufferSRV;
		PassParameters->RWInstanceBuffer = InOutputResources.InstanceBufferUAV;
		PassParameters->RWIndirectArgsBuffer = InOutputResources.IndirectArgsBufferUAV;

		TShaderRef<FCullInstances> ComputeShader;
		if (InViewDesc.bIsMainView)
		{
			ComputeShader = InGlobalShaderMap->GetShader< TCullInstancesCS<true> >();
		}
		else
		{
			ComputeShader = InGlobalShaderMap->GetShader< TCullInstancesCS<false> >();
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CullInstances"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader, IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer](FRHICommandList& RHICmdList)
		{
			IndirectArgsBuffer->MarkResourceAsUsed();
			int32 IndirectArgOffset = VirtualHeightfieldMesh::IndirectArgsByteOffset_FinalCull;
			FComputeShaderUtils::DispatchIndirect(RHICmdList, ComputeShader, *PassParameters, IndirectArgsBuffer->GetIndirectRHICallBuffer(), IndirectArgOffset);
		});
	}

	/** */
	void GPUCullMainView( FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FMainViewDesc const& InViewDesc)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "MainView");

		AddPass_InitBuffers(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources);
		AddPass_CollectQuads(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InViewDesc);
		AddPass_RenderLodMap(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources);
		AddPass_ResolveNeighborLods(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InViewDesc);
	}

	/** */
	void GPUCullChildView(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FOutputResources& InOutputResources, FChildViewDesc const& InViewDesc)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "CullView");

		AddPass_InitInstanceBuffer(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InOutputResources);
		AddPass_CullInstances(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InOutputResources, InViewDesc);
	}
}

void FVirtualHeightfieldMeshRendererExtension::SubmitWork(FRHICommandListImmediate& InRHICmdList)
{
	SCOPED_DRAW_EVENT(InRHICmdList, VirtualHeightfieldMesh);

	// Collect feedback buffers from each pass to submit together after RenderGraph execution.
	// todo: Convert feedback submission to RDG so that it can be included in the render graph. 
	//       Then the render graph builder can be passed in and executed externally.
	TArray< TRefCountPtr< FRDGPooledBuffer > > FeedbackBuffers;

	FRDGBuilder GraphBuilder(InRHICmdList);
	{
		// Sort work so that we can batch by proxy/view
		WorkDescs.Sort(FWorkDescSort());

		const int32 NumWorkItems = WorkDescs.Num();
		int32 WorkIndex = 0;
		while (WorkIndex < NumWorkItems)
		{
			// Gather data per proxy
			FVirtualHeightfieldMeshSceneProxy const* Proxy = SceneProxies[WorkDescs[WorkIndex].ProxyIndex];
			IAllocatedVirtualTexture* AllocatedVirtualTexture = Proxy->AllocatedVirtualTexture;
			
			const float PageSize = AllocatedVirtualTexture->GetVirtualTileSize();
			const float PageBorderSize = AllocatedVirtualTexture->GetTileBorderSize();
			const float PageAndBorderSize = PageSize + PageBorderSize * 2.f;
			const float HalfTexelSize = 0.5f;
			const float PhysicalTextureSize = AllocatedVirtualTexture->GetPhysicalTextureSize(0);
			const FVector4 PhysicalPageTransform = FVector4(PageAndBorderSize, PageSize, PageBorderSize, HalfTexelSize) * (1.f / PhysicalTextureSize);

			const float PageTableSizeX = AllocatedVirtualTexture->GetWidthInTiles();
			const float PageTableSizeY = AllocatedVirtualTexture->GetHeightInTiles();
			const FVector4 PageTableSize = FVector4(PageTableSizeX, PageTableSizeY, 1.f / PageTableSizeX, 1.f / PageTableSizeY);

			VirtualHeightfieldMesh::FProxyDesc ProxyDesc;
			ProxyDesc.PageTableTexture = AllocatedVirtualTexture->GetPageTableTexture(0);
			ProxyDesc.MinMaxTexture = Proxy->MinMaxTexture ? Proxy->MinMaxTexture->Resource->TextureRHI : VirtualHeightfieldMesh::GMinMaxDefaultTexture->TextureRHI;
			ProxyDesc.MinMaxLevelOffset = ProxyDesc.MinMaxTexture->GetNumMips() - 1 - AllocatedVirtualTexture->GetMaxLevel();
			ProxyDesc.MaxLevel = AllocatedVirtualTexture->GetMaxLevel();
			ProxyDesc.PageTableSize = PageTableSize;
			ProxyDesc.PhysicalPageTransform = PhysicalPageTransform;
			ProxyDesc.NumPhysicalAddressBits = AllocatedVirtualTexture->GetPageTableFormat() == EVTPageTableFormat::UInt16 ? 6 : 8; // See packing in PageTableUpdate.usf
			ProxyDesc.PageTableFeedbackId = AllocatedVirtualTexture->GetSpaceID() << 28;
			ProxyDesc.UVToWorld = Proxy->UVToWorld;
			ProxyDesc.UVToWorldScale = Proxy->UVToWorldScale;
			ProxyDesc.NumQuadsPerTileSide = Proxy->NumQuadsPerTileSide;
			ProxyDesc.MaxPersistentQueueItems = 1 << FMath::CeilLogTwo(CVarVHMMaxPersistentQueueItems.GetValueOnRenderThread());
			ProxyDesc.MaxRenderItems = CVarVHMMaxRenderItems.GetValueOnRenderThread();
			ProxyDesc.MaxFeedbackItems = CVarVHMMaxFeedbackItems.GetValueOnRenderThread();
			ProxyDesc.NumCollectPassWavefronts = CVarVHMCollectPassWavefronts.GetValueOnRenderThread();

			while (WorkIndex < NumWorkItems && SceneProxies[WorkDescs[WorkIndex].ProxyIndex] == Proxy)
			{
				// Gather data per main view
				FSceneView const* MainView = MainViews[WorkDescs[WorkIndex].MainViewIndex];
				
				VirtualHeightfieldMesh::FViewData MainViewData;
				VirtualHeightfieldMesh::GetViewData(MainView, MainViewData);

				VirtualHeightfieldMesh::FMainViewDesc MainViewDesc;
				MainViewDesc.ViewDebug = MainView;

				// ViewOrigin and Frustum Planes are all converted to UV space for the shader.
				MainViewDesc.ViewOrigin = Proxy->WorldToUV.TransformPosition(MainViewData.ViewOrigin);
				MainViewDesc.LodDistances = VirtualHeightfieldMesh::CalculateLodRanges(MainView, Proxy);

				const int32 MainViewNumPlanes = FMath::Min(MainViewData.ViewFrustum.Planes.Num(), 5);
				for (int32 PlaneIndex = 0; PlaneIndex < MainViewNumPlanes; ++PlaneIndex)
				{
					FPlane Plane = MainViewData.ViewFrustum.Planes[PlaneIndex];
					Plane = VirtualHeightfieldMesh::TransformPlane(Plane, Proxy->WorldToUV, Proxy->WorldToUVTransposeAdjoint);
					MainViewDesc.Planes[PlaneIndex] = VirtualHeightfieldMesh::ConvertPlane(Plane);
				}
				for (int32 PlaneIndex = MainViewNumPlanes; PlaneIndex < 5; ++PlaneIndex)
				{
					MainViewDesc.Planes[PlaneIndex] = FPlane(0, 0, 0, 1); // Null plane won't cull anything
				}

				FOcclusionResults* OcclusionResults = GOcclusionResults.Find(FOcclusionResultsKey(Proxy, MainView));
				if (OcclusionResults == nullptr)
				{
					MainViewDesc.OcclusionTexture = GBlackTexture->TextureRHI;
					MainViewDesc.OcclusionLevelOffset = (int32)ProxyDesc.MaxLevel;
				}
				else
				{
					MainViewDesc.OcclusionTexture = OcclusionResults->OcclusionTexture;
					MainViewDesc.OcclusionLevelOffset = (int32)ProxyDesc.MaxLevel - OcclusionResults->NumTextureMips + 1;
				}

				// Build volatile graph resources
				VirtualHeightfieldMesh::FVolatileResources VolatileResources;
				VirtualHeightfieldMesh::InitializeResources(GraphBuilder, ProxyDesc, MainViewDesc, VolatileResources);

				// Build graph
				VirtualHeightfieldMesh::GPUCullMainView(GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel), ProxyDesc, VolatileResources, MainViewDesc);

				// Tag feedback buffer for extraction
				FeedbackBuffers.AddDefaulted();
				GraphBuilder.QueueBufferExtraction(VolatileResources.FeedbackBuffer, &FeedbackBuffers.Last(), ERHIAccess::SRVMask);

				while (WorkIndex < NumWorkItems && MainViews[WorkDescs[WorkIndex].MainViewIndex] == MainView)
				{
					// Gather data per child view
					FSceneView const* CullView = CullViews[WorkDescs[WorkIndex].CullViewIndex];
					FConvexVolume const* ShadowFrustum = CullView->GetDynamicMeshElementsShadowCullFrustum();
					FConvexVolume const& Frustum = ShadowFrustum != nullptr && ShadowFrustum->Planes.Num() > 0 ? *ShadowFrustum : CullView->ViewFrustum;
					const FVector PreShadowTranslation = ShadowFrustum != nullptr ? CullView->GetPreShadowTranslation() : FVector::ZeroVector;

					VirtualHeightfieldMesh::FChildViewDesc ChildViewDesc;
					ChildViewDesc.ViewDebug = MainView;
					ChildViewDesc.bIsMainView = CullView == MainView;
					
					const int32 ChildViewNumPlanes = FMath::Min(Frustum.Planes.Num(), 5);
					for (int32 PlaneIndex = 0; PlaneIndex < ChildViewNumPlanes; ++PlaneIndex)
					{
						FPlane Plane = Frustum.Planes[PlaneIndex];
						Plane = VirtualHeightfieldMesh::TranslatePlane(Plane, PreShadowTranslation);
						Plane = VirtualHeightfieldMesh::TransformPlane(Plane, Proxy->WorldToUV, Proxy->WorldToUVTransposeAdjoint);
						ChildViewDesc.Planes[PlaneIndex] = VirtualHeightfieldMesh::ConvertPlane(Plane);
					}
					for (int32 PlaneIndex = ChildViewNumPlanes; PlaneIndex < 5; ++PlaneIndex)
					{
						MainViewDesc.Planes[PlaneIndex] = FPlane(0, 0, 0, 1); // Null plane won't cull anything
					}

					// Build output graph resources
					VirtualHeightfieldMesh::FDrawInstanceBuffers& InstanceBuffers = Buffers[WorkDescs[WorkIndex].BufferIndex];
					VirtualHeightfieldMesh::FOutputResources OutputResources;
					VirtualHeightfieldMesh::InitializeResources(GraphBuilder, InstanceBuffers, OutputResources);

					// Build graph
					VirtualHeightfieldMesh::GPUCullChildView(GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel), ProxyDesc, VolatileResources, OutputResources, ChildViewDesc);

					WorkIndex++;
				}
			}
		}
	}

	GraphBuilder.Execute();

	// Subit feedback buffers
	{
		SCOPED_DRAW_EVENT(InRHICmdList, CopyVirtualTextureFeedback);
		for (int32 FeedbackIndex = 0; FeedbackIndex < FeedbackBuffers.Num(); ++FeedbackIndex)
		{
			FVirtualTextureFeedbackBufferDesc Desc;
			Desc.Init(CVarVHMMaxFeedbackItems.GetValueOnRenderThread() + 1);
			SubmitVirtualTextureFeedbackBuffer(GraphBuilder.RHICmdList, FeedbackBuffers[0].GetReference()->GetVertexBufferRHI(), Desc);
		}
	}
}
