// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PrimitiveSceneInfo.cpp: Primitive scene info implementation.
=============================================================================*/

#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "Components/PrimitiveComponent.h"
#include "SceneManagement.h"
#include "SceneCore.h"
#include "VelocityRendering.h"
#include "ScenePrivate.h"
#include "RendererModule.h"
#include "HAL/LowLevelMemTracker.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "VT/RuntimeVirtualTextureSceneProxy.h"
#include "VT/VirtualTextureSystem.h"
#include "GPUScene.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/ExternalProfiler.h"

/** An implementation of FStaticPrimitiveDrawInterface that stores the drawn elements for the rendering thread to use. */
class FBatchingSPDI : public FStaticPrimitiveDrawInterface
{
public:

	// Constructor.
	FBatchingSPDI(FPrimitiveSceneInfo* InPrimitiveSceneInfo):
		PrimitiveSceneInfo(InPrimitiveSceneInfo)
	{}

	// FStaticPrimitiveDrawInterface.
	virtual void SetHitProxy(HHitProxy* HitProxy) final override
	{
		CurrentHitProxy = HitProxy;

		if(HitProxy)
		{
			// Only use static scene primitive hit proxies in the editor.
			if(GIsEditor)
			{
				// Keep a reference to the hit proxy from the FPrimitiveSceneInfo, to ensure it isn't deleted while the static mesh still
				// uses its id.
				PrimitiveSceneInfo->HitProxies.Add(HitProxy);
			}
		}
	}

	virtual void ReserveMemoryForMeshes(int32 MeshNum)
	{
		if (PrimitiveSceneInfo->StaticMeshRelevances.GetSlack() < MeshNum)
		{
			PrimitiveSceneInfo->StaticMeshRelevances.Reserve(PrimitiveSceneInfo->StaticMeshRelevances.Max() + MeshNum);
		}
		if (PrimitiveSceneInfo->StaticMeshes.GetSlack() < MeshNum)
		{
			PrimitiveSceneInfo->StaticMeshes.Reserve(PrimitiveSceneInfo->StaticMeshes.Max() + MeshNum);
		}
	}

	virtual void DrawMesh(const FMeshBatch& Mesh, float ScreenSize) final override
	{
		if (Mesh.HasAnyDrawCalls())
		{
			checkSlow(IsInParallelRenderingThread());

			FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveSceneInfo->Proxy;
			const ERHIFeatureLevel::Type FeatureLevel = PrimitiveSceneInfo->Scene->GetFeatureLevel();

			if (!Mesh.Validate(PrimitiveSceneProxy, FeatureLevel))
			{
				return;
			}

			FStaticMeshBatch* StaticMesh = new(PrimitiveSceneInfo->StaticMeshes) FStaticMeshBatch(
				PrimitiveSceneInfo,
				Mesh,
				CurrentHitProxy ? CurrentHitProxy->Id : FHitProxyId()
			);

			StaticMesh->PreparePrimitiveUniformBuffer(PrimitiveSceneProxy, FeatureLevel);
			// Volumetric self shadow mesh commands need to be generated every frame, as they depend on single frame uniform buffers with self shadow data.
			const bool bSupportsCachingMeshDrawCommands = SupportsCachingMeshDrawCommands(*StaticMesh, FeatureLevel) && !PrimitiveSceneProxy->CastsVolumetricTranslucentShadow();

			const FMaterial& Material = Mesh.MaterialRenderProxy->GetIncompleteMaterialWithFallback(FeatureLevel);
			bool bUseSkyMaterial = Material.IsSky();
			bool bUseSingleLayerWaterMaterial = Material.GetShadingModels().HasShadingModel(MSM_SingleLayerWater);
			bool bUseAnisotropy = Material.GetShadingModels().HasAnyShadingModel({MSM_DefaultLit, MSM_ClearCoat}) && Material.MaterialUsesAnisotropy_RenderThread();
			FStaticMeshBatchRelevance* StaticMeshRelevance = new(PrimitiveSceneInfo->StaticMeshRelevances) FStaticMeshBatchRelevance(
				*StaticMesh, 
				ScreenSize, 
				bSupportsCachingMeshDrawCommands,
				bUseSkyMaterial,
				bUseSingleLayerWaterMaterial,
				bUseAnisotropy,
				FeatureLevel
				);
		}
	}

private:
	FPrimitiveSceneInfo* PrimitiveSceneInfo;
	TRefCountPtr<HHitProxy> CurrentHitProxy;
};

FPrimitiveFlagsCompact::FPrimitiveFlagsCompact(const FPrimitiveSceneProxy* Proxy)
	: bCastDynamicShadow(Proxy->CastsDynamicShadow())
	, bStaticLighting(Proxy->HasStaticLighting())
	, bCastStaticShadow(Proxy->CastsStaticShadow())
{}

FPrimitiveSceneInfoCompact::FPrimitiveSceneInfoCompact(FPrimitiveSceneInfo* InPrimitiveSceneInfo) :
	PrimitiveFlagsCompact(InPrimitiveSceneInfo->Proxy)
{
	PrimitiveSceneInfo = InPrimitiveSceneInfo;
	Proxy = PrimitiveSceneInfo->Proxy;
	Bounds = PrimitiveSceneInfo->Proxy->GetBounds();
	MinDrawDistance = PrimitiveSceneInfo->Proxy->GetMinDrawDistance();
	MaxDrawDistance = PrimitiveSceneInfo->Proxy->GetMaxDrawDistance();

	VisibilityId = PrimitiveSceneInfo->Proxy->GetVisibilityId();
}

FPrimitiveSceneInfo::FPrimitiveSceneInfo(UPrimitiveComponent* InComponent,FScene* InScene):
	Proxy(InComponent->SceneProxy),
	PrimitiveComponentId(InComponent->ComponentId),
	OwnerLastRenderTime(FActorLastRenderTime::GetPtr(InComponent->GetOwner())),
	IndirectLightingCacheAllocation(NULL),
	CachedPlanarReflectionProxy(NULL),
	CachedReflectionCaptureProxy(NULL),
	bNeedsCachedReflectionCaptureUpdate(true),
	DefaultDynamicHitProxy(NULL),
	LightList(NULL),
	LastRenderTime(-FLT_MAX),
	Scene(InScene),
	NumMobileMovablePointLights(0),
	bShouldRenderInMainPass(InComponent->SceneProxy->ShouldRenderInMainPass()),
	bVisibleInRealTimeSkyCapture(InComponent->SceneProxy->IsVisibleInRealTimeSkyCaptures()),
#if RHI_RAYTRACING
	bDrawInGame(Proxy->IsDrawnInGame()),
	bIsVisibleInReflectionCaptures(InComponent->SceneProxy->IsVisibleInReflectionCaptures()),
	bIsRayTracingRelevant(InComponent->SceneProxy->IsRayTracingRelevant()),
	bIsRayTracingStaticRelevant(InComponent->SceneProxy->IsRayTracingStaticRelevant()),
	bIsVisibleInRayTracing(InComponent->SceneProxy->IsVisibleInRayTracing()),
#endif
	PackedIndex(INDEX_NONE),
	ComponentForDebuggingOnly(InComponent),
	bNeedsStaticMeshUpdateWithoutVisibilityCheck(false),
	bNeedsUniformBufferUpdate(false),
	bIndirectLightingCacheBufferDirty(false),
	bRegisteredVirtualTextureProducerCallback(false),
	LightmapDataOffset(INDEX_NONE),
	NumLightmapDataEntries(0)
{
	check(ComponentForDebuggingOnly);
	check(PrimitiveComponentId.IsValid());
	check(Proxy);

	const UPrimitiveComponent* SearchParentComponent = InComponent->GetLightingAttachmentRoot();

	if (SearchParentComponent && SearchParentComponent != InComponent)
	{
		LightingAttachmentRoot = SearchParentComponent->ComponentId;
	}

	// Only create hit proxies in the Editor as that's where they are used.
	if (GIsEditor)
	{
		// Create a dynamic hit proxy for the primitive. 
		DefaultDynamicHitProxy = Proxy->CreateHitProxies(InComponent,HitProxies);
		if( DefaultDynamicHitProxy )
		{
			DefaultDynamicHitProxyId = DefaultDynamicHitProxy->Id;
		}
	}

	// set LOD parent info if exists
	UPrimitiveComponent* LODParent = InComponent->GetLODParentPrimitive();
	if (LODParent)
	{
		LODParentComponentId = LODParent->ComponentId;
	}

	FMemory::Memzero(CachedReflectionCaptureProxies);

#if RHI_RAYTRACING
	RayTracingGeometries = InComponent->SceneProxy->MoveRayTracingGeometries();
#endif
}

FPrimitiveSceneInfo::~FPrimitiveSceneInfo()
{
	check(!OctreeId.IsValidId());
	for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; PassIndex++)
	{
		check(StaticMeshCommandInfos.Num() == 0);
	}
}

#if RHI_RAYTRACING
FRHIRayTracingGeometry* FPrimitiveSceneInfo::GetStaticRayTracingGeometryInstance(int LodLevel) const
{
	if (RayTracingGeometries.Num() > LodLevel)
	{
		// TODO: Select different LOD, when build is still pending for this LOD?
		if (RayTracingGeometries[LodLevel]->HasPendingBuildRequest())
		{
			RayTracingGeometries[LodLevel]->BoostBuildPriority();
			return nullptr;
		}
		else
		{
			return RayTracingGeometries[LodLevel]->RayTracingGeometryRHI;
		}
	}
	else
	{
		return nullptr;
	}
}
#endif

void FPrimitiveSceneInfo::CacheMeshDrawCommands(FRHICommandListImmediate& RHICmdList, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos)
{
	//@todo - only need material uniform buffers to be created since we are going to cache pointers to them
	// Any updates (after initial creation) don't need to be forced here
	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_CacheMeshDrawCommands, FColor::Emerald);

	QUICK_SCOPE_CYCLE_COUNTER(STAT_CacheMeshDrawCommands);
	FMemMark Mark(FMemStack::Get());

	static constexpr int BATCH_SIZE = 64;
	const int NumBatches = (SceneInfos.Num() + BATCH_SIZE - 1) / BATCH_SIZE;

	auto DoWorkLambda = [Scene, SceneInfos](int32 Index)
	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_CacheMeshDrawCommand, FColor::Green);

		struct FMeshInfoAndIndex
		{
			int32 InfoIndex;
			int32 MeshIndex;
		};

		TArray<FMeshInfoAndIndex, TMemStackAllocator<>> MeshBatches;
		MeshBatches.Reserve(3 * BATCH_SIZE);

		int LocalNum = FMath::Min((Index * BATCH_SIZE) + BATCH_SIZE, SceneInfos.Num());
		for (int LocalIndex = (Index * BATCH_SIZE); LocalIndex < LocalNum; LocalIndex++)
		{
			FPrimitiveSceneInfo* SceneInfo = SceneInfos[LocalIndex];
			check(SceneInfo->StaticMeshCommandInfos.Num() == 0);
			SceneInfo->StaticMeshCommandInfos.AddDefaulted(EMeshPass::Num * SceneInfo->StaticMeshes.Num());
			FPrimitiveSceneProxy* SceneProxy = SceneInfo->Proxy;

			// Volumetric self shadow mesh commands need to be generated every frame, as they depend on single frame uniform buffers with self shadow data.
			if (!SceneProxy->CastsVolumetricTranslucentShadow())
			{
				for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
				{
					FStaticMeshBatch& Mesh = SceneInfo->StaticMeshes[MeshIndex];
					if (SupportsCachingMeshDrawCommands(Mesh))
					{
						MeshBatches.Add(FMeshInfoAndIndex{ LocalIndex, MeshIndex });
					}
				}
			}
		}

		for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; PassIndex++)
		{
			const EShadingPath ShadingPath = Scene->GetShadingPath();
			EMeshPass::Type PassType = (EMeshPass::Type)PassIndex;

			if ((FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::CachedMeshCommands) != EMeshPassFlags::None)
			{
				FCachedMeshDrawCommandInfo CommandInfo(PassType);

				FCriticalSection& CachedMeshDrawCommandLock = Scene->CachedMeshDrawCommandLock[PassType];
				FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[PassType];
				FStateBucketMap& CachedMeshDrawCommandStateBuckets = Scene->CachedMeshDrawCommandStateBuckets[PassType];
				FCachedPassMeshDrawListContext CachedPassMeshDrawListContext(CommandInfo, CachedMeshDrawCommandLock, SceneDrawList, CachedMeshDrawCommandStateBuckets, *Scene);

				PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
				FMeshPassProcessor* PassMeshProcessor = CreateFunction(Scene, nullptr, &CachedPassMeshDrawListContext);

				if (PassMeshProcessor != nullptr)
				{
					for (const FMeshInfoAndIndex& MeshAndInfo : MeshBatches)
					{
						FPrimitiveSceneInfo* SceneInfo = SceneInfos[MeshAndInfo.InfoIndex];
						FStaticMeshBatch& Mesh = SceneInfo->StaticMeshes[MeshAndInfo.MeshIndex];
						
						CommandInfo = FCachedMeshDrawCommandInfo(PassType);
						FStaticMeshBatchRelevance& MeshRelevance = SceneInfo->StaticMeshRelevances[MeshAndInfo.MeshIndex];

						check(!MeshRelevance.CommandInfosMask.Get(PassType));

						uint64 BatchElementMask = ~0ull;
						PassMeshProcessor->AddMeshBatch(Mesh, BatchElementMask, SceneInfo->Proxy);

						if (CommandInfo.CommandIndex != -1 || CommandInfo.StateBucketId != -1)
						{
							static_assert(sizeof(MeshRelevance.CommandInfosMask) * 8 >= EMeshPass::Num, "CommandInfosMask is too small to contain all mesh passes.");
							MeshRelevance.CommandInfosMask.Set(PassType);
							MeshRelevance.CommandInfosBase++;

							int CommandInfoIndex = MeshAndInfo.MeshIndex * EMeshPass::Num + PassType;
							check(SceneInfo->StaticMeshCommandInfos[CommandInfoIndex].MeshPass == EMeshPass::Num);
							SceneInfo->StaticMeshCommandInfos[CommandInfoIndex] = CommandInfo;
#if DO_GUARD_SLOW
							if (ShadingPath == EShadingPath::Deferred)
							{
								FScopeLock Lock(&CachedMeshDrawCommandLock);
								const FMeshDrawCommand* MeshDrawCommand = CommandInfo.StateBucketId >= 0
									? &Scene->CachedMeshDrawCommandStateBuckets[PassType].GetByElementId(CommandInfo.StateBucketId).Key
									: &SceneDrawList.MeshDrawCommands[CommandInfo.CommandIndex];

								ensureMsgf(MeshDrawCommand->VertexStreams.GetAllocatedSize() == 0, TEXT("Cached Mesh Draw command overflows VertexStreams.  VertexStream inline size should be tweaked."));

								if (PassType == EMeshPass::BasePass || PassType == EMeshPass::DepthPass || PassType == EMeshPass::CSMShadowDepth)
								{
									TArray<EShaderFrequency, TInlineAllocator<SF_NumFrequencies>> ShaderFrequencies;
									MeshDrawCommand->ShaderBindings.GetShaderFrequencies(ShaderFrequencies);
								
									int32 DataOffset = 0;
									for (int32 i = 0; i < ShaderFrequencies.Num(); i++)
									{
										FMeshDrawSingleShaderBindings SingleShaderBindings = const_cast<FMeshDrawCommand*>(MeshDrawCommand)->ShaderBindings.GetSingleShaderBindings(ShaderFrequencies[i], DataOffset);
										static int32 LogCount = 0;
										if (SingleShaderBindings.GetParameterMapInfo().LooseParameterBuffers.Num() != 0 && ((LogCount++ % 1000) == 0))
										{
											UE_LOG(LogRenderer, Warning, TEXT("Cached Mesh Draw command uses loose parameters.  This causes overhead and will break dynamic instancing, potentially reducing performance further.  Use Uniform Buffers instead."));
										}
										ensureMsgf(SingleShaderBindings.GetParameterMapInfo().SRVs.Num() == 0, TEXT("Cached Mesh Draw command uses individual SRVs.  This will break dynamic instancing in performance critical pass.  Use Uniform Buffers instead."));
										ensureMsgf(SingleShaderBindings.GetParameterMapInfo().TextureSamplers.Num() == 0, TEXT("Cached Mesh Draw command uses individual Texture Samplers.  This will break dynamic instancing in performance critical pass.  Use Uniform Buffers instead."));
									}
								}
							}
#endif
						}
					}
					PassMeshProcessor->~FMeshPassProcessor();
				}
			}
		}

		for (int LocalIndex = (Index * BATCH_SIZE); LocalIndex < LocalNum; LocalIndex++)
		{
			FPrimitiveSceneInfo* SceneInfo = SceneInfos[LocalIndex];	
			int PrefixSum = 0;
			for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
			{
				FStaticMeshBatchRelevance& MeshRelevance = SceneInfo->StaticMeshRelevances[MeshIndex];
				if (MeshRelevance.CommandInfosBase > 0)
				{
					EMeshPass::Type PassType = EMeshPass::DepthPass;
					int NewPrefixSum = PrefixSum;
					for (;;)
					{
						PassType = MeshRelevance.CommandInfosMask.SkipEmpty(PassType);
						if (PassType == EMeshPass::Num)
						{
							break;
						}

						int CommandInfoIndex = MeshIndex * EMeshPass::Num + PassType;
						checkSlow(CommandInfoIndex >= NewPrefixSum);
						SceneInfo->StaticMeshCommandInfos[NewPrefixSum] = SceneInfo->StaticMeshCommandInfos[CommandInfoIndex];
						NewPrefixSum++;
						PassType = EMeshPass::Type(PassType + 1);
					}

#if DO_GUARD_SLOW
					int NumBits = MeshRelevance.CommandInfosMask.GetNum();
					check(PrefixSum + NumBits == NewPrefixSum);
					int LastPass = -1;
					for (int32 TestIndex = PrefixSum; TestIndex < NewPrefixSum; TestIndex++)
					{
						int MeshPass = SceneInfo->StaticMeshCommandInfos[TestIndex].MeshPass;
						check(MeshPass > LastPass);
						LastPass = MeshPass;
					}
#endif
					MeshRelevance.CommandInfosBase = PrefixSum;
					PrefixSum = NewPrefixSum;
				}
			}
			SceneInfo->StaticMeshCommandInfos.SetNum(PrefixSum, true);
		}
	};

	if (FApp::ShouldUseThreadingForPerformance())
	{
		ParallelForTemplate(NumBatches, DoWorkLambda, EParallelForFlags::PumpRenderingThread);
	}
	else
	{
		for (int Idx = 0; Idx < NumBatches; Idx++)
		{
			DoWorkLambda(Idx);
		}
	}

	if (!FParallelMeshDrawCommandPass::IsOnDemandShaderCreationEnabled())
	{
		FGraphicsMinimalPipelineStateId::InitializePersistentIds();
	}

#if RHI_RAYTRACING
	if (IsRayTracingEnabled() && !(Scene->World->WorldType == EWorldType::EditorPreview || Scene->World->WorldType == EWorldType::GamePreview))
	{
		checkf(RHISupportsMultithreadedShaderCreation(GMaxRHIShaderPlatform), TEXT("Raytracing code needs the ability to create shaders from task threads."));

		FCachedRayTracingMeshCommandContext CommandContext(Scene->CachedRayTracingMeshCommands);
		FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
		FRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, Scene, nullptr, PassDrawRenderState);

		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			if (SceneInfo->RayTracingGeometries.Num() > 0 && SceneInfo->StaticMeshes.Num() > 0)
			{
				int32 MaxLOD = -1;
				for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
				{
					FStaticMeshBatch& Mesh = SceneInfo->StaticMeshes[MeshIndex];
					MaxLOD = MaxLOD < Mesh.LODIndex ? Mesh.LODIndex : MaxLOD;
				}

				SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD.Empty(MaxLOD + 1);
				SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD.AddDefaulted(MaxLOD + 1);

				SceneInfo->CachedRayTracingMeshCommandsHashPerLOD.Empty(MaxLOD + 1);
				SceneInfo->CachedRayTracingMeshCommandsHashPerLOD.AddZeroed(MaxLOD + 1);

				for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
				{
					FStaticMeshBatch& Mesh = SceneInfo->StaticMeshes[MeshIndex];

					RayTracingMeshProcessor.AddMeshBatch(Mesh, ~0ull, SceneInfo->Proxy);

					if (CommandContext.CommandIndex >= 0)
					{
						uint64& Hash = SceneInfo->CachedRayTracingMeshCommandsHashPerLOD[Mesh.LODIndex];
						Hash <<= 1;
						Hash ^= Scene->CachedRayTracingMeshCommands.RayTracingMeshCommands[CommandContext.CommandIndex].ShaderBindings.GetDynamicInstancingHash();

						SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD[Mesh.LODIndex].Add(CommandContext.CommandIndex);
						CommandContext.CommandIndex = -1;
					}
				}
			}
		}
	}
#endif
}

void FPrimitiveSceneInfo::RemoveCachedMeshDrawCommands()
{
	checkSlow(IsInRenderingThread());

	for (int32 CommandIndex = 0; CommandIndex < StaticMeshCommandInfos.Num(); ++CommandIndex)
	{
		const FCachedMeshDrawCommandInfo& CachedCommand = StaticMeshCommandInfos[CommandIndex];

		if (CachedCommand.StateBucketId != INDEX_NONE)
		{
			EMeshPass::Type PassIndex = CachedCommand.MeshPass;
			FScopeLock Lock(&Scene->CachedMeshDrawCommandLock[PassIndex]);

			FGraphicsMinimalPipelineStateId CachedPipelineId = Scene->CachedMeshDrawCommandStateBuckets[PassIndex].GetByElementId(CachedCommand.StateBucketId).Key.CachedPipelineId;

			FMeshDrawCommandCount& StateBucketCount = Scene->CachedMeshDrawCommandStateBuckets[PassIndex].GetByElementId(CachedCommand.StateBucketId).Value;
			check(StateBucketCount.Num > 0);
			StateBucketCount.Num--;
			if (StateBucketCount.Num == 0)
			{
				Scene->CachedMeshDrawCommandStateBuckets[PassIndex].RemoveByElementId(CachedCommand.StateBucketId);
			}

			FGraphicsMinimalPipelineStateId::RemovePersistentId(CachedPipelineId);
		}
		else if (CachedCommand.CommandIndex >= 0)
		{
			FCachedPassMeshDrawList& PassDrawList = Scene->CachedDrawLists[CachedCommand.MeshPass];
			FGraphicsMinimalPipelineStateId CachedPipelineId = PassDrawList.MeshDrawCommands[CachedCommand.CommandIndex].CachedPipelineId;
			
			PassDrawList.MeshDrawCommands.RemoveAt(CachedCommand.CommandIndex);
			FGraphicsMinimalPipelineStateId::RemovePersistentId(CachedPipelineId);

			// Track the lowest index that might be free for faster AddAtLowestFreeIndex
			PassDrawList.LowestFreeIndexSearchStart = FMath::Min(PassDrawList.LowestFreeIndexSearchStart, CachedCommand.CommandIndex);
		}

	}

	for (int32 MeshIndex = 0; MeshIndex < StaticMeshRelevances.Num(); ++MeshIndex)
	{
		FStaticMeshBatchRelevance& MeshRelevance = StaticMeshRelevances[MeshIndex];

		MeshRelevance.CommandInfosMask.Reset();
	}

	StaticMeshCommandInfos.Empty();

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		for (auto& CachedRayTracingMeshCommandIndices : CachedRayTracingMeshCommandIndicesPerLOD)
		{
			for (auto CommandIndex : CachedRayTracingMeshCommandIndices)
			{
				if (CommandIndex >= 0)
				{
					Scene->CachedRayTracingMeshCommands.RayTracingMeshCommands.RemoveAt(CommandIndex);
				}
			}
		}

		CachedRayTracingMeshCommandIndicesPerLOD.Empty();

		CachedRayTracingMeshCommandsHashPerLOD.Empty();
	}
#endif
}

void FPrimitiveSceneInfo::AddStaticMeshes(FRHICommandListImmediate& RHICmdList, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos, bool bAddToStaticDrawLists)
{
	LLM_SCOPE(ELLMTag::StaticMesh);

	{
		ParallelForTemplate(SceneInfos.Num(), [Scene, &SceneInfos](int32 Index)
		{
			SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddStaticMeshes_DrawStaticElements, FColor::Magenta);
			FPrimitiveSceneInfo* SceneInfo = SceneInfos[Index];
			// Cache the primitive's static mesh elements.
			FBatchingSPDI BatchingSPDI(SceneInfo);
			BatchingSPDI.SetHitProxy(SceneInfo->DefaultDynamicHitProxy);
			SceneInfo->Proxy->DrawStaticElements(&BatchingSPDI);
			SceneInfo->StaticMeshes.Shrink();
			SceneInfo->StaticMeshRelevances.Shrink();

			check(SceneInfo->StaticMeshRelevances.Num() == SceneInfo->StaticMeshes.Num());
		});
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddStaticMeshes_UpdateSceneArrays, FColor::Blue);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
			{
				FStaticMeshBatchRelevance& MeshRelevance = SceneInfo->StaticMeshRelevances[MeshIndex];
				FStaticMeshBatch& Mesh = SceneInfo->StaticMeshes[MeshIndex];

				// Add the static mesh to the scene's static mesh list.
				FSparseArrayAllocationInfo SceneArrayAllocation = Scene->StaticMeshes.AddUninitialized();
				Scene->StaticMeshes[SceneArrayAllocation.Index] = &Mesh;
				Mesh.Id = SceneArrayAllocation.Index;
				MeshRelevance.Id = SceneArrayAllocation.Index;
			}
		}
	}

	if (bAddToStaticDrawLists)
	{
		CacheMeshDrawCommands(RHICmdList, Scene, SceneInfos);
	}
}

static void OnVirtualTextureDestroyed(const FVirtualTextureProducerHandle& InHandle, void* Baton)
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo = static_cast<FPrimitiveSceneInfo*>(Baton);

	// Update the main uniform buffer
	PrimitiveSceneInfo->UpdateStaticLightingBuffer();

	// Also need to update lightmap data inside GPUScene, if that's enabled
	AddPrimitiveToUpdateGPU(*PrimitiveSceneInfo->Scene, PrimitiveSceneInfo->GetIndex());
}

static void GetRuntimeVirtualTextureLODRange(TArray<class FStaticMeshBatchRelevance> const& MeshRelevances, int8& OutMinLOD, int8& OutMaxLOD)
{
	OutMinLOD = MAX_int8;
	OutMaxLOD = 0;

	for (int32 MeshIndex = 0; MeshIndex < MeshRelevances.Num(); ++MeshIndex)
	{
		const FStaticMeshBatchRelevance& MeshRelevance = MeshRelevances[MeshIndex];
		if (MeshRelevance.bRenderToVirtualTexture)
		{
			OutMinLOD = FMath::Min(OutMinLOD, MeshRelevance.LODIndex);
			OutMaxLOD = FMath::Max(OutMaxLOD, MeshRelevance.LODIndex);
		}
	}

	check(OutMinLOD <= OutMaxLOD);
}

int32 FPrimitiveSceneInfo::UpdateStaticLightingBuffer()
{
	checkSlow(IsInRenderingThread());

	if (bRegisteredVirtualTextureProducerCallback)
	{
		// Remove any previous VT callbacks
		FVirtualTextureSystem::Get().RemoveAllProducerDestroyedCallbacks(this);
		bRegisteredVirtualTextureProducerCallback = false;
	}

	FPrimitiveSceneProxy::FLCIArray LCIs;
	Proxy->GetLCIs(LCIs);
	for (int32 i = 0; i < LCIs.Num(); ++i)
	{
		FLightCacheInterface* LCI = LCIs[i];

		if (LCI)
		{
			LCI->CreatePrecomputedLightingUniformBuffer_RenderingThread(Scene->GetFeatureLevel());

			// If lightmap is using virtual texture, need to set a callback to update our uniform buffers if VT is destroyed,
			// since we cache VT parameters inside these uniform buffers
			FVirtualTextureProducerHandle VTProducerHandle;
			if (LCI->GetVirtualTextureLightmapProducer(Scene->GetFeatureLevel(), VTProducerHandle))
			{
				FVirtualTextureSystem::Get().AddProducerDestroyedCallback(VTProducerHandle, &OnVirtualTextureDestroyed, this);
				bRegisteredVirtualTextureProducerCallback = true;
			}
		}
	}

	return LCIs.Num();
}

void FPrimitiveSceneInfo::AddToScene(FRHICommandListImmediate& RHICmdList, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos, bool bUpdateStaticDrawLists, bool bAddToStaticDrawLists, bool bAsyncCreateLPIs)
{
	check(IsInRenderingThread());

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_IndirectLightingCacheUniformBuffer, FColor::Turquoise);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			FPrimitiveSceneProxy* Proxy = SceneInfo->Proxy;
			// Create an indirect lighting cache uniform buffer if we attaching a primitive that may require it, as it may be stored inside a cached mesh command.
			if (IsIndirectLightingCacheAllowed(Scene->GetFeatureLevel())
				&& Proxy->WillEverBeLit()
				&& ((Proxy->HasStaticLighting() && Proxy->NeedsUnbuiltPreviewLighting()) || (Proxy->IsMovable() && Proxy->GetIndirectLightingCacheQuality() != ILCQ_Off) || Proxy->GetLightmapType() == ELightmapType::ForceVolumetric))
			{
				if (!SceneInfo->IndirectLightingCacheUniformBuffer)
				{
					FIndirectLightingCacheUniformParameters Parameters;

					GetIndirectLightingCacheParameters(
						Scene->GetFeatureLevel(),
						Parameters,
						nullptr,
						nullptr,
						FVector(0.0f, 0.0f, 0.0f),
						0,
						nullptr);

					SceneInfo->IndirectLightingCacheUniformBuffer = TUniformBufferRef<FIndirectLightingCacheUniformParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
				}
			}
		}
	}
	
	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_IndirectLightingCacheAllocation, FColor::Orange);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			FPrimitiveSceneProxy* Proxy = SceneInfo->Proxy;
			// If we are attaching a primitive that should be statically lit but has unbuilt lighting,
			// Allocate space in the indirect lighting cache so that it can be used for previewing indirect lighting
			if (Proxy->HasStaticLighting()
				&& Proxy->NeedsUnbuiltPreviewLighting()
				&& IsIndirectLightingCacheAllowed(Scene->GetFeatureLevel()))
			{
				FIndirectLightingCacheAllocation* PrimitiveAllocation = Scene->IndirectLightingCache.FindPrimitiveAllocation(SceneInfo->PrimitiveComponentId);

				if (PrimitiveAllocation)
				{
					SceneInfo->IndirectLightingCacheAllocation = PrimitiveAllocation;
					PrimitiveAllocation->SetDirty();
				}
				else
				{
					PrimitiveAllocation = Scene->IndirectLightingCache.AllocatePrimitive(SceneInfo, true);
					PrimitiveAllocation->SetDirty();
					SceneInfo->IndirectLightingCacheAllocation = PrimitiveAllocation;
				}
			}
			SceneInfo->MarkIndirectLightingCacheBufferDirty();
		}
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_LightmapDataOffset, FColor::Green);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			const bool bAllowStaticLighting = FReadOnlyCVARCache::Get().bAllowStaticLighting;
			if (bAllowStaticLighting)
			{
				SceneInfo->NumLightmapDataEntries = SceneInfo->UpdateStaticLightingBuffer();
				if (SceneInfo->NumLightmapDataEntries > 0 && UseGPUScene(GMaxRHIShaderPlatform, Scene->GetFeatureLevel()))
				{
					SceneInfo->LightmapDataOffset = Scene->GPUScene.LightmapDataAllocator.Allocate(SceneInfo->NumLightmapDataEntries);
				}
			}
		}
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_ReflectionCaptures, FColor::Yellow);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			// Cache the nearest reflection proxy if needed
			if (SceneInfo->NeedsReflectionCaptureUpdate())
			{
				SceneInfo->CacheReflectionCaptures();
			}
		}
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_AddStaticMeshes, FColor::Magenta);
		if (bUpdateStaticDrawLists)
		{
			AddStaticMeshes(RHICmdList, Scene, SceneInfos, bAddToStaticDrawLists);
		}
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_AddToPrimitiveOctree, FColor::Red);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			// create potential storage for our compact info
			FPrimitiveSceneInfoCompact CompactPrimitiveSceneInfo(SceneInfo);

			// Add the primitive to the octree.
			check(!SceneInfo->OctreeId.IsValidId());
			Scene->PrimitiveOctree.AddElement(CompactPrimitiveSceneInfo);
			check(SceneInfo->OctreeId.IsValidId());
		}
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_UpdateBounds, FColor::Cyan);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			FPrimitiveSceneProxy* Proxy = SceneInfo->Proxy;
			int32 PackedIndex = SceneInfo->PackedIndex;

			if (Proxy->CastsDynamicIndirectShadow())
			{
				Scene->DynamicIndirectCasterPrimitives.Add(SceneInfo);
			}

			Scene->PrimitiveSceneProxies[PackedIndex] = Proxy;
			Scene->PrimitiveTransforms[PackedIndex] = Proxy->GetLocalToWorld();

			// Set bounds.
			FPrimitiveBounds& PrimitiveBounds = Scene->PrimitiveBounds[PackedIndex];
			FBoxSphereBounds BoxSphereBounds = Proxy->GetBounds();
			PrimitiveBounds.BoxSphereBounds = BoxSphereBounds;
			PrimitiveBounds.MinDrawDistanceSq = FMath::Square(Proxy->GetMinDrawDistance());
			PrimitiveBounds.MaxDrawDistance = Proxy->GetMaxDrawDistance();
			PrimitiveBounds.MaxCullDistance = PrimitiveBounds.MaxDrawDistance;

			Scene->PrimitiveFlagsCompact[PackedIndex] = FPrimitiveFlagsCompact(Proxy);

			// Store precomputed visibility ID.
			int32 VisibilityBitIndex = Proxy->GetVisibilityId();
			FPrimitiveVisibilityId& VisibilityId = Scene->PrimitiveVisibilityIds[PackedIndex];
			VisibilityId.ByteIndex = VisibilityBitIndex / 8;
			VisibilityId.BitMask = (1 << (VisibilityBitIndex & 0x7));

			// Store occlusion flags.
			uint8 OcclusionFlags = EOcclusionFlags::None;
			if (Proxy->CanBeOccluded())
			{
				OcclusionFlags |= EOcclusionFlags::CanBeOccluded;
			}
			if (Proxy->HasSubprimitiveOcclusionQueries())
			{
				OcclusionFlags |= EOcclusionFlags::HasSubprimitiveQueries;
			}
			if (Proxy->AllowApproximateOcclusion()
				// Allow approximate occlusion if attached, even if the parent does not have bLightAttachmentsAsGroup enabled
				|| SceneInfo->LightingAttachmentRoot.IsValid())
			{
				OcclusionFlags |= EOcclusionFlags::AllowApproximateOcclusion;
			}
			if (VisibilityBitIndex >= 0)
			{
				OcclusionFlags |= EOcclusionFlags::HasPrecomputedVisibility;
			}
			Scene->PrimitiveOcclusionFlags[PackedIndex] = OcclusionFlags;
	
			// Store occlusion bounds.
			FBoxSphereBounds OcclusionBounds = BoxSphereBounds;
			if (Proxy->HasCustomOcclusionBounds())
			{
				OcclusionBounds = Proxy->GetCustomOcclusionBounds();
			}
			OcclusionBounds.BoxExtent.X = OcclusionBounds.BoxExtent.X + OCCLUSION_SLOP;
			OcclusionBounds.BoxExtent.Y = OcclusionBounds.BoxExtent.Y + OCCLUSION_SLOP;
			OcclusionBounds.BoxExtent.Z = OcclusionBounds.BoxExtent.Z + OCCLUSION_SLOP;
			OcclusionBounds.SphereRadius = OcclusionBounds.SphereRadius + OCCLUSION_SLOP;
			Scene->PrimitiveOcclusionBounds[PackedIndex] = OcclusionBounds;

			// Store the component.
			Scene->PrimitiveComponentIds[PackedIndex] = SceneInfo->PrimitiveComponentId;
		}
	}

	{
		SCOPED_NAMED_EVENT(FPrimitiveSceneInfo_AddToScene_UpdateVirtualTexture, FColor::Emerald);
		for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
		{
			FPrimitiveSceneProxy* Proxy = SceneInfo->Proxy;
			// Store the runtime virtual texture flags.
			SceneInfo->UpdateRuntimeVirtualTextureFlags();
			Scene->PrimitiveVirtualTextureFlags[SceneInfo->PackedIndex] = SceneInfo->RuntimeVirtualTextureFlags;

			// Store the runtime virtual texture Lod info.
			if (SceneInfo->RuntimeVirtualTextureFlags.bRenderToVirtualTexture)
			{
				int8 MinLod, MaxLod;
				GetRuntimeVirtualTextureLODRange(SceneInfo->StaticMeshRelevances, MinLod, MaxLod);

				FPrimitiveVirtualTextureLodInfo& LodInfo = Scene->PrimitiveVirtualTextureLod[SceneInfo->PackedIndex];
				LodInfo.MinLod = FMath::Clamp((int32)MinLod, 0, 15);
				LodInfo.MaxLod = FMath::Clamp((int32)MaxLod, 0, 15);
				LodInfo.LodBias = FMath::Clamp(Proxy->GetVirtualTextureLodBias() + FPrimitiveVirtualTextureLodInfo::LodBiasOffset, 0, 15);
				LodInfo.CullMethod = Proxy->GetVirtualTextureMinCoverage() == 0 ? 0 : 1;
				LodInfo.CullValue = LodInfo.CullMethod == 0 ? Proxy->GetVirtualTextureCullMips() : Proxy->GetVirtualTextureMinCoverage();
			}
		}
	}

	// Find lights that affect the primitive in the light octree.
	for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
	{
		Scene->CreateLightPrimitiveInteractionsForPrimitive(SceneInfo, bAsyncCreateLPIs);

		FPrimitiveSceneProxy* Proxy = SceneInfo->Proxy;
		INC_MEMORY_STAT_BY(STAT_PrimitiveInfoMemory, sizeof(*SceneInfo) + SceneInfo->StaticMeshes.GetAllocatedSize() + SceneInfo->StaticMeshRelevances.GetAllocatedSize() + Proxy->GetMemoryFootprint());
	}
}

void FPrimitiveSceneInfo::RemoveStaticMeshes()
{
	// Remove static meshes from the scene.
	StaticMeshes.Empty();
	StaticMeshRelevances.Empty();
	RemoveCachedMeshDrawCommands();
}

void FPrimitiveSceneInfo::RemoveFromScene(bool bUpdateStaticDrawLists)
{
	check(IsInRenderingThread());

	// implicit linked list. The destruction will update this "head" pointer to the next item in the list.
	while(LightList)
	{
		FLightPrimitiveInteraction::Destroy(LightList);
	}
	
	// Remove the primitive from the octree.
	check(OctreeId.IsValidId());
	check(Scene->PrimitiveOctree.GetElementById(OctreeId).PrimitiveSceneInfo == this);
	Scene->PrimitiveOctree.RemoveElement(OctreeId);
	OctreeId = FOctreeElementId2();

	if (LightmapDataOffset != INDEX_NONE && UseGPUScene(GMaxRHIShaderPlatform, Scene->GetFeatureLevel()))
	{
		Scene->GPUScene.LightmapDataAllocator.Free(LightmapDataOffset, NumLightmapDataEntries);
	}
	
	if (Proxy->CastsDynamicIndirectShadow())
	{
		Scene->DynamicIndirectCasterPrimitives.RemoveSingleSwap(this);
	}

	IndirectLightingCacheAllocation = NULL;

	if (Proxy->IsOftenMoving())
	{
		MarkIndirectLightingCacheBufferDirty();
	}

	DEC_MEMORY_STAT_BY(STAT_PrimitiveInfoMemory, sizeof(*this) + StaticMeshes.GetAllocatedSize() + StaticMeshRelevances.GetAllocatedSize() + Proxy->GetMemoryFootprint());

	if (bUpdateStaticDrawLists)
	{
		if (IsIndexValid()) // PackedIndex
		{
			Scene->PrimitivesNeedingStaticMeshUpdate[PackedIndex] = false;
		}

		if (bNeedsStaticMeshUpdateWithoutVisibilityCheck)
		{
			Scene->PrimitivesNeedingStaticMeshUpdateWithoutVisibilityCheck.Remove(this);

			bNeedsStaticMeshUpdateWithoutVisibilityCheck = false;
		}

		// IndirectLightingCacheUniformBuffer may be cached inside cached mesh draw commands, so we 
		// can't delete it unless we also update cached mesh command.
		IndirectLightingCacheUniformBuffer.SafeRelease();

		RemoveStaticMeshes();
	}

	if (bRegisteredVirtualTextureProducerCallback)
	{
		FVirtualTextureSystem::Get().RemoveAllProducerDestroyedCallbacks(this);
		bRegisteredVirtualTextureProducerCallback = false;
	}
}

void FPrimitiveSceneInfo::UpdateRuntimeVirtualTextureFlags()
{
	RuntimeVirtualTextureFlags.bRenderToVirtualTexture = false;
	RuntimeVirtualTextureFlags.RuntimeVirtualTextureMask = 0;

	if (Proxy->WritesVirtualTexture())
	{
		if (StaticMeshes.Num() == 0)
		{
			UE_LOG(LogRenderer, Warning, TEXT("Rendering a primitive in a runtime virtual texture implies that there is a mesh to render. Please disable this option on primitive component : %s"), *Proxy->GetOwnerName().ToString());
		}
		else
		{
			RuntimeVirtualTextureFlags.bRenderToVirtualTexture = true;

			// Performance assumption: The arrays of runtime virtual textures are small (less that 5?) so that O(n^2) scan isn't expensive
			for (TSparseArray<FRuntimeVirtualTextureSceneProxy*>::TConstIterator It(Scene->RuntimeVirtualTextures); It; ++It)
			{
				int32 SceneIndex = It.GetIndex();
				if (SceneIndex < FPrimitiveVirtualTextureFlags::RuntimeVirtualTexture_BitCount)
				{
					URuntimeVirtualTexture* SceneVirtualTexture = (*It)->VirtualTexture;
					if (Proxy->WritesVirtualTexture(SceneVirtualTexture))
					{
						RuntimeVirtualTextureFlags.RuntimeVirtualTextureMask |= 1 << SceneIndex;
					}
				}
			}
		}
	}
}

bool FPrimitiveSceneInfo::NeedsUpdateStaticMeshes()
{
	return Scene->PrimitivesNeedingStaticMeshUpdate[PackedIndex];
}

void FPrimitiveSceneInfo::UpdateStaticMeshes(FRHICommandListImmediate& RHICmdList, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos, bool bReAddToDrawLists)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FPrimitiveSceneInfo_UpdateStaticMeshes);

	const bool bNeedsStaticMeshUpdate = !bReAddToDrawLists;

	for (int Index = 0; Index < SceneInfos.Num(); Index++)
	{
		FPrimitiveSceneInfo* SceneInfo = SceneInfos[Index];
		Scene->PrimitivesNeedingStaticMeshUpdate[SceneInfo->PackedIndex] = bNeedsStaticMeshUpdate;

		if (!bNeedsStaticMeshUpdate && SceneInfo->bNeedsStaticMeshUpdateWithoutVisibilityCheck)
		{
			Scene->PrimitivesNeedingStaticMeshUpdateWithoutVisibilityCheck.Remove(SceneInfo);

			SceneInfo->bNeedsStaticMeshUpdateWithoutVisibilityCheck = false;
		}

		SceneInfo->RemoveCachedMeshDrawCommands();
	}

	if (bReAddToDrawLists)
	{
		CacheMeshDrawCommands(RHICmdList, Scene, SceneInfos);
	}
}

void FPrimitiveSceneInfo::UpdateUniformBuffer(FRHICommandListImmediate& RHICmdList)
{
	checkSlow(bNeedsUniformBufferUpdate);
	bNeedsUniformBufferUpdate = false;
	Proxy->UpdateUniformBuffer();
	AddPrimitiveToUpdateGPU(*Scene, PackedIndex);
}

void FPrimitiveSceneInfo::BeginDeferredUpdateStaticMeshes()
{
	// Set a flag which causes InitViews to update the static meshes the next time the primitive is visible.
	if (IsIndexValid()) // PackedIndex
	{
		Scene->PrimitivesNeedingStaticMeshUpdate[PackedIndex] = true;
	}
}

void FPrimitiveSceneInfo::BeginDeferredUpdateStaticMeshesWithoutVisibilityCheck()
{
	if (NeedsUpdateStaticMeshes() && !bNeedsStaticMeshUpdateWithoutVisibilityCheck)
	{
		bNeedsStaticMeshUpdateWithoutVisibilityCheck = true;

		Scene->PrimitivesNeedingStaticMeshUpdateWithoutVisibilityCheck.Add(this);
	}
}

void FPrimitiveSceneInfo::FlushRuntimeVirtualTexture()
{
	if (RuntimeVirtualTextureFlags.bRenderToVirtualTexture)
	{
		uint32 RuntimeVirtualTextureIndex = 0;
		uint32 Mask = RuntimeVirtualTextureFlags.RuntimeVirtualTextureMask;
		while (Mask != 0)
		{
			if (Mask & 1)
			{
				Scene->RuntimeVirtualTextures[RuntimeVirtualTextureIndex]->Dirty(Proxy->GetBounds());
			}
			Mask >>= 1;
			RuntimeVirtualTextureIndex++;
		}
	}
}

void FPrimitiveSceneInfo::LinkLODParentComponent()
{
	if (LODParentComponentId.IsValid())
	{
		Scene->SceneLODHierarchy.AddChildNode(LODParentComponentId, this);
	}
}

void FPrimitiveSceneInfo::UnlinkLODParentComponent()
{
	if(LODParentComponentId.IsValid())
	{
		Scene->SceneLODHierarchy.RemoveChildNode(LODParentComponentId, this);
	}
}

void FPrimitiveSceneInfo::LinkAttachmentGroup()
{
	// Add the primitive to its attachment group.
	if (LightingAttachmentRoot.IsValid())
	{
		FAttachmentGroupSceneInfo* AttachmentGroup = Scene->AttachmentGroups.Find(LightingAttachmentRoot);

		if (!AttachmentGroup)
		{
			// If this is the first primitive attached that uses this attachment parent, create a new attachment group.
			AttachmentGroup = &Scene->AttachmentGroups.Add(LightingAttachmentRoot, FAttachmentGroupSceneInfo());
		}

		AttachmentGroup->Primitives.Add(this);
	}
	else if (Proxy->LightAttachmentsAsGroup())
	{
		FAttachmentGroupSceneInfo* AttachmentGroup = Scene->AttachmentGroups.Find(PrimitiveComponentId);

		if (!AttachmentGroup)
		{
			// Create an empty attachment group 
			AttachmentGroup = &Scene->AttachmentGroups.Add(PrimitiveComponentId, FAttachmentGroupSceneInfo());
		}

		AttachmentGroup->ParentSceneInfo = this;
	}
}

void FPrimitiveSceneInfo::UnlinkAttachmentGroup()
{
	// Remove the primitive from its attachment group.
	if (LightingAttachmentRoot.IsValid())
	{
		FAttachmentGroupSceneInfo& AttachmentGroup = Scene->AttachmentGroups.FindChecked(LightingAttachmentRoot);
		AttachmentGroup.Primitives.RemoveSwap(this);

		if (AttachmentGroup.Primitives.Num() == 0 && AttachmentGroup.ParentSceneInfo == nullptr)
		{
			// If this was the last primitive attached that uses this attachment group and the root has left the building, free the group.
			Scene->AttachmentGroups.Remove(LightingAttachmentRoot);
		}
	}
	else if (Proxy->LightAttachmentsAsGroup())
	{
		FAttachmentGroupSceneInfo* AttachmentGroup = Scene->AttachmentGroups.Find(PrimitiveComponentId);
		
		if (AttachmentGroup)
		{
			AttachmentGroup->ParentSceneInfo = NULL;
			if (AttachmentGroup->Primitives.Num() == 0)
			{
				// If this was the owner and the group is empty, remove it (otherwise the above will remove when the last attached goes).
				Scene->AttachmentGroups.Remove(PrimitiveComponentId);
			}
		}
	}
}

void FPrimitiveSceneInfo::GatherLightingAttachmentGroupPrimitives(TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator>& OutChildSceneInfos)
{
#if ENABLE_NAN_DIAGNOSTIC
	// local function that returns full name of object
	auto GetObjectName = [](const UPrimitiveComponent* InPrimitive)->FString
	{
		return (InPrimitive) ? InPrimitive->GetFullName() : FString(TEXT("Unknown Object"));
	};

	// verify that the current object has a valid bbox before adding it
	const float& BoundsRadius = this->Proxy->GetBounds().SphereRadius;
	if (ensureMsgf(!FMath::IsNaN(BoundsRadius) && FMath::IsFinite(BoundsRadius),
		TEXT("%s had an ill-formed bbox and was skipped during shadow setup, contact DavidH."), *GetObjectName(this->ComponentForDebuggingOnly)))
	{
		OutChildSceneInfos.Add(this);
	}
	else
	{
		// return, leaving the TArray empty
		return;
	}

#else 
	// add self at the head of this queue
	OutChildSceneInfos.Add(this);
#endif

	if (!LightingAttachmentRoot.IsValid() && Proxy->LightAttachmentsAsGroup())
	{
		const FAttachmentGroupSceneInfo* AttachmentGroup = Scene->AttachmentGroups.Find(PrimitiveComponentId);

		if (AttachmentGroup)
		{
			
			for (int32 ChildIndex = 0, ChildIndexMax = AttachmentGroup->Primitives.Num(); ChildIndex < ChildIndexMax; ChildIndex++)
			{
				FPrimitiveSceneInfo* ShadowChild = AttachmentGroup->Primitives[ChildIndex];
#if ENABLE_NAN_DIAGNOSTIC
				// Only enqueue objects with valid bounds using the normality of the SphereRaduis as criteria.

				const float& ShadowChildBoundsRadius = ShadowChild->Proxy->GetBounds().SphereRadius;

				if (ensureMsgf(!FMath::IsNaN(ShadowChildBoundsRadius) && FMath::IsFinite(ShadowChildBoundsRadius),
					TEXT("%s had an ill-formed bbox and was skipped during shadow setup, contact DavidH."), *GetObjectName(ShadowChild->ComponentForDebuggingOnly)))
				{
					checkSlow(!OutChildSceneInfos.Contains(ShadowChild))
				    OutChildSceneInfos.Add(ShadowChild);
				}
#else
				// enqueue all objects.
				checkSlow(!OutChildSceneInfos.Contains(ShadowChild))
			    OutChildSceneInfos.Add(ShadowChild);
#endif
			}
		}
	}
}

void FPrimitiveSceneInfo::GatherLightingAttachmentGroupPrimitives(TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator>& OutChildSceneInfos) const
{
	OutChildSceneInfos.Add(this);

	if (!LightingAttachmentRoot.IsValid() && Proxy->LightAttachmentsAsGroup())
	{
		const FAttachmentGroupSceneInfo* AttachmentGroup = Scene->AttachmentGroups.Find(PrimitiveComponentId);

		if (AttachmentGroup)
		{
			for (int32 ChildIndex = 0, ChildIndexMax = AttachmentGroup->Primitives.Num(); ChildIndex < ChildIndexMax; ChildIndex++)
			{
				const FPrimitiveSceneInfo* ShadowChild = AttachmentGroup->Primitives[ChildIndex];

				checkSlow(!OutChildSceneInfos.Contains(ShadowChild))
			    OutChildSceneInfos.Add(ShadowChild);
			}
		}
	}
}

FBoxSphereBounds FPrimitiveSceneInfo::GetAttachmentGroupBounds() const
{
	FBoxSphereBounds Bounds = Proxy->GetBounds();

	if (!LightingAttachmentRoot.IsValid() && Proxy->LightAttachmentsAsGroup())
	{
		const FAttachmentGroupSceneInfo* AttachmentGroup = Scene->AttachmentGroups.Find(PrimitiveComponentId);

		if (AttachmentGroup)
		{
			for (int32 ChildIndex = 0; ChildIndex < AttachmentGroup->Primitives.Num(); ChildIndex++)
			{
				FPrimitiveSceneInfo* AttachmentChild = AttachmentGroup->Primitives[ChildIndex];
				Bounds = Bounds + AttachmentChild->Proxy->GetBounds();
			}
		}
	}

	return Bounds;
}

uint32 FPrimitiveSceneInfo::GetMemoryFootprint()
{
	return( sizeof( *this ) + HitProxies.GetAllocatedSize() + StaticMeshes.GetAllocatedSize() + StaticMeshRelevances.GetAllocatedSize() );
}

void FPrimitiveSceneInfo::ApplyWorldOffset(FVector InOffset)
{
	Proxy->ApplyWorldOffset(InOffset);
}

void FPrimitiveSceneInfo::UpdateIndirectLightingCacheBuffer(
	const FIndirectLightingCache* LightingCache,
	const FIndirectLightingCacheAllocation* LightingAllocation,
	FVector VolumetricLightmapLookupPosition,
	uint32 SceneFrameNumber,
	FVolumetricLightmapSceneData* VolumetricLightmapSceneData)
{
	FIndirectLightingCacheUniformParameters Parameters;

	GetIndirectLightingCacheParameters(
		Scene->GetFeatureLevel(),
		Parameters,
		LightingCache,
		LightingAllocation,
		VolumetricLightmapLookupPosition,
		SceneFrameNumber,
		VolumetricLightmapSceneData);

	if (IndirectLightingCacheUniformBuffer)
	{
		IndirectLightingCacheUniformBuffer.UpdateUniformBufferImmediate(Parameters);
	}
}

void FPrimitiveSceneInfo::UpdateIndirectLightingCacheBuffer()
{
	if (bIndirectLightingCacheBufferDirty)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateIndirectLightingCacheBuffer);

		if (Scene->GetFeatureLevel() < ERHIFeatureLevel::SM5
			&& Scene->VolumetricLightmapSceneData.HasData()
			&& (Proxy->IsMovable() || Proxy->NeedsUnbuiltPreviewLighting() || Proxy->GetLightmapType() == ELightmapType::ForceVolumetric)
			&& Proxy->WillEverBeLit())
		{
			UpdateIndirectLightingCacheBuffer(
				nullptr, 
				nullptr,
				Proxy->GetBounds().Origin,
				Scene->GetFrameNumber(),
				&Scene->VolumetricLightmapSceneData);
		}
		// The update is invalid if the lighting cache allocation was not in a functional state.
		else if (IndirectLightingCacheAllocation && (Scene->IndirectLightingCache.IsInitialized() && IndirectLightingCacheAllocation->bHasEverUpdatedSingleSample))
		{
			UpdateIndirectLightingCacheBuffer(
				&Scene->IndirectLightingCache,
				IndirectLightingCacheAllocation,
				FVector(0, 0, 0),
				0,
				nullptr);
		}
		else
		{
			// Fallback to the global empty buffer parameters
			UpdateIndirectLightingCacheBuffer(nullptr, nullptr, FVector(0.0f, 0.0f, 0.0f), 0, nullptr);
		}

		bIndirectLightingCacheBufferDirty = false;
	}
}

void FPrimitiveSceneInfo::GetStaticMeshesLODRange(int8& OutMinLOD, int8& OutMaxLOD) const
{
	OutMinLOD = MAX_int8;
	OutMaxLOD = 0;

	for (int32 MeshIndex = 0; MeshIndex < StaticMeshRelevances.Num(); ++MeshIndex)
	{
		const FStaticMeshBatchRelevance& MeshRelevance = StaticMeshRelevances[MeshIndex];
		OutMinLOD = FMath::Min(OutMinLOD, MeshRelevance.LODIndex);
		OutMaxLOD = FMath::Max(OutMaxLOD, MeshRelevance.LODIndex);
	}
}

const FMeshBatch* FPrimitiveSceneInfo::GetMeshBatch(int8 InLODIndex) const
{
	if (StaticMeshes.IsValidIndex(InLODIndex))
	{
		return &StaticMeshes[InLODIndex];
	}

	return nullptr;
}

bool FPrimitiveSceneInfo::NeedsReflectionCaptureUpdate() const
{
	return bNeedsCachedReflectionCaptureUpdate && 
		// For mobile, the per-object reflection is used for everything
		(Scene->GetShadingPath() == EShadingPath::Mobile || IsForwardShadingEnabled(Scene->GetShaderPlatform()));
}

void FPrimitiveSceneInfo::CacheReflectionCaptures()
{
	// do not use Scene->PrimitiveBounds here, as it may be not initialized yet
	FBoxSphereBounds BoxSphereBounds = Proxy->GetBounds(); 
	
	CachedReflectionCaptureProxy = Scene->FindClosestReflectionCapture(BoxSphereBounds.Origin);
	CachedPlanarReflectionProxy = Scene->FindClosestPlanarReflection(BoxSphereBounds);
	if (Scene->GetShadingPath() == EShadingPath::Mobile)
	{
		// mobile HQ reflections
		Scene->FindClosestReflectionCaptures(BoxSphereBounds.Origin, CachedReflectionCaptureProxies);
	}
	
	bNeedsCachedReflectionCaptureUpdate = false;
}

void FPrimitiveSceneInfo::RemoveCachedReflectionCaptures()
{
	CachedReflectionCaptureProxy = nullptr;
	CachedPlanarReflectionProxy = nullptr;
	FMemory::Memzero(CachedReflectionCaptureProxies);
	bNeedsCachedReflectionCaptureUpdate = true;
}

void FPrimitiveSceneInfo::UpdateComponentLastRenderTime(float CurrentWorldTime, bool bUpdateLastRenderTimeOnScreen) const
{
	ComponentForDebuggingOnly->LastRenderTime = CurrentWorldTime;
	if (bUpdateLastRenderTimeOnScreen)
	{
		ComponentForDebuggingOnly->LastRenderTimeOnScreen = CurrentWorldTime;
	}
	if (OwnerLastRenderTime)
	{
		*OwnerLastRenderTime = CurrentWorldTime; // Sets OwningActor->LastRenderTime
	}
}
