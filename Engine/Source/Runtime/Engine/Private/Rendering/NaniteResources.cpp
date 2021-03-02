// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "PrimitiveSceneInfo.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/InstancedStaticMesh.h"
#include "Materials/Material.h"
#include "RenderingThread.h"
#include "UnifiedBuffer.h"
#include "CommonRenderResources.h"
#include "StaticMeshResources.h"
#include "DistanceFieldAtlas.h"
#include "RenderGraphUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "Misc/Compression.h"
#include "HAL/LowLevelMemStats.h"
#include "Interfaces/ITargetPlatform.h"
#include "NaniteSceneProxy.h"

#if RHI_RAYTRACING
#include "RayTracingInstance.h"
#endif

DEFINE_GPU_STAT(NaniteStreaming);
DEFINE_GPU_STAT(NaniteReadback);

#if ENABLE_LOW_LEVEL_MEM_TRACKER
DECLARE_LLM_MEMORY_STAT(TEXT("Nanite"), STAT_NaniteLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Nanite"), STAT_NaniteSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(Nanite, NAME_None, NAME_None, GET_STATFNAME(STAT_NaniteLLM), GET_STATFNAME(STAT_NaniteSummaryLLM));
#endif // ENABLE_LOW_LEVEL_MEM_TRACKER

#define MAX_CLUSTERS	(16 * 1024 * 1024)
#define MAX_NODES		 (2 * 1024 * 1024)

#define FORCE_NANITE_DEFAULT_MATERIAL 0

int32 GNaniteOptimizedRelevance = 1;
FAutoConsoleVariableRef CVarNaniteOptimizedRelevance(
	TEXT("r.Nanite.OptimizedRelevance"),
	GNaniteOptimizedRelevance,
	TEXT("Whether to optimize Nanite relevance (outside of editor)."),
	ECVF_RenderThreadSafe
);

int32 GNaniteMaxCandidateClusters = 8 * 1048576;
FAutoConsoleVariableRef CVarNaniteMaxCandidateClusters(
	TEXT("r.Nanite.MaxCandidateClusters"),
	GNaniteMaxCandidateClusters,
	TEXT("Maximum number of Nanite clusters before cluster culling."),
	ECVF_ReadOnly
);

int32 GNaniteMaxVisibleClusters = 2 * 1048576;
FAutoConsoleVariableRef CVarNaniteMaxVisibleClusters(
	TEXT("r.Nanite.MaxVisibleClusters"),
	GNaniteMaxVisibleClusters,
	TEXT("Maximum number of visible Nanite clusters."),
	ECVF_ReadOnly
);

int32 GRayTracingNaniteProxyMeshes = 1;
FAutoConsoleVariableRef CVarRayTracingNaniteProxyMeshes(
	TEXT("r.RayTracing.Geometry.NaniteProxies"),
	GRayTracingNaniteProxyMeshes,
	TEXT("Include Nanite proxy meshes in ray tracing effects (default = 1 (Nanite proxy meshes enabled in ray tracing))"),
	ECVF_RenderThreadSafe
);

namespace Nanite
{

static_assert(sizeof(FPackedCluster) == NUM_PACKED_CLUSTER_FLOAT4S * 16, "NUM_PACKED_CLUSTER_FLOAT4S out of sync with sizeof(FPackedCluster)");

FArchive& operator<<(FArchive& Ar, FPackedHierarchyNode& Node)
{
	for (uint32 i = 0; i < MAX_BVH_NODE_FANOUT; i++)
	{
		Ar << Node.LODBounds[ i ];
		Ar << Node.Misc0[ i ].BoxBoundsCenter;
		Ar << Node.Misc0[ i ].MinLODError_MaxParentLODError;
		Ar << Node.Misc1[ i ].BoxBoundsExtent;
		Ar << Node.Misc1[ i ].ChildStartReference;
		Ar << Node.Misc2[ i ].ResourcePageIndex_NumPages_GroupPartSize;
	}
	
	return Ar;
}

FArchive& operator<<( FArchive& Ar, FPageStreamingState& PageStreamingState )
{
	Ar << PageStreamingState.BulkOffset;
	Ar << PageStreamingState.BulkSize;
	Ar << PageStreamingState.PageUncompressedSize;
	Ar << PageStreamingState.DependenciesStart;
	Ar << PageStreamingState.DependenciesNum;
	return Ar;
}

void FResources::InitResources()
{
	// TODO: Should remove bulk data from built data if platform cannot run Nanite in any capacity
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	if (PageStreamingStates.Num() == 0)
	{
		// Skip resources that have their render data stripped
		return;
	}
	
	// Root pages should be available here. If they aren't, this resource has probably already been initialized and added to the streamer. Investigate!
	check(RootClusterPage.Num() > 0);

	ENQUEUE_RENDER_COMMAND(InitNaniteResources)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			GStreamingManager.Add(this);
		}
	);
}

void FResources::ReleaseResources()
{
	// TODO: Should remove bulk data from built data if platform cannot run Nanite in any capacity
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	if (PageStreamingStates.Num() == 0)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(ReleaseNaniteResources)(
		[this]( FRHICommandListImmediate& RHICmdList)
		{
			GStreamingManager.Remove(this);
		}
	);
}

static void DecompressPages(FResources& Resources, TArray<uint8>& OutRootClusterPage, FByteBulkData& OutStreamableClusterPages, TArray<FPageStreamingState>& OutPageStreamingStates)
{
	check(Resources.bLZCompressed);

	// Decompress root and streaming pages
	const uint32 NumPages = Resources.PageStreamingStates.Num();
	
	uint32 NewSizes[2] = {};
	
	TArray<uint8> StreamingData((uint8*)Resources.StreamableClusterPages.Lock(LOCK_READ_ONLY), Resources.StreamableClusterPages.GetBulkDataSize());
	Resources.StreamableClusterPages.Unlock();

	// Calculate new root and streaming buffer sizes
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const bool bIsRootPage = PageIndex < NUM_ROOT_PAGES;
		const FPageStreamingState& State = Resources.PageStreamingStates[PageIndex];
		const TArray<uint8>& OldData = bIsRootPage ? Resources.RootClusterPage : StreamingData;
		const FFixupChunk& FixupChunk = *(const FFixupChunk*)(OldData.GetData() + State.BulkOffset);
		NewSizes[bIsRootPage] += FixupChunk.GetSize() + State.PageUncompressedSize;
	}

	OutRootClusterPage.SetNumUninitialized(NewSizes[1]);
	
	OutStreamableClusterPages.Lock(LOCK_READ_WRITE);
	uint8* StreamingDataPtr = (uint8*)OutStreamableClusterPages.Realloc(NewSizes[0]);

	OutPageStreamingStates = Resources.PageStreamingStates;
	
	// Decompress data
	uint32 UncompressedOffsets[2] = {};
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const bool bIsRootPage = PageIndex < NUM_ROOT_PAGES;
		const TArray<uint8>& OldData = bIsRootPage ? Resources.RootClusterPage : StreamingData;

		FPageStreamingState& State = OutPageStreamingStates[PageIndex];
		const FFixupChunk& FixupChunk = *(const FFixupChunk*)(OldData.GetData() + State.BulkOffset);
		const uint32 FixupChunkSize = FixupChunk.GetSize();

		uint8* DstPtr = bIsRootPage ? OutRootClusterPage.GetData() : StreamingDataPtr;
		FMemory::Memcpy(DstPtr + UncompressedOffsets[bIsRootPage], &FixupChunk, FixupChunkSize);

		verify(FCompression::UncompressMemory(NAME_LZ4, DstPtr + UncompressedOffsets[bIsRootPage] + FixupChunkSize, State.PageUncompressedSize, OldData.GetData() + State.BulkOffset + FixupChunkSize, State.BulkSize - FixupChunkSize));
		State.BulkSize = FixupChunkSize + State.PageUncompressedSize;
		State.BulkOffset = UncompressedOffsets[bIsRootPage];
		UncompressedOffsets[bIsRootPage] += State.BulkSize;
	}
	check(UncompressedOffsets[0] == NewSizes[0]);
	check(UncompressedOffsets[1] == NewSizes[1]);

	OutStreamableClusterPages.Unlock();
	OutStreamableClusterPages.ResetBulkDataFlags(Resources.StreamableClusterPages.GetBulkDataFlags());
}

void FResources::Serialize(FArchive& Ar, UObject* Owner)
{
	LLM_SCOPE_BYTAG(Nanite);

	// Note: this is all derived data, native versioning is not needed, but be sure to bump NANITE_DERIVEDDATA_VER when modifying!
	FStripDataFlags StripFlags( Ar, 0 );
	if( !StripFlags.IsDataStrippedForServer() )
	{
		// HACK/TODO: Decompress data on platforms that already support LZ decompression in hardware.
		// Meshes are ALWAYS cooked on the host platform, so just including compression in the DDC key would double cook times for platforms with hardware LZ.
		// Needs to be revisited when new resource system lands.
		bool bWantsUncompressedSave = false;
		
#if WITH_EDITOR
		bWantsUncompressedSave = Ar.IsCooking() && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::HardwareLZDecompression) && RootClusterPage.Num() > 0 && !Ar.IsObjectReferenceCollector();
		if (bWantsUncompressedSave && bLZCompressed)
		{
			// Decompress and serialize, but don't change the state of the resource itself
			if (!bHasDecompressedData)
			{
				DecompressPages(*this, DecompressedRootClusterPage, DecompressedStreamableClusterPages, DecompressedPageStreamingStates);
				bHasDecompressedData = true;
			}
			
			bool bNewLZCompressed = false;
			Ar << bNewLZCompressed;
			Ar << DecompressedRootClusterPage;
			DecompressedStreamableClusterPages.Serialize(Ar, Owner, 0);
			Ar << DecompressedPageStreamingStates;
		}
		else
#endif
		{
			check(!Ar.IsSaving() || RootClusterPage.Num() == 0 || (bWantsUncompressedSave == !bLZCompressed));

			Ar << bLZCompressed;
			Ar << RootClusterPage;
			StreamableClusterPages.Serialize(Ar, Owner, 0);
			Ar << PageStreamingStates;
		}
		
		Ar << HierarchyNodes;
		Ar << HierarchyRootOffsets;
		Ar << PageDependencies;
		Ar << ImposterAtlas;
		
		check(!Ar.IsLoading() || RootClusterPage.Num() == 0 || bLZCompressed == !FPlatformProperties::SupportsHardwareLZDecompression());		

#if WITH_EDITOR
		if (Ar.IsLoading() && bHasDecompressedData)
		{
			// Cached decompressed data is no longer valid after loading new data. Clear it.
			DecompressedRootClusterPage.Empty();
			DecompressedPageStreamingStates.Empty();
			DecompressedStreamableClusterPages.RemoveBulkData();
			bHasDecompressedData = false;
		}
#endif
	}
}

class FVertexFactory : public ::FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FVertexFactory);

public:
	FVertexFactory(ERHIFeatureLevel::Type FeatureLevel) : ::FVertexFactory(FeatureLevel)
	{
	}

	~FVertexFactory()
	{
		ReleaseResource();
	}

	virtual void InitRHI() override final
	{
		LLM_SCOPE_BYTAG(Nanite);

		FVertexStream VertexStream;
		VertexStream.VertexBuffer = &GScreenRectangleVertexBuffer;
		VertexStream.Offset = 0;

		Streams.Add(VertexStream);

		SetDeclaration(GFilterVertexDeclaration.VertexDeclarationRHI);
	}

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
	{
		bool bShouldCompile = 
			DoesPlatformSupportNanite(Parameters.Platform) &&
			Parameters.ShaderType->GetFrequency() == SF_Pixel &&
			RHISupportsComputeShaders(Parameters.Platform) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
			Parameters.MaterialParameters.BlendMode == BLEND_Opaque;

		return bShouldCompile;
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		::FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_NANITE"), 1);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_UNIFORM_BUFFER"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_VIEW_UNIFORM_BUFFER"), 1);
	}
};
IMPLEMENT_VERTEX_FACTORY_TYPE_EX(Nanite::FVertexFactory, "/Engine/Private/Nanite/NaniteVertexFactory.ush", true /* bUsedWithMaterials */, true /* bSupportsStaticLighting */, false /* bSupportsDynamicLighting */, false /* bPrecisePrevWorldPos */, false /* bSupportsPositionOnly */, false /* bSupportsCachingMeshDrawCommands */, true /* bSupportsPrimitiveIdStream */, true /* bSupportsNaniteRendering */);

SIZE_T FSceneProxyBase::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FSceneProxy::FSceneProxy(UStaticMeshComponent* Component)
: FSceneProxyBase(Component)
, MeshInfo(Component)
, Resources(&Component->GetStaticMesh()->GetRenderData()->NaniteResources)
, RenderData(Component->GetStaticMesh()->GetRenderData())
, StaticMesh(Component->GetStaticMesh())
#if NANITE_ENABLE_DEBUG_RENDERING
, Owner(Component->GetOwner())
, LightMapResolution(Component->GetStaticLightMapResolution())
, BodySetup(Component->GetBodySetup())
, CollisionTraceFlag(ECollisionTraceFlag::CTF_UseSimpleAndComplex)
, CollisionResponse(Component->GetCollisionResponseToChannels())
, LODForCollision(Component->GetStaticMesh()->LODForCollision)
, bDrawMeshCollisionIfComplex(Component->bDrawMeshCollisionIfComplex)
, bDrawMeshCollisionIfSimple(Component->bDrawMeshCollisionIfSimple)
#endif
{
	LLM_SCOPE_BYTAG(Nanite);

	// Nanite requires GPUScene
	checkSlow(UseGPUScene(GMaxRHIShaderPlatform, GetScene().GetFeatureLevel()));
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	MaterialRelevance = Component->GetMaterialRelevance(Component->GetScene()->GetFeatureLevel());

	// Nanite supports the GPUScene instance data buffer.
	bSupportsInstanceDataBuffer = true;

	// Nanite supports distance field representation.
	bSupportsDistanceFieldRepresentation = MaterialRelevance.bOpaque;

	// Nanite supports mesh card representation.
	bSupportsMeshCardRepresentation = true;

	// Use fast path that does not update static draw lists.
	bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;

	// We always use local vertex factory, which gets its primitive data from
	// GPUScene, so we can skip expensive primitive uniform buffer updates.
	bVFRequiresPrimitiveUniformBuffer = false;

	// Indicates if 1 or more materials contain settings not supported by Nanite.
	bHasMaterialErrors = false;

	const bool bHasSurfaceStaticLighting = MeshInfo.GetLightMap() != nullptr || MeshInfo.GetShadowMap() != nullptr;

	// Check if the assigned material can be rendered in Nanite. If not, default.
	const bool IsRenderable = Nanite::FSceneProxy::IsNaniteRenderable(MaterialRelevance);
	if (!IsRenderable)
	{
		bHasMaterialErrors = true;
	}

	const uint32 LODIndex = 0; // only LOD0 is supported
	const FStaticMeshLODResources& MeshResources = RenderData->LODResources[LODIndex];
	const FStaticMeshSectionArray& MeshSections = MeshResources.Sections;
	
	MaterialSections.SetNumZeroed(MeshSections.Num());

#if RHI_RAYTRACING
	CachedRayTracingMaterials.Reserve(MaterialSections.Num());
#endif 

	for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& MeshSection = MeshSections[SectionIndex];
		const bool bValidMeshSection = MeshSection.MaterialIndex != INDEX_NONE;

		UMaterialInterface* MaterialInterface = bValidMeshSection ? Component->GetMaterial(MeshSection.MaterialIndex) : nullptr;

		const bool bInvalidMaterial = !MaterialInterface || MaterialInterface->GetBlendMode() != BLEND_Opaque;
		if (bInvalidMaterial)
		{
			bHasMaterialErrors = true;
			if (MaterialInterface)
			{
				UE_LOG
				(
					LogStaticMesh, Warning,
					TEXT("Invalid material [%s] used on Nanite static mesh [%s] - forcing default material instead. Only opaque blend mode is currently supported, [%s] blend mode was specified."),
					*MaterialInterface->GetName(),
					*StaticMesh->GetName(),
					*GetBlendModeString(MaterialInterface->GetBlendMode())
				);
			}
		}

		const bool bForceDefaultMaterial = !!FORCE_NANITE_DEFAULT_MATERIAL || bHasMaterialErrors || (bHasSurfaceStaticLighting && !MaterialInterface->CheckMaterialUsage_Concurrent(MATUSAGE_StaticLighting));
		if (bForceDefaultMaterial)
		{
			MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		// Should never be null here
		check(MaterialInterface != nullptr);

		// Should always be opaque blend mode here.
		check(MaterialInterface->GetBlendMode() == BLEND_Opaque);

		MaterialSections[SectionIndex].Material = MaterialInterface;

	#if RHI_RAYTRACING
		FMeshBatch& MeshBatch = CachedRayTracingMaterials.AddDefaulted_GetRef();
		MeshBatch.VertexFactory = &RenderData->LODVertexFactories[LODIndex].VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();
		MeshBatch.bWireframe = false;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.LODIndex = LODIndex;
	#endif
	}

	// Copy the pointer to the volume data, async building of the data may modify the one on FStaticMeshLODResources while we are rendering
	DistanceFieldData = MeshResources.DistanceFieldData;
	CardRepresentationData = MeshResources.CardRepresentationData;

	Instances.SetNumZeroed(1);
	FPrimitiveInstance& Instance = Instances[0];
	Instance.PrimitiveId = ~uint32(0);
	Instance.InstanceToLocal.SetIdentity();
	Instance.LocalToInstance.SetIdentity();
	Instance.LocalToWorld.SetIdentity();
	Instance.RenderBounds = Component->GetStaticMesh()->GetBounds();
	Instance.LocalBounds = Instance.RenderBounds;
	Instance.LightMapAndShadowMapUVBias = FVector4(ForceInitToZero);
	Instance.PerInstanceRandom = 0;
	Instance.Flags = 0;
	Instance.Flags |= bCastShadow ? 1 : 0;

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		const FStaticMeshLODResourcesArray& LODResources = Component->GetStaticMesh()->GetRenderData()->LODResources;
		if (LODResources.Num() && LODResources[LODIndex].GetNumVertices())
		{
			RayTracingGeometry = &LODResources[LODIndex].RayTracingGeometry;
			bHasRayTracingInstances = true;
		}

		// This will be filled later (on the render thread) and cached.
		CachedRayTracingInstanceMaskAndFlags.Mask = 0;
	}
#endif
}

FSceneProxy::FSceneProxy(UInstancedStaticMeshComponent* Component)
: FSceneProxy(static_cast<UStaticMeshComponent*>(Component))
{
	LLM_SCOPE_BYTAG(Nanite);

	Instances.SetNumZeroed(Component->GetInstanceCount());
	for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); ++InstanceIndex)
	{
		FTransform InstanceTransform;
		Component->GetInstanceTransform(InstanceIndex, InstanceTransform);
		
		// TODO: KevinO cleanup		
		FTransform InstancePrevTransform;
		const bool bHasPrevTransform = Component->GetInstancePrevTransform(InstanceIndex, InstancePrevTransform);

		FVector4 InstanceTransformVec[3];
		FVector4 InstanceLightMapAndShadowMapUVBias = FVector4(ForceInitToZero);
		FVector4 InstanceOrigin = FVector::ZeroVector;

		if (Component->PerInstanceRenderData != nullptr && Component->PerInstanceRenderData->InstanceBuffer_GameThread != nullptr)
		{
			if (Component->PerInstanceRenderData->InstanceBuffer_GameThread->IsValidIndex(InstanceIndex))
			{
				Component->PerInstanceRenderData->InstanceBuffer_GameThread->GetInstanceShaderValues(
					InstanceIndex,
					InstanceTransformVec,
					InstanceLightMapAndShadowMapUVBias,
					InstanceOrigin
				);
			}
		}

		FPrimitiveInstance& Instance = Instances[InstanceIndex];
		Instance.PrimitiveId = ~uint32(0);
		Instance.InstanceToLocal = InstanceTransform.ToMatrixWithScale();
		
		// TODO: KevinO cleanup
		if (bHasPrevTransform)
		{
			bHasPrevInstanceTransforms = true;
			Instance.PrevInstanceToLocal = InstancePrevTransform.ToMatrixWithScale();
		}

		Instance.LocalToWorld = Instance.InstanceToLocal;
		Instance.LocalToInstance = Instance.LocalToWorld.Inverse();
		Instance.RenderBounds = Component->GetStaticMesh()->GetBounds();
		Instance.LocalBounds = Instance.RenderBounds.TransformBy(Instance.InstanceToLocal);
		Instance.LightMapAndShadowMapUVBias = InstanceLightMapAndShadowMapUVBias;
		Instance.PerInstanceRandom = InstanceOrigin.W; // Per-instance random packed into W component
	}

#if RHI_RAYTRACING
	if (Instances.Num() == 0)
	{
		bHasRayTracingInstances = false;
	}
#endif
}

FSceneProxy::FSceneProxy(UHierarchicalInstancedStaticMeshComponent* Component)
: FSceneProxy(static_cast<UInstancedStaticMeshComponent*>(Component))
{
}

void FSceneProxy::CreateRenderThreadResources()
{
	// These couldn't be copied on the game-thread because they are initialized
	// by the StreamingManager on the render thread. Initialize them now.
	check(Resources->RuntimeResourceID != 0xFFFFFFFFu);
	check(Resources->HierarchyOffset != -1);
	bool bHasImposter = Resources->ImposterAtlas.Num() > 0;
	FNaniteInfo NaniteInfo = FNaniteInfo(Resources->RuntimeResourceID, Resources->HierarchyOffset, bHasImposter);
	for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); ++InstanceIndex)
	{
		Instances[InstanceIndex].NaniteInfo = NaniteInfo;
	}
}

FPrimitiveViewRelevance FSceneProxy::GetViewRelevance( const FSceneView* View ) const
{
	LLM_SCOPE_BYTAG(Nanite);

#if WITH_EDITOR
	const bool bOptimizedRelevance = false;
#else
	const bool bOptimizedRelevance = GNaniteOptimizedRelevance != 0;
#endif

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.NaniteMeshes;
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();

	// Always render the Nanite mesh data with static relevance.
	Result.bStaticRelevance = true;

	// Should always be covered by constructor of Nanite scene proxy.
	Result.bRenderInMainPass = true;

	if (bOptimizedRelevance) // No dynamic relevance if optimized.
	{
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity();
	}
	else
	{
	#if WITH_EDITOR
		//only check these in the editor
		Result.bEditorVisualizeLevelInstanceRelevance = IsEditingLevelInstanceChild();
		Result.bEditorStaticSelectionRelevance = (IsSelected() || IsHovered());
	#endif

	#if NANITE_ENABLE_DEBUG_RENDERING
		bool bDrawSimpleCollision = false, bDrawComplexCollision = false;
		const bool bInCollisionView = IsCollisionView(View->Family->EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);
	#else
		bool bInCollisionView = false;
	#endif

		// Set dynamic relevance for overlays like collision and bounds.
		bool bSetDynamicRelevance = false;
	#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
		bSetDynamicRelevance |= (IsRichView(*View->Family) ||
			View->Family->EngineShowFlags.Collision ||
			bInCollisionView ||
			View->Family->EngineShowFlags.Bounds);
	#endif
	#if WITH_EDITOR
		bSetDynamicRelevance |= (IsSelected() && View->Family->EngineShowFlags.VertexColors);
	#endif
	#if NANITE_ENABLE_DEBUG_RENDERING
		bSetDynamicRelevance |= bDrawMeshCollisionIfComplex || bDrawMeshCollisionIfSimple;
	#endif

		if (bSetDynamicRelevance)
		{
			Result.bDynamicRelevance = true;

		#if NANITE_ENABLE_DEBUG_RENDERING
			// If we want to draw collision, needs to make sure we are considered relevant even if hidden
			if (View->Family->EngineShowFlags.Collision || bInCollisionView)
			{
				Result.bDrawRelevance = true;
			}
		#endif
		}

		if (!View->Family->EngineShowFlags.Materials
		#if NANITE_ENABLE_DEBUG_RENDERING
			|| bInCollisionView
		#endif
			)
		{
			Result.bOpaque = true;
		}

		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = Result.bOpaque && Result.bRenderInMainPass && DrawsVelocity();
	}

	return Result;
}

void FSceneProxy::GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const
{
	// Attach the light to the primitive's static meshes.
	const ELightInteractionType InteractionType = MeshInfo.GetInteraction(LightSceneProxy).GetType();
	bRelevant     = (InteractionType != LIT_CachedIrrelevant);
	bDynamic      = (InteractionType == LIT_Dynamic);
	bLightMapped  = (InteractionType == LIT_CachedLightMap || InteractionType == LIT_CachedIrrelevant);
	bShadowMapped = (InteractionType == LIT_CachedSignedDistanceFieldShadowMap2D);
}

#if WITH_EDITOR

HHitProxy* FSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	LLM_SCOPE_BYTAG(Nanite);

	if (Component->GetOwner())
	{
		// Generate separate hit proxies for each material section, so that we can perform hit tests against each one.
		for (int32 SectionIndex = 0; SectionIndex < MaterialSections.Num(); ++SectionIndex)
		{
			FMaterialSection& Section = MaterialSections[SectionIndex];
			HHitProxy* ActorHitProxy = new HActor(Component->GetOwner(), Component, SectionIndex, SectionIndex);
			check(!Section.HitProxy);
			Section.HitProxy = ActorHitProxy;
			OutHitProxies.Add(ActorHitProxy);
		}
	}

	// We don't want a default hit proxy, or to output any hit proxies (avoid 2x registration).
	return nullptr;
}

#endif

FSceneProxy::FMeshInfo::FMeshInfo(const UStaticMeshComponent* InComponent)
{
	LLM_SCOPE_BYTAG(Nanite);

	if (InComponent->LightmapType == ELightmapType::ForceVolumetric)
	{
		SetGlobalVolumeLightmap(true);
	}
	else if (InComponent->LODData.Num() > 0)
	{
		const FStaticMeshComponentLODInfo& ComponentLODInfo = InComponent->LODData[0];

		const FMeshMapBuildData* MeshMapBuildData = InComponent->GetMeshMapBuildData(ComponentLODInfo);
		if (MeshMapBuildData)
		{
			SetLightMap(MeshMapBuildData->LightMap);
			SetShadowMap(MeshMapBuildData->ShadowMap);
			SetResourceCluster(MeshMapBuildData->ResourceCluster);
			IrrelevantLights = MeshMapBuildData->IrrelevantLights;
		}
	}

	//const bool bHasSurfaceStaticLighting = GetLightMap() != nullptr || GetShadowMap() != nullptr;
}

FLightInteraction FSceneProxy::FMeshInfo::GetInteraction(const FLightSceneProxy* LightSceneProxy) const
{
	// Ask base class
	ELightInteractionType LightInteraction = GetStaticInteraction(LightSceneProxy, IrrelevantLights);

	if (LightInteraction != LIT_MAX)
	{
		return FLightInteraction(LightInteraction);
	}

	// Use dynamic lighting if the light doesn't have static lighting.
	return FLightInteraction::Dynamic();
}

void FSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	// TODO: Refactor into FSceneProxyBase

	LLM_SCOPE_BYTAG(Nanite);

	for (int32 SectionIndex = 0; SectionIndex < MaterialSections.Num(); ++SectionIndex)
	{
		const FMaterialSection& Section = MaterialSections[SectionIndex];
		const UMaterialInterface* Material = Section.Material;
		if (!Material)
		{
			continue;
		}

		FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
		check(MaterialProxy);

		FMeshBatch MeshBatch;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.VertexFactory = GGlobalResources.GetVertexFactory();
		MeshBatch.Type = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;
		MeshBatch.ReverseCulling = false;
		MeshBatch.bDisableBackfaceCulling = true;
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = INDEX_NONE;
		MeshBatch.MaterialRenderProxy = MaterialProxy;
		MeshBatch.bWireframe = false;
		MeshBatch.bCanApplyViewModeOverrides = false;
		MeshBatch.LCI = &MeshInfo;
		MeshBatch.Elements[0].IndexBuffer = &GScreenRectangleIndexBuffer;
		if (GRHISupportsRectTopology)
		{
			MeshBatch.Elements[0].FirstIndex = 9;
			MeshBatch.Elements[0].NumPrimitives = 1;
			MeshBatch.Elements[0].MinVertexIndex = 1;
			MeshBatch.Elements[0].MaxVertexIndex = 3;
		}
		else
		{
			MeshBatch.Elements[0].FirstIndex = 0;
			MeshBatch.Elements[0].NumPrimitives = 2;
			MeshBatch.Elements[0].MinVertexIndex = 0;
			MeshBatch.Elements[0].MaxVertexIndex = 3;
		}
		MeshBatch.Elements[0].NumInstances = 1;
		MeshBatch.Elements[0].PrimitiveIdMode = PrimID_ForceZero;
		MeshBatch.Elements[0].PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;

	#if WITH_EDITOR
		HHitProxy* HitProxy = Section.HitProxy;
		//check(HitProxy); // TODO: Is this valid? SME seems to have null proxies, but normal editor doesn't
		PDI->SetHitProxy(HitProxy);
	#endif
		PDI->DrawMesh(MeshBatch, FLT_MAX);
	}
}

void FSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
#if !WITH_EDITOR
	if (GNaniteOptimizedRelevance != 0)
	{
		// No dynamic relevance.
		return;
	}
#endif

	LLM_SCOPE_BYTAG(Nanite);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_NaniteSceneProxy_GetMeshElements);
	checkSlow(IsInRenderingThread());

	const bool bIsLightmapSettingError = HasStaticLighting() && !HasValidSettingsForStaticLighting();
	const bool bProxyIsSelected = IsSelected();
	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	bool bDrawSimpleCollision = false, bDrawComplexCollision = false;
	const bool bInCollisionView = IsCollisionView(EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);

#if NANITE_ENABLE_DEBUG_RENDERING
	// Collision and bounds drawing
	FColor SimpleCollisionColor = FColor(157, 149, 223, 255);
	FColor ComplexCollisionColor = FColor(0, 255, 255, 255);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if (AllowDebugViewmodes())
			{
#if 0 // NANITE_TODO: Complex collision rendering
				// Should we draw the mesh wireframe to indicate we are using the mesh as collision
				bool bDrawComplexWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled() && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
				
				// Requested drawing complex in wireframe, but check that we are not using simple as complex
				bDrawComplexWireframeCollision |= (bDrawMeshCollisionIfComplex && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex);
				
				// Requested drawing simple in wireframe, and we are using complex as simple
				bDrawComplexWireframeCollision |= (bDrawMeshCollisionIfSimple && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);

				// If drawing complex collision as solid or wireframe
				if (bDrawComplexWireframeCollision || (bInCollisionView && bDrawComplexCollision))
				{
					// If we have at least one valid LOD to draw
					if (RenderData->LODResources.Num() > 0)
					{
						// Get LOD used for collision
						int32 DrawLOD = FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num() - 1);
						const FStaticMeshLODResources& LODModel = RenderData->LODResources[DrawLOD];

						UMaterial* MaterialToUse = UMaterial::GetDefaultMaterial(MD_Surface);
						FLinearColor DrawCollisionColor = GetWireframeColor();
						// Collision view modes draw collision mesh as solid
						if (bInCollisionView)
						{
							MaterialToUse = GEngine->ShadedLevelColorationUnlitMaterial;
						}
						// Wireframe, choose color based on complex or simple
						else
						{
							MaterialToUse = GEngine->WireframeMaterial;
							DrawCollisionColor = (CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple) ? SimpleCollisionColor : ComplexCollisionColor;
						}

						// Iterate over sections of that LOD
						for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
						{
							// If this section has collision enabled
							if (LODModel.Sections[SectionIndex].bEnableCollision)
							{
							#if WITH_EDITOR
								// See if we are selected
								const bool bSectionIsSelected = LODs[DrawLOD].Sections[SectionIndex].bSelected;
							#else
								const bool bSectionIsSelected = false;
							#endif

								// Create colored proxy
								FColoredMaterialRenderProxy* CollisionMaterialInstance = new FColoredMaterialRenderProxy(MaterialToUse->GetRenderProxy(), DrawCollisionColor);
								Collector.RegisterOneFrameMaterialProxy(CollisionMaterialInstance);

								// Iterate over batches
								for (int32 BatchIndex = 0; BatchIndex < GetNumMeshBatches(); BatchIndex++)
								{
									FMeshBatch& CollisionElement = Collector.AllocateMesh();
									if (GetCollisionMeshElement(DrawLOD, BatchIndex, SectionIndex, SDPG_World, CollisionMaterialInstance, CollisionElement))
									{
										Collector.AddMesh(ViewIndex, CollisionElement);
										INC_DWORD_STAT_BY(STAT_NaniteTriangles, CollisionElement.GetNumPrimitives());
									}
								}
							}
						}
					}
				}
#endif // NANITE_TODO
			}

			// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
			// NANITE_TODO: const bool bDrawSimpleWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled() && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple); 
			const bool bDrawSimpleWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled());

			if ((bDrawSimpleCollision || bDrawSimpleWireframeCollision) && BodySetup)
			{
				if (FMath::Abs(GetLocalToWorld().Determinant()) < SMALL_NUMBER)
				{
					// Catch this here or otherwise GeomTransform below will assert
					// This spams so commented out
					//UE_LOG(LogNanite, Log, TEXT("Zero scaling not supported (%s)"), *StaticMesh->GetPathName());
				}
				else
				{
					const bool bDrawSolid = !bDrawSimpleWireframeCollision;

					if (AllowDebugViewmodes() && bDrawSolid)
					{
						// Make a material for drawing solid collision stuff
						auto SolidMaterialInstance = new FColoredMaterialRenderProxy(
							GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(),
							GetWireframeColor()
							);

						Collector.RegisterOneFrameMaterialProxy(SolidMaterialInstance);

						FTransform GeomTransform(GetLocalToWorld());
						BodySetup->AggGeom.GetAggGeom(GeomTransform, GetWireframeColor().ToFColor(true), SolidMaterialInstance, false, true, DrawsVelocity(), ViewIndex, Collector);
					}
					// wireframe
					else
					{
						FTransform GeomTransform(GetLocalToWorld());
						BodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(SimpleCollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true), nullptr, (Owner == nullptr), false, DrawsVelocity(), ViewIndex, Collector);
					}


					// The simple nav geometry is only used by dynamic obstacles for now
					if (StaticMesh->GetNavCollision() && StaticMesh->GetNavCollision()->IsDynamicObstacle())
					{
						// Draw the static mesh's body setup (simple collision)
						FTransform GeomTransform(GetLocalToWorld());
						FColor NavCollisionColor = FColor(118,84,255,255);
						StaticMesh->GetNavCollision()->DrawSimpleGeom(Collector.GetPDI(ViewIndex), GeomTransform, GetSelectionColor(NavCollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true));
					}
				}
			}

			if (EngineShowFlags.MassProperties && DebugMassData.Num() > 0)
			{
				DebugMassData[0].DrawDebugMass(Collector.GetPDI(ViewIndex), FTransform(GetLocalToWorld()));
			}
	
			if (EngineShowFlags.StaticMeshes)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), EngineShowFlags, GetBounds(), !Owner || IsSelected());
			}
		}
	}
#endif // NANITE_ENABLE_DEBUG_RENDERING
}

#if RHI_RAYTRACING
void FSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances)
{
	if (GRayTracingNaniteProxyMeshes == 0 || !bHasRayTracingInstances)
	{
		return;
	}

	FRayTracingInstance& RayTracingInstance = OutRayTracingInstances.Emplace_GetRef();

	RayTracingInstance.Geometry = RayTracingGeometry;

	const int32 InstanceCount = Instances.Num();
	if (CachedRayTracingInstanceTransforms.Num() != InstanceCount || GetLocalToWorld() != CachedRayTracingInstanceLocalToWorld)
	{
		CachedRayTracingInstanceTransforms.SetNumUninitialized(InstanceCount);
		for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
		{
			const FPrimitiveInstance& Instance = Instances[InstanceIndex];
			CachedRayTracingInstanceTransforms[InstanceIndex] = Instance.InstanceToLocal * GetLocalToWorld();
		}
		CachedRayTracingInstanceLocalToWorld = GetLocalToWorld();
	}

	// Transforms are persistently allocated, so we can just return them by pointer.
	RayTracingInstance.InstanceTransformsView = CachedRayTracingInstanceTransforms;
	RayTracingInstance.NumTransforms = CachedRayTracingInstanceTransforms.Num();

	RayTracingInstance.MaterialsView = CachedRayTracingMaterials;

	if (CachedRayTracingInstanceMaskAndFlags.Mask == 0)
	{
		CachedRayTracingInstanceMaskAndFlags = BuildRayTracingInstanceMaskAndFlags(CachedRayTracingMaterials);
	}

	RayTracingInstance.Mask = CachedRayTracingInstanceMaskAndFlags.Mask;
	RayTracingInstance.bForceOpaque = CachedRayTracingInstanceMaskAndFlags.bForceOpaque;
}
#endif

const FCardRepresentationData* FSceneProxy::GetMeshCardRepresentation() const
{
	return CardRepresentationData;
}

void FSceneProxy::GetDistancefieldAtlasData(FBox& LocalVolumeBounds, FVector2D& OutDistanceMinMax, FIntVector& OutBlockMin, FIntVector& OutBlockSize, bool& bOutBuiltAsIfTwoSided, float& SelfShadowBias, bool& bOutThrottled) const
{
	if (DistanceFieldData)
	{
		LocalVolumeBounds = DistanceFieldData->LocalBoundingBox;
		OutDistanceMinMax = DistanceFieldData->DistanceMinMax;
		OutBlockMin = DistanceFieldData->VolumeTexture.GetAllocationMin();
		OutBlockSize = DistanceFieldData->VolumeTexture.GetAllocationSize();
		bOutBuiltAsIfTwoSided = DistanceFieldData->bBuiltAsIfTwoSided;
		SelfShadowBias = DistanceFieldSelfShadowBias;
		bOutThrottled = DistanceFieldData->VolumeTexture.Throttled();
	}
	else
	{
		LocalVolumeBounds = FBox(ForceInit);
		OutDistanceMinMax = FVector2D(0, 0);
		OutBlockMin = FIntVector(-1, -1, -1);
		OutBlockSize = FIntVector(0, 0, 0);
		bOutBuiltAsIfTwoSided = false;
		SelfShadowBias = 0;
		bOutThrottled = false;
	}
}

void FSceneProxy::GetDistancefieldInstanceData(TArray<FMatrix>& ObjectLocalToWorldTransforms) const
{
	if (DistanceFieldData)
	{
		const TArray<FPrimitiveInstance>* PrimitiveInstances = GetPrimitiveInstances();
		if (PrimitiveInstances)
		{
			for (int32 InstanceIndex = 0; InstanceIndex < PrimitiveInstances->Num(); ++InstanceIndex)
			{
				// FPrimitiveInstance LocalToWorld is actually InstanceToWorld
				const FMatrix& InstanceToWorld = (*PrimitiveInstances)[InstanceIndex].LocalToWorld;
				ObjectLocalToWorldTransforms.Add(InstanceToWorld);
			}
		}
		else
		{
			ObjectLocalToWorldTransforms.Add(GetLocalToWorld());
		}
	}
}

bool FSceneProxy::HasDistanceFieldRepresentation() const
{
	return CastsDynamicShadow() && AffectsDistanceFieldLighting() && DistanceFieldData && DistanceFieldData->VolumeTexture.IsValidDistanceFieldVolume();
}

int32 FSceneProxy::GetLightMapCoordinateIndex() const
{
	const int32 LightMapCoordinateIndex = StaticMesh != nullptr ? StaticMesh->GetLightMapCoordinateIndex() : INDEX_NONE;
	return LightMapCoordinateIndex;
}

bool FSceneProxy::IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const
{
	bDrawSimpleCollision = bDrawComplexCollision = false;

	const bool bInCollisionView = EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn;

#if NANITE_ENABLE_DEBUG_RENDERING
	// If in a 'collision view' and collision is enabled
	if (bInCollisionView && IsCollisionEnabled())
	{
		// See if we have a response to the interested channel
		bool bHasResponse = EngineShowFlags.CollisionPawn && CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore;
		bHasResponse |= EngineShowFlags.CollisionVisibility && CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore;

		if (bHasResponse)
		{
			// Visibility uses complex and pawn uses simple. However, if UseSimpleAsComplex or UseComplexAsSimple is used we need to adjust accordingly
			bDrawComplexCollision = (EngineShowFlags.CollisionVisibility && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex) || (EngineShowFlags.CollisionPawn && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
			bDrawSimpleCollision = (EngineShowFlags.CollisionPawn && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple) || (EngineShowFlags.CollisionVisibility && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseSimpleAsComplex);
		}
	}
#endif
	return bInCollisionView;
}

uint32 FSceneProxy::GetMemoryFootprint() const
{
	return sizeof( *this ) + GetAllocatedSize();
}

void FGlobalResources::InitRHI()
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE_BYTAG(Nanite);
		VertexFactory = new FVertexFactory(ERHIFeatureLevel::SM5);
		VertexFactory->InitResource();
	}
}

void FGlobalResources::ReleaseRHI()
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE_BYTAG(Nanite);

		MainPassBuffers.CandidateNodesAndClustersBuffer.SafeRelease();
		PostPassBuffers.CandidateNodesAndClustersBuffer.SafeRelease();

		MainPassBuffers.StatsRasterizeArgsSWHWBuffer.SafeRelease();
		PostPassBuffers.StatsRasterizeArgsSWHWBuffer.SafeRelease();

		StatsBuffer.SafeRelease();

#if NANITE_USE_SCRATCH_BUFFERS
		PrimaryVisibleClustersBuffer.SafeRelease();
		ScratchVisibleClustersBuffer.SafeRelease();
		ScratchOccludedInstancesBuffer.SafeRelease();
#endif

		delete VertexFactory;
		VertexFactory = nullptr;
	}
}

void FGlobalResources::Update(FRDGBuilder& GraphBuilder)
{
	check(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

#if NANITE_USE_SCRATCH_BUFFERS
	// Any buffer may be released from the pool, so check each individually not just one of them.
	if (!PrimaryVisibleClustersBuffer.IsValid() || 
		!ScratchVisibleClustersBuffer.IsValid())
	{
		FRDGBufferDesc VisibleClustersBufferDesc = FRDGBufferDesc::CreateStructuredDesc(4, 3 * GetMaxVisibleClusters()); // uint3 per cluster
		VisibleClustersBufferDesc.Usage = EBufferUsageFlags(VisibleClustersBufferDesc.Usage | BUF_ByteAddressBuffer);

		// Allocate scratch buffers (TODO: RDG should support external non-RDG buffers).
		// Can't do this in InitRHI as RHICmdList doesn't have a valid context yet.

		if (!PrimaryVisibleClustersBuffer.IsValid())
		{
			GetPooledFreeBuffer(GraphBuilder.RHICmdList, VisibleClustersBufferDesc, PrimaryVisibleClustersBuffer, TEXT("Nanite.VisibleClustersSWHW"));
		}

		if (!ScratchVisibleClustersBuffer.IsValid())
		{
			GetPooledFreeBuffer(GraphBuilder.RHICmdList, VisibleClustersBufferDesc, ScratchVisibleClustersBuffer, TEXT("Nanite.VisibleClustersSWHW"));
		}

		check(PrimaryVisibleClustersBuffer.IsValid());
		check(ScratchVisibleClustersBuffer.IsValid());
	}
	if(!StructureBufferStride8.IsValid())
	{
		FRDGBufferDesc StructureBufferStride8Desc = FRDGBufferDesc::CreateStructuredDesc(8, 1);
		GetPooledFreeBuffer(GraphBuilder.RHICmdList, StructureBufferStride8Desc, StructureBufferStride8, TEXT("Nanite.StructureBufferStride8"));
		check(StructureBufferStride8.IsValid());
	}
#endif
}

uint32 FGlobalResources::GetMaxCandidateClusters()
{
	checkf(GNaniteMaxCandidateClusters <= MAX_CLUSTERS, TEXT("r.Nanite.MaxCandidateClusters must be <= MAX_CLUSTERS"));
	return GNaniteMaxCandidateClusters;
}

uint32 FGlobalResources::GetMaxVisibleClusters()
{
	checkf(GNaniteMaxVisibleClusters <= MAX_CLUSTERS, TEXT("r.Nanite.MaxVisibleClusters must be <= MAX_CLUSTERS"));
	return GNaniteMaxVisibleClusters;
}

uint32 FGlobalResources::GetMaxNodes()
{
	return MAX_NODES;
}

TGlobalResource< FGlobalResources > GGlobalResources;

} // namespace Nanite
