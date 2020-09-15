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

DEFINE_GPU_STAT(NaniteStreaming);

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

int32 GNaniteMaxInstanceCount = 1048576;
FAutoConsoleVariableRef CVarNaniteMaxInstanceCount(
	TEXT("r.Nanite.MaxInstanceCount"),
	GNaniteMaxInstanceCount,
	TEXT("Maximum number of Nanite instances in the scene."),
	ECVF_ReadOnly
);

namespace Nanite
{

static_assert(sizeof(FPackedTriCluster) == NUM_PACKED_CLUSTER_FLOAT4S * 16, "NUM_PACKED_CLUSTER_FLOAT4S out of sync with sizeof(FPackedTriCluster)");

FArchive& operator<<(FArchive& Ar, FPackedHierarchyNode& Node)
{
	for (uint32 i = 0; i < 64; i++)
	{
		Ar << Node.LODBounds[ i ];
		Ar << Node.Bounds[ i ];
		Ar << Node.Misc[ i ].MinLODError_MaxParentLODError;
		Ar << Node.Misc[ i ].ChildStartReference;
		Ar << Node.Misc[ i ].ResourcePageIndex_NumPages_GroupPartSize;
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

void FResources::Serialize(FArchive& Ar, UObject* Owner)
{
	LLM_SCOPE(ELLMTag::Nanite);

	// Note: this is all derived data, native versioning is not needed, but be sure to bump NANITE_DERIVEDDATA_VER when modifying!

	FStripDataFlags StripFlags( Ar, 0 );
	if( !StripFlags.IsDataStrippedForServer() )
	{
		Ar << RootClusterPage;
		StreamableClusterPages.Serialize(Ar, Owner, 0);
		Ar << HierarchyNodes;
		Ar << PageStreamingStates;
		Ar << PageDependencies;
		Ar << bLZCompressed;
	}
}

void FResources::DecompressPages()
{
	if (!bLZCompressed)
		return;

	// Decompress root and streaming pages
	const uint32 NumPages = PageStreamingStates.Num();
	
	uint32 NewSizes[2] = {};
	TArray<uint8> OldRootData = RootClusterPage;

	uint8* StreamingDataPtr = (uint8*)StreamableClusterPages.Lock(LOCK_READ_WRITE);
	TArray<uint8> OldStreamingData(StreamingDataPtr, StreamableClusterPages.GetBulkDataSize());

	// Calculate new root and streaming buffer sizes
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const bool bIsRootPage = PageIndex < NUM_ROOT_PAGES;
		const FPageStreamingState& State = PageStreamingStates[PageIndex];
		const TArray<uint8>& OldData = bIsRootPage ? OldRootData : OldStreamingData;
		const FFixupChunk& FixupChunk = *(const FFixupChunk*)(OldData.GetData() + State.BulkOffset);
		NewSizes[bIsRootPage] += FixupChunk.GetSize() + State.PageUncompressedSize;
	}

	StreamingDataPtr = (uint8*)StreamableClusterPages.Realloc(NewSizes[0]);
	RootClusterPage.SetNumUninitialized(NewSizes[1]);

	// Decompress data
	uint32 UncompressedOffsets[2] = {};
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const bool bIsRootPage = PageIndex < NUM_ROOT_PAGES;
		const TArray<uint8>& OldData = bIsRootPage ? OldRootData : OldStreamingData;

		FPageStreamingState& State = PageStreamingStates[PageIndex];
		const FFixupChunk& FixupChunk = *(const FFixupChunk*)(OldData.GetData() + State.BulkOffset);
		const uint32 FixupChunkSize = FixupChunk.GetSize();

		uint8* DstPtr = bIsRootPage ? RootClusterPage.GetData() : StreamingDataPtr;
		FMemory::Memcpy(DstPtr + UncompressedOffsets[bIsRootPage], &FixupChunk, FixupChunkSize);

		verify(FCompression::UncompressMemory(NAME_LZ4, DstPtr + UncompressedOffsets[bIsRootPage] + FixupChunkSize, State.PageUncompressedSize, OldData.GetData() + State.BulkOffset + FixupChunkSize, State.BulkSize - FixupChunkSize));
		State.BulkSize = FixupChunkSize + State.PageUncompressedSize;
		State.BulkOffset = UncompressedOffsets[bIsRootPage];
		UncompressedOffsets[bIsRootPage] += State.BulkSize;
	}
	check(UncompressedOffsets[0] == NewSizes[0]);
	check(UncompressedOffsets[1] == NewSizes[1]);

	StreamableClusterPages.Unlock();
	StreamableClusterPages.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
	bLZCompressed = false;
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
		LLM_SCOPE(ELLMTag::Nanite);

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
IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FVertexFactory, "/Engine/Private/Nanite/NaniteVertexFactory.ush", true, false, false, false, false, false, true, true);

SIZE_T FSceneProxyBase::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FSceneProxy::FSceneProxy(UStaticMeshComponent* Component)
: FSceneProxyBase(Component)
, MeshInfo(Component)
, Resources(&Component->GetStaticMesh()->RenderData->NaniteResources)
, RenderData(Component->GetStaticMesh()->RenderData.Get())
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
	LLM_SCOPE(ELLMTag::Nanite);

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
	bVFRequiresPrimitiveUniformBuffer = true;

	// Indicates if 1 or more materials contain settings not supported by Nanite.
	bHasMaterialErrors = false;

	// Check if the assigned material can be rendered in Nanite. If not, default.
	const bool IsRenderable = Nanite::FSceneProxy::IsNaniteRenderable(MaterialRelevance);

	if (!IsRenderable)
	{
		UE_LOG(LogStaticMesh, Warning, TEXT("Nanite rendering was chosen for rendering mesh with materials that are not supported. Using default instead."));
		bHasMaterialErrors = true;
	}

	const auto& MeshSections = RenderData->LODResources[0].Sections;
	const int32 MeshSectionCount = MeshSections.Num();

	MaterialSections.Reserve(MeshSectionCount);

	for (const auto& MeshSection : MeshSections)
	{
		if (MeshSection.MaterialIndex == INDEX_NONE)
		{
			continue;
		}

		if (!MaterialSections.IsValidIndex(MeshSection.MaterialIndex))
		{
			MaterialSections.SetNumZeroed(MeshSection.MaterialIndex + 1);
		}

		FMaterialSection& Section = MaterialSections[MeshSection.MaterialIndex];
		if (!Section.Material)
		{
			UMaterialInterface* Material = Component->GetMaterial(MeshSection.MaterialIndex);
			Section.Material = Material;
		}

		const bool bInvalidMaterial = !Section.Material || Section.Material->GetBlendMode() != BLEND_Opaque;
		if (bInvalidMaterial)
		{
			bHasMaterialErrors = true;
		}

		const bool bForceDefaultMaterial = !!FORCE_NANITE_DEFAULT_MATERIAL || bHasMaterialErrors;
		if (bForceDefaultMaterial)
		{
			Section.Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		// Should never be null here
		check(Section.Material != nullptr);

		// Should always be opaque blend mode here.
		check(Section.Material->GetBlendMode() == BLEND_Opaque);
	}

	// Copy the pointer to the volume data, async building of the data may modify the one on FStaticMeshLODResources while we are rendering
	DistanceFieldData = RenderData->LODResources[0].DistanceFieldData;
	CardRepresentationData = RenderData->LODResources[0].CardRepresentationData;

	Instances.SetNumZeroed(1);
	FPrimitiveInstance& Instance = Instances[0];
	Instance.PrimitiveId = ~uint32(0);
	Instance.InstanceToLocal.SetIdentity();
	Instance.LocalToInstance.SetIdentity();
	Instance.LocalToWorld.SetIdentity();
	Instance.WorldToLocal.SetIdentity();
	Instance.RenderBounds = Component->GetStaticMesh()->GetBounds();
	Instance.LocalBounds = Instance.RenderBounds;
	Instance.NaniteInfo.HierarchyOffset = Resources->HierarchyOffset;
	Instance.NaniteInfo.RuntimeResourceID = Resources->RuntimeResourceID;
}

FSceneProxy::FSceneProxy(UInstancedStaticMeshComponent* Component)
: FSceneProxy(static_cast<UStaticMeshComponent*>(Component))
{
	LLM_SCOPE(ELLMTag::Nanite);

	Instances.SetNumZeroed(Component->GetInstanceCount());
	for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); ++InstanceIndex)
	{
		FTransform InstanceTransform;
		Component->GetInstanceTransform(InstanceIndex, InstanceTransform);

		FPrimitiveInstance& Instance = Instances[InstanceIndex];
		Instance.PrimitiveId = ~uint32(0);
		Instance.InstanceToLocal = InstanceTransform.ToMatrixWithScale();
		Instance.LocalToInstance = Instance.LocalToWorld.Inverse();
		Instance.LocalToWorld = Instance.InstanceToLocal;
		Instance.WorldToLocal = Instance.LocalToInstance;
		Instance.RenderBounds = Component->GetStaticMesh()->GetBounds();
		Instance.LocalBounds = Instance.RenderBounds.TransformBy(Instance.InstanceToLocal);
		Instance.NaniteInfo.HierarchyOffset = Resources->HierarchyOffset;
		Instance.NaniteInfo.RuntimeResourceID = Resources->RuntimeResourceID;
	}
}

FSceneProxy::FSceneProxy(UHierarchicalInstancedStaticMeshComponent* Component)
: FSceneProxy(static_cast<UInstancedStaticMeshComponent*>(Component))
{
}

FPrimitiveViewRelevance FSceneProxy::GetViewRelevance( const FSceneView* View ) const
{
	LLM_SCOPE(ELLMTag::Nanite);

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
		Result.bVelocityRelevance = IsMovable();
	}
	else
	{
	#if WITH_EDITOR
		//only check these in the editor
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
		Result.bVelocityRelevance = Result.bOpaque && Result.bRenderInMainPass && IsMovable();
	}

	return Result;
}

#if WITH_EDITOR

HHitProxy* FSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	LLM_SCOPE(ELLMTag::Nanite);

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
	LLM_SCOPE(ELLMTag::Nanite);

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

	LLM_SCOPE(ELLMTag::Nanite);

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

	LLM_SCOPE(ELLMTag::Nanite);
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
					if (StaticMesh->NavCollision && StaticMesh->NavCollision->IsDynamicObstacle())
					{
						// Draw the static mesh's body setup (simple collision)
						FTransform GeomTransform(GetLocalToWorld());
						FColor NavCollisionColor = FColor(118,84,255,255);
						StaticMesh->NavCollision->DrawSimpleGeom(Collector.GetPDI(ViewIndex), GeomTransform, GetSelectionColor(NavCollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true));
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

const FCardRepresentationData* FSceneProxy::GetMeshCardRepresentation() const
{
	return CardRepresentationData;
}

void FSceneProxy::GetDistancefieldAtlasData(FBox& LocalVolumeBounds, FVector2D& OutDistanceMinMax, FIntVector& OutBlockMin, FIntVector& OutBlockSize, bool& bOutBuiltAsIfTwoSided, bool& bMeshWasPlane, float& SelfShadowBias, bool& bOutThrottled) const
{
	if (DistanceFieldData)
	{
		LocalVolumeBounds = DistanceFieldData->LocalBoundingBox;
		OutDistanceMinMax = DistanceFieldData->DistanceMinMax;
		OutBlockMin = DistanceFieldData->VolumeTexture.GetAllocationMin();
		OutBlockSize = DistanceFieldData->VolumeTexture.GetAllocationSize();
		bOutBuiltAsIfTwoSided = DistanceFieldData->bBuiltAsIfTwoSided;
		bMeshWasPlane = DistanceFieldData->bMeshWasPlane;
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
		bMeshWasPlane = false;
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
		LLM_SCOPE(ELLMTag::Nanite);
		VertexFactory = new FVertexFactory(ERHIFeatureLevel::SM5);
		VertexFactory->InitResource();
	}
}

void FGlobalResources::ReleaseRHI()
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE(ELLMTag::Nanite);

		MainPassBuffers.NodesBuffer.SafeRelease();
		PostPassBuffers.NodesBuffer.SafeRelease();

		MainPassBuffers.StatsRasterizeArgsSWHWBuffer.SafeRelease();
		PostPassBuffers.StatsRasterizeArgsSWHWBuffer.SafeRelease();

		MainPassBuffers.StatsCandidateClustersArgsBuffer.SafeRelease();
		PostPassBuffers.StatsCandidateClustersArgsBuffer.SafeRelease();

		StatsBuffer.SafeRelease();

#if NANITE_USE_SCRATCH_BUFFERS
		PrimaryVisibleClustersBuffer.SafeRelease();
		ScratchVisibleClustersBuffer.SafeRelease();

		MainPassBuffers.ScratchCandidateClustersBuffer.SafeRelease();
		PostPassBuffers.ScratchCandidateClustersBuffer.SafeRelease();
#endif

		delete VertexFactory;
		VertexFactory = nullptr;
	}
}

void FGlobalResources::Update(FRHICommandListImmediate& RHICmdList)
{
	check(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

#if NANITE_USE_SCRATCH_BUFFERS
	// Any buffer may be released from the pool, so check each individually not just one of them.
	if (!PrimaryVisibleClustersBuffer.IsValid() || 
		!ScratchVisibleClustersBuffer.IsValid() ||
		!ScratchOccludedInstancesBuffer.IsValid() ||
		!MainPassBuffers.ScratchCandidateClustersBuffer.IsValid() ||
		!PostPassBuffers.ScratchCandidateClustersBuffer.IsValid())
	{
		FRDGBufferDesc ClustersBufferDesc = FRDGBufferDesc::CreateStructuredDesc(4, 3 * GetMaxClusters()); // uint3 per cluster
		ClustersBufferDesc.Usage = EBufferUsageFlags(ClustersBufferDesc.Usage | BUF_ByteAddressBuffer);

		// Allocate scratch buffers (TODO: RDG should support external non-RDG buffers).
		// Can't do this in InitRHI as RHICmdList doesn't have a valid context yet.

		if (!PrimaryVisibleClustersBuffer.IsValid())
		{
			GetPooledFreeBuffer(RHICmdList, ClustersBufferDesc, PrimaryVisibleClustersBuffer, TEXT("VisibleClustersSWHW"));
		}

		if (!ScratchVisibleClustersBuffer.IsValid())
		{
			GetPooledFreeBuffer(RHICmdList, ClustersBufferDesc, ScratchVisibleClustersBuffer, TEXT("VisibleClustersSWHW"));
		}

		if (!ScratchOccludedInstancesBuffer.IsValid())
		{
			GetPooledFreeBuffer(RHICmdList, FRDGBufferDesc::CreateStructuredDesc(4, GetMaxInstances()), ScratchOccludedInstancesBuffer, TEXT("OccludedInstances"));
		}

		if (!MainPassBuffers.ScratchCandidateClustersBuffer.IsValid())
		{
			GetPooledFreeBuffer(RHICmdList, ClustersBufferDesc, MainPassBuffers.ScratchCandidateClustersBuffer, TEXT("MainPass.CandidateClusters"));
		}

		if (!PostPassBuffers.ScratchCandidateClustersBuffer.IsValid())
		{
			GetPooledFreeBuffer(RHICmdList, ClustersBufferDesc, PostPassBuffers.ScratchCandidateClustersBuffer, TEXT("PostPass.CandidateClusters"));
		}

		check(PrimaryVisibleClustersBuffer.IsValid());
		check(ScratchVisibleClustersBuffer.IsValid());
		check(MainPassBuffers.ScratchCandidateClustersBuffer.IsValid());
		check(PostPassBuffers.ScratchCandidateClustersBuffer.IsValid());
	}
	if(!StructureBufferStride8.IsValid())
	{
		FRDGBufferDesc StructureBufferStride8Desc = FRDGBufferDesc::CreateStructuredDesc(8, 1);
		GetPooledFreeBuffer(RHICmdList, StructureBufferStride8Desc, StructureBufferStride8, TEXT("StructureBufferStride8"));
		check(StructureBufferStride8.IsValid());
	}
#endif
}

uint32 FGlobalResources::GetMaxInstances()
{
	// We don't want to allocate 16mil instances here (wasting memory), so pull a smaller value
	// from config and verify it's within range of MAX_INSTANCES.
	checkf(GNaniteMaxInstanceCount <= MAX_INSTANCES, TEXT("r.Nanite.MaxInstanceCount must be <= MAX_INSTANCES"));
	return GNaniteMaxInstanceCount;
}

uint32 FGlobalResources::GetMaxClusters()
{
	return MAX_CLUSTERS;
}

uint32 FGlobalResources::GetMaxNodes()
{
	return MAX_NODES;
}

TGlobalResource< FGlobalResources > GGlobalResources;

} // namespace Nanite
