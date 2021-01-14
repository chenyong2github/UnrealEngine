// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright (C) Microsoft. All rights reserved.

/*=============================================================================
GPUSkinCache.cpp: Performs skinning on a compute shader into a buffer to avoid vertex buffer skinning.
=============================================================================*/

#include "GPUSkinCache.h"
#include "RawIndexBuffer.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "GlobalShader.h"
#include "SkeletalRenderGPUSkin.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "Shader.h"
#include "MeshMaterialShader.h"
#include "RenderGraphResources.h"
#include "Algo/Unique.h"

DEFINE_STAT(STAT_GPUSkinCache_TotalNumChunks);
DEFINE_STAT(STAT_GPUSkinCache_TotalNumVertices);
DEFINE_STAT(STAT_GPUSkinCache_TotalMemUsed);
DEFINE_STAT(STAT_GPUSkinCache_TangentsIntermediateMemUsed);
DEFINE_STAT(STAT_GPUSkinCache_NumTrianglesForRecomputeTangents);
DEFINE_STAT(STAT_GPUSkinCache_NumSectionsProcessed);
DEFINE_STAT(STAT_GPUSkinCache_NumSetVertexStreams);
DEFINE_STAT(STAT_GPUSkinCache_NumPreGDME);
DEFINE_LOG_CATEGORY_STATIC(LogSkinCache, Log, All);

static int32 GEnableGPUSkinCacheShaders = 0;

static FAutoConsoleVariableRef CVarEnableGPUSkinCacheShaders(
	TEXT("r.SkinCache.CompileShaders"),
	GEnableGPUSkinCacheShaders,
	TEXT("Whether or not to compile the GPU compute skinning cache shaders.\n")
	TEXT("This will compile the shaders for skinning on a compute job and not skin on the vertex shader.\n")
	TEXT("GPUSkinVertexFactory.usf needs to be touched to cause a recompile if this changes.\n")
	TEXT("0 is off(default), 1 is on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

// 0/1
int32 GEnableGPUSkinCache = 1;
static TAutoConsoleVariable<int32> CVarEnableGPUSkinCache(
	TEXT("r.SkinCache.Mode"),
	1,
	TEXT("Whether or not to use the GPU compute skinning cache.\n")
	TEXT("This will perform skinning on a compute job and not skin on the vertex shader.\n")
	TEXT("Requires r.SkinCache.CompileShaders=1\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on(default)\n")
	TEXT(" 2: only use skin cache for skinned meshes that ticked the Recompute Tangents checkbox (unavailable in shipping builds)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarDefaultGPUSkinCacheBehavior(
	TEXT("r.SkinCache.DefaultBehavior"),
	(int32)ESkinCacheDefaultBehavior::Inclusive,
	TEXT("Default behavior if all skeletal meshes are included/excluded from the skin cache. If Ray Tracing is enabled, will imply Inclusive.\n")
	TEXT(" Exclusive ( 0): All skeletal meshes are excluded from the skin cache. Each must opt in individually.\n")
	TEXT(" Inclusive ( 1): All skeletal meshes are included into the skin cache. Each must opt out individually. (default)")
	);

int32 GSkinCacheRecomputeTangents = 2;
TAutoConsoleVariable<int32> CVarGPUSkinCacheRecomputeTangents(
	TEXT("r.SkinCache.RecomputeTangents"),
	2,
	TEXT("This option enables recomputing the vertex tangents on the GPU.\n")
	TEXT("Can be changed at runtime, requires both r.SkinCache.CompileShaders=1 and r.SkinCache.Mode=1\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on, forces all skinned object to Recompute Tangents\n")
	TEXT(" 2: on, only recompute tangents on skinned objects who ticked the Recompute Tangents checkbox(default)\n"),
	ECVF_RenderThreadSafe
);

static int32 GForceRecomputeTangents = 0;
FAutoConsoleVariableRef CVarGPUSkinCacheForceRecomputeTangents(
	TEXT("r.SkinCache.ForceRecomputeTangents"),
	GForceRecomputeTangents,
	TEXT("0: off (default)\n")
	TEXT("1: Forces enabling and using the skincache and forces all skinned object to Recompute Tangents\n"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNumTangentIntermediateBuffers = 1;
static TAutoConsoleVariable<float> CVarGPUSkinNumTangentIntermediateBuffers(
	TEXT("r.SkinCache.NumTangentIntermediateBuffers"),
	1,
	TEXT("How many intermediate buffers to use for intermediate results while\n")
	TEXT("doing Recompute Tangents; more may allow the GPU to overlap compute jobs."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarGPUSkinCacheDebug(
	TEXT("r.SkinCache.Debug"),
	1.0f,
	TEXT("A scaling constant passed to the SkinCache shader, useful for debugging"),
	ECVF_RenderThreadSafe
);

static float GSkinCacheSceneMemoryLimitInMB = 128.0f;
static TAutoConsoleVariable<float> CVarGPUSkinCacheSceneMemoryLimitInMB(
	TEXT("r.SkinCache.SceneMemoryLimitInMB"),
	128.0f,
	TEXT("Maximum memory allowed to be allocated per World/Scene in Megs"),
	ECVF_RenderThreadSafe
);

////temporary disable until resource lifetimes are safe for all cases
static int32 GAllowDupedVertsForRecomputeTangents = 0;
FAutoConsoleVariableRef CVarGPUSkinCacheAllowDupedVertesForRecomputeTangents(
	TEXT("r.SkinCache.AllowDupedVertsForRecomputeTangents"),
	GAllowDupedVertsForRecomputeTangents,
	TEXT("0: off (default)\n")
	TEXT("1: Forces that vertices at the same position will be treated differently and has the potential to cause seams when verts are split.\n"),
	ECVF_RenderThreadSafe
);

static int32 GBlendUsingVertexColorForRecomputeTangents = 0;
FAutoConsoleVariableRef CVarGPUSkinCacheBlendUsingVertexColorForRecomputeTangents(
	TEXT("r.SkinCache.BlendUsingVertexColorForRecomputeTangents"),
	GBlendUsingVertexColorForRecomputeTangents,
	TEXT("0: off (default)\n")
	TEXT("1: No blending, choose between source and recompute tangents.\n")
	TEXT("2: Linear interpolation between source and recompute tangents.\n")
	TEXT("3: Vector slerp between source and recompute tangents.\n")
	TEXT("4: Convert tangents into quaternion, apply slerp, then convert from quaternion back to tangents (most expensive).\n"),
	ECVF_RenderThreadSafe
);

static int32 GGPUSkinCacheFlushCounter = 0;

#if RHI_RAYTRACING
static int32 GMemoryLimitForBatchedRayTracingGeometryUpdates = 512;
FAutoConsoleVariableRef CVarGPUSkinCacheMemoryLimitForBatchedRayTracingGeometryUpdates(
	TEXT("r.SkinCache.MemoryLimitForBatchedRayTracingGeometryUpdates"),
	GMemoryLimitForBatchedRayTracingGeometryUpdates,
	TEXT(""),
	ECVF_RenderThreadSafe
);
#endif

static inline bool DoesPlatformSupportGPUSkinCache(const FStaticShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5 || IsMetalSM5Platform(Platform) || IsVulkanSM5Platform(Platform) || FDataDrivenShaderPlatformInfo::GetSupportsGPUSkinCache(Platform);
}

ENGINE_API bool IsGPUSkinCacheAvailable(EShaderPlatform Platform)
{
	return (GEnableGPUSkinCacheShaders != 0 || GForceRecomputeTangents != 0) && DoesPlatformSupportGPUSkinCache(Platform);
}

ENGINE_API bool GPUSkinCacheNeedsDuplicatedVertices()
{
#if WITH_EDITOR // Duplicated vertices are used in the editor when merging meshes
	return true;
#else
	return (bool)GAllowDupedVertsForRecomputeTangents;
#endif
}

// We don't have it always enabled as it's not clear if this has a performance cost
// Call on render thread only!
// Should only be called if SM5 (compute shaders, atomics) are supported.
ENGINE_API bool DoSkeletalMeshIndexBuffersNeedSRV()
{
	// currently only implemented and tested on Window SM5 (needs Compute, Atomics, SRV for index buffers, UAV for VertexBuffers)
	//#todo-gpuskin: Enable on PS4 when SRVs for IB exist
	return IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform);
}

ENGINE_API bool DoRecomputeSkinTangentsOnGPU_RT()
{
	// currently only implemented and tested on Window SM5 (needs Compute, Atomics, SRV for index buffers, UAV for VertexBuffers)
	//#todo-gpuskin: Enable on PS4 when SRVs for IB exist
	return DoesPlatformSupportGPUSkinCache(GMaxRHIShaderPlatform) && GEnableGPUSkinCacheShaders != 0 && ((GEnableGPUSkinCache && GSkinCacheRecomputeTangents != 0) || GForceRecomputeTangents != 0);
}

// determine if during DispatchUpdateSkinning caching should occur
enum class EGPUSkinCacheDispatchFlags
{
	DispatchPrevPosition	= 1 << 0,
	DispatchPosition		= 1 << 1,
};

class FGPUSkinCacheEntry
{
public:
	FGPUSkinCacheEntry(FGPUSkinCache* InSkinCache, FSkeletalMeshObjectGPUSkin* InGPUSkin, FGPUSkinCache::FRWBuffersAllocation* InPositionAllocation)
		: PositionAllocation(InPositionAllocation)
		, SkinCache(InSkinCache)
		, GPUSkin(InGPUSkin)
		, MorphBuffer(0)
		, LOD(InGPUSkin->GetLOD())
	{
		
		const TArray<FSkelMeshRenderSection>& Sections = InGPUSkin->GetRenderSections(LOD);
		DispatchData.AddDefaulted(Sections.Num());
		BatchElementsUserData.AddZeroed(Sections.Num());
		for (int32 Index = 0; Index < Sections.Num(); ++Index)
		{
			BatchElementsUserData[Index].Entry = this;
			BatchElementsUserData[Index].Section = Index;
		}

		UpdateSkinWeightBuffer();
	}

	~FGPUSkinCacheEntry()
	{
		check(!PositionAllocation);
	}

	struct FSectionDispatchData
	{
		FGPUSkinCache::FRWBufferTracker PositionTracker;

		FGPUBaseSkinVertexFactory* SourceVertexFactory = nullptr;
		FGPUSkinPassthroughVertexFactory* TargetVertexFactory = nullptr;

		// triangle index buffer (input for the RecomputeSkinTangents, might need special index buffer unique to position and normal, not considering UV/vertex color)
		FRHIShaderResourceView* IndexBuffer = nullptr;

		const FSkelMeshRenderSection* Section = nullptr;

		// for debugging / draw events, -1 if not set
		uint32 SectionIndex = -1;

		// 0:normal, 1:with morph target, 2:with APEX cloth (not yet implemented)
		uint16 SkinType = 0;

		// See EGPUSkinCacheDispatchFlags
		uint16 DispatchFlags = 0;

		//
		uint32 NumBoneInfluences = 0;

		// in floats (4 bytes)
		uint32 OutputStreamStart = 0;
		uint32 NumVertices = 0;

		// in vertices
		uint32 InputStreamStart = 0;
		uint32 NumTexCoords = 1;
		uint32 SelectedTexCoord = 0;

		FShaderResourceViewRHIRef TangentBufferSRV = nullptr;
		FShaderResourceViewRHIRef UVsBufferSRV = nullptr;
		FShaderResourceViewRHIRef ColorBufferSRV = nullptr;
		FShaderResourceViewRHIRef PositionBufferSRV = nullptr;
		FShaderResourceViewRHIRef ClothPositionsAndNormalsBuffer = nullptr;

		// skin weight input
		uint32 InputWeightStart = 0;

		// morph input
		uint32 MorphBufferOffset = 0;

        // cloth input
		uint32 ClothBufferOffset = 0;
        float ClothBlendWeight = 0.0f;

        FMatrix ClothLocalToWorld = FMatrix::Identity;
        FMatrix ClothWorldToLocal = FMatrix::Identity;

		// triangle index buffer (input for the RecomputeSkinTangents, might need special index buffer unique to position and normal, not considering UV/vertex color)
		uint32 IndexBufferOffsetValue = 0;
		uint32 NumTriangles = 0;

		FRWBuffer* TangentBuffer = nullptr;
		FRWBuffer* IntermediateTangentBuffer = nullptr;
		FRWBuffer* PositionBuffer = nullptr;
		FRWBuffer* PreviousPositionBuffer = nullptr;

        // Handle duplicates
        FShaderResourceViewRHIRef DuplicatedIndicesIndices = nullptr;
        FShaderResourceViewRHIRef DuplicatedIndices = nullptr;

		FSectionDispatchData() = default;

		inline FRWBuffer* GetPreviousPositionRWBuffer()
		{
			check(PreviousPositionBuffer);
			return PreviousPositionBuffer;
		}

		inline FRWBuffer* GetPositionRWBuffer()
		{
			check(PositionBuffer);
			return PositionBuffer;
		}

		inline FRHIShaderResourceView* GetPreSkinPositionSRV()
		{
			check(SourceVertexFactory);
			check(SourceVertexFactory->GetPositionsSRV());

			return SourceVertexFactory->GetPositionsSRV().GetReference();
		}

		inline FRWBuffer* GetTangentRWBuffer()
		{
			return TangentBuffer;
		}

		FRWBuffer* GetActiveTangentRWBuffer()
		{
			bool bUseIntermediateTangentBuffer = IndexBuffer && GBlendUsingVertexColorForRecomputeTangents > 0;

			if (bUseIntermediateTangentBuffer)
			{
				return IntermediateTangentBuffer;
			}
			else
			{
				return TangentBuffer;
			}
		}

		void UpdateVertexFactoryDeclaration()
		{
			TargetVertexFactory->UpdateVertexDeclaration(SourceVertexFactory, GetPositionRWBuffer(), GetPreSkinPositionSRV(), GetTangentRWBuffer());
		}
	};

	void UpdateVertexFactoryDeclaration(int32 Section)
	{
		DispatchData[Section].UpdateVertexFactoryDeclaration();
	}

	inline FCachedGeometry::Section GetCachedGeometry(int32 SectionIndex) const
	{
		FCachedGeometry::Section MeshSection;
		const FSkelMeshRenderSection& Section = *DispatchData[SectionIndex].Section;
		MeshSection.PositionBuffer	= DispatchData[SectionIndex].PositionBuffer->SRV;
		MeshSection.UVsBuffer		= DispatchData[SectionIndex].UVsBufferSRV;
		MeshSection.TotalVertexCount= DispatchData[SectionIndex].PositionBuffer->NumBytes / (sizeof(float)*3);
		MeshSection.NumPrimitives	= Section.NumTriangles;
		MeshSection.NumVertices		= Section.NumVertices;
		MeshSection.IndexBaseIndex	= Section.BaseIndex;
		MeshSection.VertexBaseIndex = Section.BaseVertexIndex;
		MeshSection.IndexBuffer		= nullptr;
		MeshSection.TotalIndexCount	= 0;
		MeshSection.LODIndex		= 0;
		MeshSection.SectionIndex	= SectionIndex;
		return MeshSection;
	}

	bool IsSectionValid(int32 Section) const
	{
		const FSectionDispatchData& SectionData = DispatchData[Section];
		return SectionData.SectionIndex == Section;
	}

	bool IsSourceFactoryValid(int32 Section, FGPUBaseSkinVertexFactory* SourceVertexFactory) const
	{
		const FSectionDispatchData& SectionData = DispatchData[Section];
		return SectionData.SourceVertexFactory == SourceVertexFactory;
	}

	bool IsValid(FSkeletalMeshObjectGPUSkin* InSkin) const
	{
		return GPUSkin == InSkin && GPUSkin->GetLOD() == LOD;
	}

	void UpdateSkinWeightBuffer()
	{
		FSkinWeightVertexBuffer* WeightBuffer = GPUSkin->GetSkinWeightVertexBuffer(LOD);
		bUse16BitBoneIndex = WeightBuffer->Use16BitBoneIndex();
		InputWeightIndexSize = WeightBuffer->GetBoneIndexByteSize();
		InputWeightStride = WeightBuffer->GetConstantInfluencesVertexStride();
		InputWeightStreamSRV = WeightBuffer->GetDataVertexBuffer()->GetSRV();
		InputWeightLookupStreamSRV = WeightBuffer->GetLookupVertexBuffer()->GetSRV();
				
		if (WeightBuffer->GetBoneInfluenceType() == GPUSkinBoneInfluenceType::DefaultBoneInfluence)
		{
			int32 MaxBoneInfluences = WeightBuffer->GetMaxBoneInfluences();
			BoneInfluenceType = MaxBoneInfluences > MAX_INFLUENCES_PER_STREAM ? 1 : 0;
		}
		else
		{
			BoneInfluenceType = 2;
		}
	}

	void SetupSection(int32 SectionIndex, FGPUSkinCache::FRWBuffersAllocation* InPositionAllocation, FSkelMeshRenderSection* Section, const FMorphVertexBuffer* MorphVertexBuffer, const FSkeletalMeshVertexClothBuffer* ClothVertexBuffer,
		uint32 NumVertices, uint32 InputStreamStart, FGPUBaseSkinVertexFactory* InSourceVertexFactory, FGPUSkinPassthroughVertexFactory* InTargetVertexFactory)
	{
		//UE_LOG(LogSkinCache, Warning, TEXT("*** SetupSection E %p Alloc %p Sec %d(%p) LOD %d"), this, InAllocation, SectionIndex, Section, LOD);
		FSectionDispatchData& Data = DispatchData[SectionIndex];
		check(!Data.PositionTracker.Allocation || Data.PositionTracker.Allocation == InPositionAllocation);

		Data.PositionTracker.Allocation = InPositionAllocation;

		Data.SectionIndex = SectionIndex;
		Data.Section = Section;

		check(GPUSkin->GetLOD() == LOD);
		FSkeletalMeshRenderData& SkelMeshRenderData = GPUSkin->GetSkeletalMeshRenderData();
		FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData.LODRenderData[LOD];
		check(Data.SectionIndex == LodData.FindSectionIndex(*Section));

		Data.NumVertices = NumVertices;
		const bool bMorph = MorphVertexBuffer && MorphVertexBuffer->SectionIds.Contains(SectionIndex);
		if (bMorph)
		{
			// in bytes
			const uint32 MorphStride = sizeof(FMorphGPUSkinVertex);

			// see GPU code "check(MorphStride == sizeof(float) * 6);"
			check(MorphStride == sizeof(float) * 6);

			Data.MorphBufferOffset = Section->BaseVertexIndex;
		}
		if (ClothVertexBuffer && ClothVertexBuffer->GetClothIndexMapping().Num() > SectionIndex)
		{
			Data.ClothBufferOffset = (ClothVertexBuffer->GetClothIndexMapping()[SectionIndex] & 0xFFFFFFFF);
		}

		//INC_DWORD_STAT(STAT_GPUSkinCache_TotalNumChunks);

		// SkinType 0:normal, 1:with morph target, 2:with cloth
		Data.SkinType = ClothVertexBuffer ? 2 : (bMorph ? 1 : 0);
		Data.InputStreamStart = InputStreamStart;
		Data.OutputStreamStart = Section->BaseVertexIndex;

		Data.TangentBufferSRV = InSourceVertexFactory->GetTangentsSRV();
		Data.UVsBufferSRV = InSourceVertexFactory->GetTextureCoordinatesSRV();
		Data.ColorBufferSRV = InSourceVertexFactory->GetColorComponentsSRV();
		Data.NumTexCoords = InSourceVertexFactory->GetNumTexCoords();
		Data.PositionBufferSRV = InSourceVertexFactory->GetPositionsSRV();

		Data.NumBoneInfluences = InSourceVertexFactory->GetNumBoneInfluences();
		check(Data.TangentBufferSRV && Data.PositionBufferSRV);

		// weight buffer
		Data.InputWeightStart = (InputWeightStride * Section->BaseVertexIndex) / sizeof(float);
		Data.SourceVertexFactory = InSourceVertexFactory;
		Data.TargetVertexFactory = InTargetVertexFactory;

		InTargetVertexFactory->InvalidateStreams();

		int32 RecomputeTangentsMode = GForceRecomputeTangents > 0 ? 1 : GSkinCacheRecomputeTangents;
		if (RecomputeTangentsMode > 0)
		{
			if (Section->bRecomputeTangent || RecomputeTangentsMode == 1)
			{
				FRawStaticIndexBuffer16or32Interface* IndexBuffer = LodData.MultiSizeIndexContainer.GetIndexBuffer();
				Data.IndexBuffer = IndexBuffer->GetSRV();
				if (Data.IndexBuffer)
				{
					Data.NumTriangles = Section->NumTriangles;
					Data.IndexBufferOffsetValue = Section->BaseIndex;
				}
			}
		}
	}

#if RHI_RAYTRACING
	void GetRayTracingSegmentVertexBuffers(TArrayView<FRayTracingGeometrySegment> OutSegments) const
	{
		check(OutSegments.Num() == DispatchData.Num());

		for (int32 SectionIdx = 0; SectionIdx < DispatchData.Num(); SectionIdx++)
		{
			const FSectionDispatchData& SectionData = DispatchData[SectionIdx];
			FRayTracingGeometrySegment& Segment = OutSegments[SectionIdx];

			Segment.VertexBuffer = SectionData.PositionBuffer->Buffer;
			Segment.VertexBufferOffset = 0;

			check(SectionData.Section->NumTriangles == Segment.NumPrimitives);
		}
	}
#endif // RHI_RAYTRACING

protected:
	FGPUSkinCache::FRWBuffersAllocation* PositionAllocation;
	FGPUSkinCache* SkinCache;
	TArray<FGPUSkinBatchElementUserData> BatchElementsUserData;
	TArray<FSectionDispatchData> DispatchData;
	FSkeletalMeshObjectGPUSkin* GPUSkin;
	int BoneInfluenceType;
	bool bUse16BitBoneIndex;
	uint32 InputWeightIndexSize;
	uint32 InputWeightStride;
	uint32 VertexOffsetUsage = 0;
	FShaderResourceViewRHIRef InputWeightStreamSRV;
	FShaderResourceViewRHIRef InputWeightLookupStreamSRV;
	FRHIShaderResourceView* PreSkinningVertexOffsetSRV = nullptr;
	FRHIShaderResourceView* PostSkinningVertexOffsetSRV = nullptr;
	FRHIShaderResourceView* MorphBuffer;
	FShaderResourceViewRHIRef ClothBuffer;
	int32 LOD;

	bool bMultipleClothSkinInfluences;

	friend class FGPUSkinCache;
	friend class FBaseGPUSkinCacheCS;
	friend class FBaseRecomputeTangentsPerTriangleShader;
};

class FBaseGPUSkinCacheCS : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FBaseGPUSkinCacheCS, NonVirtual);
public:
	FBaseGPUSkinCacheCS() {}

	FBaseGPUSkinCacheCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		//DebugParameter.Bind(Initializer.ParameterMap, TEXT("DebugParameter"));

		NumVertices.Bind(Initializer.ParameterMap, TEXT("NumVertices"));
		SkinCacheStart.Bind(Initializer.ParameterMap, TEXT("SkinCacheStart"));
		BoneMatrices.Bind(Initializer.ParameterMap, TEXT("BoneMatrices"));
		TangentInputBuffer.Bind(Initializer.ParameterMap, TEXT("TangentInputBuffer"));
		PositionInputBuffer.Bind(Initializer.ParameterMap, TEXT("PositionInputBuffer"));

		VertexOffsetUsage.Bind(Initializer.ParameterMap, TEXT("VertexOffsetUsage"));
		PreSkinOffsets.Bind(Initializer.ParameterMap, TEXT("PreSkinOffsets"));
		PostSkinOffsets.Bind(Initializer.ParameterMap, TEXT("PostSkinOffsets"));

		InputStreamStart.Bind(Initializer.ParameterMap, TEXT("InputStreamStart"));

		NumBoneInfluences.Bind(Initializer.ParameterMap, TEXT("NumBoneInfluences"));
		InputWeightIndexSize.Bind(Initializer.ParameterMap, TEXT("InputWeightIndexSize"));
		InputWeightStart.Bind(Initializer.ParameterMap, TEXT("InputWeightStart"));
		InputWeightStride.Bind(Initializer.ParameterMap, TEXT("InputWeightStride"));
		InputWeightStream.Bind(Initializer.ParameterMap, TEXT("InputWeightStream"));
		InputWeightLookupStream.Bind(Initializer.ParameterMap, TEXT("InputWeightLookupStream"));

		PositionBufferUAV.Bind(Initializer.ParameterMap, TEXT("PositionBufferUAV"));
		TangentBufferUAV.Bind(Initializer.ParameterMap, TEXT("TangentBufferUAV"));

		MorphBuffer.Bind(Initializer.ParameterMap, TEXT("MorphBuffer"));
		MorphBufferOffset.Bind(Initializer.ParameterMap, TEXT("MorphBufferOffset"));
		SkinCacheDebug.Bind(Initializer.ParameterMap, TEXT("SkinCacheDebug"));

		ClothBuffer.Bind(Initializer.ParameterMap, TEXT("ClothBuffer"));
		ClothPositionsAndNormalsBuffer.Bind(Initializer.ParameterMap, TEXT("ClothPositionsAndNormalsBuffer"));
		ClothBufferOffset.Bind(Initializer.ParameterMap, TEXT("ClothBufferOffset"));
		ClothBlendWeight.Bind(Initializer.ParameterMap, TEXT("ClothBlendWeight"));
		ClothLocalToWorld.Bind(Initializer.ParameterMap, TEXT("ClothLocalToWorld"));
		ClothWorldToLocal.Bind(Initializer.ParameterMap, TEXT("ClothWorldToLocal"));
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList, 
		const FVertexBufferAndSRV& BoneBuffer,
		FGPUSkinCacheEntry* Entry,
		FGPUSkinCacheEntry::FSectionDispatchData& DispatchData,
		FRHIUnorderedAccessView* PositionUAV, FRHIUnorderedAccessView* TangentUAV
		)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, NumVertices, DispatchData.NumVertices);
		SetShaderValue(RHICmdList, ShaderRHI, InputStreamStart, DispatchData.InputStreamStart);

		check(BoneBuffer.VertexBufferSRV);
		SetSRVParameter(RHICmdList, ShaderRHI, BoneMatrices, BoneBuffer.VertexBufferSRV);

		SetSRVParameter(RHICmdList, ShaderRHI, TangentInputBuffer, DispatchData.TangentBufferSRV);
		SetSRVParameter(RHICmdList, ShaderRHI, PositionInputBuffer, DispatchData.PositionBufferSRV);

		SetShaderValue(RHICmdList, ShaderRHI, VertexOffsetUsage, Entry->VertexOffsetUsage);
		SetSRVParameter(RHICmdList, ShaderRHI, PreSkinOffsets, Entry->PreSkinningVertexOffsetSRV ? Entry->PreSkinningVertexOffsetSRV : GNullVertexBuffer.VertexBufferSRV.GetReference());
		SetSRVParameter(RHICmdList, ShaderRHI, PostSkinOffsets, Entry->PostSkinningVertexOffsetSRV ? Entry->PostSkinningVertexOffsetSRV : GNullVertexBuffer.VertexBufferSRV.GetReference());

		SetShaderValue(RHICmdList, ShaderRHI, NumBoneInfluences, DispatchData.NumBoneInfluences);
		SetShaderValue(RHICmdList, ShaderRHI, InputWeightIndexSize, Entry->InputWeightIndexSize);
		SetShaderValue(RHICmdList, ShaderRHI, InputWeightStart, DispatchData.InputWeightStart);
		SetShaderValue(RHICmdList, ShaderRHI, InputWeightStride, Entry->InputWeightStride);
		SetSRVParameter(RHICmdList, ShaderRHI, InputWeightStream, Entry->InputWeightStreamSRV);
		SetSRVParameter(RHICmdList, ShaderRHI, InputWeightLookupStream, Entry->InputWeightLookupStreamSRV);

		// output UAV
		SetUAVParameter(RHICmdList, ShaderRHI, PositionBufferUAV, PositionUAV);
		SetUAVParameter(RHICmdList, ShaderRHI, TangentBufferUAV, TangentUAV);
		SetShaderValue(RHICmdList, ShaderRHI, SkinCacheStart, DispatchData.OutputStreamStart);

		const bool bMorph = DispatchData.SkinType == 1;
		if (bMorph)
		{
			SetSRVParameter(RHICmdList, ShaderRHI, MorphBuffer, Entry->MorphBuffer);
			SetShaderValue(RHICmdList, ShaderRHI, MorphBufferOffset, DispatchData.MorphBufferOffset);
		}

		const bool bCloth = DispatchData.SkinType == 2;
		if (bCloth)
		{
			SetSRVParameter(RHICmdList, ShaderRHI, ClothBuffer, Entry->ClothBuffer);
			SetSRVParameter(RHICmdList, ShaderRHI, ClothPositionsAndNormalsBuffer, DispatchData.ClothPositionsAndNormalsBuffer);
			SetShaderValue(RHICmdList, ShaderRHI, ClothBufferOffset, DispatchData.ClothBufferOffset);
			SetShaderValue(RHICmdList, ShaderRHI, ClothBlendWeight, DispatchData.ClothBlendWeight);
			SetShaderValue(RHICmdList, ShaderRHI, ClothLocalToWorld, DispatchData.ClothLocalToWorld);
			SetShaderValue(RHICmdList, ShaderRHI, ClothWorldToLocal, DispatchData.ClothWorldToLocal);
		}

		SetShaderValue(RHICmdList, ShaderRHI, SkinCacheDebug, CVarGPUSkinCacheDebug.GetValueOnRenderThread());
	}


	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, PositionBufferUAV, 0);
		SetUAVParameter(RHICmdList, ShaderRHI, TangentBufferUAV, 0);
	}

private:
	
	LAYOUT_FIELD(FShaderParameter, NumVertices)
	LAYOUT_FIELD(FShaderParameter, SkinCacheDebug)
	LAYOUT_FIELD(FShaderParameter, InputStreamStart)
	LAYOUT_FIELD(FShaderParameter, SkinCacheStart)

	//LAYOUT_FIELD(FShaderParameter, DebugParameter)

	LAYOUT_FIELD(FShaderUniformBufferParameter, SkinUniformBuffer)

	LAYOUT_FIELD(FShaderResourceParameter, BoneMatrices)
	LAYOUT_FIELD(FShaderResourceParameter, TangentInputBuffer)
	LAYOUT_FIELD(FShaderResourceParameter, PositionInputBuffer)
	LAYOUT_FIELD(FShaderResourceParameter, PositionBufferUAV)
	LAYOUT_FIELD(FShaderResourceParameter, TangentBufferUAV)

	LAYOUT_FIELD(FShaderParameter, VertexOffsetUsage)
	LAYOUT_FIELD(FShaderResourceParameter, PreSkinOffsets)
	LAYOUT_FIELD(FShaderResourceParameter, PostSkinOffsets)

	LAYOUT_FIELD(FShaderParameter, NumBoneInfluences);
	LAYOUT_FIELD(FShaderParameter, InputWeightIndexSize);
	LAYOUT_FIELD(FShaderParameter, InputWeightStart)
	LAYOUT_FIELD(FShaderParameter, InputWeightStride)
	LAYOUT_FIELD(FShaderResourceParameter, InputWeightStream)
	LAYOUT_FIELD(FShaderResourceParameter, InputWeightLookupStream);

	LAYOUT_FIELD(FShaderResourceParameter, MorphBuffer)
	LAYOUT_FIELD(FShaderParameter, MorphBufferOffset)

	LAYOUT_FIELD(FShaderResourceParameter, ClothBuffer)
	LAYOUT_FIELD(FShaderResourceParameter, ClothPositionsAndNormalsBuffer)
	LAYOUT_FIELD(FShaderParameter, ClothBufferOffset)
	LAYOUT_FIELD(FShaderParameter, ClothBlendWeight)
	LAYOUT_FIELD(FShaderParameter, ClothLocalToWorld)
	LAYOUT_FIELD(FShaderParameter, ClothWorldToLocal)

};

/** Compute shader that skins a batch of vertices. */
// @param SkinType 0:normal, 1:with morph targets calculated outside the cache, 2: with cloth, 3:with morph target calculated insde the cache (not yet implemented)
//        BoneInfluenceType 0:normal, 1:extra bone influences, 2:unlimited bone influences
//        BoneIndex16 0: 8-bit indices, 1: 16-bit indices
//        MultipleClothInfluences 0:single influence 1:multiple influences
template <int Permutation>
class TGPUSkinCacheCS : public FBaseGPUSkinCacheCS
{
	constexpr static bool bMultipleClothInfluences = (32 == (Permutation & 32));
	constexpr static bool bBoneIndex16 = (16 == (Permutation & 16));
	constexpr static bool bUnlimitedBoneInfluence = (8 == (Permutation & 12));
	constexpr static bool bUseExtraBoneInfluencesT = (4 == (Permutation & 12));
	constexpr static bool bApexCloth = (2 == (Permutation & 3));
    constexpr static bool bMorphBlend = (1 == (Permutation & 3));

	DECLARE_SHADER_TYPE(TGPUSkinCacheCS, Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsGPUSkinCacheAvailable(Parameters.Platform) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const uint32 BoneIndex16 = bBoneIndex16;
		const uint32 UnlimitedBoneInfluence = bUnlimitedBoneInfluence;
		const uint32 UseExtraBoneInfluences = bUseExtraBoneInfluencesT;
		const uint32 MorphBlend = bMorphBlend;
		const uint32 ApexCloth = bApexCloth;
		const uint32 MultipleClothInfluences = bMultipleClothInfluences;
		OutEnvironment.SetDefine(TEXT("GPUSKIN_UNLIMITED_BONE_INFLUENCE"), UnlimitedBoneInfluence);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_USE_EXTRA_INFLUENCES"), UseExtraBoneInfluences);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_MORPH_BLEND"), MorphBlend);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_APEX_CLOTH"), ApexCloth);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_MULTIPLE_CLOTH_INFLUENCES"), MultipleClothInfluences);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_RWBUFFER_OFFSET_TANGENT_X"), FGPUSkinCache::RWTangentXOffsetInFloats);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_RWBUFFER_OFFSET_TANGENT_Z"), FGPUSkinCache::RWTangentZOffsetInFloats);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_BONE_INDEX_UINT16"), BoneIndex16);
	}

	TGPUSkinCacheCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseGPUSkinCacheCS(Initializer)
	{
	}

	TGPUSkinCacheCS()
	{
	}
};

IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<0>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_0, SkinType_0 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<1>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_0, SkinType_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<2>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_0, SkinType_2 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<4>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_1, SkinType_0 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<5>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_1, SkinType_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<6>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_1, SkinType_2 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<8>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_2, SkinType_0 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<9>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_2, SkinType_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<10>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_2, SkinType_2 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<16>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_0, SkinType_0 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<17>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_0, SkinType_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<18>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_0, SkinType_2 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<20>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_1, SkinType_0 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<21>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_1, SkinType_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<22>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_1, SkinType_2 
// Multi-influences for cloth:
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<34>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_0, SkinType_2, MultipleClothInfluences_1
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<38>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_1, SkinType_2, MultipleClothInfluences_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<42>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);	// 16bit_0, BoneInfluenceType_2, SkinType_2, MultipleClothInfluences_1
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<50>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_0, SkinType_2, MultipleClothInfluences_1 
IMPLEMENT_SHADER_TYPE(template<>, TGPUSkinCacheCS<54>, TEXT("/Engine/Private/GpuSkinCacheComputeShader.usf"), TEXT("SkinCacheUpdateBatchCS"), SF_Compute);  // 16bit_1, BoneInfluenceType_1, SkinType_2, MultipleClothInfluences_1 

FGPUSkinCache::FGPUSkinCache(bool bInRequiresMemoryLimit)
	: UsedMemoryInBytes(0)
	, ExtraRequiredMemory(0)
	, FlushCounter(0)
	, bRequiresMemoryLimit(bInRequiresMemoryLimit)
	, CurrentStagingBufferIndex(0)
{
}

FGPUSkinCache::~FGPUSkinCache()
{
	Cleanup();
}

void FGPUSkinCache::Cleanup()
{
	for (int32 Index = 0; Index < StagingBuffers.Num(); ++Index)
	{
		StagingBuffers[Index].Release();
	}

	while (Entries.Num() > 0)
	{
		ReleaseSkinCacheEntry(Entries.Last());
	}
	ensure(Allocations.Num() == 0);
}

void FGPUSkinCache::AddBufferToTransition(FRHIUnorderedAccessView* InUAV)
{
	// add UAV to set to remove duplicated entries but could still be different UAVs on the same resource
	// then this code will need better filtering because mutliple transitions on the same resource
	// is not allowed
	BuffersToTransition.Add(InUAV);
}

void FGPUSkinCache::TransitionAllToReadable(FRHICommandList& RHICmdList)
{
	if (BuffersToTransition.Num() > 0)
	{
		FMemMark Mark(FMemStack::Get());
		TArray<FRHITransitionInfo, SceneRenderingAllocator> UAVs;
		UAVs.Reserve(BuffersToTransition.Num());
		for (TSet<FRHIUnorderedAccessView*>::TConstIterator SetIt(BuffersToTransition); SetIt; ++SetIt)
		{
			UAVs.Add(FRHITransitionInfo(*SetIt, ERHIAccess::Unknown, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask));
		}
		RHICmdList.Transition(UAVs);

		BuffersToTransition.Empty(BuffersToTransition.Num());
	}
}

#if RHI_RAYTRACING
void FGPUSkinCache::CommitRayTracingGeometryUpdates(FRHICommandList& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, CommitSkeletalRayTracingGeometryUpdates);

	if (RayTracingGeometriesToUpdate.Num())
	{
		TArray<FAccelerationStructureBuildParams> Updates;
		for (const FRayTracingGeometry* RayTracingGeometry : RayTracingGeometriesToUpdate)
		{
			FAccelerationStructureBuildParams Params;
			Params.BuildMode = EAccelerationStructureBuildMode::Update;
			Params.Geometry = RayTracingGeometry->RayTracingGeometryRHI;
			Params.Segments = RayTracingGeometry->Initializer.Segments;
			Updates.Add(Params);
		}

		RHICmdList.BuildAccelerationStructures(Updates);
		RayTracingGeometriesToUpdate.Reset();
	}
}
#endif // RHI_RAYTRACING

#if 0
void FGPUSkinCache::TransitionToWriteable(FRHICommandList& RHICmdList)
{
	int32 BufferIndex = InternalUpdateCount % GPUSKINCACHE_FRAMES;
	RHICmdList.Transition(FRHITransitionInfo(SkinCacheBuffers[BufferIndex].UAV, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier));
}

void FGPUSkinCache::TransitionAllToWriteable(FRHICommandList& RHICmdList)
{
	if (bInitialized)
	{
		FRHITransitionInfo OutUAVs[GPUSKINCACHE_FRAMES];
		for (int32 Index = 0; Index < GPUSKINCACHE_FRAMES; ++Index)
		{
			OutUAVs[Index] = FRHITransitionInfo(SkinCacheBuffer[Index].UAV, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier);
		}
		RHICmdList.Transition(MakeArrayView(OutUAVs, UE_ARRAY_COUNT(OutUAVs)));
	}
}
#endif

/** base of the FRecomputeTangentsPerTrianglePassCS class */
class FBaseRecomputeTangentsPerTriangleShader : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FBaseRecomputeTangentsPerTriangleShader, NonVirtual);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// currently only implemented and tested on Window SM5 (needs Compute, Atomics, SRV for index buffers, UAV for VertexBuffers)
		return DoesPlatformSupportGPUSkinCache(Parameters.Platform) && IsGPUSkinCacheAvailable(Parameters.Platform);
	}

	static const uint32 ThreadGroupSizeX = 64;

	FBaseRecomputeTangentsPerTriangleShader()
	{}

	FBaseRecomputeTangentsPerTriangleShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		IntermediateAccumBufferUAV.Bind(Initializer.ParameterMap, TEXT("IntermediateAccumBufferUAV"));
		NumTriangles.Bind(Initializer.ParameterMap, TEXT("NumTriangles"));
		GPUPositionCacheBuffer.Bind(Initializer.ParameterMap, TEXT("GPUPositionCacheBuffer"));
		GPUTangentCacheBuffer.Bind(Initializer.ParameterMap, TEXT("GPUTangentCacheBuffer"));
		SkinCacheStart.Bind(Initializer.ParameterMap, TEXT("SkinCacheStart"));
		IndexBuffer.Bind(Initializer.ParameterMap, TEXT("IndexBuffer"));
		IndexBufferOffset.Bind(Initializer.ParameterMap, TEXT("IndexBufferOffset"));

		InputStreamStart.Bind(Initializer.ParameterMap, TEXT("InputStreamStart"));
		NumTexCoords.Bind(Initializer.ParameterMap, TEXT("NumTexCoords"));
		SelectedTexCoord.Bind(Initializer.ParameterMap, TEXT("SelectedTexCoord"));
		TangentInputBuffer.Bind(Initializer.ParameterMap, TEXT("TangentInputBuffer"));
		UVsInputBuffer.Bind(Initializer.ParameterMap, TEXT("UVsInputBuffer"));

        DuplicatedIndices.Bind(Initializer.ParameterMap, TEXT("DuplicatedIndices"));
        DuplicatedIndicesIndices.Bind(Initializer.ParameterMap, TEXT("DuplicatedIndicesIndices"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, FGPUSkinCacheEntry::FSectionDispatchData& DispatchData, FRWBuffer& StagingBuffer)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

//later		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View);

		SetShaderValue(RHICmdList, ShaderRHI, NumTriangles, DispatchData.NumTriangles);

		SetSRVParameter(RHICmdList, ShaderRHI, GPUPositionCacheBuffer, DispatchData.GetPositionRWBuffer()->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, GPUTangentCacheBuffer, GBlendUsingVertexColorForRecomputeTangents > 0 ? DispatchData.IntermediateTangentBuffer->SRV : DispatchData.GetTangentRWBuffer()->SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, UVsInputBuffer, DispatchData.UVsBufferSRV);

		SetShaderValue(RHICmdList, ShaderRHI, SkinCacheStart, DispatchData.OutputStreamStart);

		SetSRVParameter(RHICmdList, ShaderRHI, IndexBuffer, DispatchData.IndexBuffer);
		SetShaderValue(RHICmdList, ShaderRHI, IndexBufferOffset, DispatchData.IndexBufferOffsetValue);
		
		SetShaderValue(RHICmdList, ShaderRHI, InputStreamStart, DispatchData.InputStreamStart);
		SetShaderValue(RHICmdList, ShaderRHI, NumTexCoords, DispatchData.NumTexCoords);
		SetShaderValue(RHICmdList, ShaderRHI, SelectedTexCoord, DispatchData.SelectedTexCoord);
		SetSRVParameter(RHICmdList, ShaderRHI, TangentInputBuffer, DispatchData.TangentBufferSRV);
		SetSRVParameter(RHICmdList, ShaderRHI, TangentInputBuffer, DispatchData.UVsBufferSRV);

		// UAV
		SetUAVParameter(RHICmdList, ShaderRHI, IntermediateAccumBufferUAV, StagingBuffer.UAV);

        if (!GAllowDupedVertsForRecomputeTangents)
        {
		    SetSRVParameter(RHICmdList, ShaderRHI, DuplicatedIndices, DispatchData.DuplicatedIndices);
            SetSRVParameter(RHICmdList, ShaderRHI, DuplicatedIndicesIndices, DispatchData.DuplicatedIndicesIndices);
        }
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, IntermediateAccumBufferUAV, 0);
	}

	LAYOUT_FIELD(FShaderResourceParameter, IntermediateAccumBufferUAV);
	LAYOUT_FIELD(FShaderParameter, NumTriangles);
	LAYOUT_FIELD(FShaderResourceParameter, GPUPositionCacheBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, GPUTangentCacheBuffer);
	LAYOUT_FIELD(FShaderParameter, SkinCacheStart);
	LAYOUT_FIELD(FShaderResourceParameter, IndexBuffer);
	LAYOUT_FIELD(FShaderParameter, IndexBufferOffset);
	LAYOUT_FIELD(FShaderParameter, InputStreamStart);
	LAYOUT_FIELD(FShaderParameter, NumTexCoords);
	LAYOUT_FIELD(FShaderParameter, SelectedTexCoord);
	LAYOUT_FIELD(FShaderResourceParameter, TangentInputBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, UVsInputBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, DuplicatedIndices);
	LAYOUT_FIELD(FShaderResourceParameter, DuplicatedIndicesIndices);
};

/** Encapsulates the RecomputeSkinTangents compute shader. */
template <int Permutation>
class FRecomputeTangentsPerTrianglePassCS : public FBaseRecomputeTangentsPerTriangleShader
{
    constexpr static bool bMergeDuplicatedVerts = (2 == (Permutation & 2));
	constexpr static bool bFullPrecisionUV = (1 == (Permutation & 1));

	DECLARE_SHADER_TYPE(FRecomputeTangentsPerTrianglePassCS, Global);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MERGE_DUPLICATED_VERTICES"), bMergeDuplicatedVerts);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INTERMEDIATE_ACCUM_BUFFER_NUM_INTS"), FGPUSkinCache::IntermediateAccumBufferNumInts);
		OutEnvironment.SetDefine(TEXT("FULL_PRECISION_UV"), bFullPrecisionUV);
	}

	FRecomputeTangentsPerTrianglePassCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseRecomputeTangentsPerTriangleShader(Initializer)
	{
	}

	FRecomputeTangentsPerTrianglePassCS()
	{}
};

IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerTrianglePassCS<0>, TEXT("/Engine/Private/RecomputeTangentsPerTrianglePass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerTrianglePassCS<1>, TEXT("/Engine/Private/RecomputeTangentsPerTrianglePass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerTrianglePassCS<2>, TEXT("/Engine/Private/RecomputeTangentsPerTrianglePass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerTrianglePassCS<3>, TEXT("/Engine/Private/RecomputeTangentsPerTrianglePass.usf"), TEXT("MainCS"), SF_Compute);

/** Encapsulates the RecomputeSkinTangentsResolve compute shader. */
class FBaseRecomputeTangentsPerVertexShader : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FBaseRecomputeTangentsPerVertexShader, NonVirtual);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// currently only implemented and tested on Window SM5 (needs Compute, Atomics, SRV for index buffers, UAV for VertexBuffers)
		return DoesPlatformSupportGPUSkinCache(Parameters.Platform) && IsGPUSkinCacheAvailable(Parameters.Platform);
	}

	static const uint32 ThreadGroupSizeX = 64;

	LAYOUT_FIELD(FShaderResourceParameter, IntermediateAccumBufferUAV);
	LAYOUT_FIELD(FShaderResourceParameter, TangentBufferUAV);
	LAYOUT_FIELD(FShaderResourceParameter, TangentInputBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, ColorInputBuffer);
	LAYOUT_FIELD(FShaderParameter, SkinCacheStart);
	LAYOUT_FIELD(FShaderParameter, NumVertices);
	LAYOUT_FIELD(FShaderParameter, InputStreamStart);
	LAYOUT_FIELD(FShaderParameter, VertexColorChannel); // which channel to use to read mask colors (0-R, 1-G, 2-B)

	FBaseRecomputeTangentsPerVertexShader() {}

	FBaseRecomputeTangentsPerVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		IntermediateAccumBufferUAV.Bind(Initializer.ParameterMap, TEXT("IntermediateAccumBufferUAV"));
		TangentBufferUAV.Bind(Initializer.ParameterMap, TEXT("TangentBufferUAV"));
		TangentInputBuffer.Bind(Initializer.ParameterMap, TEXT("TangentInputBuffer"));
		ColorInputBuffer.Bind(Initializer.ParameterMap, TEXT("ColorInputBuffer"));
		SkinCacheStart.Bind(Initializer.ParameterMap, TEXT("SkinCacheStart"));
		NumVertices.Bind(Initializer.ParameterMap, TEXT("NumVertices"));
		InputStreamStart.Bind(Initializer.ParameterMap, TEXT("InputStreamStart"));
		VertexColorChannel.Bind(Initializer.ParameterMap, TEXT("VertexColorChannel"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, FGPUSkinCacheEntry::FSectionDispatchData& DispatchData, FRWBuffer& StagingBuffer)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		check(StagingBuffer.UAV);

		//later		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View);

		SetShaderValue(RHICmdList, ShaderRHI, SkinCacheStart, DispatchData.OutputStreamStart);
		SetShaderValue(RHICmdList, ShaderRHI, NumVertices, DispatchData.NumVertices);
		SetShaderValue(RHICmdList, ShaderRHI, InputStreamStart, DispatchData.InputStreamStart);
		SetShaderValue(RHICmdList, ShaderRHI, VertexColorChannel, uint32(DispatchData.Section->RecomputeTangentsVertexMaskChannel));

		// UAVs
		SetUAVParameter(RHICmdList, ShaderRHI, IntermediateAccumBufferUAV, StagingBuffer.UAV);
		SetUAVParameter(RHICmdList, ShaderRHI, TangentBufferUAV, DispatchData.GetTangentRWBuffer()->UAV);

		SetSRVParameter(RHICmdList, ShaderRHI, TangentInputBuffer, DispatchData.IntermediateTangentBuffer ? DispatchData.IntermediateTangentBuffer->SRV : nullptr);

		SetSRVParameter(RHICmdList, ShaderRHI, ColorInputBuffer, DispatchData.ColorBufferSRV);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, TangentBufferUAV, 0);
		SetUAVParameter(RHICmdList, ShaderRHI, IntermediateAccumBufferUAV, 0);
	}
};

template <int Permutation>
class FRecomputeTangentsPerVertexPassCS : public FBaseRecomputeTangentsPerVertexShader
{
	DECLARE_SHADER_TYPE(FRecomputeTangentsPerVertexPassCS, Global);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// this pass cannot read the input as it doesn't have the permutation
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_RWBUFFER_OFFSET_TANGENT_X"), FGPUSkinCache::RWTangentXOffsetInFloats);
		OutEnvironment.SetDefine(TEXT("GPUSKIN_RWBUFFER_OFFSET_TANGENT_Z"), FGPUSkinCache::RWTangentZOffsetInFloats);
		OutEnvironment.SetDefine(TEXT("INTERMEDIATE_ACCUM_BUFFER_NUM_INTS"), FGPUSkinCache::IntermediateAccumBufferNumInts);
		OutEnvironment.SetDefine(TEXT("BLEND_USING_VERTEX_COLOR"), Permutation);
	}

	FRecomputeTangentsPerVertexPassCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseRecomputeTangentsPerVertexShader(Initializer)
	{
	}

	FRecomputeTangentsPerVertexPassCS()
	{}
};

IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerVertexPassCS<0>, TEXT("/Engine/Private/RecomputeTangentsPerVertexPass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerVertexPassCS<1>, TEXT("/Engine/Private/RecomputeTangentsPerVertexPass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerVertexPassCS<2>, TEXT("/Engine/Private/RecomputeTangentsPerVertexPass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerVertexPassCS<3>, TEXT("/Engine/Private/RecomputeTangentsPerVertexPass.usf"), TEXT("MainCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FRecomputeTangentsPerVertexPassCS<4>, TEXT("/Engine/Private/RecomputeTangentsPerVertexPass.usf"), TEXT("MainCS"), SF_Compute);

void FGPUSkinCache::DispatchUpdateSkinTangents(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, int32 SectionIndex)
{
	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[SectionIndex];

	{
		// no need to clear the intermediate buffer because we create it cleared and clear it after each usage in the per vertex pass

		FSkeletalMeshRenderData& SkelMeshRenderData = Entry->GPUSkin->GetSkeletalMeshRenderData();
		int32 LODIndex = Entry->LOD;
		FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData.LODRenderData[Entry->LOD];

		//SetRenderTarget(RHICmdList, FTextureRHIRef(), FTextureRHIRef());

		FRawStaticIndexBuffer16or32Interface* IndexBuffer = LodData.MultiSizeIndexContainer.GetIndexBuffer();
		FRHIIndexBuffer* IndexBufferRHI = IndexBuffer->IndexBufferRHI;

		const uint32 RequiredVertexCount = LodData.GetNumVertices();

		uint32 MaxVertexCount = RequiredVertexCount;

		if (StagingBuffers.Num() != GNumTangentIntermediateBuffers)
		{
			// Release extra buffers if shrinking
			for (int32 Index = GNumTangentIntermediateBuffers; Index < StagingBuffers.Num(); ++Index)
			{
				StagingBuffers[Index].Release();
			}
			StagingBuffers.SetNum(GNumTangentIntermediateBuffers, false);
		}

		uint32 NumIntsPerBuffer = DispatchData.NumTriangles * 3 * FGPUSkinCache::IntermediateAccumBufferNumInts;
		CurrentStagingBufferIndex = (CurrentStagingBufferIndex + 1) % StagingBuffers.Num();
		FRWBuffer& StagingBuffer = StagingBuffers[CurrentStagingBufferIndex];
		if (StagingBuffer.NumBytes < NumIntsPerBuffer * sizeof(uint32))
		{
			StagingBuffer.Release();
			StagingBuffer.Initialize(sizeof(int32), NumIntsPerBuffer, PF_R32_SINT, BUF_UnorderedAccess, TEXT("SkinTangentIntermediate"));
			RHICmdList.BindDebugLabelName(StagingBuffer.UAV, TEXT("SkinTangentIntermediate"));

			const uint32 MemSize = NumIntsPerBuffer * sizeof(uint32);
			SET_MEMORY_STAT(STAT_GPUSkinCache_TangentsIntermediateMemUsed, MemSize);

			// The UAV must be zero-filled. We leave it zeroed after each round (see RecomputeTangentsPerVertexPass.usf), so this is only needed on when the buffer is first created.
			RHICmdList.ClearUAVUint(StagingBuffer.UAV, FUintVector4(0, 0, 0, 0));
		}

		// This code can be optimized by batched up and doing it with less Dispatch calls (costs more memory)
		{
			auto* GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<0>> ComputeShader00(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<1>> ComputeShader01(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<2>> ComputeShader10(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<3>> ComputeShader11(GlobalShaderMap);

			bool bFullPrecisionUV = LodData.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs();

			TShaderRef<FBaseRecomputeTangentsPerTriangleShader> Shader;

			if (bFullPrecisionUV)
			{
				if (GAllowDupedVertsForRecomputeTangents) Shader = ComputeShader11;
				else Shader = ComputeShader01;
			}
			else
			{
				if (GAllowDupedVertsForRecomputeTangents) Shader = ComputeShader10;
				else Shader = ComputeShader00;
			}

			check(Shader.IsValid());

			uint32 NumTriangles = DispatchData.NumTriangles;
			uint32 ThreadGroupCountValue = FMath::DivideAndRoundUp(NumTriangles, FBaseRecomputeTangentsPerTriangleShader::ThreadGroupSizeX);

			SCOPED_DRAW_EVENTF(RHICmdList, SkinTangents_PerTrianglePass, TEXT("TangentsTri IndexStart=%d Tri=%d BoneInfluenceType=%d UVPrecision=%d"),
				DispatchData.IndexBufferOffsetValue, DispatchData.NumTriangles, Entry->BoneInfluenceType, bFullPrecisionUV);

			FRHIComputeShader* ShaderRHI = Shader.GetComputeShader();
			RHICmdList.SetComputeShader(ShaderRHI);

			RHICmdList.Transition(FRHITransitionInfo(StagingBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier));

			if (!GAllowDupedVertsForRecomputeTangents)
			{
				check(LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.DupVertData.Num() && LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.DupVertIndexData.Num());
				DispatchData.DuplicatedIndices = LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.DuplicatedVerticesIndexBuffer.VertexBufferSRV;
				DispatchData.DuplicatedIndicesIndices = LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.LengthAndIndexDuplicatedVerticesIndexBuffer.VertexBufferSRV;
			}

			RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetPositionRWBuffer()->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			if (!GBlendUsingVertexColorForRecomputeTangents)
			{
				RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetTangentRWBuffer()->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			}
			else if (DispatchData.IntermediateTangentBuffer)
			{
				RHICmdList.Transition(FRHITransitionInfo(DispatchData.IntermediateTangentBuffer->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			}

			INC_DWORD_STAT_BY(STAT_GPUSkinCache_NumTrianglesForRecomputeTangents, NumTriangles);
			Shader->SetParameters(RHICmdList, Entry, DispatchData, StagingBuffer);
			DispatchComputeShader(RHICmdList, Shader.GetShader(), ThreadGroupCountValue, 1, 1);
			Shader->UnsetParameters(RHICmdList);
		}

		{
			SCOPED_DRAW_EVENTF(RHICmdList, SkinTangents_PerVertexPass, TEXT("TangentsVertex InputStreamStart=%d, OutputStreamStart=%d, Vert=%d"),
				DispatchData.InputStreamStart, DispatchData.OutputStreamStart, DispatchData.NumVertices);
			//#todo-gpuskin Feature level?
			auto* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FRecomputeTangentsPerVertexPassCS<0>> ComputeShader0(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerVertexPassCS<1>> ComputeShader1(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerVertexPassCS<2>> ComputeShader2(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerVertexPassCS<3>> ComputeShader3(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerVertexPassCS<4>> ComputeShader4(GlobalShaderMap);
			TShaderRef<FBaseRecomputeTangentsPerVertexShader> ComputeShader;
			if (GBlendUsingVertexColorForRecomputeTangents == 1)
				ComputeShader = ComputeShader1;
			else if (GBlendUsingVertexColorForRecomputeTangents == 2)
				ComputeShader = ComputeShader2;
			else if (GBlendUsingVertexColorForRecomputeTangents == 3)
				ComputeShader = ComputeShader3;
			else if (GBlendUsingVertexColorForRecomputeTangents == 4)
				ComputeShader = ComputeShader4;
			else
				ComputeShader = ComputeShader0;
			RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

			uint32 VertexCount = DispatchData.NumVertices;
			uint32 ThreadGroupCountValue = FMath::DivideAndRoundUp(VertexCount, ComputeShader->ThreadGroupSizeX);

			RHICmdList.Transition(FRHITransitionInfo(StagingBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
			RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetTangentRWBuffer()->UAV.GetReference(), GBlendUsingVertexColorForRecomputeTangents ? ERHIAccess::Unknown : ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));

			ComputeShader->SetParameters(RHICmdList, Entry, DispatchData, StagingBuffer);
			DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), ThreadGroupCountValue, 1, 1);
			ComputeShader->UnsetParameters(RHICmdList);
		}
		// todo				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, TangentsBlendBuffer.VertexBufferSRV);
		//			ensureMsgf(DestRenderTarget.TargetableTexture == DestRenderTarget.ShaderResourceTexture, TEXT("%s should be resolved to a separate SRV"), *DestRenderTarget.TargetableTexture->GetName().ToString());	
	}
}

FGPUSkinCache::FRWBuffersAllocation* FGPUSkinCache::TryAllocBuffer(uint32 NumVertices, bool WithTangnents)
{
	uint64 MaxSizeInBytes = (uint64)(GSkinCacheSceneMemoryLimitInMB * 1024.0f * 1024.0f);
	uint64 RequiredMemInBytes = FRWBuffersAllocation::CalculateRequiredMemory(NumVertices, WithTangnents);
	if (bRequiresMemoryLimit && UsedMemoryInBytes + RequiredMemInBytes >= MaxSizeInBytes)
	{
		ExtraRequiredMemory += RequiredMemInBytes;

		// Can't fit
		return nullptr;
	}

	FRWBuffersAllocation* NewAllocation = new FRWBuffersAllocation(NumVertices, WithTangnents);
	Allocations.Add(NewAllocation);

	UsedMemoryInBytes += RequiredMemInBytes;
	INC_MEMORY_STAT_BY(STAT_GPUSkinCache_TotalMemUsed, RequiredMemInBytes);

	return NewAllocation;
}

void FGPUSkinCache::DoDispatch(FRHICommandListImmediate& RHICmdList)
{
	int32 BatchCount = BatchDispatches.Num();
	INC_DWORD_STAT_BY(STAT_GPUSkinCache_TotalNumChunks, BatchCount);

	TArray<FRHIUnorderedAccessView*> OverlappedUAVBuffers;
	OverlappedUAVBuffers.Reserve(BatchCount * 2);
	{
		for (int32 i = 0; i < BatchCount; ++i)
		{
			FDispatchEntry& DispatchItem = BatchDispatches[i];
			PrepareUpdateSkinning(DispatchItem.SkinCacheEntry, DispatchItem.Section, DispatchItem.RevisionNumber, &OverlappedUAVBuffers);
		}

		Algo::Sort(OverlappedUAVBuffers, [](const FRHIUnorderedAccessView* A, const FRHIUnorderedAccessView* B){return A < B;});
		OverlappedUAVBuffers.SetNum(Algo::Unique(OverlappedUAVBuffers));
	}

	RHICmdList.BeginUAVOverlap(OverlappedUAVBuffers);
	for (int32 i = 0; i < BatchCount; ++i)
	{
		FDispatchEntry& DispatchItem = BatchDispatches[i];
		DispatchUpdateSkinning(RHICmdList, DispatchItem.SkinCacheEntry, DispatchItem.Section, DispatchItem.RevisionNumber);
	}
	RHICmdList.EndUAVOverlap(OverlappedUAVBuffers);

	for (int32 i = 0; i < BatchCount; ++i)
	{
		FDispatchEntry& DispatchItem = BatchDispatches[i];
		DispatchItem.SkinCacheEntry->UpdateVertexFactoryDeclaration(DispatchItem.Section);

		if (DispatchItem.SkinCacheEntry->DispatchData[DispatchItem.Section].IndexBuffer)
		{
			DispatchUpdateSkinTangents(RHICmdList, DispatchItem.SkinCacheEntry, DispatchItem.Section);
		}

		DispatchItem.SkinCacheEntry->UpdateVertexFactoryDeclaration(DispatchItem.Section);
	}
}

void FGPUSkinCache::DoDispatch(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* SkinCacheEntry, int32 Section, int32 RevisionNumber)
{
	INC_DWORD_STAT(STAT_GPUSkinCache_TotalNumChunks);
	PrepareUpdateSkinning(SkinCacheEntry, Section, RevisionNumber, nullptr);
	DispatchUpdateSkinning(RHICmdList, SkinCacheEntry, Section, RevisionNumber);
	//RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DispatchData.GetRWBuffer());
	SkinCacheEntry->UpdateVertexFactoryDeclaration(Section);

	if (SkinCacheEntry->DispatchData[Section].IndexBuffer)
	{
		DispatchUpdateSkinTangents(RHICmdList, SkinCacheEntry, Section);
	}
}

void FGPUSkinCache::ProcessEntry(
	FRHICommandListImmediate& RHICmdList, 
	FGPUBaseSkinVertexFactory* VertexFactory,
	FGPUSkinPassthroughVertexFactory* TargetVertexFactory, 
	const FSkelMeshRenderSection& BatchElement, 
	FSkeletalMeshObjectGPUSkin* Skin,
	FVertexOffsetBuffers* VertexOffsetBuffers,
	const FMorphVertexBuffer* MorphVertexBuffer,
	const FSkeletalMeshVertexClothBuffer* ClothVertexBuffer, 
	const FClothSimulData* SimData,
	const FMatrix& ClothLocalToWorld, 
	float ClothBlendWeight, 
	uint32 RevisionNumber, 
	int32 Section, 
	FGPUSkinCacheEntry*& InOutEntry
	)
{
	INC_DWORD_STAT(STAT_GPUSkinCache_NumSectionsProcessed);

	const int32 NumVertices = BatchElement.GetNumVertices();
	//#todo-gpuskin Check that stream 0 is the position stream
	const uint32 InputStreamStart = BatchElement.BaseVertexIndex;

	FSkeletalMeshRenderData& SkelMeshRenderData = Skin->GetSkeletalMeshRenderData();
	int32 LODIndex = Skin->GetLOD();
	FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData.LODRenderData[LODIndex];

	if (FlushCounter < GGPUSkinCacheFlushCounter)
	{
		FlushCounter = GGPUSkinCacheFlushCounter;
		InvalidateAllEntries();
	}

	if (InOutEntry)
	{
		// If the LOD changed, the entry has to be invalidated
		if (!InOutEntry->IsValid(Skin))
		{
			Release(InOutEntry);
			InOutEntry = nullptr;
		}
		else
		{
			if (!InOutEntry->IsSectionValid(Section) || !InOutEntry->IsSourceFactoryValid(Section, VertexFactory))
			{
				// This section might not be valid yet, so set it up
				InOutEntry->SetupSection(Section, InOutEntry->PositionAllocation, &LodData.RenderSections[Section], MorphVertexBuffer, ClothVertexBuffer, NumVertices, InputStreamStart, VertexFactory, TargetVertexFactory);
			}
		}
	}

	int32 RecomputeTangentsMode = GForceRecomputeTangents > 0 ? 1 : GSkinCacheRecomputeTangents;
	// Try to allocate a new entry
	if (!InOutEntry)
	{
		bool WithTangents = RecomputeTangentsMode > 0;
		int32 TotalNumVertices = VertexFactory->GetNumVertices();
		FRWBuffersAllocation* NewPositionAllocation = TryAllocBuffer(TotalNumVertices, WithTangents);
		if (!NewPositionAllocation)
		{
			// Couldn't fit; caller will notify OOM
			return;
		}

		InOutEntry = new FGPUSkinCacheEntry(this, Skin, NewPositionAllocation);
		InOutEntry->GPUSkin = Skin;

		InOutEntry->SetupSection(Section, NewPositionAllocation, &LodData.RenderSections[Section], MorphVertexBuffer, ClothVertexBuffer, NumVertices, InputStreamStart, VertexFactory, TargetVertexFactory);
		Entries.Add(InOutEntry);
	}

	InOutEntry->VertexOffsetUsage = VertexOffsetBuffers->GetUsage();
	InOutEntry->PreSkinningVertexOffsetSRV = VertexOffsetBuffers->PreSkinningOffsetsVertexBuffer.GetSRV();
	InOutEntry->PostSkinningVertexOffsetSRV = VertexOffsetBuffers->PostSkinningOffsetsVertexBuffer.GetSRV();

	const bool bMorph = MorphVertexBuffer && MorphVertexBuffer->SectionIds.Contains(Section);
	if (bMorph)
	{
		InOutEntry->MorphBuffer = MorphVertexBuffer->GetSRV();
		check(InOutEntry->MorphBuffer);

		const uint32 MorphStride = sizeof(FMorphGPUSkinVertex);

		// see GPU code "check(MorphStride == sizeof(float) * 6);"
		check(MorphStride == sizeof(float) * 6);

		InOutEntry->DispatchData[Section].MorphBufferOffset = BatchElement.BaseVertexIndex;

		// weight buffer
		FSkinWeightVertexBuffer* WeightBuffer = Skin->GetSkinWeightVertexBuffer(LODIndex);
		uint32 WeightStride = WeightBuffer->GetConstantInfluencesVertexStride();
		InOutEntry->DispatchData[Section].InputWeightStart = (WeightStride * BatchElement.BaseVertexIndex) / sizeof(float);
		InOutEntry->InputWeightStride = WeightStride;
		InOutEntry->InputWeightStreamSRV = WeightBuffer->GetDataVertexBuffer()->GetSRV();
	}

    FVertexBufferAndSRV ClothPositionAndNormalsBuffer;
    TSkeletalMeshVertexData<FClothSimulEntry> VertexAndNormalData(true);
    if (ClothVertexBuffer)
    {
        InOutEntry->ClothBuffer = ClothVertexBuffer->GetSRV();
        check(InOutEntry->ClothBuffer);

        check(SimData->Positions.Num() == SimData->Normals.Num());
        VertexAndNormalData.ResizeBuffer( SimData->Positions.Num() );

        uint8* Data = VertexAndNormalData.GetDataPointer();
        uint32 Stride = VertexAndNormalData.GetStride();

        // Copy the vertices into the buffer.
        checkSlow(Stride*VertexAndNormalData.GetNumVertices() == sizeof(FClothSimulEntry) * SimData->Positions.Num());
        check(sizeof(FClothSimulEntry) == 6 * sizeof(float));
		
		if (ClothVertexBuffer && ClothVertexBuffer->GetClothIndexMapping().Num() > Section)
		{
			InOutEntry->DispatchData[Section].ClothBufferOffset = (ClothVertexBuffer->GetClothIndexMapping()[Section] & 0xFFFFFFFF);
		}

        for (int32 Index = 0;Index < SimData->Positions.Num();Index++)
        {
            FClothSimulEntry NewEntry;
            NewEntry.Position = SimData->Positions[Index];
            NewEntry.Normal = SimData->Normals[Index];
            *((FClothSimulEntry*)(Data + Index * Stride)) = NewEntry;
        }

        FResourceArrayInterface* ResourceArray = VertexAndNormalData.GetResourceArray();
        check(ResourceArray->GetResourceDataSize() > 0);

        FRHIResourceCreateInfo CreateInfo(ResourceArray);
        ClothPositionAndNormalsBuffer.VertexBufferRHI = RHICreateVertexBuffer( ResourceArray->GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
        ClothPositionAndNormalsBuffer.VertexBufferSRV = RHICreateShaderResourceView(ClothPositionAndNormalsBuffer.VertexBufferRHI, sizeof(FVector2D), PF_G32R32F);
        InOutEntry->DispatchData[Section].ClothPositionsAndNormalsBuffer = ClothPositionAndNormalsBuffer.VertexBufferSRV;

        InOutEntry->DispatchData[Section].ClothBlendWeight = ClothBlendWeight;
        InOutEntry->DispatchData[Section].ClothLocalToWorld = ClothLocalToWorld;
        InOutEntry->DispatchData[Section].ClothWorldToLocal = ClothLocalToWorld.Inverse();

		const int32 NumWrapWeights = InOutEntry->DispatchData[Section].Section->ClothMappingData.Num();
		if (NumWrapWeights > NumVertices)
		{
			InOutEntry->bMultipleClothSkinInfluences = true;
		}
		else
		{
			InOutEntry->bMultipleClothSkinInfluences = false;
		}
    }
    InOutEntry->DispatchData[Section].SkinType = ClothVertexBuffer ? 2 : (bMorph ? 1 : 0);

	if (bShouldBatchDispatches)
	{
		BatchDispatches.Add({
			InOutEntry,
			&LodData,
			RevisionNumber,
			uint32(Section),
#if RHI_RAYTRACING
			Skin->bRequireRecreatingRayTracingGeometry,
#else
			false,
#endif
			Skin->DoesAnySegmentUsesWorldPositionOffset()
			});
	}
	else
	{
		DoDispatch(RHICmdList, InOutEntry, Section, RevisionNumber);
	}
}

#if RHI_RAYTRACING
void FGPUSkinCache::ProcessRayTracingGeometryToUpdate(
	FRHICommandListImmediate& RHICmdList,
	FGPUSkinCacheEntry* SkinCacheEntry,
	FSkeletalMeshLODRenderData& LODModel,
	bool bRequireRecreatingRayTracingGeometry,
	bool bAnySegmentUsesWorldPositionOffset
	)
{
	if (IsRayTracingEnabled() && GEnableGPUSkinCache && SkinCacheEntry)
	{
		FRayTracingGeometry& RayTracingGeometry = SkinCacheEntry->GPUSkin->RayTracingGeometry;

		if (bRequireRecreatingRayTracingGeometry)
		{
			uint32 MemoryEstimation = 0;

			FIndexBufferRHIRef IndexBufferRHI = LODModel.MultiSizeIndexContainer.GetIndexBuffer()->IndexBufferRHI;
			MemoryEstimation += IndexBufferRHI->GetSize();
			uint32 VertexBufferStride = LODModel.StaticVertexBuffers.PositionVertexBuffer.GetStride();
			MemoryEstimation += LODModel.StaticVertexBuffers.PositionVertexBuffer.VertexBufferRHI->GetSize();

			//#dxr_todo: do we need support for separate sections in FRayTracingGeometryData?
			uint32 TrianglesCount = 0;
			for (int32 SectionIndex = 0; SectionIndex < LODModel.RenderSections.Num(); SectionIndex++)
			{
				const FSkelMeshRenderSection& Section = LODModel.RenderSections[SectionIndex];
				TrianglesCount += Section.NumTriangles;
			}

			FRayTracingGeometryInitializer Initializer;
			static const FName DebugName("FSkeletalMeshObjectGPUSkin");
			static int32 DebugNumber = 0;
			Initializer.DebugName = FName(DebugName, DebugNumber++);

			FRHIResourceCreateInfo CreateInfo;

			Initializer.IndexBuffer = IndexBufferRHI;
			Initializer.TotalPrimitiveCount = TrianglesCount;
			Initializer.GeometryType = RTGT_Triangles;
			Initializer.bFastBuild = true;
			Initializer.bAllowUpdate = true;

			Initializer.Segments.Reserve(LODModel.RenderSections.Num());
			for (const FSkelMeshRenderSection& Section : LODModel.RenderSections)
			{
				FRayTracingGeometrySegment Segment;
				Segment.VertexBuffer = nullptr;
				Segment.VertexBufferElementType = VET_Float3;
				Segment.VertexBufferStride = VertexBufferStride;
				Segment.VertexBufferOffset = 0;
				Segment.FirstPrimitive = Section.BaseIndex / 3;
				Segment.NumPrimitives = Section.NumTriangles;
				Segment.bEnabled = !Section.bDisabled;
				Initializer.Segments.Add(Segment);
			}

			FGPUSkinCache::GetRayTracingSegmentVertexBuffers(*SkinCacheEntry, Initializer.Segments);

			// Flush pending resource barriers before BVH is built for the first time
			TransitionAllToReadable(RHICmdList);

			if (RayTracingGeometry.RayTracingGeometryRHI.IsValid())
			{
				// CreateRayTracingGeometry releases the old RT geometry, however due to the deferred deletion nature of RHI resources
				// they will not be released until the end of the frame. We may get OOM in the middle of batched updates if not flushing.
				// This memory size is an estimation based on vertex & index buffer size. In reality the flush happens at 2-3x of the number specified.
				RayTracingGeometryMemoryPendingRelease += MemoryEstimation;

				if (RayTracingGeometryMemoryPendingRelease >= GMemoryLimitForBatchedRayTracingGeometryUpdates * 1024ull * 1024ull)
				{
					RayTracingGeometryMemoryPendingRelease = 0;
					RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
					UE_LOG(LogSkinCache, Display, TEXT("Flushing RHI resource pending deletes due to %d MB limit"), GMemoryLimitForBatchedRayTracingGeometryUpdates);
				}
			}

			if (LODModel.RayTracingData.Num())
			{
				Initializer.OfflineData = &LODModel.RayTracingData;
				Initializer.bDiscardOfflineData = false; // The RayTracingData can be used for multiple SkeletalMeshObjects , so we need to keep it around
			}

			RayTracingGeometry.SetInitializer(Initializer);
			RayTracingGeometry.CreateRayTracingGeometry(ERTAccelerationStructureBuildPriority::Immediate);
		}
		else
		{
			// If we are not using world position offset in material, handle BLAS refit here
			if (!bAnySegmentUsesWorldPositionOffset)
			{
				// Refit BLAS with new vertex buffer data
				FGPUSkinCache::GetRayTracingSegmentVertexBuffers(*SkinCacheEntry, RayTracingGeometry.Initializer.Segments);
				AddRayTracingGeometryToUpdate(&RayTracingGeometry);
			}
			else
			{
				// Otherwise, we will run the dynamic ray tracing geometry path, i.e. runnning VSinCS and refit geometry there, so do nothing here
			}
		}
	}
}
#endif

void FGPUSkinCache::BeginBatchDispatch(FRHICommandListImmediate& RHICmdList)
{
	check(BatchDispatches.Num() == 0);
	bShouldBatchDispatches = true;
}

void FGPUSkinCache::EndBatchDispatch(FRHICommandListImmediate& RHICmdList)
{
	DoDispatch(RHICmdList);

#if RHI_RAYTRACING
	if (IsRayTracingEnabled() && GEnableGPUSkinCache)
	{
		TSet<FGPUSkinCacheEntry*> SkinCacheEntriesProcessed;

		// Process batched dispatches in reverse order to filter out duplicated ones and keep the last one
		for (int32 Index = BatchDispatches.Num() - 1; Index >= 0; Index--)
		{
			FDispatchEntry& DispatchItem = BatchDispatches[Index];

			FGPUSkinCacheEntry* SkinCacheEntry = DispatchItem.SkinCacheEntry;
			FSkeletalMeshLODRenderData& LODModel = *DispatchItem.LODModel;

			if (SkinCacheEntriesProcessed.Contains(SkinCacheEntry))
			{
				continue;
			}

			SkinCacheEntriesProcessed.Add(SkinCacheEntry);

			ProcessRayTracingGeometryToUpdate(RHICmdList, SkinCacheEntry, LODModel, DispatchItem.bRequireRecreatingRayTracingGeometry, DispatchItem.bAnySegmentUsesWorldPositionOffset);
		}
	}
#endif

	BatchDispatches.Reset();
	bShouldBatchDispatches = false;
}

void FGPUSkinCache::Release(FGPUSkinCacheEntry*& SkinCacheEntry)
{
	if (SkinCacheEntry)
	{
		ReleaseSkinCacheEntry(SkinCacheEntry);
		SkinCacheEntry = nullptr;
	}
}

void FGPUSkinCache::SetVertexStreams(FGPUSkinCacheEntry* Entry, int32 Section, FRHICommandList& RHICmdList,
	FRHIVertexShader* ShaderRHI, const FGPUSkinPassthroughVertexFactory* VertexFactory,
	uint32 BaseVertexIndex, FShaderResourceParameter GPUSkinCachePreviousPositionBuffer)
{
	INC_DWORD_STAT(STAT_GPUSkinCache_NumSetVertexStreams);
	check(Entry);
	check(Entry->IsSectionValid(Section));

	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[Section];

	//UE_LOG(LogSkinCache, Warning, TEXT("*** SetVertexStreams E %p All %p Sec %d(%p) LOD %d"), Entry, Entry->DispatchData[Section].Allocation, Section, Entry->DispatchData[Section].Section, Entry->LOD);
	RHICmdList.SetStreamSource(VertexFactory->GetPositionStreamIndex(), DispatchData.GetPositionRWBuffer()->Buffer, 0);
	if (VertexFactory->GetTangentStreamIndex() > -1 && DispatchData.GetTangentRWBuffer())
	{
		RHICmdList.SetStreamSource(VertexFactory->GetTangentStreamIndex(), DispatchData.GetTangentRWBuffer()->Buffer, 0);
	}

	if (ShaderRHI && GPUSkinCachePreviousPositionBuffer.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ShaderRHI, GPUSkinCachePreviousPositionBuffer.GetBaseIndex(), DispatchData.GetPreviousPositionRWBuffer()->SRV);
	}
}

void FGPUSkinCache::GetShaderBindings(
	FGPUSkinCacheEntry* Entry, 
	int32 Section,
	const FShader* Shader, 
	const FGPUSkinPassthroughVertexFactory* VertexFactory,
	uint32 BaseVertexIndex, 
	FShaderResourceParameter GPUSkinCachePositionBuffer,
	FShaderResourceParameter GPUSkinCachePreviousPositionBuffer,
	class FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams)
{
	INC_DWORD_STAT(STAT_GPUSkinCache_NumSetVertexStreams);
	check(Entry);
	check(Entry->IsSectionValid(Section));

	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[Section];

	//UE_LOG(LogSkinCache, Warning, TEXT("*** SetVertexStreams E %p All %p Sec %d(%p) LOD %d"), Entry, Entry->DispatchData[Section].Allocation, Section, Entry->DispatchData[Section].Section, Entry->LOD);

	VertexStreams.Add(FVertexInputStream(VertexFactory->GetPositionStreamIndex(), 0, DispatchData.GetPositionRWBuffer()->Buffer));

	if (VertexFactory->GetTangentStreamIndex() > -1 && DispatchData.GetTangentRWBuffer())
	{
		VertexStreams.Add(FVertexInputStream(VertexFactory->GetTangentStreamIndex(), 0, DispatchData.GetTangentRWBuffer()->Buffer));
	}

	ShaderBindings.Add(GPUSkinCachePositionBuffer, DispatchData.GetPositionRWBuffer()->SRV);
	ShaderBindings.Add(GPUSkinCachePreviousPositionBuffer, DispatchData.GetPreviousPositionRWBuffer()->SRV);
}

void FGPUSkinCache::PrepareUpdateSkinning(FGPUSkinCacheEntry* Entry, int32 Section, uint32 RevisionNumber, TArray<FRHIUnorderedAccessView*>* OverlappedUAVs)
{
	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[Section];
	FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = DispatchData.SourceVertexFactory->GetShaderData();

	const FVertexBufferAndSRV& BoneBuffer = ShaderData.GetBoneBufferForReading(false);
	const FVertexBufferAndSRV& PrevBoneBuffer = ShaderData.GetBoneBufferForReading(true);

	uint32 CurrentRevision = ShaderData.GetRevisionNumber(false);
	uint32 PreviousRevision = ShaderData.GetRevisionNumber(true);

	DispatchData.DispatchFlags = 0;

	auto BufferUpdate = [&DispatchData, OverlappedUAVs](
		FRWBuffer*& PositionBuffer,
		const FVertexBufferAndSRV& BoneBuffer, 
		uint32 Revision,
		const FVertexBufferAndSRV& PrevBoneBuffer,
		uint32 PrevRevision,
		uint32 UpdateFlag
		)
	{
		PositionBuffer = DispatchData.PositionTracker.Find(BoneBuffer, Revision);
		if (!PositionBuffer)
		{
			DispatchData.PositionTracker.Advance(BoneBuffer, Revision, PrevBoneBuffer, PrevRevision);
			PositionBuffer = DispatchData.PositionTracker.Find(BoneBuffer, Revision);
			check(PositionBuffer);

			DispatchData.DispatchFlags |= UpdateFlag;

			if (OverlappedUAVs)
			{
				(*OverlappedUAVs).Emplace(PositionBuffer->UAV);
			}
		}
	};

	BufferUpdate(
		DispatchData.PreviousPositionBuffer,
		PrevBoneBuffer,
		PreviousRevision,
		BoneBuffer,
		CurrentRevision,
		(uint32)EGPUSkinCacheDispatchFlags::DispatchPrevPosition
		);

	BufferUpdate(
		DispatchData.PositionBuffer, 
		BoneBuffer, 
		CurrentRevision, 
		PrevBoneBuffer, 
		PreviousRevision, 
		(uint32)EGPUSkinCacheDispatchFlags::DispatchPosition
		);

	DispatchData.TangentBuffer = DispatchData.PositionTracker.GetTangentBuffer();
	DispatchData.IntermediateTangentBuffer = DispatchData.PositionTracker.GetIntermediateTangentBuffer();

	if (OverlappedUAVs && DispatchData.DispatchFlags != 0 && DispatchData.GetActiveTangentRWBuffer())
	{
		 (*OverlappedUAVs).Emplace(DispatchData.GetActiveTangentRWBuffer()->UAV);
	}

	check(DispatchData.PreviousPositionBuffer != DispatchData.PositionBuffer);
}

void FGPUSkinCache::DispatchUpdateSkinning(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, int32 Section, uint32 RevisionNumber)
{
	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[Section];
	FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = DispatchData.SourceVertexFactory->GetShaderData();

	SCOPED_DRAW_EVENTF(RHICmdList, SkinCacheDispatch,
		TEXT("Skinning%d%d%d Chunk=%d InStreamStart=%d OutStart=%d Vert=%d Morph=%d/%d"),
		(int32)Entry->bUse16BitBoneIndex, (int32)Entry->BoneInfluenceType, DispatchData.SkinType,
		DispatchData.SectionIndex, DispatchData.InputStreamStart, DispatchData.OutputStreamStart, DispatchData.NumVertices, Entry->MorphBuffer != 0, DispatchData.MorphBufferOffset);
	auto* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<TGPUSkinCacheCS<0>> SkinCacheCS000(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_0, SkinType_0
	TShaderMapRef<TGPUSkinCacheCS<1>> SkinCacheCS001(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_0, SkinType_1
	TShaderMapRef<TGPUSkinCacheCS<2>> SkinCacheCS002(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_0, SkinType_2
	TShaderMapRef<TGPUSkinCacheCS<4>> SkinCacheCS010(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_1, SkinType_0
	TShaderMapRef<TGPUSkinCacheCS<5>> SkinCacheCS011(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_1, SkinType_1
	TShaderMapRef<TGPUSkinCacheCS<6>> SkinCacheCS012(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_1, SkinType_2
	TShaderMapRef<TGPUSkinCacheCS<8>> SkinCacheCS020(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_2, SkinType_0
	TShaderMapRef<TGPUSkinCacheCS<9>> SkinCacheCS021(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_2, SkinType_1
	TShaderMapRef<TGPUSkinCacheCS<10>> SkinCacheCS022(GlobalShaderMap);		// 16bit_0, BoneInfluenceType_2, SkinType_2
	TShaderMapRef<TGPUSkinCacheCS<16>>  SkinCacheCS100(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_0, SkinType_0
	TShaderMapRef<TGPUSkinCacheCS<17>>  SkinCacheCS101(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_0, SkinType_1
	TShaderMapRef<TGPUSkinCacheCS<18>>  SkinCacheCS102(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_0, SkinType_2
	TShaderMapRef<TGPUSkinCacheCS<20>>  SkinCacheCS110(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_1, SkinType_0
	TShaderMapRef<TGPUSkinCacheCS<21>>  SkinCacheCS111(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_1, SkinType_1
	TShaderMapRef<TGPUSkinCacheCS<22>>  SkinCacheCS112(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_1, SkinType_2

	// Multi-influences for cloth:
	TShaderMapRef<TGPUSkinCacheCS<34>>  SkinCacheCS0021(GlobalShaderMap);	// 16bit_0, BoneInfluenceType_0, SkinType_2, MultipleClothInfluences_1
	TShaderMapRef<TGPUSkinCacheCS<38>>  SkinCacheCS0121(GlobalShaderMap);	// 16bit_0, BoneInfluenceType_1, SkinType_2, MultipleClothInfluences_1
	TShaderMapRef<TGPUSkinCacheCS<42>>  SkinCacheCS0221(GlobalShaderMap);	// 16bit_0, BoneInfluenceType_2, SkinType_2, MultipleClothInfluences_1
	TShaderMapRef<TGPUSkinCacheCS<50>>  SkinCacheCS1021(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_0, SkinType_2, MultipleClothInfluences_1
	TShaderMapRef<TGPUSkinCacheCS<54>>  SkinCacheCS1121(GlobalShaderMap);	// 16bit_1, BoneInfluenceType_1, SkinType_2, MultipleClothInfluences_1

	TShaderRef<FBaseGPUSkinCacheCS> Shader;
	switch (DispatchData.SkinType)
	{
	case 0:
		if (Entry->BoneInfluenceType == 0)
		{
			if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS100;
			else Shader = SkinCacheCS000;
		}
		else if (Entry->BoneInfluenceType == 1)
		{
			if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS110;
			else Shader = SkinCacheCS010;
		}
		else
		{
			Shader = SkinCacheCS020;
		}
		break;
	case 1:
		if (Entry->BoneInfluenceType == 0)
		{
			if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS101;
			else Shader = SkinCacheCS001;
		}
		else if (Entry->BoneInfluenceType == 1)
		{
			if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS111;
			else Shader = SkinCacheCS011;
		}
		else
		{
			Shader = SkinCacheCS021;
		}
		break;
	case 2:
		if (Entry->bMultipleClothSkinInfluences)
		{
			// Multiple influences for cloth skinning
			if (Entry->BoneInfluenceType == 0)
			{
				if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS1021;
				else Shader = SkinCacheCS0021;
			}
			else if (Entry->BoneInfluenceType == 1)
			{
				if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS1121;
				else Shader = SkinCacheCS0121;
			}
			else
			{
				Shader = SkinCacheCS0221;
			}
		}
		else
		{
			// Single influence for cloth skinning
			if (Entry->BoneInfluenceType == 0)
			{
				if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS102;
				else Shader = SkinCacheCS002;
			}
			else if (Entry->BoneInfluenceType == 1)
			{
				if (Entry->bUse16BitBoneIndex) Shader = SkinCacheCS112;
				else Shader = SkinCacheCS012;
			}
			else
			{
				Shader = SkinCacheCS022;
			}
		}
		break;
	default:
		check(0);
	}
	check(Shader.IsValid());

	const FVertexBufferAndSRV& BoneBuffer = ShaderData.GetBoneBufferForReading(false);
	const FVertexBufferAndSRV& PrevBoneBuffer = ShaderData.GetBoneBufferForReading(true);

	uint32 CurrentRevision = ShaderData.GetRevisionNumber(false);
	uint32 PreviousRevision = ShaderData.GetRevisionNumber(true);

	if ((DispatchData.DispatchFlags & (uint32)EGPUSkinCacheDispatchFlags::DispatchPrevPosition) != 0)
	{
		RHICmdList.SetComputeShader(Shader.GetComputeShader());

		Shader->SetParameters(
			RHICmdList,
			PrevBoneBuffer,
			Entry,
			DispatchData,
			DispatchData.GetPreviousPositionRWBuffer()->UAV,
			DispatchData.GetActiveTangentRWBuffer() ? DispatchData.GetActiveTangentRWBuffer()->UAV : nullptr
			);

		RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetPreviousPositionRWBuffer()->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		AddBufferToTransition(DispatchData.GetPreviousPositionRWBuffer()->UAV);

		if (DispatchData.GetActiveTangentRWBuffer())
		{
			RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetActiveTangentRWBuffer()->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			AddBufferToTransition(DispatchData.GetActiveTangentRWBuffer()->UAV);
		}

		uint32 VertexCountAlign64 = FMath::DivideAndRoundUp(DispatchData.NumVertices, (uint32)64);
		INC_DWORD_STAT_BY(STAT_GPUSkinCache_TotalNumVertices, VertexCountAlign64 * 64);
		RHICmdList.DispatchComputeShader(VertexCountAlign64, 1, 1);
		Shader->UnsetParameters(RHICmdList);

	}

	if ((DispatchData.DispatchFlags & (uint32)EGPUSkinCacheDispatchFlags::DispatchPosition) != 0)
	{
		RHICmdList.SetComputeShader(Shader.GetComputeShader());

		Shader->SetParameters(
			RHICmdList, 
			BoneBuffer, 
			Entry, 
			DispatchData, 
			DispatchData.GetPositionRWBuffer()->UAV, 
			DispatchData.GetActiveTangentRWBuffer() ? DispatchData.GetActiveTangentRWBuffer()->UAV : nullptr
			);

		RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetPositionRWBuffer()->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		AddBufferToTransition(DispatchData.GetPositionRWBuffer()->UAV);

		if (DispatchData.GetActiveTangentRWBuffer())
		{
			RHICmdList.Transition(FRHITransitionInfo(DispatchData.GetActiveTangentRWBuffer()->UAV.GetReference(), ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			AddBufferToTransition(DispatchData.GetActiveTangentRWBuffer()->UAV);
		}

		uint32 VertexCountAlign64 = FMath::DivideAndRoundUp(DispatchData.NumVertices, (uint32)64);
		INC_DWORD_STAT_BY(STAT_GPUSkinCache_TotalNumVertices, VertexCountAlign64 * 64);
		RHICmdList.DispatchComputeShader(VertexCountAlign64, 1, 1);
		Shader->UnsetParameters(RHICmdList);
	}

	check(DispatchData.PreviousPositionBuffer != DispatchData.PositionBuffer);
}

void FGPUSkinCache::FRWBuffersAllocation::RemoveAllFromTransitionArray(TSet<FRHIUnorderedAccessView*>& InBuffersToTransition)
{
	for (uint32 i = 0; i < NUM_BUFFERS; i++)
	{
		FRWBuffer& RWBuffer = RWBuffers[i];
		if (RWBuffer.UAV.IsValid())
		{
			InBuffersToTransition.Remove(RWBuffer.UAV);
		}
		if (auto TangentBuffer = GetTangentBuffer())
		{
			if (TangentBuffer->UAV.IsValid())
			{
				InBuffersToTransition.Remove(TangentBuffer->UAV);
			}
		}
		if (auto IntermediateTangentBuffer = GetIntermediateTangentBuffer())
		{
			if (IntermediateTangentBuffer->UAV.IsValid())
			{
				InBuffersToTransition.Remove(IntermediateTangentBuffer->UAV);
			}
		}
	}
}

void FGPUSkinCache::ReleaseSkinCacheEntry(FGPUSkinCacheEntry* SkinCacheEntry)
{
	FGPUSkinCache* SkinCache = SkinCacheEntry->SkinCache;
#if RHI_RAYTRACING
	SkinCache->RemoveRayTracingGeometryUpdate(&SkinCacheEntry->GPUSkin->RayTracingGeometry);
#endif // RHI_RAYTRACING
	FRWBuffersAllocation* PositionAllocation = SkinCacheEntry->PositionAllocation;
	if (PositionAllocation)
	{
		uint64 RequiredMemInBytes = PositionAllocation->GetNumBytes();
		SkinCache->UsedMemoryInBytes -= RequiredMemInBytes;
		DEC_MEMORY_STAT_BY(STAT_GPUSkinCache_TotalMemUsed, RequiredMemInBytes);

		SkinCache->Allocations.Remove(PositionAllocation);
		PositionAllocation->RemoveAllFromTransitionArray(SkinCache->BuffersToTransition);

		delete PositionAllocation;

		SkinCacheEntry->PositionAllocation = nullptr;
	}

	SkinCache->Entries.RemoveSingleSwap(SkinCacheEntry, false);
	delete SkinCacheEntry;
}

#if RHI_RAYTRACING
void FGPUSkinCache::GetRayTracingSegmentVertexBuffers(const FGPUSkinCacheEntry& SkinCacheEntry, TArrayView<FRayTracingGeometrySegment> OutSegments)
{
	SkinCacheEntry.GetRayTracingSegmentVertexBuffers(OutSegments);
}
#endif // RHI_RAYTRACING

bool FGPUSkinCache::IsEntryValid(FGPUSkinCacheEntry* SkinCacheEntry, int32 Section)
{
	return SkinCacheEntry->IsSectionValid(Section);
}

bool FGPUSkinCache::UseIntermediateTangents()
{
	int32 RecomputeTangentsMode = GForceRecomputeTangents > 0 ? 1 : GSkinCacheRecomputeTangents;
	return (RecomputeTangentsMode > 0) && (GBlendUsingVertexColorForRecomputeTangents > 0);
}

FGPUSkinBatchElementUserData* FGPUSkinCache::InternalGetFactoryUserData(FGPUSkinCacheEntry* Entry, int32 Section)
{
	return &Entry->BatchElementsUserData[Section];
}

void FGPUSkinCache::InvalidateAllEntries()
{
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		Entries[Index]->LOD = -1;
	}

	for (int32 Index = 0; Index < StagingBuffers.Num(); ++Index)
	{
		StagingBuffers[Index].Release();
	}
	StagingBuffers.SetNum(0, false);
	SET_MEMORY_STAT(STAT_GPUSkinCache_TangentsIntermediateMemUsed, 0);
}

FCachedGeometry FGPUSkinCache::GetCachedGeometry(uint32 ComponentId) const
{
	FCachedGeometry Out;
	for (FGPUSkinCacheEntry* Entry : Entries)
	{
		if (Entry && Entry->GPUSkin && Entry->GPUSkin->GetComponentId() == ComponentId)
		{
			const uint32 LODIndex = Entry->GPUSkin->GetLOD();
			const FSkeletalMeshRenderData& RenderData = Entry->GPUSkin->GetSkeletalMeshRenderData();
			const FSkeletalMeshLODRenderData& LODData = RenderData.LODRenderData[LODIndex];
			const uint32 SectionCount = LODData.RenderSections.Num();
			for (uint32 SectionIdx=0; SectionIdx< SectionCount;++SectionIdx)
			{
				FCachedGeometry::Section CachedSection = Entry->GetCachedGeometry(SectionIdx);
				CachedSection.IndexBuffer		= LODData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
				CachedSection.TotalIndexCount	= LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
				CachedSection.LODIndex			= LODIndex;
				CachedSection.UVsChannelOffset	= 0; // Assume that we needs to pair meshes based on UVs 0
				CachedSection.UVsChannelCount	= LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
				Out.Sections.Add(CachedSection);
			}
			break;
		}
	}

	return Out;
}

FCachedGeometry::Section FGPUSkinCache::GetCachedGeometry(FGPUSkinCacheEntry* InOutEntry, uint32 sectionIndex)
{
	return InOutEntry ? InOutEntry->GetCachedGeometry(sectionIndex) : FCachedGeometry::Section();
}

void FGPUSkinCache::UpdateSkinWeightBuffer(FGPUSkinCacheEntry* Entry)
{
	if (Entry)
	{
		Entry->UpdateSkinWeightBuffer();
	}
}

void FGPUSkinCache::CVarSinkFunction()
{
	int32 NewGPUSkinCacheValue = CVarEnableGPUSkinCache.GetValueOnAnyThread() != 0;
	int32 NewRecomputeTangentsValue = CVarGPUSkinCacheRecomputeTangents.GetValueOnAnyThread();
	float NewSceneMaxSizeInMb = CVarGPUSkinCacheSceneMemoryLimitInMB.GetValueOnAnyThread();
	int32 NewNumTangentIntermediateBuffers = CVarGPUSkinNumTangentIntermediateBuffers.GetValueOnAnyThread();

	if (GEnableGPUSkinCacheShaders)
	{
		if (GIsRHIInitialized && IsRayTracingEnabled())
		{
			// Skin cache is *required* for ray tracing.
			NewGPUSkinCacheValue = 1;
		}
	}
	else
	{
		NewGPUSkinCacheValue = 0;
		NewRecomputeTangentsValue = 0;
	}

	if (NewGPUSkinCacheValue != GEnableGPUSkinCache || NewRecomputeTangentsValue != GSkinCacheRecomputeTangents
		|| NewSceneMaxSizeInMb != GSkinCacheSceneMemoryLimitInMB || NewNumTangentIntermediateBuffers != GNumTangentIntermediateBuffers)
	{
		ENQUEUE_RENDER_COMMAND(DoEnableSkinCaching)(
			[NewRecomputeTangentsValue, NewGPUSkinCacheValue, NewSceneMaxSizeInMb, NewNumTangentIntermediateBuffers](FRHICommandList& RHICmdList)
		{
			GNumTangentIntermediateBuffers = FMath::Max(NewNumTangentIntermediateBuffers, 1);
			GEnableGPUSkinCache = NewGPUSkinCacheValue;
			GSkinCacheRecomputeTangents = NewRecomputeTangentsValue;
			GSkinCacheSceneMemoryLimitInMB = NewSceneMaxSizeInMb;
			++GGPUSkinCacheFlushCounter;
		}
		);
	}
}

FAutoConsoleVariableSink FGPUSkinCache::CVarSink(FConsoleCommandDelegate::CreateStatic(&CVarSinkFunction));
