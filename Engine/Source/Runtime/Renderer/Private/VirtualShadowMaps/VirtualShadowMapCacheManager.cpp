// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMap.h:
=============================================================================*/
#include "VirtualShadowMapCacheManager.h"
#include "VirtualShadowMapClipmap.h"
#include "RendererModule.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "ScenePrivate.h"
#include "HAL/FileManager.h"

#include "PrimitiveSceneInfo.h"
#include "ShaderDebug.h"
#include "RendererOnScreenNotification.h"

static TAutoConsoleVariable<int32> CVarAccumulateStats(
	TEXT("r.Shadow.Virtual.AccumulateStats"),
	0,
	TEXT("AccumulateStats"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCacheVirtualSMs(
	TEXT("r.Shadow.Virtual.Cache"),
	1,
	TEXT("Turn on to enable caching"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarDrawInvalidatingBounds(
	TEXT("r.Shadow.Virtual.DrawInvalidatingBounds"),
	0,
	TEXT("Turn on debug render cache invalidating instance bounds, heat mapped by number of pages invalidated.\n")
	TEXT("   1  = Draw all bounds.\n")
	TEXT("   2  = Draw those invalidating static cached pages only\n")
	TEXT("   3  = Draw those invalidating dynamic cached pages only"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCacheVsmUseHzb(
	TEXT("r.Shadow.Virtual.Cache.InvalidateUseHZB"),
	1,
	TEXT("Enables testing HZB for Virtual Shadow Map invalidations."),
	ECVF_RenderThreadSafe);

int32 GClipmapPanning = 1;
FAutoConsoleVariableRef CVarEnableClipmapPanning(
	TEXT("r.Shadow.Virtual.Cache.ClipmapPanning"),
	GClipmapPanning,
	TEXT("Enable support for panning cached clipmap pages for directional lights."),
	ECVF_RenderThreadSafe
);

void FVirtualShadowMapCacheEntry::UpdateClipmap(
	int32 VirtualShadowMapId,
	const FMatrix &WorldToLight,
	FIntPoint PageSpaceLocation,
	float LevelRadius,
	float ViewCenterZ,
	// NOTE: ViewRadiusZ must be constant for a given clipmap level
	float ViewRadiusZ)
{
	bool bCacheValid = (CurrentVirtualShadowMapId != INDEX_NONE);
	
	if (bCacheValid && WorldToLight != Clipmap.WorldToLight)
	{
		bCacheValid = false;
		//UE_LOG(LogRenderer, Display, TEXT("Invalidated clipmap level (VSM %d) due to light movement"), VirtualShadowMapId);
	}

	if (bCacheValid && GClipmapPanning == 0)
	{
		if (PageSpaceLocation.X != PrevPageSpaceLocation.X ||
			PageSpaceLocation.Y != PrevPageSpaceLocation.Y)
		{
			bCacheValid = false;
			//UE_LOG(LogRenderer, Display, TEXT("Invalidated clipmap level (VSM %d) with page space location %d,%d (Prev %d, %d)"),
			//	VirtualShadowMapId, PageSpaceLocation.X, PageSpaceLocation.Y, PrevPageSpaceLocation.X, PrevPageSpaceLocation.Y);
		}
	}

	// Invalidate if the new Z radius strayed too close/outside the guardband of the cached shadow map
	if (bCacheValid)
	{
		float DeltaZ = FMath::Abs(ViewCenterZ - Clipmap.ViewCenterZ);
		if ((DeltaZ + LevelRadius) > 0.9f * Clipmap.ViewRadiusZ)
		{
			bCacheValid = false;
			//UE_LOG(LogRenderer, Display, TEXT("Invalidated clipmap level (VSM %d) due to depth range movement"), VirtualShadowMapId);
		}
	}

	bool bRadiusMatches = (ViewRadiusZ == Clipmap.ViewRadiusZ);

	if (bCacheValid && bRadiusMatches)
	{
		PrevVirtualShadowMapId = CurrentVirtualShadowMapId;
	}
	else
	{
		if (bCacheValid && !bRadiusMatches)
		{
			// These really should be exact by construction currently
			UE_LOG(LogRenderer, Warning, TEXT("Invalidated clipmap level (VSM %d) due to Z radius mismatch"), VirtualShadowMapId);
		}

		// New cached level
		PrevVirtualShadowMapId = INDEX_NONE;
		Clipmap.WorldToLight = WorldToLight;
		Clipmap.ViewCenterZ = ViewCenterZ;
		Clipmap.ViewRadiusZ = ViewRadiusZ;
	}
		
	PrevPageSpaceLocation = CurrentPageSpaceLocation;
	bPrevRendered = bCurrentRendered;
	
	CurrentVirtualShadowMapId = VirtualShadowMapId;
	CurrentPageSpaceLocation = PageSpaceLocation;
	bCurrentRendered = false;
}

void FVirtualShadowMapCacheEntry::UpdateLocal(int32 VirtualShadowMapId, const FWholeSceneProjectedShadowInitializer &InCacheValidKey)
{
	// Swap previous frame data over.
	PrevPageSpaceLocation = FIntPoint(0, 0);		// Not used for local lights
	PrevVirtualShadowMapId = CurrentVirtualShadowMapId;
	bPrevRendered = bCurrentRendered;

	// Check cache validity based of shadow setup
	if (!LocalCacheValidKey.IsCachedShadowValid(InCacheValidKey))
	{
		PrevVirtualShadowMapId = INDEX_NONE;
		//UE_LOG(LogRenderer, Display, TEXT("Invalidated!"));
	}
	LocalCacheValidKey = InCacheValidKey;

	CurrentVirtualShadowMapId = VirtualShadowMapId;
	CurrentPageSpaceLocation = FIntPoint(0, 0);		// Not used for local lights
	bCurrentRendered = false;
}

FVirtualShadowMapArrayCacheManager::FInvalidatingPrimitiveCollector::FInvalidatingPrimitiveCollector(FVirtualShadowMapArrayCacheManager* InVirtualShadowMapArrayCacheManager)
	: AlreadyAddedPrimitives(false, InVirtualShadowMapArrayCacheManager->Scene->Primitives.Num())
	, Scene(*InVirtualShadowMapArrayCacheManager->Scene)
	, GPUScene(InVirtualShadowMapArrayCacheManager->Scene->GPUScene)
	, Manager(*InVirtualShadowMapArrayCacheManager)
{
	// Add and clear pending invalidations enqueued on the GPU Scene from dynamic primitives added since last invalidation
	for (const FGPUScene::FInstanceRange& Range : GPUScene.DynamicPrimitiveInstancesToInvalidate)
	{
		LoadBalancer.Add(Range.InstanceSceneDataOffset, Range.NumInstanceSceneDataEntries, 0U);
#if VSM_LOG_INVALIDATIONS
		RangesStr.Appendf(TEXT("[%6d, %6d), "), Range.InstanceSceneDataOffset, Range.InstanceSceneDataOffset + Range.NumInstanceSceneDataEntries);
#endif
		TotalInstanceCount += Range.NumInstanceSceneDataEntries;
	}

	GPUScene.DynamicPrimitiveInstancesToInvalidate.Reset();
}
void FVirtualShadowMapArrayCacheManager::FInvalidatingPrimitiveCollector::Add(const FPrimitiveSceneInfo * PrimitiveSceneInfo)
{
	int32 PrimitiveID = PrimitiveSceneInfo->GetIndex();
	if (PrimitiveID >= 0
		&& !AlreadyAddedPrimitives[PrimitiveID]
		&& PrimitiveSceneInfo->GetInstanceSceneDataOffset() != INDEX_NONE
		// Don't process primitives that are still in the 'added' state because this means that they
		// have not been uploaded to the GPU yet and may be pending from a previous call to update primitive scene infos.
		&& !EnumHasAnyFlags(GPUScene.GetPrimitiveDirtyState(PrimitiveID), EPrimitiveDirtyState::Added))
	{
		AlreadyAddedPrimitives[PrimitiveID] = true;
		int32 PersistentPrimitiveIndex = PrimitiveSceneInfo->GetPersistentIndex().Index;

		const int32 NumInstanceSceneDataEntries = PrimitiveSceneInfo->GetNumInstanceSceneDataEntries();
		// Add for non-directional lights, mark for skipping clipmaps as these are handled individually below
		LoadBalancer.Add(PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries, 1U);

		// Nanite meshes need special handling because they don't get culled on CPU, thus always process invalidations for those
		const bool bIsNaniteMesh = Scene.PrimitiveFlagsCompact[PrimitiveID].bIsNaniteMesh;
		// Process directional lights, where we explicitly filter out primitives that were not rendered (and mark this fact)
		for (auto& CacheEntry : Manager.PrevCacheEntries)
		{
			TBitArray<>& RenderedPrimitives = CacheEntry.Value->RenderedPrimitives;
			if (bIsNaniteMesh || (PersistentPrimitiveIndex < RenderedPrimitives.Num() && RenderedPrimitives[PersistentPrimitiveIndex]))
			{
				if (!bIsNaniteMesh)
				{
					// Clear the record as we're wiping it out.
					RenderedPrimitives[PersistentPrimitiveIndex] = false;
				}

				// Add item for each shadow map explicitly, inflates host data but improves load balancing,
				// TODO: maybe add permutation so we can strip the loop completely.
				for (const auto& SmCacheEntry : CacheEntry.Value->ShadowMapEntries)
				{
					if (SmCacheEntry.IsValid())
					{
						// Lowest bit indicates whether to skip the clipmap loop, add 1 to ID so != 0 <==> single level processing
						LoadBalancer.Add(PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries, (uint32(SmCacheEntry->CurrentVirtualShadowMapId + 1) << 1U) | 0U);
					}
				}
			}
		}
#if VSM_LOG_INVALIDATIONS
		RangesStr.Appendf(TEXT("[%6d, %6d), "), PrimitiveSceneInfo->GetInstanceSceneDataOffset(), PrimitiveSceneInfo->GetInstanceSceneDataOffset() + NumInstanceSceneDataEntries);
#endif
		TotalInstanceCount += NumInstanceSceneDataEntries;
	}
}



FVirtualShadowMapArrayCacheManager::FVirtualShadowMapArrayCacheManager(FScene* InScene) 
	: Scene(InScene)
{
	// Handle message with status sent back from GPU
	StatusFeedbackSocket = GPUMessage::RegisterHandler(TEXT("Shadow.Virtual.StatusFeedback"), [this](GPUMessage::FReader Message)
	{
		// Get the frame that the message was sent.
		uint32 FrameNumber = Message.Read<uint32>(0);
		// Goes negative on underflow
		int32 NumPagesFree = Message.Read<int32>(0);

		if (NumPagesFree < 0)
		{
			static const auto* CVarResolutionLodBiasLocalPtr = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"));
			const float LodBiasLocal = CVarResolutionLodBiasLocalPtr->GetValueOnRenderThread();

			static const auto* CVarResolutionLodBiasDirectionalPtr = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"));
			const float LodBiasDirectional = CVarResolutionLodBiasDirectionalPtr->GetValueOnRenderThread();

			static const auto* CVarMaxPhysicalPagesPtr = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.Virtual.MaxPhysicalPages"));
			const int32 MaxPhysicalPages = CVarMaxPhysicalPagesPtr->GetValueOnRenderThread();

#if !UE_BUILD_SHIPPING
			if (!bLoggedPageOverflow)
			{
				UE_LOG(LogRenderer, Warning, TEXT("Virtual Shadow Map Page Pool overflow (%d page allocations were not served), this will produce visual artifacts (missing shadow), increase the page pool limit or reduce resolution bias to avoid.\n")
					TEXT(" See r.Shadow.Virtual.MaxPhysicalPages (%d), r.Shadow.Virtual.ResolutionLodBiasLocal (%.2f), and r.Shadow.Virtual.ResolutionLodBiasDirectional (%.2f)"),
					-NumPagesFree,
					MaxPhysicalPages,
					LodBiasLocal,
					LodBiasDirectional);
				bLoggedPageOverflow = true;
			}
			LastOverflowFrame = Scene->GetFrameNumber();
#endif
		}
#if !UE_BUILD_SHIPPING
		else
		{
			bLoggedPageOverflow = false;
		}
#endif
	});

#if !UE_BUILD_SHIPPING
	ScreenMessageDelegate = FRendererOnScreenNotification::Get().AddLambda([this](TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText >& OutMessages)
	{
		// Show for ~5s after last overflow
		int32 CurrentFrameNumber = Scene->GetFrameNumber();
		if (LastOverflowFrame >= 0 && CurrentFrameNumber - LastOverflowFrame < 30 * 5)
		{
			OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Warning, FText::FromString(FString::Printf(TEXT("Virtual Shadow Map Page Pool overflow detected (%d frames ago)"), CurrentFrameNumber - LastOverflowFrame)));
		}
	});
#endif
}

FVirtualShadowMapArrayCacheManager::~FVirtualShadowMapArrayCacheManager()
{
#if !UE_BUILD_SHIPPING
	FRendererOnScreenNotification::Get().Remove(ScreenMessageDelegate);
#endif
}


TRefCountPtr<IPooledRenderTarget> FVirtualShadowMapArrayCacheManager::SetPhysicalPoolSize(FRDGBuilder& GraphBuilder, FIntPoint RequestedSize)
{
	if (!PhysicalPagePool || PhysicalPagePool->GetDesc().Extent != RequestedSize)
	{
		FPooledRenderTargetDesc Desc2D = FPooledRenderTargetDesc::Create2DDesc(
			RequestedSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_UAV,
			false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc2D, PhysicalPagePool, TEXT("Shadow.Virtual.PhysicalPagePool"));

		Invalidate();
		//UE_LOG(LogRenderer, Display, TEXT("Recreating Shadow.Virtual.PhysicalPagePool. This will also drop any cached pages."));
	}

	return PhysicalPagePool;
}

void FVirtualShadowMapArrayCacheManager::FreePhysicalPool()
{
	if (PhysicalPagePool)
	{
		PhysicalPagePool = nullptr;
		Invalidate();
	}
}

void FVirtualShadowMapArrayCacheManager::Invalidate()
{
	// Clear the cache
	PrevCacheEntries.Empty();
	CacheEntries.Reset();
}

TSharedPtr<FVirtualShadowMapCacheEntry> FVirtualShadowMapPerLightCacheEntry::FindCreateShadowMapEntry(int32 Index)
{
	check(Index >= 0);
	ShadowMapEntries.SetNum(FMath::Max(Index + 1, ShadowMapEntries.Num()));

	TSharedPtr<FVirtualShadowMapCacheEntry>& EntryRef = ShadowMapEntries[Index];

	if (!EntryRef.IsValid())
	{
		EntryRef = MakeShared<FVirtualShadowMapCacheEntry>();
	}

	return EntryRef;
}

TSharedPtr<FVirtualShadowMapPerLightCacheEntry> FVirtualShadowMapArrayCacheManager::FindCreateLightCacheEntry(int32 LightSceneId)
{
	if (CVarCacheVirtualSMs.GetValueOnRenderThread() == 0)
	{
		return nullptr;
	}

	if (TSharedPtr<FVirtualShadowMapPerLightCacheEntry> *LightEntry = CacheEntries.Find(LightSceneId))
	{
		return *LightEntry;
	}

	// Add to current frame / active set.
	TSharedPtr<FVirtualShadowMapPerLightCacheEntry>& NewLightEntry = CacheEntries.Add(LightSceneId);

	// Copy data if available
	if (TSharedPtr<FVirtualShadowMapPerLightCacheEntry>* PrevNewLightEntry = PrevCacheEntries.Find(LightSceneId))
	{
		NewLightEntry = *PrevNewLightEntry;
	}
	else
	{
		NewLightEntry = MakeShared<FVirtualShadowMapPerLightCacheEntry>(Scene->GetMaxPersistentPrimitiveIndex());
	}

	// return entry
	return NewLightEntry;
}



TSharedPtr<FVirtualShadowMapCacheEntry> FVirtualShadowMapArrayCacheManager::FindCreateCacheEntry(int32 LightSceneId, int32 Index)
{
	if (CVarCacheVirtualSMs.GetValueOnRenderThread() == 0)
	{
		return nullptr;
	}

	return FindCreateLightCacheEntry(LightSceneId)->FindCreateShadowMapEntry(Index);
}


class FVirtualSmCopyStatsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmCopyStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmCopyStatsCS, FGlobalShader)
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, InStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer< uint >, AccumulatedStatsBufferOut)
		SHADER_PARAMETER(uint32, NumStats)
	END_SHADER_PARAMETER_STRUCT()
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("MAX_STAT_FRAMES"), FVirtualShadowMapArrayCacheManager::MaxStatFrames);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmCopyStatsCS, "/Engine/Private/VirtualShadowMaps/CopyStats.usf", "CopyStatsCS", SF_Compute);

void FVirtualShadowMapArrayCacheManager::ExtractFrameData(
	FRDGBuilder& GraphBuilder,	
	FVirtualShadowMapArray &VirtualShadowMapArray,
	bool bEnableCaching)
{
	// Drop all refs.
	PrevBuffers = FVirtualShadowMapArrayFrameData();
	PrevUniformParameters.NumShadowMaps = 0;

	if (bEnableCaching && VirtualShadowMapArray.IsAllocated())
	{
		bool bExtractPageTable = false;

		// HZB and associated page table are needed by next frame even when VSM physical page caching is disabled
		if (VirtualShadowMapArray.HZBPhysical)
		{
			bExtractPageTable = true;
			GraphBuilder.QueueTextureExtraction(VirtualShadowMapArray.HZBPhysical, &PrevBuffers.HZBPhysical);
			PrevBuffers.HZBMetadata = VirtualShadowMapArray.HZBMetadata;
		}

		if (CVarCacheVirtualSMs.GetValueOnRenderThread() != 0)
		{
			bExtractPageTable = true;
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.PageFlagsRDG, &PrevBuffers.PageFlags);
		
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.PhysicalPageMetaDataRDG, &PrevBuffers.PhysicalPageMetaData);
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.DynamicCasterPageFlagsRDG, &PrevBuffers.DynamicCasterPageFlags);
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.ProjectionDataRDG, &PrevBuffers.ProjectionData);
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.PageRectBoundsRDG, &PrevBuffers.PageRectBounds);

			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.InvalidatingInstancesRDG, &PrevBuffers.InvalidatingInstancesBuffer);
			PrevBuffers.NumInvalidatingInstanceSlots = VirtualShadowMapArray.NumInvalidatingInstanceSlots;

			// Move cache entries to previous frame, this implicitly removes any that were not used
			PrevCacheEntries = CacheEntries;
			PrevUniformParameters = VirtualShadowMapArray.UniformParameters;
		}

		if (bExtractPageTable)
		{
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.PageTableRDG, &PrevBuffers.PageTable);
		}

		CacheEntries.Reset();
	}
	else
	{
		// We drop the physical page pool here as well to ensure that it disappears in the case where
		// thumbnail rendering or similar creates multiple FSceneRenderers that never get deleted.
		// Caching is disabled on these contexts intentionally to avoid these issues.
		FreePhysicalPool();
	}

	// Drop any temp references embedded in the uniform parameters this frame.
	// We'll reestablish them when we reimport the extracted resources next frame
	PrevUniformParameters.ProjectionData = nullptr;
	PrevUniformParameters.PageTable = nullptr;
	PrevUniformParameters.PhysicalPagePool = nullptr;

	if (VirtualShadowMapArray.IsEnabled())
	{
		ExtractStats(GraphBuilder, VirtualShadowMapArray);
	}
}

void FVirtualShadowMapArrayCacheManager::ExtractStats(FRDGBuilder& GraphBuilder, FVirtualShadowMapArray &VirtualShadowMapArray)
{
	FRDGBufferRef AccumulatedStatsBufferRDG = nullptr;

	// Note: stats accumulation thing is here because it needs to persist over frames.
	if (AccumulatedStatsBuffer.IsValid())
	{
		AccumulatedStatsBufferRDG = GraphBuilder.RegisterExternalBuffer(AccumulatedStatsBuffer, TEXT("Shadow.Virtual.AccumulatedStatsBuffer"));
	}

	if (IsAccumulatingStats())
	{
		if (!AccumulatedStatsBuffer.IsValid())
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(4, 1 + FVirtualShadowMapArray::NumStats * MaxStatFrames);
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);

			AccumulatedStatsBufferRDG = GraphBuilder.CreateBuffer(Desc, TEXT("Shadow.Virtual.AccumulatedStatsBuffer"));	// TODO: Can't be a structured buffer as EnqueueCopy is only defined for vertex buffers
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumulatedStatsBufferRDG, PF_R32_UINT), 0);
			AccumulatedStatsBuffer = GraphBuilder.ConvertToExternalBuffer(AccumulatedStatsBufferRDG);
		}

		// Initialize/clear
		if (!bAccumulatingStats)
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumulatedStatsBufferRDG, PF_R32_UINT), 0);
			bAccumulatingStats = true;
		}

		FVirtualSmCopyStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmCopyStatsCS::FParameters>();

		PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(VirtualShadowMapArray.StatsBufferRDG, PF_R32_UINT);
		PassParameters->AccumulatedStatsBufferOut = GraphBuilder.CreateUAV(AccumulatedStatsBufferRDG, PF_R32_UINT);
		PassParameters->NumStats = FVirtualShadowMapArray::NumStats;

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmCopyStatsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Copy Stats"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}
	else if (bAccumulatingStats)
	{
		bAccumulatingStats = false;

		GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("Shadow.Virtual.AccumulatedStatsBufferReadback"));
		AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, AccumulatedStatsBufferRDG, 0u);
	}
	else if (AccumulatedStatsBuffer.IsValid())
	{
		AccumulatedStatsBuffer.SafeRelease();
	}

	if (GPUBufferReadback && GPUBufferReadback->IsReady())
	{
		TArray<uint32> Tmp;
		Tmp.AddDefaulted(1 + FVirtualShadowMapArray::NumStats * MaxStatFrames);

		{
			const uint32* BufferPtr = (const uint32*)GPUBufferReadback->Lock((1 + FVirtualShadowMapArray::NumStats * MaxStatFrames) * sizeof(uint32));
			FPlatformMemory::Memcpy(Tmp.GetData(), BufferPtr, Tmp.Num() * Tmp.GetTypeSize());
			GPUBufferReadback->Unlock();

			delete GPUBufferReadback;
			GPUBufferReadback = nullptr;
		}

		FString FileName = TEXT("VirtualShadowMapCacheStats.csv");// FString::Printf(TEXT("%s.csv"), *FileNameToUse);
		FArchive * FileToLogTo = IFileManager::Get().CreateFileWriter(*FileName, false);
		ensure(FileToLogTo);
		if (FileToLogTo)
		{
			static const FString StatNames[] =
			{
				TEXT("Allocated"),
				TEXT("StaticCached"),
				TEXT("StaticInvalidated"),
				TEXT("DynamicCached"),
				TEXT("DynamicInvalidated"),
				TEXT("NumSms"),
				TEXT("NonNaniteInstances"),
				TEXT("NonNaniteInstancesDrawn"),
				TEXT("NonNaniteInstancesHZBCulled"),
				TEXT("NonNaniteInstancesPageMaskCulled"),
				TEXT("NonNaniteInstancesEmptyRectCulled"),
				TEXT("NonNaniteInstancesFrustumCulled"),
			};

			// Print header
			FString StringToPrint;
			for (int32 Index = 0; Index < FVirtualShadowMapArray::NumStats; ++Index)
			{
				if (!StringToPrint.IsEmpty())
				{
					StringToPrint += TEXT(",");
				}
				if (Index < int32(UE_ARRAY_COUNT(StatNames)))
				{
					StringToPrint.Append(StatNames[Index]);
				}
				else
				{
					StringToPrint.Appendf(TEXT("Stat_%d"), Index);
				}
			}

			StringToPrint += TEXT("\n");
			FileToLogTo->Serialize(TCHAR_TO_ANSI(*StringToPrint), StringToPrint.Len());

			uint32 Num = Tmp[0];
			for (uint32 Ind = 0; Ind < Num; ++Ind)
			{
				StringToPrint.Empty();

				for (uint32 StatInd = 0; StatInd < FVirtualShadowMapArray::NumStats; ++StatInd)
				{
					if (!StringToPrint.IsEmpty())
					{
						StringToPrint += TEXT(",");
					}

					StringToPrint += FString::Printf(TEXT("%d"), Tmp[1 + Ind * FVirtualShadowMapArray::NumStats + StatInd]);
				}

				StringToPrint += TEXT("\n");
				FileToLogTo->Serialize(TCHAR_TO_ANSI(*StringToPrint), StringToPrint.Len());
			}


			FileToLogTo->Close();
		}
	}
}


bool FVirtualShadowMapArrayCacheManager::IsValid()
{
	return CVarCacheVirtualSMs.GetValueOnRenderThread() != 0
		&& PrevBuffers.PageTable
		&& PrevBuffers.PageFlags
		&& PrevBuffers.PhysicalPageMetaData
		&& PrevBuffers.DynamicCasterPageFlags;
}


bool FVirtualShadowMapArrayCacheManager::IsAccumulatingStats()
{
	return CVarAccumulateStats.GetValueOnRenderThread() != 0;
}

void FVirtualShadowMapArrayCacheManager::ProcessRemovedOrUpdatedPrimitives(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, FInvalidatingPrimitiveCollector& InvalidatingPrimitiveCollector)
{
	if (CVarCacheVirtualSMs.GetValueOnRenderThread() != 0 && PrevBuffers.DynamicCasterPageFlags.IsValid())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Shadow.Virtual.ProcessRemovedOrUpdatedPrimitives");
		ProcessGPUInstanceInvalidations(GraphBuilder, GPUScene);

		if (!InvalidatingPrimitiveCollector.IsEmpty())
		{
#if VSM_LOG_INVALIDATIONS
			UE_LOG(LogTemp, Warning, TEXT("ProcessRemovedOrUpdatedPrimitives: \n%s"), *InvalidatingPrimitiveCollector.RangesStr);
#endif
			ProcessInvalidations(GraphBuilder, InvalidatingPrimitiveCollector.LoadBalancer, InvalidatingPrimitiveCollector.TotalInstanceCount, GPUScene);
		}
	}
}

static void ResizeFlagArray(TBitArray<>& BitArray, int32 NewMax)
{
	if (BitArray.Num() > NewMax)
	{
		// Trim off excess items
		BitArray.SetNumUninitialized(NewMax);
	}
	else if (BitArray.Num() < NewMax)
	{
		// Add false
		BitArray.Add(false, NewMax - BitArray.Num());
	}
}

void FVirtualShadowMapArrayCacheManager::OnSceneChange()
{
	if (CVarCacheVirtualSMs.GetValueOnRenderThread() != 0)
	{
		for (auto& CacheEntry : PrevCacheEntries)
		{
			ResizeFlagArray(CacheEntry.Value->RenderedPrimitives, Scene->GetMaxPersistentPrimitiveIndex());
		}
		for (auto& CacheEntry : CacheEntries)
		{
			ResizeFlagArray(CacheEntry.Value->RenderedPrimitives, Scene->GetMaxPersistentPrimitiveIndex());
		}
	}
}


/**
 * Compute shader to project and invalidate the rectangles of given instances.
 */
class FVirtualSmInvalidateInstancePagesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmInvalidateInstancePagesCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmInvalidateInstancePagesCS, FGlobalShader)

	enum EInputDataKind
	{
		EInputKind_GPUInstances,
		EInputKind_LoadBalancer,
		EInputKind_Num
	};

	class FDebugDim : SHADER_PERMUTATION_BOOL("ENABLE_DEBUG_MODE");
	class FInputKindDim : SHADER_PERMUTATION_INT("INPUT_KIND", EInputKind_Num);
	class FUseHzbDim : SHADER_PERMUTATION_BOOL("USE_HZB_OCCLUSION");
	using FPermutationDomain = TShaderPermutationDomain<FUseHzbDim, FDebugDim, FInputKindDim>;

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderParameters, ShaderDrawUniformBuffer)
		SHADER_PARAMETER(uint32, bDrawBounds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutDynamicCasterPageFlags)

		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, GPUSceneFrameNumber)
		SHADER_PARAMETER(uint32, InstanceSceneDataSOAStride)
		SHADER_PARAMETER(uint32, GPUSceneNumAllocatedInstances)
		SHADER_PARAMETER(uint32, GPUSceneNumAllocatedPrimitives)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, ShadowHZBPageTable)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		SHADER_PARAMETER( FVector2f,	HZBSize )

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InvalidatingInstances)
		SHADER_PARAMETER(uint32, NumInvalidatingInstanceSlots)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)

		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUScene::FInstanceGPULoadBalancer::FShaderParameters, LoadBalancerParameters)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int Cs1dGroupSizeX = FVirtualShadowMapArrayCacheManager::FInstanceGPULoadBalancer::ThreadGroupSize;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CS_1D_GROUP_SIZE_X"), Cs1dGroupSizeX);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		
		OutEnvironment.SetDefine(TEXT("INPUT_KIND_GPU_INSTANCES"), EInputKind_GPUInstances);
		OutEnvironment.SetDefine(TEXT("INPUT_KIND_LOAD_BALANCER"), EInputKind_LoadBalancer);

		FGPUScene::FInstanceGPULoadBalancer::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmInvalidateInstancePagesCS, "/Engine/Private/VirtualShadowMaps/CacheManagement.usf", "VirtualSmInvalidateInstancePagesCS", SF_Compute);


TRDGUniformBufferRef<FVirtualShadowMapUniformParameters> FVirtualShadowMapArrayCacheManager::GetPreviousUniformBuffer(FRDGBuilder& GraphBuilder) const
{
	FVirtualShadowMapUniformParameters* VersionedParameters = GraphBuilder.AllocParameters<FVirtualShadowMapUniformParameters>();
	*VersionedParameters = PrevUniformParameters;
	return GraphBuilder.CreateUniformBuffer(VersionedParameters);
}

void FVirtualShadowMapArrayCacheManager::SetHZBViewParams(int32 HZBKey, Nanite::FPackedViewParams& OutParams)
{
	FVirtualShadowMapHZBMetadata* PrevHZBMeta = PrevBuffers.HZBMetadata.Find(HZBKey);
	if (PrevHZBMeta)
	{
		OutParams.PrevTargetLayerIndex = PrevHZBMeta->TargetLayerIndex;
		OutParams.PrevViewMatrices = PrevHZBMeta->ViewMatrices;
		OutParams.Flags = NANITE_VIEW_FLAG_HZBTEST;
	}
}

static void SetupCommonParameters(FRDGBuilder& GraphBuilder, FVirtualShadowMapArrayCacheManager* CacheManager, int32 TotalInstanceCount, const FGPUScene& GPUScene, 
	FVirtualSmInvalidateInstancePagesCS::FParameters& OutPassParameters,
	FVirtualSmInvalidateInstancePagesCS::FPermutationDomain &OutPermutationVector)
{

	auto RegExtCreateSrv = [&GraphBuilder](const TRefCountPtr<FRDGPooledBuffer>& Buffer, const TCHAR* Name) -> FRDGBufferSRVRef
	{
		return GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(Buffer, Name));
	};

	const bool bDrawBounds = CVarDrawInvalidatingBounds.GetValueOnRenderThread() != 0;


	if (bDrawBounds)
	{
		ShaderDrawDebug::SetEnabled(true);
		ShaderDrawDebug::RequestSpaceForElements(TotalInstanceCount * 12);
	}

	// Note: this disables the whole debug permutation since the parameters must be bound.
	const bool bUseDebugPermutation = bDrawBounds && ShaderDrawDebug::IsDefaultViewEnabled();

	FVirtualShadowMapArrayFrameData &PrevBuffers = CacheManager->PrevBuffers;

	// Update references in our last frame uniform buffer with reimported resources for this frame
	CacheManager->PrevUniformParameters.ProjectionData = RegExtCreateSrv(PrevBuffers.ProjectionData, TEXT("Shadow.Virtual.PrevProjectionData"));
	CacheManager->PrevUniformParameters.PageTable = RegExtCreateSrv(PrevBuffers.PageTable, TEXT("Shadow.Virtual.PrevPageTable"));
	CacheManager->PrevUniformParameters.PageFlags = RegExtCreateSrv(PrevBuffers.PageFlags, TEXT("Shadow.Virtual.PrevPageFlags"));
	CacheManager->PrevUniformParameters.PageRectBounds = RegExtCreateSrv(PrevBuffers.PageRectBounds, TEXT("Shadow.Virtual.PrevPageRectBounds"));
	// Unused in this path
	CacheManager->PrevUniformParameters.PhysicalPagePool = GSystemTextures.GetZeroUIntDummy(GraphBuilder);

	OutPassParameters.VirtualShadowMap = CacheManager->GetPreviousUniformBuffer(GraphBuilder);

	FRDGBufferRef DynamicCasterPageFlagsRDG = GraphBuilder.RegisterExternalBuffer(PrevBuffers.DynamicCasterPageFlags, TEXT("Shadow.Virtual.PrevDynamicCasterFlags"));
	OutPassParameters.OutDynamicCasterPageFlags = GraphBuilder.CreateUAV(DynamicCasterPageFlagsRDG);

	OutPassParameters.GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
	OutPassParameters.GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	OutPassParameters.GPUSceneInstancePayloadData = GPUScene.InstancePayloadDataBuffer.SRV;
	OutPassParameters.GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();
	OutPassParameters.GPUSceneNumAllocatedInstances = GPUScene.GetNumInstances();
	OutPassParameters.GPUSceneNumAllocatedPrimitives = GPUScene.GetNumPrimitives();

	OutPassParameters.InstanceSceneDataSOAStride = GPUScene.InstanceSceneDataSOAStride;
	OutPassParameters.bDrawBounds = bDrawBounds;

	if (bUseDebugPermutation)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, OutPassParameters.ShaderDrawUniformBuffer);
	}

	const bool bUseHZB = (CVarCacheVsmUseHzb.GetValueOnRenderThread() != 0);
	const TRefCountPtr<IPooledRenderTarget> PrevHZBPhysical = bUseHZB ? PrevBuffers.HZBPhysical : nullptr;
	if (PrevHZBPhysical)
	{
		// Same, since we are not producing a new frame just yet
		OutPassParameters.ShadowHZBPageTable = CacheManager->PrevUniformParameters.PageTable;
		OutPassParameters.HZBTexture = GraphBuilder.RegisterExternalTexture(PrevHZBPhysical);
		OutPassParameters.HZBSize = PrevHZBPhysical->GetDesc().Extent;
		OutPassParameters.HZBSampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	}
	OutPermutationVector.Set<FVirtualSmInvalidateInstancePagesCS::FDebugDim>(bUseDebugPermutation);
	OutPermutationVector.Set<FVirtualSmInvalidateInstancePagesCS::FUseHzbDim>(PrevHZBPhysical != nullptr);
}


void FVirtualShadowMapArrayCacheManager::ProcessInvalidations(FRDGBuilder& GraphBuilder, FInstanceGPULoadBalancer& Instances, int32 TotalInstanceCount, const FGPUScene& GPUScene)
{
	if (Instances.IsEmpty())
	{
		return;
	}

	Instances.FinalizeBatches();

	RDG_EVENT_SCOPE(GraphBuilder, "ProcessInvalidations [%d batches]", Instances.GetBatches().Num());

	FVirtualSmInvalidateInstancePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmInvalidateInstancePagesCS::FParameters>();

	FVirtualSmInvalidateInstancePagesCS::FPermutationDomain PermutationVector;
	SetupCommonParameters(GraphBuilder, this, TotalInstanceCount, GPUScene, *PassParameters, PermutationVector);
	Instances.Upload(GraphBuilder).GetShaderParameters(GraphBuilder, PassParameters->LoadBalancerParameters);

	PermutationVector.Set<FVirtualSmInvalidateInstancePagesCS::FInputKindDim>(FVirtualSmInvalidateInstancePagesCS::EInputKind_LoadBalancer);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmInvalidateInstancePagesCS>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("VirtualSmInvalidateInstancePagesCS"),
		ComputeShader,
		PassParameters,
		Instances.GetWrappedCsGroupCount()
	);

}


void FVirtualShadowMapArrayCacheManager::ProcessGPUInstanceInvalidations(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene)
{
	// Dispatch CS indirectly to process instances that are marked to update from the GPU side.
	if (PrevBuffers.InvalidatingInstancesBuffer.IsValid())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "ProcessGPUInstanceInvalidations [GPU-Instances]");

		FRDGBufferRef InvalidatingInstancesBufferRDG = GraphBuilder.RegisterExternalBuffer(PrevBuffers.InvalidatingInstancesBuffer, TEXT("Shadow.Virtual.PrevInvalidatingInstancesBuffer"));
		FRDGBufferRef IndirectArgs = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(GraphBuilder, InvalidatingInstancesBufferRDG, TEXT("Shadow.Virtual.ProcessGPUInstanceInvalidationsIndirectArgs"), FVirtualSmInvalidateInstancePagesCS::Cs1dGroupSizeX);

		FVirtualSmInvalidateInstancePagesCS::FPermutationDomain PermutationVector;
		FVirtualSmInvalidateInstancePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmInvalidateInstancePagesCS::FParameters>();
		SetupCommonParameters(GraphBuilder, this, 16*1024, GPUScene, *PassParameters, PermutationVector);

		PassParameters->IndirectArgs = IndirectArgs;
		PassParameters->InvalidatingInstances = GraphBuilder.CreateSRV(InvalidatingInstancesBufferRDG);
		PassParameters->NumInvalidatingInstanceSlots = PrevBuffers.NumInvalidatingInstanceSlots;

		PermutationVector.Set<FVirtualSmInvalidateInstancePagesCS::FInputKindDim>(FVirtualSmInvalidateInstancePagesCS::EInputKind_GPUInstances);
		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmInvalidateInstancePagesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VirtualSmInvalidateInstancePagesCS"),
			ComputeShader,
			PassParameters,
			IndirectArgs,
			0
		);

		// Drop the InvalidatingInstancesBuffer to make sure we don't redundantly process the associated invalidations if ProcessRemovedOrUpdatedPrimitives is called multiple times.
		PrevBuffers.InvalidatingInstancesBuffer.SafeRelease();
	}
}