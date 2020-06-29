// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshSceneProxy.h"

#include "CommonRenderResources.h"
#include "EngineModule.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "VirtualHeightfieldMeshComponent.h"
#include "VirtualHeightfieldMeshVertexFactory.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/VirtualTextureFeedbackBuffer.h"

//#include "ShaderPrintParameters.h"
//#include "GpuDebugRendering.h"

PRAGMA_DISABLE_OPTIMIZATION

DECLARE_STATS_GROUP(TEXT("Virtual Heightfield Mesh"), STATGROUP_VirtualHeightfieldMesh, STATCAT_Advanced);

DECLARE_LOG_CATEGORY_EXTERN(LogVirtualHeightfieldMesh, Warning, All);
DEFINE_LOG_CATEGORY(LogVirtualHeightfieldMesh);

static TAutoConsoleVariable<float> CVarVHMLodScale(
	TEXT("r.VHM.LodScale"),
	1.f,
	TEXT("Global LOD scale applied for Virtual Heightfield Mesh."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMFixCullingCamera(
	TEXT("r.VHM.FixCullingCamera"),
	0,
	TEXT("Disable update of Virtual Heightfield Mesh culling camera."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVHMOcclusion(
	TEXT("r.VHM.Occlusion"),
	1,
	TEXT("Enable occlusion queries."),
	ECVF_RenderThreadSafe
);

namespace VirtualHeightfieldMesh
{
	/** Buffers filled by GPU culling used by the Virtual Heightfield Mesh final draw call. */
	struct FDrawInstanceBuffers
	{
		/* Culled instance buffer. */
		TRefCountPtr<FPooledRDGBuffer> InstanceBuffer;
		FShaderResourceViewRHIRef InstanceBufferSRV;

		/* IndirectArgs buffer for final DrawInstancedIndirect. */
		TRefCountPtr<FPooledRDGBuffer> IndirectArgsBuffer;
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

/** Renderer extension to manage the buffer pool and add hooks for GPU culling passes. */
class FVirtualHeightfieldMeshRendererExtension : public IPersistentViewUniformBufferExtension
{
public:
	FVirtualHeightfieldMeshRendererExtension()
		: DiscardId(0)
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
	//~ Begin IPersistentViewUniformBufferExtension Interface
	virtual void BeginFrame() override;
	virtual void EndFrame() override;
	//~ End IPersistentViewUniformBufferExtension Interface

private:
	/* Buffers to fill. Resources can persist between frames to reduce allocation cost, but contents don't persist. */
	TArray<VirtualHeightfieldMesh::FDrawInstanceBuffers> Buffers;
	/* Per buffer frame time stamp of last usage. */
	TArray<uint32> DiscardIds;
	/* Current frame time stamp. */
	uint32 DiscardId;

	/* Arrary of uniqe scene proxies to render this frame. */
	TArray<FVirtualHeightfieldMeshSceneProxy const*> SceneProxies;
	/* Arrary of unique main views to render this frame. */
	TArray<FSceneView const*> MainViews;
	/* Arrary of unique culling views to render this frame. */
	TArray<FSceneView const*> CullViews;

	/** Key for each buffer we need to generate. */
	struct FWorkDesc
	{
		int32 ProxyIndex;
		int32 MainViewIndex;
		int32 CullViewIndex;
		int32 BufferIndex;
	};

	/* Keys specifying what to render. */
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

/** Single global instance of the VirtualHeightfieldMesh extension. */
FVirtualHeightfieldMeshRendererExtension GVirtualHeightfieldMeshViewUniformBufferExtension;

void FVirtualHeightfieldMeshRendererExtension::RegisterExtension()
{
	static bool bInit = false;
	if (!bInit)
	{
		GetRendererModule().RegisterPersistentViewUniformBufferExtension(this);
		bInit = true;
	}
}

VirtualHeightfieldMesh::FDrawInstanceBuffers& FVirtualHeightfieldMeshRendererExtension::AddWork(FVirtualHeightfieldMeshSceneProxy const* InProxy, FSceneView const* InMainView, FSceneView const* InCullView)
{
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
	if (WorkDescs.Num() > 0)
	{
		SubmitWork(GetImmediateCommandList_ForRenderCommand());
	}
}

void FVirtualHeightfieldMeshRendererExtension::EndFrame()
{
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
	, RuntimeVirtualTexture(InComponent->RuntimeVirtualTexture)
	, MinMaxTexture(InComponent->MinMaxTexture)
	, AllocatedVirtualTexture(nullptr)
	, bCallbackRegistered(false)
	, NumQuadsPerTileSide(0)
	, VertexFactory(nullptr)
	, LODScale(InComponent->LODDistanceScale)
	, OcclusionData(InComponent->GetOcclusionData())
	, NumOcclusionLods(InComponent->GetNumOcclusionLods())
	, OcclusionGridSize(0, 0)
{
	GVirtualHeightfieldMeshViewUniformBufferExtension.RegisterExtension();

	const bool bValidMaterial = InComponent->Material != nullptr && InComponent->Material->CheckMaterialUsage_Concurrent(MATUSAGE_VirtualHeightfieldMesh);
	Material = bValidMaterial ? InComponent->Material->GetRenderProxy() : UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();

	UVToWorld = GetLocalToWorld();
	WorldToUV = UVToWorld.Inverse();
	WorldToUVTransposeAdjoint = WorldToUV.TransposeAdjoint();

	BuildOcclusionVolumes();
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
	UVToWorld = GetLocalToWorld();
	WorldToUV = UVToWorld.Inverse();
	WorldToUVTransposeAdjoint = WorldToUV.TransposeAdjoint();

	BuildOcclusionVolumes();
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

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && bValid;
	Result.bShadowRelevance = IsShadowCast(View) && bValid && ShouldRenderInMainPass();
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
			VirtualHeightfieldMesh::FDrawInstanceBuffers& Buffers = GVirtualHeightfieldMeshViewUniformBufferExtension.AddWork(this, ViewFamily.Views[0], Views[ViewIndex]);

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
				BatchElement.IndirectArgsBuffer = Buffers.IndirectArgsBuffer->VertexBuffer;
				BatchElement.IndirectArgsOffset = 0;

				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = 0;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = 0;

				FVirtualHeightfieldMeshUserData* UserData = &Collector.AllocateOneFrameResource<FVirtualHeightfieldMeshUserData>();
				UserData->InstanceBufferSRV = Buffers.InstanceBufferSRV;
				UserData->HeightPhysicalTexture = AllocatedVirtualTexture->GetPhysicalTexture(0);
				UserData->PageTableSize = FIntPoint(AllocatedVirtualTexture->GetWidthInTiles(), AllocatedVirtualTexture->GetHeightInTiles());
				UserData->LocalToWorld = GetLocalToWorld();
				BatchElement.UserData = (void*)UserData;

				//todo: use primitive buffer
				BatchElement.PrimitiveIdMode = PrimID_ForceZero;
				BatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
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

void FVirtualHeightfieldMeshSceneProxy::BuildOcclusionVolumes()
{
	OcclusionVolumes.Reset();
	if (NumOcclusionLods > 0)
	{
		int32 SizeX = 1 << (NumOcclusionLods - 1);
		int32 SizeY = 1 << (NumOcclusionLods - 1);
		OcclusionGridSize = FIntPoint(SizeX, SizeY);

		OcclusionVolumes.Reserve(OcclusionData.Num());

		const FMatrix Transform = GetLocalToWorld();

		int32 OcclusionDataIndex = 0;
		for (int32 LodIndex = 0; LodIndex < NumOcclusionLods; ++LodIndex)
		{
			for (int Y = 0; Y < SizeY; ++Y)
			{
				for (int X = 0; X < SizeX; ++X)
				{
					FVector2D MinMaxU = FVector2D((float)X / (float)SizeX, (float)(X + 1) / (float)SizeX);
					FVector2D MinMaxV = FVector2D((float)Y / (float)SizeY, (float)(Y + 1) / (float)SizeY);
					FVector2D MinMaxZ = OcclusionData[OcclusionDataIndex++];

					FVector Pos[8];
					Pos[0] = Transform.TransformPosition(FVector(MinMaxU.X, MinMaxV.X, MinMaxZ.X));
					Pos[1] = Transform.TransformPosition(FVector(MinMaxU.Y, MinMaxV.X, MinMaxZ.X));
					Pos[2] = Transform.TransformPosition(FVector(MinMaxU.X, MinMaxV.Y, MinMaxZ.X));
					Pos[3] = Transform.TransformPosition(FVector(MinMaxU.Y, MinMaxV.Y, MinMaxZ.X));
					Pos[4] = Transform.TransformPosition(FVector(MinMaxU.X, MinMaxV.X, MinMaxZ.Y));
					Pos[5] = Transform.TransformPosition(FVector(MinMaxU.Y, MinMaxV.X, MinMaxZ.Y));
					Pos[6] = Transform.TransformPosition(FVector(MinMaxU.X, MinMaxV.Y, MinMaxZ.Y));
					Pos[7] = Transform.TransformPosition(FVector(MinMaxU.Y, MinMaxV.Y, MinMaxZ.Y));

					const float ExpandOcclusion = 3.f;
					OcclusionVolumes.Add(FBoxSphereBounds(FBox(Pos, 8).ExpandBy(ExpandOcclusion)));
				}
			}

			SizeX = FMath::Max(SizeX / 2, 1);
			SizeY = FMath::Max(SizeY / 2, 1);
		}
	}

	// Setup a default occlusion volume array containing just the primitive bounds.
	// We use this if disabling the full set of occlusion volumes.
	DefaultOcclusionVolumes.Reset();
	DefaultOcclusionVolumes.Add(GetBounds());
}

void FVirtualHeightfieldMeshSceneProxy::AcceptOcclusionResults(FSceneView const* View, TArray<bool>* Results, int32 ResultsStart, int32 NumResults)
{
	check(IsInRenderingThread());

	if (CVarVHMOcclusion.GetValueOnAnyThread() != 0 && Results != nullptr && NumResults > 0)
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
	// todo: make these console configurable (or even track per frame and make dynamic)
	static const int32 MaxRenderItems = 1024 * 4;
	static const int32 MaxPersistentQueueItems = 1024 * 4;
	static const int32 MaxFeedbackItems = 1024 * 4;

	/* Keep indirect args offsets in sync with VirtualHeightfieldMesh.usf. */
	static const int32 IndirectArgsByteOffset_RenderLodMap = 0;
	static const int32 IndirectArgsByteOffset_FetchNeighborLod = 5 * sizeof(uint32);
	static const int32 IndirectArgsByteOffset_FinalCull = 9 * sizeof(uint32);
	static const int32 IndirectArgsByteSize = 13 * sizeof(uint32);

	/** Shader structure used for tracking work queues in persistent wave style shaders. Keep in sync with VirtualHeightfieldMesh.ush. */
	struct WorkerQueueInfo
	{
		uint32 Read;
		uint32 Write;
		int32 NumActive;
	};

	/** Item description used when traversing the virtual page table quad tree. Keep in sync with VirtualHeightfieldMesh.ush. */
	struct QuadItem
	{
		uint32 Address;
		uint32 Level;
	};

	/** Description for items that are kept by the quad tree traversal stage. Keep in sync with VirtualHeightfieldMesh.ush. */
	struct QuadRenderItem
	{
		uint32 Address;
		uint32 Level;
		FVector LocalToPhysicalUV;
		float Lod;
	};

	/** Description of neighbor items. We fill 4 of these for each QuadRenderItem. Keep in sync with VirtualHeightfieldMesh.ush. */
	struct QuadNeighborItem
	{
		FVector LocalToPhysicalUV;
		float Lod;
	};

	/** Final render instance description used by the DrawInstancedIndirect(). Keep in sync with VirtualHeightfieldMesh.ush. */
	struct QuadRenderInstance
	{
		QuadRenderItem Quad;
		QuadNeighborItem Neighbor[4];
	};

	/** Compute shader to initialize all buffers, including adding the lowest mip page(s) to the QuadBuffer. */
	class FInitBuffersCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FInitBuffersCS);
		SHADER_USE_PARAMETER_STRUCT(FInitBuffersCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector, PageTableSize)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadRenderItem>, RWQuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWFeedbackBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<WorkerQueueInfo>, RWQueueInfo)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadItem>, RWQueueBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
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
			//SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
			//SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
			SHADER_PARAMETER_TEXTURE(Texture2D, MinMaxTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, MinMaxTextureSampler)
			SHADER_PARAMETER_TEXTURE(Texture2D<float>, OcclusionTexture)
			SHADER_PARAMETER(int32, OcclusionLevelOffset)
			SHADER_PARAMETER_TEXTURE(Texture2D<uint>, PageTableTexture)
			SHADER_PARAMETER(FVector, PageTableSize)
			SHADER_PARAMETER(uint32, PageTableFeedbackId)
			SHADER_PARAMETER(FMatrix, UVToWorld)
			SHADER_PARAMETER(FVector, CameraPosition)
			SHADER_PARAMETER(float, ProjectionScale)
			SHADER_PARAMETER_ARRAY(FVector4, FrustumPlanes, [5])
			SHADER_PARAMETER(float, LodThreshold)
			SHADER_PARAMETER(FUintVector4, PageTablePackedUniform)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<WorkerQueueInfo>, RWQueueInfo)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadItem>, RWQueueBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadRenderItem>, RWQuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWFeedbackBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FCollectQuadsCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "CollectQuadsCS", SF_Compute);

	/** Compute shader to build indirect args buffer used by subsequent LOD and culling steps. */
	class FBuildIndirectArgsForLodAndCullCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FBuildIndirectArgsForLodAndCullCS);
		SHADER_USE_PARAMETER_STRUCT(FBuildIndirectArgsForLodAndCullCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<QuadRenderItem>, QuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FBuildIndirectArgsForLodAndCullCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "BuildIndirectArgsForLodAndCullCS", SF_Compute);

	/** Shader that draws to a render target the Lod info for the quads output by the Collect pass. */
	class FRenderLodMap : public FGlobalShader
	{
	public:
		SHADER_USE_PARAMETER_STRUCT(FRenderLodMap, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			RENDER_TARGET_BINDING_SLOTS()
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<QuadRenderItem>, QuadBuffer)
			SHADER_PARAMETER(FVector, PageTableSize)
			SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return true;
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
	class FReadNeighborLodsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FReadNeighborLodsCS);
		SHADER_USE_PARAMETER_STRUCT(FReadNeighborLodsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			//SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
			SHADER_PARAMETER(FVector, PageTableSize)
			SHADER_PARAMETER_TEXTURE(Texture2D, PageTableTexture)
			SHADER_PARAMETER(FUintVector4, PageTablePackedUniform)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<QuadRenderItem>, QuadBuffer)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, LodTexture)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadNeighborItem>, RWQuadNeighborBuffer)
			SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FReadNeighborLodsCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "ReadNeighborLodsCS", SF_Compute);

	/** */
	class FInitInstanceBufferCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FInitInstanceBufferCS);
		SHADER_USE_PARAMETER_STRUCT(FInitInstanceBufferCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadRenderInstance>, RWInstanceBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FInitInstanceBufferCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "InitInstanceBufferCS", SF_Compute);

	/** */
	class FCullInstancesCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FCullInstancesCS);
		SHADER_USE_PARAMETER_STRUCT(FCullInstancesCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			//SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
			SHADER_PARAMETER_TEXTURE(Texture2D, MinMaxTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, MinMaxTextureSampler)
			SHADER_PARAMETER_TEXTURE(Texture2D, PageTableTexture)
			SHADER_PARAMETER(FVector, PageTableSize)
			SHADER_PARAMETER(FMatrix, UVToWorld)
			SHADER_PARAMETER_ARRAY(FVector4, FrustumPlanes, [5])
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<QuadRenderItem>, QuadBuffer)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<QuadNeighborItem>, QuadNeighborBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<QuadRenderInstance>, RWInstanceBuffer)
			SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FCullInstancesCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "CullInstancesCS", SF_Compute);
	
	/** */
	class FBuildIndirectArgsForDrawCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FBuildIndirectArgsForDrawCS);
		SHADER_USE_PARAMETER_STRUCT(FBuildIndirectArgsForDrawCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(int, NumIndices)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<QuadRenderInstance>, InstanceBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FBuildIndirectArgsForDrawCS, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMesh.usf", "BuildIndirectArgsForDrawCS", SF_Compute);

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

	/** Convert FPlane to Xx+Yy+Zz+W=0 form for simpler use in shader. */
	FVector4 ConvertPlane(FPlane const& Plane)
	{
		return FVector4(-Plane.X, -Plane.Y, -Plane.Z, Plane.W);
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
		FVector PageTableSize;
		FUintVector4 PageTablePackedUniform;
		uint32 PageTableFeedbackId;
		FMatrix UVToWorld;
		uint32 NumQuadsPerTileSide;
		float LodScale;

		int32 MaxPersistentQueueItems;
		int32 MaxFeedbackItems;
	};

	/** View description used for LOD calculation in the main view. */
	struct FMainViewDesc
	{
		FSceneView const* ViewDebug;
		FVector CameraPosition;
		float ProjectionScale;
		float LodThreshold;
		FVector4 Planes[5];
		FTextureRHIRef OcclusionTexture;
		int32 OcclusionLevelOffset;
	};

	/** View description used for culling in the child view. */
	struct FChildViewDesc
	{
		FSceneView const* ViewDebug;
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
		SHADER_PARAMETER_RDG_BUFFER_UPLOAD(, InstanceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UPLOAD(, IndirectArgsBuffer)
	END_SHADER_PARAMETER_STRUCT()

	void InitializeInstanceBuffers(FRHICommandListImmediate& InRHICmdList, FDrawInstanceBuffers& InBuffers)
	{
		// We use a fake RDG pass for allocation. Is there a more direct way to do this for PooledRDGBuffer objects?
		// An alternative is use standard RHI allocation, but then we need to be manage resource transitions.
		FRDGBuilder GraphBuilder(InRHICmdList);

		FRDGBufferRef InstanceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(VirtualHeightfieldMesh::QuadRenderInstance), VirtualHeightfieldMesh::MaxRenderItems + 1), TEXT("QuadBuffer"));
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

		GraphBuilder.QueueBufferExtraction(InstanceBuffer, &InBuffers.InstanceBuffer, FRDGResourceState::EAccess::Write, FRDGResourceState::EPipeline::Compute);
		GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer, &InBuffers.IndirectArgsBuffer, FRDGResourceState::EAccess::Write, FRDGResourceState::EPipeline::Compute);

		GraphBuilder.Execute();

		// The SRV objects referenced by final rendering are managed outside of RDG.
		InBuffers.InstanceBufferSRV = RHICreateShaderResourceView(InBuffers.InstanceBuffer->StructuredBuffer);
	}

	/** Initialize the volatie resources used in the render graph. */
	void InitializeResources(FRDGBuilder& GraphBuilder, FProxyDesc const& InDesc, FMainViewDesc const& InMainViewDesc, FVolatileResources& OutResources)
	{
		OutResources.QueueInfo = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(WorkerQueueInfo), 1), TEXT("QueueInfo"));
		OutResources.QueueInfoUAV = GraphBuilder.CreateUAV(OutResources.QueueInfo);
		OutResources.QueueBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(QuadItem), InDesc.MaxPersistentQueueItems), TEXT("QuadQueue"));
		OutResources.QueueBufferUAV = GraphBuilder.CreateUAV(OutResources.QueueBuffer);

		OutResources.QuadBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(VirtualHeightfieldMesh::QuadRenderItem), VirtualHeightfieldMesh::MaxRenderItems + 1), TEXT("QuadBuffer"));
		OutResources.QuadBufferUAV = GraphBuilder.CreateUAV(OutResources.QuadBuffer);
		OutResources.QuadBufferSRV = GraphBuilder.CreateSRV(OutResources.QuadBuffer);

		FRDGBufferDesc FeedbackBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InDesc.MaxFeedbackItems + 1);
		FeedbackBufferDesc.Usage = EBufferUsageFlags(FeedbackBufferDesc.Usage | BUF_SourceCopy);
		OutResources.FeedbackBuffer = GraphBuilder.CreateBuffer(FeedbackBufferDesc, TEXT("FeedbackBuffer"));
		OutResources.FeedbackBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutResources.FeedbackBuffer, PF_R32_UINT));

		OutResources.IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsByteSize), TEXT("IndirectArgsBuffer"));
		OutResources.IndirectArgsBufferUAV = GraphBuilder.CreateUAV(OutResources.IndirectArgsBuffer);

		FPooledRenderTargetDesc LodTextureDesc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(InDesc.PageTableSize.X, InDesc.PageTableSize.Y),
			PF_R8G8,
			FClearValueBinding::None,
			TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false);
		OutResources.LodTexture = GraphBuilder.CreateTexture(LodTextureDesc, TEXT("LodTexture"));

		OutResources.QuadNeighborBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(VirtualHeightfieldMesh::QuadNeighborItem), VirtualHeightfieldMesh::MaxRenderItems * 4), TEXT("QuadNeighborBuffer"));
		OutResources.QuadNeighborBufferUAV = GraphBuilder.CreateUAV(OutResources.QuadNeighborBuffer);
		OutResources.QuadNeighborBufferSRV = GraphBuilder.CreateSRV(OutResources.QuadNeighborBuffer);
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
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->RWQuadBuffer = InVolatileResources.QuadBufferUAV;
		PassParameters->RWFeedbackBuffer = InVolatileResources.FeedbackBufferUAV;
		PassParameters->RWQueueInfo = InVolatileResources.QueueInfoUAV;
		PassParameters->RWQueueBuffer = InVolatileResources.QueueBufferUAV;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("InitBuffers"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader](FRHICommandList& RHICmdList)
			{
				//todo: If feedback parsing understands append counter we don't need to fully clear
				RHICmdList.ClearUAVUint(PassParameters->RWFeedbackBuffer->GetRHI(), FUintVector4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff));
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EGfxToGfx, PassParameters->RWFeedbackBuffer->GetRHI());

				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, FIntVector(1, 1, 1));
			});
	}

	/** */
	void AddPass_CollectQuads(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FMainViewDesc const& InViewDesc)
	{
		TShaderMapRef<FCollectQuadsCS> ComputeShader(InGlobalShaderMap);

		FCollectQuadsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCollectQuadsCS::FParameters>();
		//ShaderPrint::SetParameters(*InViewDesc.ViewDebug, PassParameters->ShaderPrintParameters);
		//ShaderDrawDebug::SetParameters(GraphBuilder, *InViewDesc.ViewDebug, PassParameters->ShaderDrawParameters);
		PassParameters->MinMaxTexture = InDesc.MinMaxTexture;
		PassParameters->MinMaxTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->OcclusionTexture = InViewDesc.OcclusionTexture;
		PassParameters->OcclusionLevelOffset = InViewDesc.OcclusionLevelOffset;
		PassParameters->PageTableTexture = InDesc.PageTableTexture;
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->PageTableFeedbackId = InDesc.PageTableFeedbackId;
		PassParameters->UVToWorld = InDesc.UVToWorld;
		PassParameters->CameraPosition = InViewDesc.CameraPosition;
		PassParameters->ProjectionScale = InViewDesc.ProjectionScale;
		for (int32 PlaneIndex = 0; PlaneIndex < 5; ++PlaneIndex)
		{
			PassParameters->FrustumPlanes[PlaneIndex] = InViewDesc.Planes[PlaneIndex];
		}
		PassParameters->LodThreshold = InDesc.LodScale * InViewDesc.LodThreshold;
		PassParameters->PageTablePackedUniform = InDesc.PageTablePackedUniform;
		PassParameters->RWQueueInfo = InVolatileResources.QueueInfoUAV;
		PassParameters->RWQueueBuffer = InVolatileResources.QueueBufferUAV;
		PassParameters->RWQuadBuffer = InVolatileResources.QuadBufferUAV;
		PassParameters->RWFeedbackBuffer = InVolatileResources.FeedbackBufferUAV;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CollectQuads"),
			ComputeShader, PassParameters, FIntVector(128, 1, 1));
	}

	/** */
	void AddPass_BuildIndirectArgsForLodAndCull(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources)
	{
		TShaderMapRef<FBuildIndirectArgsForLodAndCullCS> ComputeShader(InGlobalShaderMap);

		FBuildIndirectArgsForLodAndCullCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildIndirectArgsForLodAndCullCS::FParameters>();
		PassParameters->QuadBuffer = InVolatileResources.QuadBufferSRV;
		PassParameters->RWIndirectArgsBuffer = InVolatileResources.IndirectArgsBufferUAV;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildIndirectArgs"),
			ComputeShader, PassParameters, FIntVector(1, 1, 1));
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
	void AddPass_ReadNeighborLods(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FMainViewDesc const& InViewDesc)
	{
		TShaderMapRef<FReadNeighborLodsCS> ComputeShader(InGlobalShaderMap);

		FReadNeighborLodsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReadNeighborLodsCS::FParameters>();
		//ShaderPrint::SetParameters(*InViewDesc.ViewDebug, PassParameters->ShaderPrintParameters);
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->PageTableTexture = InDesc.PageTableTexture;
		PassParameters->PageTablePackedUniform = InDesc.PageTablePackedUniform;
		PassParameters->QuadBuffer = InVolatileResources.QuadBufferSRV;
		PassParameters->LodTexture = InVolatileResources.LodTexture;
		PassParameters->RWQuadNeighborBuffer = InVolatileResources.QuadNeighborBufferUAV;
		PassParameters->IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ReadNeighborLods"),
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
		PassParameters->RWInstanceBuffer = InOutputResources.InstanceBufferUAV;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitInstanceBuffer"),
			ComputeShader, PassParameters, FIntVector(1, 1, 1));
	}

	/** */
	void AddPass_CullInstances(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FOutputResources& InOutputResources, FChildViewDesc const& InViewDesc)
	{
		TShaderMapRef<FCullInstancesCS> ComputeShader(InGlobalShaderMap);

		FCullInstancesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullInstancesCS::FParameters>();
		//ShaderPrint::SetParameters(*InViewDesc.ViewDebug, PassParameters->ShaderPrintParameters);
		PassParameters->MinMaxTexture = InDesc.MinMaxTexture;
		PassParameters->MinMaxTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->PageTableTexture = InDesc.PageTableTexture;
		PassParameters->PageTableSize = InDesc.PageTableSize;
		PassParameters->UVToWorld = InDesc.UVToWorld;
		for (int32 PlaneIndex = 0; PlaneIndex < 5; ++PlaneIndex)
		{
			PassParameters->FrustumPlanes[PlaneIndex] = InViewDesc.Planes[PlaneIndex];
		}
		PassParameters->QuadBuffer = InVolatileResources.QuadBufferSRV;
		PassParameters->QuadNeighborBuffer = InVolatileResources.QuadNeighborBufferSRV;
		PassParameters->RWInstanceBuffer = InOutputResources.InstanceBufferUAV;
		PassParameters->IndirectArgsBuffer = InVolatileResources.IndirectArgsBuffer;

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
	void AddPass_BuildFinalIndirectArgsForDraw(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FOutputResources& InOutputResources)
	{
		TShaderMapRef<FBuildIndirectArgsForDrawCS> ComputeShader(InGlobalShaderMap);

		FBuildIndirectArgsForDrawCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildIndirectArgsForDrawCS::FParameters>();
		PassParameters->NumIndices = InDesc.NumQuadsPerTileSide * InDesc.NumQuadsPerTileSide * 6;
		PassParameters->InstanceBuffer = InOutputResources.InstanceBufferSRV;
		PassParameters->RWIndirectArgsBuffer = InOutputResources.IndirectArgsBufferUAV;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildIndirectArgs"),
			ComputeShader, PassParameters, FIntVector(1, 1, 1));
	}

	/** */
	void GPUCullMainView( FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FMainViewDesc const& InViewDesc)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "MainView");

		AddPass_InitBuffers(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources);
		AddPass_CollectQuads(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InViewDesc);
		AddPass_BuildIndirectArgsForLodAndCull(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources);
		AddPass_RenderLodMap(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources);
		AddPass_ReadNeighborLods(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InViewDesc);
	}

	/** */
	void GPUCullChildView(FRDGBuilder& GraphBuilder, FGlobalShaderMap* InGlobalShaderMap, FProxyDesc const& InDesc, FVolatileResources& InVolatileResources, FOutputResources& InOutputResources, FChildViewDesc const& InViewDesc)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "CullView");

		AddPass_InitInstanceBuffer(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InOutputResources);
		AddPass_CullInstances(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InOutputResources, InViewDesc);
		AddPass_BuildFinalIndirectArgsForDraw(GraphBuilder, InGlobalShaderMap, InDesc, InVolatileResources, InOutputResources);
	}
}

void FVirtualHeightfieldMeshRendererExtension::SubmitWork(FRHICommandListImmediate& InRHICmdList)
{
	SCOPED_DRAW_EVENT(InRHICmdList, VirtualHeightfieldMesh);

	// Collect feedback buffers from each pass to submit together after RenderGraph execution.
	// todo: Convert feedback submission to RDG so that it can be included in the render graph. 
	//       Then the render graph builder can be passed in and executed externally.
	TArray< TRefCountPtr< FPooledRDGBuffer > > FeedbackBuffers;

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
			
			FUintVector4 PageTablePackedUniform;
			AllocatedVirtualTexture->GetPackedUniform(&PageTablePackedUniform, 0);

			VirtualHeightfieldMesh::FProxyDesc ProxyDesc;
			ProxyDesc.PageTableTexture = AllocatedVirtualTexture->GetPageTableTexture(0);
			ProxyDesc.MinMaxTexture = Proxy->MinMaxTexture ? Proxy->MinMaxTexture->Resource->TextureRHI : VirtualHeightfieldMesh::GMinMaxDefaultTexture->TextureRHI;;
			ProxyDesc.PageTableSize = FVector(AllocatedVirtualTexture->GetWidthInTiles(), AllocatedVirtualTexture->GetHeightInTiles(), AllocatedVirtualTexture->GetMaxLevel());
			ProxyDesc.PageTablePackedUniform = PageTablePackedUniform;
			ProxyDesc.PageTableFeedbackId = AllocatedVirtualTexture->GetSpaceID() << 28;
			ProxyDesc.UVToWorld = Proxy->UVToWorld;
			ProxyDesc.NumQuadsPerTileSide = Proxy->NumQuadsPerTileSide;
			ProxyDesc.LodScale = FMath::Max(Proxy->LODScale * CVarVHMLodScale.GetValueOnRenderThread(), 0.01f);
			ProxyDesc.MaxPersistentQueueItems = VirtualHeightfieldMesh::MaxPersistentQueueItems;
			ProxyDesc.MaxFeedbackItems = VirtualHeightfieldMesh::MaxFeedbackItems;

			while (WorkIndex < NumWorkItems && SceneProxies[WorkDescs[WorkIndex].ProxyIndex] == Proxy)
			{
				// Gather data per main view
				FSceneView const* MainView = MainViews[WorkDescs[WorkIndex].MainViewIndex];

				static FMatrix ProjectionMatrix;
				static FConvexVolume ViewFrustum;
				static FVector CameraPosition;
				if (CVarVHMFixCullingCamera.GetValueOnRenderThread() == 0)
				{
					ProjectionMatrix = MainView->ViewMatrices.GetProjectionMatrix();
					ViewFrustum = MainView->ViewFrustum;
					CameraPosition = MainView->ViewMatrices.GetViewOrigin();
				}

				const float TargetPixelsPerTri = 64.f;

				VirtualHeightfieldMesh::FMainViewDesc MainViewDesc;
				MainViewDesc.ViewDebug = MainView;
				MainViewDesc.CameraPosition = CameraPosition;
				MainViewDesc.ProjectionScale = FMath::Max(0.5f * ProjectionMatrix.M[0][0], 0.5f * ProjectionMatrix.M[1][1]);
				MainViewDesc.LodThreshold = MainView->LODDistanceFactor * TargetPixelsPerTri * (Proxy->NumQuadsPerTileSide * Proxy->NumQuadsPerTileSide * 2) / MainView->UnconstrainedViewRect.Area();
				
				for (int32 PlaneIndex = 0; PlaneIndex < 5; ++PlaneIndex)
				{
					const int32 CopyPlaneIndex = FMath::Min(PlaneIndex, ViewFrustum.Planes.Num() - 1);
					MainViewDesc.Planes[PlaneIndex] = VirtualHeightfieldMesh::ConvertPlane(VirtualHeightfieldMesh::TransformPlane(ViewFrustum.Planes[CopyPlaneIndex], Proxy->WorldToUV, Proxy->WorldToUVTransposeAdjoint));
				}

				FOcclusionResults* OcclusionResults = GOcclusionResults.Find(FOcclusionResultsKey(Proxy, MainView));
				if (OcclusionResults == nullptr)
				{
					MainViewDesc.OcclusionTexture = GBlackTexture->TextureRHI;
					MainViewDesc.OcclusionLevelOffset = ProxyDesc.PageTableSize.Z;
				}
				else
				{
					MainViewDesc.OcclusionTexture = OcclusionResults->OcclusionTexture;
					MainViewDesc.OcclusionLevelOffset = ProxyDesc.PageTableSize.Z - OcclusionResults->NumTextureMips + 1;
				}

				// Build volatile graph resources
				VirtualHeightfieldMesh::FVolatileResources VolatileResources;
				VirtualHeightfieldMesh::InitializeResources(GraphBuilder, ProxyDesc, MainViewDesc, VolatileResources);

				// Build graph
				VirtualHeightfieldMesh::GPUCullMainView(GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel), ProxyDesc, VolatileResources, MainViewDesc);

				// Tag feedback buffer for extraction
				FeedbackBuffers.AddDefaulted();
				GraphBuilder.QueueBufferExtraction(VolatileResources.FeedbackBuffer, &FeedbackBuffers.Last(), FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);

				while (WorkIndex < NumWorkItems && MainViews[WorkDescs[WorkIndex].MainViewIndex] == MainView)
				{
					// Gather data per child view
					FSceneView const* CullView = CullViews[WorkDescs[WorkIndex].CullViewIndex];
					FConvexVolume const* ShadowFrustum = CullView->GetDynamicMeshElementsShadowCullFrustum();
					FConvexVolume const& Frustum = ShadowFrustum != nullptr && ShadowFrustum->Planes.Num() > 0 ? *ShadowFrustum : (CVarVHMFixCullingCamera.GetValueOnRenderThread() == 0) ? CullView->ViewFrustum : ViewFrustum;

					VirtualHeightfieldMesh::FChildViewDesc ChildViewDesc;
					ChildViewDesc.ViewDebug = MainView;
					
					for (int32 PlaneIndex = 0; PlaneIndex < 5; ++PlaneIndex)
					{
						const int32 CopyPlaneIndex = FMath::Min(PlaneIndex, Frustum.Planes.Num() - 1);
						ChildViewDesc.Planes[PlaneIndex] = VirtualHeightfieldMesh::ConvertPlane(VirtualHeightfieldMesh::TransformPlane(Frustum.Planes[CopyPlaneIndex], Proxy->WorldToUV, Proxy->WorldToUVTransposeAdjoint));
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
			Desc.Init(VirtualHeightfieldMesh::MaxFeedbackItems + 1);
			SubmitVirtualTextureFeedbackBuffer(GraphBuilder.RHICmdList, FeedbackBuffers[0].GetReference()->VertexBuffer, Desc);
		}
	}
}

PRAGMA_ENABLE_OPTIMIZATION
