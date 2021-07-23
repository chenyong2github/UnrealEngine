// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteCullRaster.h"
#include "MeshPassProcessor.h"

static constexpr uint32 NANITE_MAX_MATERIALS = 64;

#define NANITE_MATERIAL_STENCIL 1

DECLARE_GPU_STAT_NAMED_EXTERN(NaniteMaterials, TEXT("Nanite Materials"));
DECLARE_GPU_STAT_NAMED_EXTERN(NaniteDepth, TEXT("Nanite Depth"));

struct FNaniteMaterialPassCommand;
struct FLumenMeshCaptureMaterialPass;
class  FLumenCardPassUniformParameters;
class  FCardPageRenderData;

// VertexCountPerInstance
// InstanceCount
// StartVertexLocation
// StartInstanceLocation
#define NANITE_DRAW_INDIRECT_ARG_COUNT 4

class FNaniteCommandInfo
{
public:
	static constexpr int32 MAX_STATE_BUCKET_ID = (1 << 14) - 1; // Must match NaniteDataDecode.ush

	explicit FNaniteCommandInfo() = default;

	void SetStateBucketId(int32 InStateBucketId)
	{
		check(InStateBucketId < MAX_STATE_BUCKET_ID);
		StateBucketId = InStateBucketId;
	}

	int32 GetStateBucketId() const
	{
		check(StateBucketId < MAX_STATE_BUCKET_ID);
		return StateBucketId;
	}

	void Reset()
	{
		StateBucketId = INDEX_NONE;
	}

	uint32 GetMaterialId() const
	{
		return GetMaterialId(GetStateBucketId());
	}

	static uint32 GetMaterialId(int32 StateBucketId)
	{
		float DepthId = GetDepthId(StateBucketId);
		return *reinterpret_cast<uint32*>(&DepthId);
	}

	static float GetDepthId(int32 StateBucketId)
	{
		return float(StateBucketId + 1) / float(MAX_STATE_BUCKET_ID);
	}

private:
	// Stores the index into FScene::NaniteDrawCommands of the corresponding FMeshDrawCommand
	int32 StateBucketId = INDEX_NONE;
};

struct FNaniteMaterialPassCommand
{
	FNaniteMaterialPassCommand(const FMeshDrawCommand& InMeshDrawCommand)
	: MeshDrawCommand(InMeshDrawCommand)
	, MaterialDepth(0.0f)
	, SortKey(MeshDrawCommand.CachedPipelineId.GetId())
	{
	}

	bool operator < (const FNaniteMaterialPassCommand& Other) const
	{
		return SortKey < Other.SortKey;
	}

	FMeshDrawCommand MeshDrawCommand;
	float MaterialDepth = 0.0f;
	uint64 SortKey = 0;
};

/** Vertex shader to draw a full screen quad at a specific depth that works on all platforms. */
class FNaniteMaterialVS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FNaniteMaterialVS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, MaterialDepth)
		SHADER_PARAMETER(uint32, InstanceBaseOffset)
	END_SHADER_PARAMETER_STRUCT()

	FNaniteMaterialVS()
	{
	}

	FNaniteMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FNaniteShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		NaniteUniformBuffer.Bind(Initializer.ParameterMap, TEXT("Nanite"), SPF_Mandatory);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		ShaderBindings.Add(NaniteUniformBuffer, DrawRenderState.GetNaniteUniformBuffer());
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		const FStaticFeatureLevel FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
	}

private:
	LAYOUT_FIELD(FShaderParameter, MaterialDepth);
	LAYOUT_FIELD(FShaderUniformBufferParameter, NaniteUniformBuffer);
};

struct FNaniteMaterialEntry
{
	FNaniteMaterialEntry()
	: ReferenceCount(0)
	, MaterialId(0)
	, MaterialSlot(INDEX_NONE)
	, bNeedUpload(false)
	{
	}

	FNaniteMaterialEntry(FNaniteMaterialEntry&& Other)
	: ReferenceCount(Other.ReferenceCount.load())
	, MaterialId(Other.MaterialId)
	, MaterialSlot(Other.MaterialSlot)
	, bNeedUpload(false)
	{
		checkSlow(!Other.bNeedUpload);
	}

	std::atomic<uint32> ReferenceCount;
	uint32 MaterialId;
	int32 MaterialSlot;
	bool bNeedUpload;
};

struct FNaniteMaterialEntryKeyFuncs : TDefaultMapHashableKeyFuncs<FMeshDrawCommand, FNaniteMaterialEntry, false>
{
	static inline bool Matches(KeyInitType A, KeyInitType B)
	{
		return A.MatchesForDynamicInstancing(B);
	}

	static inline uint32 GetKeyHash(KeyInitType Key)
	{
		return Key.GetDynamicInstancingHash();
	}
};

using FNaniteMaterialEntryMap = Experimental::TRobinHoodHashMap<FMeshDrawCommand, FNaniteMaterialEntry, FNaniteMaterialEntryKeyFuncs>;

class FNaniteMaterialCommands
{
	friend class FNaniteMaterialCommandsLock;

public:
	typedef Experimental::FHashType FCommandHash;
	typedef Experimental::FHashElementId FCommandId;

public:
	FNaniteMaterialCommands(uint32 MaxMaterials = NANITE_MAX_MATERIALS);
	~FNaniteMaterialCommands();

	void Release();

	FNaniteCommandInfo Register(FMeshDrawCommand& Command);
	void Unregister(const FNaniteCommandInfo& CommandInfo);

	inline const FCommandHash ComputeCommandHash(const FMeshDrawCommand& DrawCommand) const
	{
		return EntryMap.ComputeHash(DrawCommand);
	}

	inline const FCommandId FindIdByHash(const FCommandHash CommandHash, const FMeshDrawCommand& DrawCommand) const
	{
		return EntryMap.FindIdByHash(CommandHash, DrawCommand);
	}

	inline const FCommandId FindIdByCommand(const FMeshDrawCommand& DrawCommand) const
	{
		const FCommandHash CommandHash = ComputeCommandHash(DrawCommand);
		return FindIdByHash(CommandHash, DrawCommand);
	}

	inline const FCommandId FindOrAddIdByHash(const FCommandHash HashValue, const FMeshDrawCommand& DrawCommand)
	{
		return EntryMap.FindOrAddIdByHash(HashValue, DrawCommand, FNaniteMaterialEntry());
	}

	inline void RemoveById(const FCommandId Id)
	{
		EntryMap.RemoveByElementId(Id);
	}

	inline const FMeshDrawCommand& GetCommand(const FCommandId Id) const
	{
		return EntryMap.GetByElementId(Id).Key;
	}

	inline const FNaniteMaterialEntry& GetPayload(const FCommandId Id) const
	{
		return EntryMap.GetByElementId(Id).Value;
	}

	inline FNaniteMaterialEntry& GetPayload(const FCommandId Id)
	{
		return EntryMap.GetByElementId(Id).Value;
	}

	inline const FNaniteMaterialEntryMap& GetCommands() const
	{
		return EntryMap;
	}

	void UpdateBufferState(FRDGBuilder& GraphBuilder, uint32 NumPrimitives);

	void Begin(FRHICommandListImmediate& RHICmdList, uint32 NumPrimitives, uint32 NumPrimitiveUpdates);
	void* GetDepthTablePtr(uint32 PrimitiveIndex, uint32 EntryCount);
#if WITH_EDITOR
	void* GetHitProxyTablePtr(uint32 PrimitiveIndex, uint32 EntryCount);
#endif
	void Finish(FRHICommandListImmediate& RHICmdList);

	FRHIShaderResourceView* GetDepthTableSRV() const { return DepthTableDataBuffer.SRV; }
#if WITH_EDITOR
	FRHIShaderResourceView* GetHitProxyTableSRV() const { return HitProxyTableDataBuffer.SRV; }
#endif

	FRHIShaderResourceView* GetMaterialDepthSRV() const { return MaterialDepthDataBuffer.SRV; }
	//FRHIShaderResourceView* GetMaterialArgumentSRV() const { return MaterialArgumentDataBuffer.SRV; }

private:
	FRWLock ReadWriteLock;
	FNaniteMaterialEntryMap EntryMap;

	uint32 MaxMaterials = 0;
	uint32 NumPrimitiveUpdates = 0;
	uint32 NumDepthTableUpdates = 0;
	uint32 NumHitProxyTableUpdates = 0;
	uint32 NumMaterialSlotUpdates = 0;

	// Old
	FScatterUploadBuffer DepthTableUploadBuffer;
	FRWByteAddressBuffer DepthTableDataBuffer;
	FScatterUploadBuffer HitProxyTableUploadBuffer;
	FRWByteAddressBuffer HitProxyTableDataBuffer;

	// New
	FGrowOnlySpanAllocator	MaterialSlotAllocator;

	FScatterUploadBuffer	MaterialDepthUploadBuffer; // 1 uint per slot (Depth Value)
	FRWByteAddressBuffer	MaterialDepthDataBuffer;

	//FScatterUploadBuffer	MaterialArgumentUploadBuffer; // 4 uints per slot (NANITE_DRAW_INDIRECT_ARG_COUNT)
	//FRWByteAddressBuffer	MaterialArgumentDataBuffer;
};

class FNaniteMaterialCommandsLock : public FRWScopeLock
{
public:
	explicit FNaniteMaterialCommandsLock(FNaniteMaterialCommands& InMaterialCommands, FRWScopeLockType InLockType)
	: FRWScopeLock(InMaterialCommands.ReadWriteLock, InLockType)
	{
	}

	inline void AcquireWriteAccess()
	{
		ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();
	}

private:
	UE_NONCOPYABLE(FNaniteMaterialCommandsLock);
};

extern bool UseComputeDepthExport();

namespace Nanite
{

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntVector4& SOAStrides,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ViewsBuffer,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBuffer64,
	FRDGTextureRef VelocityBuffer,
	FRDGTextureRef& OutMaterialDepth,
	FRDGTextureRef& OutNaniteMask,
	bool bPrePass,
	bool bStencilMask
);

void DrawBasePass(
	FRDGBuilder& GraphBuilder,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& NaniteMaterialPassCommands,
	const FSceneRenderer& SceneRenderer,
	const FSceneTextures& SceneTextures,
	const FDBufferTextures& DBufferTextures,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
);

void DrawLumenMeshCapturePass(
	FRDGBuilder& GraphBuilder,
	FScene& Scene,
	FViewInfo* SharedView,
	TArrayView<const FCardPageRenderData> CardPagesToRender,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	FLumenCardPassUniformParameters* PassUniformParameters,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects,
	FIntPoint ViewportSize,
	FRDGTextureRef AlbedoAtlasTexture,
	FRDGTextureRef NormalAtlasTexture,
	FRDGTextureRef EmissiveAtlasTexture,
	FRDGTextureRef DepthAtlasTexture
);

} // namespace Nanite