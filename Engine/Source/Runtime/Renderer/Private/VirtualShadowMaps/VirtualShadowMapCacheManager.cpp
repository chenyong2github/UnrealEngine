// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMap.h:
=============================================================================*/
#include "VirtualShadowMapCacheManager.h"
#include "RendererModule.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "ScenePrivate.h"
#include "HAL/FileManager.h"

#include "PrimitiveSceneInfo.h"
#include "ShaderDebug.h"

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
	TEXT("Turn on debug render cache invalidating instance bounds."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCacheVsmUseHzb(
	TEXT("r.Shadow.Virtual.Cache.InvalidateUseHZB"),
	1,
	TEXT("Enables testing HZB for Virtual Shadow Map invalidations."),
	ECVF_RenderThreadSafe);

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

	if (bCacheValid)
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

	if (bCacheValid)
	{
		PrevVirtualShadowMapId = CurrentVirtualShadowMapId;
		// These really should be exact by construction
		check(ViewRadiusZ == Clipmap.ViewRadiusZ);
	}
	else
	{
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

TSharedPtr<FVirtualShadowMapCacheEntry> FVirtualShadowMapArrayCacheManager::FindCreateCacheEntry(int32 LightSceneId, int32 Index)
{
	if (CVarCacheVirtualSMs.GetValueOnRenderThread() == 0)
	{
		return nullptr;
	}

	const FIntPoint Key(LightSceneId, Index);

	if (TSharedPtr<FVirtualShadowMapCacheEntry> *VirtualShadowMapCacheEntry = CacheEntries.Find(Key))
	{
		return *VirtualShadowMapCacheEntry;
	}

	// Add to current frame / active set.
	TSharedPtr<FVirtualShadowMapCacheEntry> &NewVirtualShadowMapCacheEntry = CacheEntries.Add(Key);

	// Copy data if available
	if (TSharedPtr<FVirtualShadowMapCacheEntry> *PrevVirtualShadowMapCacheEntry = PrevCacheEntries.Find(Key))
	{
		NewVirtualShadowMapCacheEntry = *PrevVirtualShadowMapCacheEntry;
	}
	else
	{
		NewVirtualShadowMapCacheEntry = TSharedPtr<FVirtualShadowMapCacheEntry>(new FVirtualShadowMapCacheEntry);
	}

	// return entry
	return NewVirtualShadowMapCacheEntry;
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
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
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.HPageFlagsRDG, &PrevBuffers.HPageFlags);
		
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.PhysicalPageMetaDataRDG, &PrevBuffers.PhysicalPageMetaData);
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.DynamicCasterPageFlagsRDG, &PrevBuffers.DynamicCasterPageFlags);
			GraphBuilder.QueueBufferExtraction(VirtualShadowMapArray.ShadowMapProjectionDataRDG, &PrevBuffers.ShadowMapProjectionDataBuffer);
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
	if (!AccumulatedStatsBuffer.IsValid())
	{
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(4, 1 + FVirtualShadowMapArray::NumStats * MaxStatFrames);
		Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);

		AccumulatedStatsBufferRDG = GraphBuilder.CreateBuffer(Desc, TEXT("Shadow.Virtual.AccumulatedStatsBuffer"));	// TODO: Can't be a structured buffer as EnqueueCopy is only defined for vertex buffers
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumulatedStatsBufferRDG, PF_R32_UINT), 0);
		AccumulatedStatsBuffer = GraphBuilder.ConvertToExternalBuffer(AccumulatedStatsBufferRDG);
	}
	else
	{
		AccumulatedStatsBufferRDG = GraphBuilder.RegisterExternalBuffer(AccumulatedStatsBuffer, TEXT("Shadow.Virtual.AccumulatedStatsBuffer"));
	}

	if (IsAccumulatingStats())
	{
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

		GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("Shadow.Virtual.AccumulatedStatsBuffer"));
		AddEnqueueCopyPass(GraphBuilder, GPUBufferReadback, AccumulatedStatsBufferRDG, 0u);
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
				TEXT("Cached"),
				TEXT("Dynamic"),
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

void FVirtualShadowMapArrayCacheManager::ProcessRemovedPrimives(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, const TArray<FPrimitiveSceneInfo*>& RemovedPrimitiveSceneInfos)
{
	if (CVarCacheVirtualSMs.GetValueOnRenderThread() != 0 && RemovedPrimitiveSceneInfos.Num() > 0 && PrevBuffers.DynamicCasterPageFlags.IsValid())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Shadow.Virtual.ProcessRemovedPrimives [%d]", RemovedPrimitiveSceneInfos.Num());

		TArray<FInstanceSceneDataRange, SceneRenderingAllocator> InstanceRangesLarge;
		TArray<FInstanceSceneDataRange, SceneRenderingAllocator> InstanceRangesSmall;

		int32 TotalInstanceCount = 0;
		for (const FPrimitiveSceneInfo* PrimitiveSceneInfo : RemovedPrimitiveSceneInfos)
		{
			if (PrimitiveSceneInfo->GetInstanceSceneDataOffset() != INDEX_NONE)
			{
				const int32 NumInstanceSceneDataEntries = PrimitiveSceneInfo->GetNumInstanceSceneDataEntries();
				TotalInstanceCount += NumInstanceSceneDataEntries;
				if (NumInstanceSceneDataEntries >= 8)
				{
					InstanceRangesLarge.Add(FInstanceSceneDataRange{ PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries });
				}
				else
				{
					InstanceRangesSmall.Add(FInstanceSceneDataRange{ PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries });
				}
			}
		}
		ProcessInstanceRangeInvalidation(GraphBuilder, InstanceRangesLarge, InstanceRangesSmall, TotalInstanceCount, GPUScene);
	}
}


void FVirtualShadowMapArrayCacheManager::ProcessPrimitivesToUpdate(FRDGBuilder& GraphBuilder, const FScene& Scene)
{
	const FGPUScene& GPUScene = Scene.GPUScene;
	if (IsValid())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Shadow.Virtual.ProcessPrimitivesToUpdate [%d]", GPUScene.PrimitivesToUpdate.Num());

		ProcessGPUInstanceInvalidations(GraphBuilder, GPUScene);

		if (GPUScene.PrimitivesToUpdate.Num() > 0)
		{
			// TODO: As a slight CPU optimization just pass primitive ID list and use instance ranges stored in GPU scene
			TArray<FInstanceSceneDataRange, SceneRenderingAllocator> InstanceRangesLarge;
			TArray<FInstanceSceneDataRange, SceneRenderingAllocator> InstanceRangesSmall;

			int32 TotalInstanceCount = 0;
			for (const int32 PrimitiveId : GPUScene.PrimitivesToUpdate)
			{
				// There may possibly be IDs that are out of range if they were marked for update and then removed.
				if (PrimitiveId < Scene.Primitives.Num())
				{
					EPrimitiveDirtyState PrimitiveDirtyState = GPUScene.GetPrimitiveDirtyState(PrimitiveId);

					// SKIP if marked for Add, because this means it has no previous location to invalidate.
					// SKIP if transform has not changed, as this means no invalidation needs to take place.
					const bool bDoInvalidation = !EnumHasAnyFlags(PrimitiveDirtyState, EPrimitiveDirtyState::Added) && EnumHasAnyFlags(PrimitiveDirtyState, EPrimitiveDirtyState::ChangedTransform);
					if (bDoInvalidation)
					{
						const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene.Primitives[PrimitiveId];
						if (PrimitiveSceneInfo->GetInstanceSceneDataOffset() != INDEX_NONE)
						{
							int32 NumInstanceSceneDataEntries = PrimitiveSceneInfo->GetNumInstanceSceneDataEntries();
							TotalInstanceCount += NumInstanceSceneDataEntries;
							if (NumInstanceSceneDataEntries >= 8)
							{
								InstanceRangesLarge.Add(FInstanceSceneDataRange{ PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries });
							}
							else
							{
								InstanceRangesSmall.Add(FInstanceSceneDataRange{ PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries });
							}
						}
					}
				}
			}
			ProcessInstanceRangeInvalidation(GraphBuilder, InstanceRangesLarge, InstanceRangesSmall, TotalInstanceCount, GPUScene);
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
		EInputKind_SmallRanges,
		EInputKind_LargeRanges,
		EInputKind_GPUInstances,
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

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceSceneDataRange >, InstanceSceneRanges)
		SHADER_PARAMETER(uint32, NumRemovedItems)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, HPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint4 >, PageRectBounds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutDynamicCasterFlags)

		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, GPUSceneFrameNumber)
		SHADER_PARAMETER(uint32, InstanceSceneDataSOAStride)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, ShadowHZBPageTable)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		SHADER_PARAMETER( FVector2f,	HZBSize )

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InvalidatingInstances)
		SHADER_PARAMETER(uint32, NumInvalidatingInstanceSlots)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int Cs1dGroupSizeX = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CS_1D_GROUP_SIZE_X"), Cs1dGroupSizeX);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		
		OutEnvironment.SetDefine(TEXT("INPUT_KIND_SMALL_RANGES"), EInputKind_SmallRanges);
		OutEnvironment.SetDefine(TEXT("INPUT_KIND_LARGE_RANGES"), EInputKind_LargeRanges);
		OutEnvironment.SetDefine(TEXT("INPUT_KIND_GPU_INSTANCES"), EInputKind_GPUInstances);
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
		OutParams.Flags = VIEW_FLAG_HZBTEST;
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

	FVirtualShadowMapArrayFrameData &PrevBuffers = CacheManager->PrevBuffers;;

	// Update references in our last frame uniform buffer with reimported resources for this frame
	CacheManager->PrevUniformParameters.ProjectionData = RegExtCreateSrv(PrevBuffers.ShadowMapProjectionDataBuffer, TEXT("Shadow.Virtual.PrevProjectionData"));
	CacheManager->PrevUniformParameters.PageTable = RegExtCreateSrv(PrevBuffers.PageTable, TEXT("Shadow.Virtual.PrevPageTable"));
	// Unused in this path
	CacheManager->PrevUniformParameters.PhysicalPagePool = GSystemTextures.GetZeroUIntDummy(GraphBuilder);

	OutPassParameters.VirtualShadowMap = CacheManager->GetPreviousUniformBuffer(GraphBuilder);

	OutPassParameters.PageFlags = RegExtCreateSrv(PrevBuffers.PageFlags, TEXT("Shadow.Virtual.PrevPageFlags"));
	OutPassParameters.HPageFlags = RegExtCreateSrv(PrevBuffers.HPageFlags, TEXT("Shadow.Virtual.PrevHPageFlags"));
	OutPassParameters.PageRectBounds = RegExtCreateSrv(PrevBuffers.PageRectBounds, TEXT("Shadow.Virtual.PrevPageRectBounds"));

	FRDGBufferRef DynamicCasterFlagsRDG = GraphBuilder.RegisterExternalBuffer(PrevBuffers.DynamicCasterPageFlags, TEXT("Shadow.Virtual.PrevDynamicCasterFlags"));
	OutPassParameters.OutDynamicCasterFlags = GraphBuilder.CreateUAV(DynamicCasterFlagsRDG);

	OutPassParameters.GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
	OutPassParameters.GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	OutPassParameters.GPUSceneInstancePayloadData = GPUScene.InstancePayloadDataBuffer.SRV;
	OutPassParameters.GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();
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

void FVirtualShadowMapArrayCacheManager::ProcessInstanceRangeInvalidation(FRDGBuilder& GraphBuilder, const TArray<FInstanceSceneDataRange, SceneRenderingAllocator>& InstanceRangesLarge, const TArray<FInstanceSceneDataRange, SceneRenderingAllocator>& InstanceRangesSmall, int32 TotalInstanceCount, const FGPUScene& GPUScene)
{
	if (InstanceRangesSmall.IsEmpty() && InstanceRangesLarge.IsEmpty())
	{
		return;
	}

	FRDGBufferRef InstanceRangesSmallRDG = !InstanceRangesSmall.IsEmpty() ? CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.InstanceRangesSmall"), InstanceRangesSmall) : nullptr;
	FRDGBufferRef InstanceRangesLargeRDG = !InstanceRangesLarge.IsEmpty() ? CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.InstanceRangesSmall"), InstanceRangesLarge) : nullptr;


	FVirtualSmInvalidateInstancePagesCS::FParameters PassParametersTmp;
	FVirtualSmInvalidateInstancePagesCS::FPermutationDomain PermutationVector;
	SetupCommonParameters(GraphBuilder, this, TotalInstanceCount, GPUScene, PassParametersTmp, PermutationVector);

	if (InstanceRangesSmall.Num())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "ProcessInstanceRangeInvalidation [%d small-ranges]", InstanceRangesSmall.Num());

		FVirtualSmInvalidateInstancePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmInvalidateInstancePagesCS::FParameters>();
		*PassParameters = PassParametersTmp;
		PassParameters->InstanceSceneRanges = GraphBuilder.CreateSRV(InstanceRangesSmallRDG);
		PassParameters->NumRemovedItems = InstanceRangesSmall.Num();

		PermutationVector.Set<FVirtualSmInvalidateInstancePagesCS::FInputKindDim>(FVirtualSmInvalidateInstancePagesCS::EInputKind_SmallRanges);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmInvalidateInstancePagesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VirtualSmInvalidateInstancePagesCS"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(InstanceRangesSmall.Num(), FVirtualSmInvalidateInstancePagesCS::Cs1dGroupSizeX), 1, 1)
		);
	}
	if (InstanceRangesLarge.Num())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "ProcessInstanceRangeInvalidation [%d large-ranges]", InstanceRangesLarge.Num());

		FVirtualSmInvalidateInstancePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmInvalidateInstancePagesCS::FParameters>();
		*PassParameters = PassParametersTmp;
		PassParameters->InstanceSceneRanges = GraphBuilder.CreateSRV(InstanceRangesLargeRDG);
		PassParameters->NumRemovedItems = InstanceRangesLarge.Num();

		PermutationVector.Set<FVirtualSmInvalidateInstancePagesCS::FInputKindDim>(FVirtualSmInvalidateInstancePagesCS::EInputKind_LargeRanges);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmInvalidateInstancePagesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VirtualSmInvalidateInstancePagesCS"),
			ComputeShader,
			PassParameters,
			FIntVector(InstanceRangesLarge.Num(), 1, 1)
		);
	}
}


void FVirtualShadowMapArrayCacheManager::ProcessGPUInstanceInvalidations(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene)
{
	// Dispatch CS indirectly to process instances that are marked to update from the GPU side.
	{
		RDG_EVENT_SCOPE(GraphBuilder, "ProcessInstanceRangeInvalidation [GPU-Instances]");

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

	}
}