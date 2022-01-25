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
#include "RenderCaptureInterface.h"
#include "RenderGraphResources.h"
#include "Algo/Unique.h"
#include "HAL/IConsoleManager.h"
#include "RayTracingSkinnedGeometry.h"

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
	TEXT(" 1: on(default)\n"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarDefaultGPUSkinCacheBehavior(
	TEXT("r.SkinCache.DefaultBehavior"),
	(int32)ESkinCacheDefaultBehavior::Inclusive,
	TEXT("Default behavior if all skeletal meshes are included/excluded from the skin cache. If Support Ray Tracing is enabled on a mesh, will force inclusive behavior on that mesh.\n")
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

static int32 GAllowDupedVertsForRecomputeTangents = 0;
FAutoConsoleVariableRef CVarGPUSkinCacheAllowDupedVertesForRecomputeTangents(
	TEXT("r.SkinCache.AllowDupedVertsForRecomputeTangents"),
	GAllowDupedVertsForRecomputeTangents,
	TEXT("0: off (default)\n")
	TEXT("1: Forces that vertices at the same position will be treated differently and has the potential to cause seams when verts are split.\n"),
	ECVF_RenderThreadSafe
);

int32 GRecomputeTangentsParallelDispatch = 0;
FAutoConsoleVariableRef CVarRecomputeTangentsParallelDispatch(
	TEXT("r.SkinCache.RecomputeTangentsParallelDispatch"),
	GRecomputeTangentsParallelDispatch,
	TEXT("This option enables parallel dispatches for recompute tangents.\n")
	TEXT(" 0: off (default), triangle pass is interleaved with vertex pass, requires resource barriers in between. \n")
	TEXT(" 1: on, batch triangle passes together, resource barrier, followed by vertex passes together, cost more memory. \n"),
	ECVF_RenderThreadSafe
);

static int32 GSkinCacheMaxDispatchesPerCmdList = 0;
FAutoConsoleVariableRef CVarGPUSkinCacheMaxDispatchesPerCmdList(
	TEXT("r.SkinCache.MaxDispatchesPerCmdList"),
	GSkinCacheMaxDispatchesPerCmdList,
	TEXT("Maximum number of compute shader dispatches which are batched together into a single command list to fix potential TDRs."),
	ECVF_RenderThreadSafe
);

static int32 GSkinCachePrintMemorySummary = 0;
FAutoConsoleVariableRef CVarGPUSkinCachePrintMemorySummary(
	TEXT("r.SkinCache.PrintMemorySummary"),
	GSkinCachePrintMemorySummary,
	TEXT("Print break down of memory usage.")
	TEXT(" 0: off (default),")
	TEXT(" 1: print when out of memory,")
	TEXT(" 2: print every frame"),
	ECVF_RenderThreadSafe
);

int32 GNumDispatchesToCapture = 0;
static FAutoConsoleVariableRef CVarGPUSkinCacheNumDispatchesToCapture(
	TEXT("r.SkinCache.Capture"),
	GNumDispatchesToCapture,
	TEXT("Trigger a render capture for the next skin cache dispatches."));

static int32 GGPUSkinCacheFlushCounter = 0;

const float MBSize = 1048576.f; // 1024 x 1024 bytes

static inline bool DoesPlatformSupportGPUSkinCache(const FStaticShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetSupportsGPUSkinCache(Platform);
}

ENGINE_API bool IsGPUSkinCacheAvailable(EShaderPlatform Platform)
{
	return AreSkinCacheShadersEnabled(Platform) != 0 && DoesPlatformSupportGPUSkinCache(Platform);
}

ENGINE_API bool GPUSkinCacheNeedsDuplicatedVertices()
{
#if WITH_EDITOR // Duplicated vertices are used in the editor when merging meshes
	return true;
#else
	return GAllowDupedVertsForRecomputeTangents == 0;
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

UE_DEPRECATED(5.0, "This function is no longer in use and will be removed.")
ENGINE_API bool DoRecomputeSkinTangentsOnGPU_RT()
{
	// currently only implemented and tested on Window SM5 (needs Compute, Atomics, SRV for index buffers, UAV for VertexBuffers)
	//#todo-gpuskin: Enable on PS4 when SRVs for IB exist
	return DoesPlatformSupportGPUSkinCache(GMaxRHIShaderPlatform) && GEnableGPUSkinCacheShaders != 0 && (GEnableGPUSkinCache && GSkinCacheRecomputeTangents != 0);
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
	FGPUSkinCacheEntry(FGPUSkinCache* InSkinCache, FSkeletalMeshObjectGPUSkin* InGPUSkin, FGPUSkinCache::FRWBuffersAllocation* InPositionAllocation, int32 InLOD, EGPUSkinCacheEntryMode InMode)
		: Mode(InMode)
		, PositionAllocation(InPositionAllocation)
		, SkinCache(InSkinCache)
		, GPUSkin(InGPUSkin)
		, MorphBuffer(0)
		, LOD(InLOD)
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

        FMatrix44f ClothToLocal = FMatrix44f::Identity;

		// triangle index buffer (input for the RecomputeSkinTangents, might need special index buffer unique to position and normal, not considering UV/vertex color)
		uint32 IndexBufferOffsetValue = 0;
		uint32 NumTriangles = 0;

		FGPUSkinCache::FSkinCacheRWBuffer* TangentBuffer = nullptr;
		FGPUSkinCache::FSkinCacheRWBuffer* IntermediateTangentBuffer = nullptr;
		FGPUSkinCache::FSkinCacheRWBuffer* IntermediateAccumulatedTangentBuffer = nullptr;
		uint32 IntermediateAccumulatedTangentBufferOffset = 0;
		FGPUSkinCache::FSkinCacheRWBuffer* PositionBuffer = nullptr;
		FGPUSkinCache::FSkinCacheRWBuffer* PreviousPositionBuffer = nullptr;

        // Handle duplicates
        FShaderResourceViewRHIRef DuplicatedIndicesIndices = nullptr;
        FShaderResourceViewRHIRef DuplicatedIndices = nullptr;

		FSectionDispatchData() = default;

		inline FGPUSkinCache::FSkinCacheRWBuffer* GetPreviousPositionRWBuffer() const
		{
			check(PreviousPositionBuffer);
			return PreviousPositionBuffer;
		}

		inline FGPUSkinCache::FSkinCacheRWBuffer* GetPositionRWBuffer() const
		{
			check(PositionBuffer);
			return PositionBuffer;
		}

		inline FGPUSkinCache::FSkinCacheRWBuffer* GetTangentRWBuffer() const
		{
			return TangentBuffer;
		}

		FGPUSkinCache::FSkinCacheRWBuffer* GetActiveTangentRWBuffer() const
		{
			// This is the buffer containing tangent results from the skinning CS pass
			return (IndexBuffer && IntermediateTangentBuffer) ? IntermediateTangentBuffer : TangentBuffer;
		}

		inline FGPUSkinCache::FSkinCacheRWBuffer* GetIntermediateAccumulatedTangentBuffer() const
		{
			check(IntermediateAccumulatedTangentBuffer);
			return IntermediateAccumulatedTangentBuffer;
		}

		void UpdateVertexFactoryDeclaration()
		{
			TargetVertexFactory->UpdateVertexDeclaration(SourceVertexFactory, &GetPositionRWBuffer()->Buffer, &GetTangentRWBuffer()->Buffer);
		}
	};

	void UpdateVertexFactoryDeclaration(int32 Section)
	{
		DispatchData[Section].UpdateVertexFactoryDeclaration();
	}

	inline FCachedGeometry::Section GetCachedGeometry(int32 SectionIndex) const
	{
		FCachedGeometry::Section MeshSection;
		if (SectionIndex >= 0 && SectionIndex < DispatchData.Num())
		{
			const FSkelMeshRenderSection& Section = *DispatchData[SectionIndex].Section;
			MeshSection.PositionBuffer = DispatchData[SectionIndex].PositionBuffer->Buffer.SRV;
			MeshSection.UVsBuffer = DispatchData[SectionIndex].UVsBufferSRV;
			MeshSection.TotalVertexCount = DispatchData[SectionIndex].PositionBuffer->Buffer.NumBytes / (sizeof(float) * 3);
			MeshSection.NumPrimitives = Section.NumTriangles;
			MeshSection.NumVertices = Section.NumVertices;
			MeshSection.IndexBaseIndex = Section.BaseIndex;
			MeshSection.VertexBaseIndex = Section.BaseVertexIndex;
			MeshSection.IndexBuffer = nullptr;
			MeshSection.TotalIndexCount = 0;
			MeshSection.LODIndex = 0;
			MeshSection.SectionIndex = SectionIndex;
		}
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

	bool IsValid(FSkeletalMeshObjectGPUSkin* InSkin, int32 InLOD) const
	{
		return GPUSkin == InSkin && LOD == InLOD;
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

	void SetupSection(
		int32 SectionIndex,
		FGPUSkinCache::FRWBuffersAllocation* InPositionAllocation,
		FSkelMeshRenderSection* Section,
		const FMorphVertexBuffer* MorphVertexBuffer,
		const FSkeletalMeshVertexClothBuffer* ClothVertexBuffer,
		uint32 NumVertices,
		uint32 InputStreamStart,
		FGPUBaseSkinVertexFactory* InSourceVertexFactory,
		FGPUSkinPassthroughVertexFactory* InTargetVertexFactory,
		uint32 InIntermediateAccumulatedTangentBufferOffset,
		const FClothSimulData* SimData)
	{
		//UE_LOG(LogSkinCache, Warning, TEXT("*** SetupSection E %p Alloc %p Sec %d(%p) LOD %d"), this, InAllocation, SectionIndex, Section, LOD);
		FSectionDispatchData& Data = DispatchData[SectionIndex];
		check(!Data.PositionTracker.Allocation || Data.PositionTracker.Allocation == InPositionAllocation);

		Data.PositionTracker.Allocation = InPositionAllocation;

		Data.SectionIndex = SectionIndex;
		Data.Section = Section;

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
			const FClothBufferIndexMapping& ClothBufferIndexMapping = ClothVertexBuffer->GetClothIndexMapping()[SectionIndex];

			check(SimData->LODIndex != INDEX_NONE && SimData->LODIndex <= LOD);
			const uint32 ClothLODBias = (uint32)(LOD - SimData->LODIndex);

			const uint32 ClothBufferOffset = ClothBufferIndexMapping.MappingOffset + ClothBufferIndexMapping.LODBiasStride * ClothLODBias;

			// Set the buffer offset depending on whether enough deformer mapping data exists (RaytracingMinLOD/RaytracingLODBias/ClothLODBiasMode settings)
			const uint32 NumInfluences = NumVertices ? ClothBufferIndexMapping.LODBiasStride / NumVertices : 1;
			Data.ClothBufferOffset = (ClothBufferOffset + NumVertices * NumInfluences <= ClothVertexBuffer->GetNumVertices()) ?
				ClothBufferOffset :                     // If the offset is valid, set the calculated LODBias offset
				ClothBufferIndexMapping.MappingOffset;  // Otherwise fallback to a 0 ClothLODBias to prevent from reading pass the buffer (but still raytrace broken shadows/reflections/etc.)
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

		int32 RecomputeTangentsMode = GSkinCacheRecomputeTangents;
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
					Data.IntermediateAccumulatedTangentBufferOffset = InIntermediateAccumulatedTangentBufferOffset;
				}
			}
		}
	}

#if RHI_RAYTRACING
	void GetRayTracingSegmentVertexBuffers(TArray<FBufferRHIRef>& OutVertexBuffers) const
	{
		OutVertexBuffers.SetNum(DispatchData.Num());
		for (int32 SectionIdx = 0; SectionIdx < DispatchData.Num(); SectionIdx++)
		{
			OutVertexBuffers[SectionIdx] = DispatchData[SectionIdx].PositionBuffer->Buffer.Buffer;
		}
	}
#endif // RHI_RAYTRACING

	TArray<FSectionDispatchData>& GetDispatchData() { return DispatchData; }

protected:
	EGPUSkinCacheEntryMode Mode;
	FGPUSkinCache::FRWBuffersAllocation* PositionAllocation;
	FGPUSkinCache* SkinCache;
	TArray<FGPUSkinBatchElementUserData> BatchElementsUserData;
	TArray<FSectionDispatchData> DispatchData;
	FSkeletalMeshObjectGPUSkin* GPUSkin;
	int BoneInfluenceType;
	bool bUse16BitBoneIndex;
	uint32 InputWeightIndexSize;
	uint32 InputWeightStride;
	FShaderResourceViewRHIRef InputWeightStreamSRV;
	FShaderResourceViewRHIRef InputWeightLookupStreamSRV;
	FRHIShaderResourceView* MorphBuffer;
	FShaderResourceViewRHIRef ClothBuffer;
	int32 LOD;

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
		ClothToLocal.Bind(Initializer.ParameterMap, TEXT("ClothToLocal"));
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
			SetShaderValue(RHICmdList, ShaderRHI, ClothToLocal, DispatchData.ClothToLocal);
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
	LAYOUT_FIELD(FShaderParameter, ClothToLocal)

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
		return IsGPUSkinCacheAvailable(Parameters.Platform);
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

FGPUSkinCache::FGPUSkinCache(ERHIFeatureLevel::Type InFeatureLevel, bool bInRequiresMemoryLimit, UWorld* InWorld)
	: UsedMemoryInBytes(0)
	, ExtraRequiredMemory(0)
	, FlushCounter(0)
	, bRequiresMemoryLimit(bInRequiresMemoryLimit)
	, CurrentStagingBufferIndex(0)
	, FeatureLevel(InFeatureLevel)
	, World(InWorld)
{
	check(World);
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

void FGPUSkinCache::TransitionAllToReadable(FRHICommandList& RHICmdList)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FGPUSkinCache::TransitionAllToReadable);

	if (BuffersToTransitionToRead.Num() > 0)
	{
		FMemMark Mark(FMemStack::Get());
		TArray<FRHITransitionInfo, SceneRenderingAllocator> UAVs;
		UAVs.Reserve(BuffersToTransitionToRead.Num());
		for (TSet<FSkinCacheRWBuffer*>::TConstIterator SetIt(BuffersToTransitionToRead); SetIt; ++SetIt)
		{
			UAVs.Add((*SetIt)->UpdateAccessState(ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask));
		}
		RHICmdList.Transition(UAVs);

		BuffersToTransitionToRead.Empty(BuffersToTransitionToRead.Num());
	}
}

/** base of the FRecomputeTangentsPerTrianglePassCS class */
class FBaseRecomputeTangentsPerTriangleShader : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FBaseRecomputeTangentsPerTriangleShader, NonVirtual);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// currently only implemented and tested on Window SM5 (needs Compute, Atomics, SRV for index buffers, UAV for VertexBuffers)
		return IsGPUSkinCacheAvailable(Parameters.Platform);
	}

	static const uint32 ThreadGroupSizeX = 64;

	FBaseRecomputeTangentsPerTriangleShader()
	{}

	FBaseRecomputeTangentsPerTriangleShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		IntermediateAccumBufferUAV.Bind(Initializer.ParameterMap, TEXT("IntermediateAccumBufferUAV"));
		IntermediateAccumBufferOffset.Bind(Initializer.ParameterMap, TEXT("IntermediateAccumBufferOffset"));
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

		SetSRVParameter(RHICmdList, ShaderRHI, GPUPositionCacheBuffer, DispatchData.GetPositionRWBuffer()->Buffer.SRV);
		SetSRVParameter(RHICmdList, ShaderRHI, GPUTangentCacheBuffer, DispatchData.GetActiveTangentRWBuffer()->Buffer.SRV);
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
		SetShaderValue(RHICmdList, ShaderRHI, IntermediateAccumBufferOffset, GRecomputeTangentsParallelDispatch * DispatchData.IntermediateAccumulatedTangentBufferOffset);

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
	LAYOUT_FIELD(FShaderParameter, IntermediateAccumBufferOffset);
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
		return IsGPUSkinCacheAvailable(Parameters.Platform);
	}

	static const uint32 ThreadGroupSizeX = 64;

	LAYOUT_FIELD(FShaderResourceParameter, IntermediateAccumBufferUAV);
	LAYOUT_FIELD(FShaderParameter, IntermediateAccumBufferOffset);
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
		IntermediateAccumBufferOffset.Bind(Initializer.ParameterMap, TEXT("IntermediateAccumBufferOffset"));
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
		SetShaderValue(RHICmdList, ShaderRHI, IntermediateAccumBufferOffset, GRecomputeTangentsParallelDispatch * DispatchData.IntermediateAccumulatedTangentBufferOffset);
		SetUAVParameter(RHICmdList, ShaderRHI, TangentBufferUAV, DispatchData.GetTangentRWBuffer()->Buffer.UAV);

		SetSRVParameter(RHICmdList, ShaderRHI, TangentInputBuffer, DispatchData.IntermediateTangentBuffer ? DispatchData.IntermediateTangentBuffer->Buffer.SRV : nullptr);

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

void FGPUSkinCache::DispatchUpdateSkinTangents(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, int32 SectionIndex, FSkinCacheRWBuffer*& StagingBuffer, bool bTrianglePass)
{
	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[SectionIndex];

	FSkeletalMeshRenderData& SkelMeshRenderData = Entry->GPUSkin->GetSkeletalMeshRenderData();
	const int32 LODIndex = Entry->LOD;
	FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData.LODRenderData[LODIndex];
	const FString RayTracingTag = (Entry->Mode == EGPUSkinCacheEntryMode::RayTracing ? TEXT("[RT]") : TEXT(""));

	if (bTrianglePass)
	{
		if (!GRecomputeTangentsParallelDispatch)
		{	
			if (StagingBuffers.Num() != GNumTangentIntermediateBuffers)
			{
				// Release extra buffers if shrinking
				for (int32 Index = GNumTangentIntermediateBuffers; Index < StagingBuffers.Num(); ++Index)
				{
					StagingBuffers[Index].Release();
				}
				StagingBuffers.SetNum(GNumTangentIntermediateBuffers, false);
			}

			// no need to clear the staging buffer because we create it cleared and clear it after each usage in the per vertex pass
			uint32 NumIntsPerBuffer = DispatchData.NumVertices * FGPUSkinCache::IntermediateAccumBufferNumInts;
			CurrentStagingBufferIndex = (CurrentStagingBufferIndex + 1) % StagingBuffers.Num();
			StagingBuffer = &StagingBuffers[CurrentStagingBufferIndex];
			if (StagingBuffer->Buffer.NumBytes < NumIntsPerBuffer * sizeof(uint32))
			{
				StagingBuffer->Release();
				StagingBuffer->Buffer.Initialize(TEXT("SkinTangentIntermediate"), sizeof(int32), NumIntsPerBuffer, PF_R32_SINT, BUF_UnorderedAccess);
				RHICmdList.BindDebugLabelName(StagingBuffer->Buffer.UAV, TEXT("SkinTangentIntermediate"));

				const uint32 MemSize = NumIntsPerBuffer * sizeof(uint32);
				SET_MEMORY_STAT(STAT_GPUSkinCache_TangentsIntermediateMemUsed, MemSize);

				// The UAV must be zero-filled. We leave it zeroed after each round (see RecomputeTangentsPerVertexPass.usf), so this is only needed on when the buffer is first created.
				RHICmdList.ClearUAVUint(StagingBuffer->Buffer.UAV, FUintVector4(0, 0, 0, 0));
			}
		}

		{
			auto* GlobalShaderMap = GetGlobalShaderMap(GetFeatureLevel());
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<0>> ComputeShader00(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<1>> ComputeShader01(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<2>> ComputeShader10(GlobalShaderMap);
			TShaderMapRef<FRecomputeTangentsPerTrianglePassCS<3>> ComputeShader11(GlobalShaderMap);

			bool bFullPrecisionUV = LodData.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs();

			TShaderRef<FBaseRecomputeTangentsPerTriangleShader> Shader;

			if (bFullPrecisionUV)
			{
				if (GAllowDupedVertsForRecomputeTangents) Shader = ComputeShader01;
				else Shader = ComputeShader11;
			}
			else
			{
				if (GAllowDupedVertsForRecomputeTangents) Shader = ComputeShader00;
				else Shader = ComputeShader10;
			}

			check(Shader.IsValid());

			uint32 NumTriangles = DispatchData.NumTriangles;
			uint32 ThreadGroupCountValue = FMath::DivideAndRoundUp(NumTriangles, FBaseRecomputeTangentsPerTriangleShader::ThreadGroupSizeX);

			SCOPED_DRAW_EVENTF(RHICmdList, SkinTangents_PerTrianglePass, TEXT("%sTangentsTri  Mesh=%s, LOD=%d, Chunk=%d, IndexStart=%d Tri=%d BoneInfluenceType=%d UVPrecision=%d"),
				*RayTracingTag , *GetSkeletalMeshObjectName(Entry->GPUSkin), LODIndex, SectionIndex, DispatchData.IndexBufferOffsetValue, DispatchData.NumTriangles, Entry->BoneInfluenceType, bFullPrecisionUV);

			FRHIComputeShader* ShaderRHI = Shader.GetComputeShader();
			RHICmdList.SetComputeShader(ShaderRHI);

			if (!GAllowDupedVertsForRecomputeTangents)
			{
#if WITH_EDITOR
				check(LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.DupVertData.Num() && LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.DupVertIndexData.Num());
#endif
				DispatchData.DuplicatedIndices = LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.DuplicatedVerticesIndexBuffer.VertexBufferSRV;
				DispatchData.DuplicatedIndicesIndices = LodData.RenderSections[SectionIndex].DuplicatedVerticesBuffer.LengthAndIndexDuplicatedVerticesIndexBuffer.VertexBufferSRV;
			}

			if (!GRecomputeTangentsParallelDispatch)
			{
				// When triangle & vertex passes are interleaved, resource transition is needed in between.
				RHICmdList.Transition({
					DispatchData.GetActiveTangentRWBuffer()->UpdateAccessState(ERHIAccess::SRVCompute),
					StagingBuffer->UpdateAccessState(ERHIAccess::UAVCompute)
				});
			}

			INC_DWORD_STAT_BY(STAT_GPUSkinCache_NumTrianglesForRecomputeTangents, NumTriangles);
			Shader->SetParameters(RHICmdList, Entry, DispatchData, GRecomputeTangentsParallelDispatch ? DispatchData.GetIntermediateAccumulatedTangentBuffer()->Buffer : StagingBuffer->Buffer);
			DispatchComputeShader(RHICmdList, Shader.GetShader(), ThreadGroupCountValue, 1, 1);
			Shader->UnsetParameters(RHICmdList);
			IncrementDispatchCounter(RHICmdList);
		}
	}
	else
	{
		SCOPED_DRAW_EVENTF(RHICmdList, SkinTangents_PerVertexPass, TEXT("%sTangentsVertex Mesh=%s, LOD=%d, Chunk=%d, InputStreamStart=%d, OutputStreamStart=%d, Vert=%d"),
			*RayTracingTag, *GetSkeletalMeshObjectName(Entry->GPUSkin), LODIndex, SectionIndex, DispatchData.InputStreamStart, DispatchData.OutputStreamStart, DispatchData.NumVertices);
		//#todo-gpuskin Feature level?
		auto* GlobalShaderMap = GetGlobalShaderMap(GetFeatureLevel());
		TShaderMapRef<FRecomputeTangentsPerVertexPassCS<0>> ComputeShader0(GlobalShaderMap);
		TShaderMapRef<FRecomputeTangentsPerVertexPassCS<1>> ComputeShader1(GlobalShaderMap);
		TShaderRef<FBaseRecomputeTangentsPerVertexShader> ComputeShader;
		if (DispatchData.Section->RecomputeTangentsVertexMaskChannel < ESkinVertexColorChannel::None)
			ComputeShader = ComputeShader1;
		else
			ComputeShader = ComputeShader0;
		RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

		uint32 VertexCount = DispatchData.NumVertices;
		uint32 ThreadGroupCountValue = FMath::DivideAndRoundUp(VertexCount, ComputeShader->ThreadGroupSizeX);

		if (!GRecomputeTangentsParallelDispatch)
		{
			// When triangle & vertex passes are interleaved, resource transition is needed in between.
			RHICmdList.Transition({
				DispatchData.GetTangentRWBuffer()->UpdateAccessState(ERHIAccess::UAVCompute),
				StagingBuffer->UpdateAccessState(ERHIAccess::UAVCompute)
				});
		}

		ComputeShader->SetParameters(RHICmdList, Entry, DispatchData, GRecomputeTangentsParallelDispatch ? DispatchData.GetIntermediateAccumulatedTangentBuffer()->Buffer : StagingBuffer->Buffer);
		DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), ThreadGroupCountValue, 1, 1);
		ComputeShader->UnsetParameters(RHICmdList);
		IncrementDispatchCounter(RHICmdList);
	}
}

FGPUSkinCache::FRWBuffersAllocation* FGPUSkinCache::TryAllocBuffer(uint32 NumVertices, bool WithTangnents, bool UseIntermediateTangents, uint32 NumTriangles, FRHICommandListImmediate& RHICmdList)
{
	uint64 MaxSizeInBytes = (uint64)(GSkinCacheSceneMemoryLimitInMB * 1024.0f * 1024.0f);
	uint64 RequiredMemInBytes = FRWBuffersAllocation::CalculateRequiredMemory(NumVertices, WithTangnents, UseIntermediateTangents, NumTriangles);
	if (bRequiresMemoryLimit && UsedMemoryInBytes + RequiredMemInBytes >= MaxSizeInBytes)
	{
		ExtraRequiredMemory += RequiredMemInBytes;

		// Can't fit
		return nullptr;
	}

	FRWBuffersAllocation* NewAllocation = new FRWBuffersAllocation(NumVertices, WithTangnents, UseIntermediateTangents, NumTriangles, RHICmdList);
	Allocations.Add(NewAllocation);

	UsedMemoryInBytes += RequiredMemInBytes;
	INC_MEMORY_STAT_BY(STAT_GPUSkinCache_TotalMemUsed, RequiredMemInBytes);

	return NewAllocation;
}

DECLARE_GPU_STAT(GPUSkinCache);

void FGPUSkinCache::MakeBufferTransitions(FRHICommandListImmediate& RHICmdList, TArray<FSkinCacheRWBuffer*>& Buffers, ERHIAccess ToState)
{
	if (Buffers.Num() > 0)
	{
		FMemMark Mark(FMemStack::Get());
		TArray<FRHITransitionInfo, SceneRenderingAllocator> UAVs;
		UAVs.Reserve(Buffers.Num());
		for (FSkinCacheRWBuffer* Buffer : Buffers)
		{
			UAVs.Add(Buffer->UpdateAccessState(ToState));
		}
		RHICmdList.Transition(MakeArrayView(UAVs.GetData(), UAVs.Num()));
	}
}

void FGPUSkinCache::GetBufferUAVs(const TArray<FSkinCacheRWBuffer*>& InBuffers, TArray<FRHIUnorderedAccessView*>& OutUAVs)
{
	OutUAVs.Reset(InBuffers.Num());
	for (const FSkinCacheRWBuffer* Buffer : InBuffers)
	{
		OutUAVs.Add(Buffer->Buffer.UAV);
	}
}

void FGPUSkinCache::DoDispatch(FRHICommandListImmediate& RHICmdList)
{
	int32 BatchCount = BatchDispatches.Num();
	INC_DWORD_STAT_BY(STAT_GPUSkinCache_TotalNumChunks, BatchCount);

	bool bCapture = BatchCount > 0 && GNumDispatchesToCapture > 0;
	RenderCaptureInterface::FScopedCapture RenderCapture(bCapture, &RHICmdList, TEXT("GPUSkinCache"));
	GNumDispatchesToCapture -= bCapture ? 1 : 0;

	SCOPED_GPU_STAT(RHICmdList, GPUSkinCache);

	TArray<FSkinCacheRWBuffer*> BuffersToTransitionForSkinning;
	BuffersToTransitionForSkinning.Reserve(BatchCount * 2);
	{
		for (int32 i = 0; i < BatchCount; ++i)
		{
			FDispatchEntry& DispatchItem = BatchDispatches[i];
			PrepareUpdateSkinning(DispatchItem.SkinCacheEntry, DispatchItem.Section, DispatchItem.RevisionNumber, &BuffersToTransitionForSkinning);
		}
		MakeBufferTransitions(RHICmdList, BuffersToTransitionForSkinning, ERHIAccess::UAVCompute);
	}

	TArray<FRHIUnorderedAccessView*> SkinningBuffersToOverlap;
	GetBufferUAVs(BuffersToTransitionForSkinning, SkinningBuffersToOverlap);
	RHICmdList.BeginUAVOverlap(SkinningBuffersToOverlap);
	{
		SCOPED_DRAW_EVENT(RHICmdList, GPUSkinCache_UpdateSkinningBatches);
		for (int32 i = 0; i < BatchCount; ++i)
		{
			FDispatchEntry& DispatchItem = BatchDispatches[i];
			DispatchUpdateSkinning(RHICmdList, DispatchItem.SkinCacheEntry, DispatchItem.Section, DispatchItem.RevisionNumber);
		}
	}
	RHICmdList.EndUAVOverlap(SkinningBuffersToOverlap);

	// Do necessary buffer transitions before recomputing tangents
	TArray<FSkinCacheRWBuffer*> BuffersToSRVForRecomputeTangents;
	TArray<FSkinCacheRWBuffer*> IntermediateAccumulatedTangentBuffers;
	for (int32 i = 0; i < BatchCount; ++i)
	{
		FDispatchEntry& DispatchItem = BatchDispatches[i];
		FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = DispatchItem.SkinCacheEntry->DispatchData[DispatchItem.Section];
		if (DispatchData.IndexBuffer)
		{
			BuffersToSRVForRecomputeTangents.AddUnique(DispatchData.GetPositionRWBuffer());
			BuffersToSRVForRecomputeTangents.AddUnique(DispatchData.GetActiveTangentRWBuffer());
			if (GRecomputeTangentsParallelDispatch)
			{
				IntermediateAccumulatedTangentBuffers.AddUnique(DispatchData.GetIntermediateAccumulatedTangentBuffer());
			}
			BuffersToTransitionToRead.Add(DispatchData.GetPositionRWBuffer());
		}	
	}
	MakeBufferTransitions(RHICmdList, BuffersToSRVForRecomputeTangents, ERHIAccess::SRVCompute);
	MakeBufferTransitions(RHICmdList, IntermediateAccumulatedTangentBuffers, ERHIAccess::UAVCompute);

	TArray<FRHIUnorderedAccessView*> IntermediateAccumulatedTangentBuffersToOverlap;
	GetBufferUAVs(IntermediateAccumulatedTangentBuffers, IntermediateAccumulatedTangentBuffersToOverlap);
	RHICmdList.BeginUAVOverlap(IntermediateAccumulatedTangentBuffersToOverlap);
	{
		SCOPED_DRAW_EVENT(RHICmdList, GPUSkinCache_RecomputeTangentsBatches);
		FSkinCacheRWBuffer* StagingBuffer = nullptr;
		for (int32 i = 0; i < BatchCount; ++i)
		{
			FDispatchEntry& DispatchItem = BatchDispatches[i];
			if (DispatchItem.SkinCacheEntry->DispatchData[DispatchItem.Section].IndexBuffer)
			{
				DispatchUpdateSkinTangents(RHICmdList, DispatchItem.SkinCacheEntry, DispatchItem.Section, StagingBuffer, true);
				if (!GRecomputeTangentsParallelDispatch)
				{
					// When parallel dispatching is off, triangle pass and vertex pass are dispatched interleaved.
					DispatchUpdateSkinTangents(RHICmdList, DispatchItem.SkinCacheEntry, DispatchItem.Section, StagingBuffer, false);
				}
			}
		}
		if (GRecomputeTangentsParallelDispatch)
		{
			// Do necessary buffer transitions before vertex pass dispatches
			TArray<FSkinCacheRWBuffer*> TangentBuffers;
			for (int32 i = 0; i < BatchCount; ++i)
			{
				FDispatchEntry& DispatchItem = BatchDispatches[i];
				FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = DispatchItem.SkinCacheEntry->DispatchData[DispatchItem.Section];
				TangentBuffers.AddUnique(DispatchData.GetTangentRWBuffer());
			}
			MakeBufferTransitions(RHICmdList, TangentBuffers, ERHIAccess::UAVCompute);
			MakeBufferTransitions(RHICmdList, IntermediateAccumulatedTangentBuffers, ERHIAccess::UAVCompute);
		
			TArray<FRHIUnorderedAccessView*> TangentBuffersToOverlap;
			GetBufferUAVs(TangentBuffers, TangentBuffersToOverlap);
			RHICmdList.BeginUAVOverlap(TangentBuffersToOverlap);
			for (int32 i = 0; i < BatchCount; ++i)
			{
				FDispatchEntry& DispatchItem = BatchDispatches[i];
				if (DispatchItem.SkinCacheEntry->DispatchData[DispatchItem.Section].IndexBuffer)
				{
					DispatchUpdateSkinTangents(RHICmdList, DispatchItem.SkinCacheEntry, DispatchItem.Section, StagingBuffer, false);
				}
			}
			RHICmdList.EndUAVOverlap(TangentBuffersToOverlap);
		}
	}
	RHICmdList.EndUAVOverlap(IntermediateAccumulatedTangentBuffersToOverlap);

	for (int32 i = 0; i < BatchCount; ++i)
	{
		FDispatchEntry& DispatchItem = BatchDispatches[i];
		DispatchItem.SkinCacheEntry->UpdateVertexFactoryDeclaration(DispatchItem.Section);
	}
}

void FGPUSkinCache::DoDispatch(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* SkinCacheEntry, int32 Section, int32 RevisionNumber)
{
	RenderCaptureInterface::FScopedCapture RenderCapture(GNumDispatchesToCapture > 0, &RHICmdList, TEXT("GPUSkinCache"));
	GNumDispatchesToCapture = FMath::Max(GNumDispatchesToCapture - 1, 0);

	SCOPED_GPU_STAT(RHICmdList, GPUSkinCache);

	INC_DWORD_STAT(STAT_GPUSkinCache_TotalNumChunks);

	TArray<FSkinCacheRWBuffer*> BuffersToTransitionForSkinning;
	PrepareUpdateSkinning(SkinCacheEntry, Section, RevisionNumber, &BuffersToTransitionForSkinning);
	MakeBufferTransitions(RHICmdList, BuffersToTransitionForSkinning, ERHIAccess::UAVCompute);

	TArray<FRHIUnorderedAccessView*> SkinningBuffersToOverlap;
	GetBufferUAVs(BuffersToTransitionForSkinning, SkinningBuffersToOverlap);
	RHICmdList.BeginUAVOverlap(SkinningBuffersToOverlap);
	{
	DispatchUpdateSkinning(RHICmdList, SkinCacheEntry, Section, RevisionNumber);
	}
	RHICmdList.EndUAVOverlap(SkinningBuffersToOverlap);

	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = SkinCacheEntry->DispatchData[Section];
	if (DispatchData.IndexBuffer)
	{
		RHICmdList.Transition({
			DispatchData.GetPositionRWBuffer()->UpdateAccessState(ERHIAccess::SRVCompute),
			DispatchData.GetActiveTangentRWBuffer()->UpdateAccessState(ERHIAccess::SRVCompute)
		});
		if (GRecomputeTangentsParallelDispatch)
		{
			RHICmdList.Transition(DispatchData.GetIntermediateAccumulatedTangentBuffer()->UpdateAccessState(ERHIAccess::UAVCompute));
		}
		BuffersToTransitionToRead.Add(DispatchData.GetPositionRWBuffer());

		FSkinCacheRWBuffer* StagingBuffer = nullptr;
		DispatchUpdateSkinTangents(RHICmdList, SkinCacheEntry, Section, StagingBuffer, true);
		if (GRecomputeTangentsParallelDispatch)
		{
			RHICmdList.Transition({
				DispatchData.GetTangentRWBuffer()->UpdateAccessState(ERHIAccess::UAVCompute),
				DispatchData.GetIntermediateAccumulatedTangentBuffer()->UpdateAccessState(ERHIAccess::UAVCompute)
			});
		}
		DispatchUpdateSkinTangents(RHICmdList, SkinCacheEntry, Section, StagingBuffer, false);
	}

	SkinCacheEntry->UpdateVertexFactoryDeclaration(Section);
}

bool FGPUSkinCache::ProcessEntry(
	EGPUSkinCacheEntryMode Mode,
	FRHICommandListImmediate& RHICmdList, 
	FGPUBaseSkinVertexFactory* VertexFactory,
	FGPUSkinPassthroughVertexFactory* TargetVertexFactory, 
	const FSkelMeshRenderSection& BatchElement, 
	FSkeletalMeshObjectGPUSkin* Skin,
	const FMorphVertexBuffer* MorphVertexBuffer,
	const FSkeletalMeshVertexClothBuffer* ClothVertexBuffer, 
	const FClothSimulData* SimData,
	const FMatrix44f& ClothToLocal,
	float ClothBlendWeight, 
	uint32 RevisionNumber, 
	int32 Section,
	int32 LODIndex,
	FGPUSkinCacheEntry*& InOutEntry
	)
{
	INC_DWORD_STAT(STAT_GPUSkinCache_NumSectionsProcessed);

	const int32 NumVertices = BatchElement.GetNumVertices();
	//#todo-gpuskin Check that stream 0 is the position stream
	const uint32 InputStreamStart = BatchElement.BaseVertexIndex;

	FSkeletalMeshRenderData& SkelMeshRenderData = Skin->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData.LODRenderData[LODIndex];

	if (FlushCounter < GGPUSkinCacheFlushCounter)
	{
		FlushCounter = GGPUSkinCacheFlushCounter;
		InvalidateAllEntries();
	}

	int32 RecomputeTangentsMode = GSkinCacheRecomputeTangents;
	bool bShouldRecomputeTangent = false;

	// IntermediateAccumulatedTangents buffer is needed if mesh has at least one section needing recomputing tangents.
	uint32 InterAccumTangentBufferSize = 0;
	uint32 CurrInterAccumTangentBufferOffset = 0;
	if (RecomputeTangentsMode > 0)
	{
		for (int32 i = 0; i < LodData.RenderSections.Num(); ++i)
		{			
			const FSkelMeshRenderSection& RenderSection = LodData.RenderSections[i];
			if (RecomputeTangentsMode == 1 || RenderSection.bRecomputeTangent)
			{
				bShouldRecomputeTangent = true;
				InterAccumTangentBufferSize += RenderSection.GetNumVertices();
				if (i < Section)
				{
					CurrInterAccumTangentBufferOffset += RenderSection.GetNumVertices();
				}
			}
		}
	}

	if (InOutEntry)
	{
		// If the LOD changed, the entry has to be invalidated
		if (!InOutEntry->IsValid(Skin, LODIndex))
		{
			Release(InOutEntry);
			InOutEntry = nullptr;
		}
		else
		{
			if (!InOutEntry->IsSectionValid(Section) || !InOutEntry->IsSourceFactoryValid(Section, VertexFactory))
			{
				// This section might not be valid yet, so set it up
				InOutEntry->SetupSection(Section, InOutEntry->PositionAllocation, &LodData.RenderSections[Section], MorphVertexBuffer, ClothVertexBuffer, NumVertices, InputStreamStart, 
											VertexFactory, TargetVertexFactory, CurrInterAccumTangentBufferOffset, SimData);
			}
		}
	}

	// Try to allocate a new entry
	if (!InOutEntry)
	{
		bool WithTangents = RecomputeTangentsMode > 0;
		int32 TotalNumVertices = VertexFactory->GetNumVertices();
		
		// IntermediateTangents buffer is needed if mesh has at least one section using vertex color as recompute tangents blending mask
		bool bEntryUseIntermediateTangents = false;
		if (bShouldRecomputeTangent)
		{
			for (const FSkelMeshRenderSection& RenderSection : LodData.RenderSections)
			{
				if (RenderSection.RecomputeTangentsVertexMaskChannel < ESkinVertexColorChannel::None)
				{
					bEntryUseIntermediateTangents = true;
					break;
				}
			}
		}

		FRWBuffersAllocation* NewPositionAllocation = TryAllocBuffer(TotalNumVertices, WithTangents, bEntryUseIntermediateTangents, InterAccumTangentBufferSize, RHICmdList);
		if (!NewPositionAllocation)
		{
			if (GSkinCachePrintMemorySummary > 0)
			{
				const FString RayTracingTag = (Mode == EGPUSkinCacheEntryMode::RayTracing ? TEXT("[RT]") : TEXT(""));
				uint64 RequiredMemInBytes = FRWBuffersAllocation::CalculateRequiredMemory(TotalNumVertices, WithTangents, bEntryUseIntermediateTangents, InterAccumTangentBufferSize);
				UE_LOG(LogSkinCache, Warning, TEXT("FGPUSkinCache::ProcessEntry%s failed to allocate %.3fMB for mesh %s LOD%d, extra required memory increased to %.3fMB"),
					*RayTracingTag, RequiredMemInBytes / MBSize, *GetSkeletalMeshObjectName(Skin), LODIndex, ExtraRequiredMemory / MBSize);
			}

			// Couldn't fit; caller will notify OOM
			return false;
		}

		InOutEntry = new FGPUSkinCacheEntry(this, Skin, NewPositionAllocation, LODIndex, Mode);
		InOutEntry->GPUSkin = Skin;

		InOutEntry->SetupSection(Section, NewPositionAllocation, &LodData.RenderSections[Section], MorphVertexBuffer, ClothVertexBuffer, NumVertices, InputStreamStart, 
									VertexFactory, TargetVertexFactory, CurrInterAccumTangentBufferOffset, SimData);
		Entries.Add(InOutEntry);
	}

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

		if (SimData->Positions.Num() > 0)
		{
	        check(SimData->Positions.Num() == SimData->Normals.Num());
	        VertexAndNormalData.ResizeBuffer( SimData->Positions.Num() );

	        uint8* Data = VertexAndNormalData.GetDataPointer();
	        uint32 Stride = VertexAndNormalData.GetStride();

	        // Copy the vertices into the buffer.
	        checkSlow(Stride*VertexAndNormalData.GetNumVertices() == sizeof(FClothSimulEntry) * SimData->Positions.Num());
	        check(sizeof(FClothSimulEntry) == 6 * sizeof(float));

			if (ClothVertexBuffer && ClothVertexBuffer->GetClothIndexMapping().Num() > Section)
			{
				const FClothBufferIndexMapping& ClothBufferIndexMapping = ClothVertexBuffer->GetClothIndexMapping()[Section];

				check(SimData->LODIndex != INDEX_NONE && SimData->LODIndex <= LODIndex);
				const uint32 ClothLODBias = (uint32)(LODIndex - SimData->LODIndex);

				const uint32 ClothBufferOffset = ClothBufferIndexMapping.MappingOffset + ClothBufferIndexMapping.LODBiasStride * ClothLODBias;

				// Set the buffer offset depending on whether enough deformer mapping data exists (RaytracingMinLOD/RaytracingLODBias/ClothLODBiasMode settings)
				const uint32 NumInfluences = NumVertices ? ClothBufferIndexMapping.LODBiasStride / NumVertices : 1;
				InOutEntry->DispatchData[Section].ClothBufferOffset = (ClothBufferOffset + NumVertices * NumInfluences <= ClothVertexBuffer->GetNumVertices()) ?
					ClothBufferOffset :                     // If the offset is valid, set the calculated LODBias offset
					ClothBufferIndexMapping.MappingOffset;  // Otherwise fallback to a 0 ClothLODBias to prevent from reading pass the buffer (but still raytrace broken shadows/reflections/etc.)
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

	        FRHIResourceCreateInfo CreateInfo(TEXT("ClothPositionAndNormalsBuffer"), ResourceArray);
	        ClothPositionAndNormalsBuffer.VertexBufferRHI = RHICreateVertexBuffer( ResourceArray->GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
	        ClothPositionAndNormalsBuffer.VertexBufferSRV = RHICreateShaderResourceView(ClothPositionAndNormalsBuffer.VertexBufferRHI, sizeof(FVector2f), PF_G32R32F);
	        InOutEntry->DispatchData[Section].ClothPositionsAndNormalsBuffer = ClothPositionAndNormalsBuffer.VertexBufferSRV;
		}
		else
		{
			UE_LOG(LogSkinCache, Error, TEXT("Cloth sim data is missing on mesh %s"), *GetSkeletalMeshObjectName(Skin));
		}

        InOutEntry->DispatchData[Section].ClothBlendWeight = ClothBlendWeight;
        InOutEntry->DispatchData[Section].ClothToLocal = ClothToLocal;
    }
    InOutEntry->DispatchData[Section].SkinType = ClothVertexBuffer && InOutEntry->DispatchData[Section].ClothPositionsAndNormalsBuffer ? 2 : (bMorph ? 1 : 0);

	if (bShouldBatchDispatches)
	{
		BatchDispatches.Add({ InOutEntry, &LodData, RevisionNumber, uint32(Section) });
	}
	else
	{
		DoDispatch(RHICmdList, InOutEntry, Section, RevisionNumber);
	}

	return true;
}

bool FGPUSkinCache::IsGPUSkinCacheRayTracingSupported()
{
#if RHI_RAYTRACING
	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Geometry.SupportSkeletalMeshes"));
	static const bool SupportSkeletalMeshes = CVar->GetInt() != 0;
	return IsRayTracingEnabled() && SupportSkeletalMeshes && GEnableGPUSkinCache;
#else
	return false;
#endif
}

#if RHI_RAYTRACING

void FGPUSkinCache::ProcessRayTracingGeometryToUpdate(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* SkinCacheEntry, FSkeletalMeshLODRenderData& LODModel)
{
	if (IsGPUSkinCacheRayTracingSupported() && SkinCacheEntry && SkinCacheEntry->GPUSkin && SkinCacheEntry->GPUSkin->bSupportRayTracing)
	{
		if (SkinCacheEntry->GPUSkin->bRequireRecreatingRayTracingGeometry)
		{
			// We will need to build a new BVH so flush pending skin cache resource barriers.
			TransitionAllToReadable(RHICmdList);
		}

 		TArray<FBufferRHIRef> VertexBufffers;
 		SkinCacheEntry->GetRayTracingSegmentVertexBuffers(VertexBufffers);

 		SkinCacheEntry->GPUSkin->UpdateRayTracingGeometry(LODModel, VertexBufffers);
	}
}

#endif

void FGPUSkinCache::BeginBatchDispatch(FRHICommandListImmediate& RHICmdList)
{
	check(BatchDispatches.Num() == 0);
	bShouldBatchDispatches = true;
	DispatchCounter = 0;
}

void FGPUSkinCache::EndBatchDispatch(FRHICommandListImmediate& RHICmdList)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FGPUSkinCache::EndBatchDispatch);

	DoDispatch(RHICmdList);

#if RHI_RAYTRACING
	if (IsGPUSkinCacheRayTracingSupported())
	{
		TSet<FGPUSkinCacheEntry*> SkinCacheEntriesProcessed;

		// Process batched dispatches in reverse order to filter out duplicated ones and keep the last one
		for (int32 Index = BatchDispatches.Num() - 1; Index >= 0; Index--)
		{
			FDispatchEntry& DispatchItem = BatchDispatches[Index];

			FGPUSkinCacheEntry* SkinCacheEntry = DispatchItem.SkinCacheEntry;

			if (SkinCacheEntry->GPUSkin->ShouldUseSeparateSkinCacheEntryForRayTracing() && SkinCacheEntry->Mode != EGPUSkinCacheEntryMode::RayTracing)
			{
				continue;
			}

			FSkeletalMeshLODRenderData& LODModel = *DispatchItem.LODModel;

			if (SkinCacheEntriesProcessed.Contains(SkinCacheEntry))
			{
				continue;
			}

			SkinCacheEntriesProcessed.Add(SkinCacheEntry);

			ProcessRayTracingGeometryToUpdate(RHICmdList, SkinCacheEntry, LODModel);
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

void FGPUSkinCache::GetShaderBindings(
	const FGPUSkinCacheEntry* Entry, 
	int32 Section,
	bool bVerticesInMotion,
	const FGPUSkinPassthroughVertexFactory* VertexFactory,
	FShaderResourceParameter GPUSkinCachePositionBuffer,
	FShaderResourceParameter GPUSkinCachePreviousPositionBuffer,
	FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams)
{
	INC_DWORD_STAT(STAT_GPUSkinCache_NumSetVertexStreams);
	check(Entry);
	check(Entry->IsSectionValid(Section));
	check(Entry->SkinCache);

	FGPUSkinCacheEntry::FSectionDispatchData const& DispatchData = Entry->DispatchData[Section];

	//UE_LOG(LogSkinCache, Warning, TEXT("*** SetVertexStreams E %p Sec %d(%p) LOD %d"), Entry, Section, Entry->DispatchData[Section].Section, Entry->LOD);

	VertexStreams.Add(FVertexInputStream(VertexFactory->GetPositionStreamIndex(), 0, DispatchData.GetPositionRWBuffer()->Buffer.Buffer));

	if (VertexFactory->GetTangentStreamIndex() > -1 && DispatchData.GetTangentRWBuffer())
	{
		VertexStreams.Add(FVertexInputStream(VertexFactory->GetTangentStreamIndex(), 0, DispatchData.GetTangentRWBuffer()->Buffer.Buffer));
	}

	ShaderBindings.Add(GPUSkinCachePositionBuffer, DispatchData.GetPositionRWBuffer()->Buffer.SRV);

	// If world is paused, use current frame bone matrices, so velocity is canceled and skeletal mesh isn't blurred from motion.
	ShaderBindings.Add(GPUSkinCachePreviousPositionBuffer, bVerticesInMotion ? DispatchData.GetPreviousPositionRWBuffer()->Buffer.SRV : DispatchData.GetPositionRWBuffer()->Buffer.SRV);
}

void FGPUSkinCache::PrepareUpdateSkinning(FGPUSkinCacheEntry* Entry, int32 Section, uint32 RevisionNumber, TArray<FSkinCacheRWBuffer*>* OverlappedUAVs)
{
	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[Section];
	FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = DispatchData.SourceVertexFactory->GetShaderData();

	const FVertexBufferAndSRV& BoneBuffer = ShaderData.GetBoneBufferForReading(false);
	const FVertexBufferAndSRV& PrevBoneBuffer = ShaderData.GetBoneBufferForReading(true);

	uint32 CurrentRevision = ShaderData.GetRevisionNumber(false);
	uint32 PreviousRevision = ShaderData.GetRevisionNumber(true);

	DispatchData.DispatchFlags = 0;

	auto BufferUpdate = [&DispatchData, OverlappedUAVs](
		FSkinCacheRWBuffer*& PositionBuffer,
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
				(*OverlappedUAVs).AddUnique(PositionBuffer);
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
	DispatchData.IntermediateAccumulatedTangentBuffer = DispatchData.PositionTracker.GetIntermediateAccumulatedTangentBuffer();

	if (OverlappedUAVs && DispatchData.DispatchFlags != 0 && DispatchData.GetActiveTangentRWBuffer())
	{
		 (*OverlappedUAVs).AddUnique(DispatchData.GetActiveTangentRWBuffer());
	}

	check(DispatchData.PreviousPositionBuffer != DispatchData.PositionBuffer);
}

void FGPUSkinCache::DispatchUpdateSkinning(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, int32 Section, uint32 RevisionNumber)
{
	FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->DispatchData[Section];
	FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = DispatchData.SourceVertexFactory->GetShaderData();
	const FString RayTracingTag = (Entry->Mode == EGPUSkinCacheEntryMode::RayTracing ? TEXT("[RT]") : TEXT(""));
	
	constexpr int32 ClothLODBias = 0;  // Use the same cloth LOD mapping (= 0 bias) to get the number of deformer weights
	const uint32 NumWrapDeformerWeights = DispatchData.Section->ClothMappingDataLODs.Num() ? DispatchData.Section->ClothMappingDataLODs[ClothLODBias].Num(): 0;
	const bool bMultipleWrapDeformerInfluences = (DispatchData.NumVertices < NumWrapDeformerWeights);

	SCOPED_DRAW_EVENTF(RHICmdList, SkinCacheDispatch,
		TEXT("%sSkinning%d%d%d Mesh=%s LOD=%d Chunk=%d InStreamStart=%d OutStart=%d Vert=%d Morph=%d/%d"),
		*RayTracingTag, (int32)Entry->bUse16BitBoneIndex, (int32)Entry->BoneInfluenceType, DispatchData.SkinType, *GetSkeletalMeshObjectName(Entry->GPUSkin), Entry->LOD,
		DispatchData.SectionIndex, DispatchData.InputStreamStart, DispatchData.OutputStreamStart, DispatchData.NumVertices, Entry->MorphBuffer != 0, DispatchData.MorphBufferOffset);
	auto* GlobalShaderMap = GetGlobalShaderMap(GetFeatureLevel());
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
		if (bMultipleWrapDeformerInfluences)
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
			DispatchData.GetPreviousPositionRWBuffer()->Buffer.UAV,
			DispatchData.GetActiveTangentRWBuffer() ? DispatchData.GetActiveTangentRWBuffer()->Buffer.UAV : nullptr
			);

		uint32 VertexCountAlign64 = FMath::DivideAndRoundUp(DispatchData.NumVertices, (uint32)64);
		INC_DWORD_STAT_BY(STAT_GPUSkinCache_TotalNumVertices, VertexCountAlign64 * 64);
		RHICmdList.DispatchComputeShader(VertexCountAlign64, 1, 1);
		Shader->UnsetParameters(RHICmdList);
		IncrementDispatchCounter(RHICmdList);
		BuffersToTransitionToRead.Add(DispatchData.GetPreviousPositionRWBuffer());
	}

	if ((DispatchData.DispatchFlags & (uint32)EGPUSkinCacheDispatchFlags::DispatchPosition) != 0)
	{
		RHICmdList.SetComputeShader(Shader.GetComputeShader());

		Shader->SetParameters(
			RHICmdList, 
			BoneBuffer, 
			Entry, 
			DispatchData, 
			DispatchData.GetPositionRWBuffer()->Buffer.UAV, 
			DispatchData.GetActiveTangentRWBuffer() ? DispatchData.GetActiveTangentRWBuffer()->Buffer.UAV : nullptr
			);

		uint32 VertexCountAlign64 = FMath::DivideAndRoundUp(DispatchData.NumVertices, (uint32)64);
		INC_DWORD_STAT_BY(STAT_GPUSkinCache_TotalNumVertices, VertexCountAlign64 * 64);
		RHICmdList.DispatchComputeShader(VertexCountAlign64, 1, 1);
		Shader->UnsetParameters(RHICmdList);
		IncrementDispatchCounter(RHICmdList);
		BuffersToTransitionToRead.Add(DispatchData.GetPositionRWBuffer());
	}

	BuffersToTransitionToRead.Add(DispatchData.GetTangentRWBuffer());
	check(DispatchData.PreviousPositionBuffer != DispatchData.PositionBuffer);
}

void FGPUSkinCache::FRWBuffersAllocation::RemoveAllFromTransitionArray(TSet<FSkinCacheRWBuffer*>& InBuffersToTransition)
{
	for (uint32 i = 0; i < NUM_BUFFERS; i++)
	{
		FSkinCacheRWBuffer& RWBuffer = PositionBuffers[i];
		InBuffersToTransition.Remove(&RWBuffer);
		
		if (auto TangentBuffer = GetTangentBuffer())
		{
			InBuffersToTransition.Remove(TangentBuffer);
		}
		if (auto IntermediateTangentBuffer = GetIntermediateTangentBuffer())
		{
			InBuffersToTransition.Remove(IntermediateTangentBuffer);
		}
	}
}

void FGPUSkinCache::ReleaseSkinCacheEntry(FGPUSkinCacheEntry* SkinCacheEntry)
{
	FGPUSkinCache* SkinCache = SkinCacheEntry->SkinCache;

	for (FGPUSkinCacheEntry::FSectionDispatchData& SectionData : SkinCacheEntry->GetDispatchData())
	{
		SectionData.TargetVertexFactory->InvalidateStreams();
	}

	FRWBuffersAllocation* PositionAllocation = SkinCacheEntry->PositionAllocation;
	if (PositionAllocation)
	{
		uint64 RequiredMemInBytes = PositionAllocation->GetNumBytes();
		SkinCache->UsedMemoryInBytes -= RequiredMemInBytes;
		DEC_MEMORY_STAT_BY(STAT_GPUSkinCache_TotalMemUsed, RequiredMemInBytes);

		SkinCache->Allocations.Remove(PositionAllocation);
		PositionAllocation->RemoveAllFromTransitionArray(SkinCache->BuffersToTransitionToRead);

		delete PositionAllocation;

		SkinCacheEntry->PositionAllocation = nullptr;
	}

	SkinCache->Entries.RemoveSingleSwap(SkinCacheEntry, false);
	delete SkinCacheEntry;
}

bool FGPUSkinCache::IsEntryValid(FGPUSkinCacheEntry* SkinCacheEntry, int32 Section)
{
	return SkinCacheEntry->IsSectionValid(Section);
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

FRWBuffer* FGPUSkinCache::GetPositionBuffer(uint32 ComponentId, uint32 SectionIndex) const
{
	for (FGPUSkinCacheEntry* Entry : Entries)
	{
		if (Entry && Entry->GPUSkin && Entry->GPUSkin->GetComponentId() == ComponentId)
		{
			FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->GetDispatchData()[SectionIndex];
			FSkinCacheRWBuffer* SkinCacheRWBuffer = DispatchData.PositionBuffer;
			return SkinCacheRWBuffer != nullptr ? &SkinCacheRWBuffer->Buffer : nullptr;
		}
	}

	return nullptr;
}

FRWBuffer* FGPUSkinCache::GetTangentBuffer(uint32 ComponentId, uint32 SectionIndex) const
{
	for (FGPUSkinCacheEntry* Entry : Entries)
	{
		if (Entry && Entry->GPUSkin && Entry->GPUSkin->GetComponentId() == ComponentId)
		{
			FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->GetDispatchData()[SectionIndex];
			FSkinCacheRWBuffer* SkinCacheRWBuffer = DispatchData.GetTangentRWBuffer();
			return SkinCacheRWBuffer != nullptr ? &SkinCacheRWBuffer->Buffer : nullptr;
		}
	}

	return nullptr;
}

FRHIShaderResourceView* FGPUSkinCache::GetBoneBuffer(uint32 ComponentId, uint32 SectionIndex) const
{
	for (FGPUSkinCacheEntry* Entry : Entries)
	{
		FGPUSkinCacheEntry::FSectionDispatchData& DispatchData = Entry->GetDispatchData()[SectionIndex];
		FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = DispatchData.SourceVertexFactory->GetShaderData();
		return ShaderData.GetBoneBufferForReading(false).VertexBufferSRV;
	}

	return nullptr;
}

FCachedGeometry FGPUSkinCache::GetCachedGeometry(uint32 ComponentId, EGPUSkinCacheEntryMode Mode) const
{
	auto FindEntry = [&](EGPUSkinCacheEntryMode InMode, bool bByPassModeCheck, FCachedGeometry& Out) -> bool
	{
		for (FGPUSkinCacheEntry* Entry : Entries)
		{
			if (Entry && (bByPassModeCheck || Entry->Mode == InMode) && Entry->GPUSkin && Entry->GPUSkin->GetComponentId() == ComponentId && Entry->GPUSkin->HaveValidDynamicData())
			{
				const FSkeletalMeshRenderData& RenderData = Entry->GPUSkin->GetSkeletalMeshRenderData();
				const int32 LODIndex = Entry->LOD;
				if (LODIndex >= 0 && LODIndex < RenderData.LODRenderData.Num())
				{
					const FSkeletalMeshLODRenderData& LODData = RenderData.LODRenderData[LODIndex];
					const uint32 SectionCount = LODData.RenderSections.Num();
					for (uint32 SectionIdx = 0; SectionIdx < SectionCount; ++SectionIdx)
					{
						FCachedGeometry::Section CachedSection = Entry->GetCachedGeometry(SectionIdx);
						CachedSection.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
						CachedSection.TotalIndexCount = LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
						CachedSection.LODIndex = LODIndex;
						CachedSection.UVsChannelOffset = 0; // Assume that we needs to pair meshes based on UVs 0
						CachedSection.UVsChannelCount = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
						Out.Sections.Add(CachedSection);
					}
					Out.LocalToWorld = FTransform(Entry->GPUSkin->GetTransform());
					return true;
				}
			}
		}
		return false;
	};

	// 1. Try to find a Skin cache entry which matches the requested mode (Raster/Raytracing)
	// 2. If we can't find an entry with a matching mode, use any mode type
	FCachedGeometry Out;
	if (!FindEntry(Mode, false, Out))
	{
		FindEntry(Mode, true, Out);
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
		if (GIsRHIInitialized && IsGPUSkinCacheRayTracingSupported())
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

void FGPUSkinCache::IncrementDispatchCounter(FRHICommandListImmediate& RHICmdList)
{
	if (GSkinCacheMaxDispatchesPerCmdList > 0)
	{
		DispatchCounter++;
		if (DispatchCounter >= GSkinCacheMaxDispatchesPerCmdList)
		{
			//UE_LOG(LogSkinCache, Log, TEXT("SubmitCommandsHint issued after %d dispatches"), DispatchCounter);
			RHICmdList.SubmitCommandsHint();
			DispatchCounter = 0;
		}
	}
}

uint64 FGPUSkinCache::GetExtraRequiredMemoryAndReset()
{
	if (GSkinCachePrintMemorySummary == 2 || (GSkinCachePrintMemorySummary == 1 && ExtraRequiredMemory > 0))
	{
		PrintMemorySummary();
	}

	uint64 OriginalValue = ExtraRequiredMemory;
	ExtraRequiredMemory = 0;
	return OriginalValue;
}

void FGPUSkinCache::PrintMemorySummary() const
{
	UE_LOG(LogSkinCache, Display, TEXT("======= Skin Cache Memory Usage Summary ======="));

	uint64 TotalMemInBytes = 0;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		FGPUSkinCacheEntry* Entry = Entries[i];
		if (Entry)
		{
			FString RecomputeTangentSections = TEXT("");
			for (int32 DispatchIdx = 0; DispatchIdx < Entry->DispatchData.Num(); ++DispatchIdx)
			{
				const FGPUSkinCacheEntry::FSectionDispatchData& Data = Entry->DispatchData[DispatchIdx];
				if (Data.IndexBuffer)
				{
					if (RecomputeTangentSections.IsEmpty())
					{
						RecomputeTangentSections = TEXT("[Section]") + FString::FromInt(Data.SectionIndex);
					}
					else
					{
						RecomputeTangentSections = RecomputeTangentSections + TEXT("/") + FString::FromInt(Data.SectionIndex);
					}
				}
			}
			if (RecomputeTangentSections.IsEmpty())
			{
				RecomputeTangentSections = TEXT("Off");
			}

			const FString RayTracingTag = (Entry->Mode == EGPUSkinCacheEntryMode::RayTracing ? TEXT("[RT]") : TEXT(""));
			uint64 MemInBytes = Entry->PositionAllocation ? Entry->PositionAllocation->GetNumBytes() : 0;
			uint64 TangentsInBytes = (Entry->PositionAllocation && Entry->PositionAllocation->GetTangentBuffer()) ? Entry->PositionAllocation->GetTangentBuffer()->Buffer.NumBytes : 0;
			uint64 IntermediateTangentsInBytes = (Entry->PositionAllocation && Entry->PositionAllocation->GetIntermediateTangentBuffer()) ? Entry->PositionAllocation->GetIntermediateTangentBuffer()->Buffer.NumBytes : 0;
			uint64 IntermediateAccumulatedTangentsInBytes = (Entry->PositionAllocation && Entry->PositionAllocation->GetIntermediateAccumulatedTangentBuffer()) ? Entry->PositionAllocation->GetIntermediateAccumulatedTangentBuffer()->Buffer.NumBytes : 0;

			UE_LOG(LogSkinCache, Display, TEXT("   SkinCacheEntry_%d: %sMesh=%s, LOD=%d, RecomputeTangent=%s, Mem=%.3fKB (Tangents=%.3fKB, InterTangents=%.3fKB, InterAccumTangents=%.3fKB)"), 
					i, *RayTracingTag, *GetSkeletalMeshObjectName(Entry->GPUSkin), Entry->LOD, *RecomputeTangentSections, 
					MemInBytes / 1024.f, TangentsInBytes / 1024.f, IntermediateTangentsInBytes / 1024.f, IntermediateAccumulatedTangentsInBytes / 1024.f);
			TotalMemInBytes += MemInBytes;
		}
	}
	ensure(TotalMemInBytes == UsedMemoryInBytes);

	uint64 MaxSizeInBytes = (uint64)(GSkinCacheSceneMemoryLimitInMB * MBSize);
	uint64 UnusedSizeInBytes = MaxSizeInBytes - UsedMemoryInBytes;

	UE_LOG(LogSkinCache, Display, TEXT("Used: %.3fMB"), UsedMemoryInBytes / MBSize);
	UE_LOG(LogSkinCache, Display, TEXT("Available: %.3fMB"), UnusedSizeInBytes / MBSize);
	UE_LOG(LogSkinCache, Display, TEXT("Total limit: %.3fMB"), GSkinCacheSceneMemoryLimitInMB);
	UE_LOG(LogSkinCache, Display, TEXT("Extra required: %.3fMB"), ExtraRequiredMemory / MBSize);
	UE_LOG(LogSkinCache, Display, TEXT("==============================================="));
}

FString FGPUSkinCache::GetSkeletalMeshObjectName(const FSkeletalMeshObjectGPUSkin* GPUSkin) const
{
	FString Name = TEXT("None");
	if (GPUSkin)
	{
#if !UE_BUILD_SHIPPING
		Name = GPUSkin->DebugName.ToString();
#endif // !UE_BUILD_SHIPPING
	}
	return Name;
}
