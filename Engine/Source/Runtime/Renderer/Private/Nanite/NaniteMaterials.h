// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteCullRaster.h"
#include "MeshPassProcessor.h"

static constexpr uint32 NANITE_MAX_MATERIALS = 64;

// TODO: Until RHIs no longer set stencil ref to 0 on a PSO change, this optimization 
// is actually worse (forces a context roll per unique material draw, back to back).
#define NANITE_MATERIAL_STENCIL 0

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
	explicit FNaniteCommandInfo() = default;

	inline void SetStateBucketId(int32 InStateBucketId)
	{
		check(InStateBucketId < NANITE_MAX_STATE_BUCKET_ID);
		StateBucketId = InStateBucketId;
	}

	inline int32 GetStateBucketId() const
	{
		check(StateBucketId < NANITE_MAX_STATE_BUCKET_ID);
		return StateBucketId;
	}

	inline void Reset()
	{
		StateBucketId = INDEX_NONE;
	}

	inline uint32 GetMaterialId() const
	{
		return GetMaterialId(GetStateBucketId());
	}

	inline void SetMaterialSlot(int32 InMaterialSlot)
	{
		MaterialSlot = InMaterialSlot;
	}

	inline int32 GetMaterialSlot() const
	{
		return MaterialSlot;
	}

	static uint32 GetMaterialId(int32 StateBucketId)
	{
		float DepthId = GetDepthId(StateBucketId);
		return *reinterpret_cast<uint32*>(&DepthId);
	}

	static float GetDepthId(int32 StateBucketId)
	{
		return float(StateBucketId + 1) / float(NANITE_MAX_STATE_BUCKET_ID);
	}

private:
	// Stores the index into FScene::NaniteDrawCommands of the corresponding FMeshDrawCommand
	int32 StateBucketId = INDEX_NONE;

	int32 MaterialSlot = INDEX_NONE;
};

struct FNaniteMaterialPassCommand
{
	FNaniteMaterialPassCommand(const FMeshDrawCommand& InMeshDrawCommand)
	: MeshDrawCommand(InMeshDrawCommand)
	, MaterialDepth(0.0f)
	, MaterialSlot(INDEX_NONE)
	, SortKey(MeshDrawCommand.CachedPipelineId.GetId())
	{
	}

	bool operator < (const FNaniteMaterialPassCommand& Other) const
	{
		return SortKey < Other.SortKey;
	}

	FMeshDrawCommand MeshDrawCommand;
	float MaterialDepth = 0.0f;
	int32 MaterialSlot = INDEX_NONE;
	uint64 SortKey = 0;
};

class FNaniteMultiViewMaterialVS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNaniteMultiViewMaterialVS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float,   MaterialDepth)
		SHADER_PARAMETER(uint32,  InstanceBaseOffset)
	END_SHADER_PARAMETER_STRUCT()

	FNaniteMultiViewMaterialVS() = default;

	FNaniteMultiViewMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FNaniteGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		NaniteUniformBuffer.Bind(Initializer.ParameterMap, TEXT("Nanite"), SPF_Mandatory);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NANITE_MATERIAL_MULTIVIEW"), 1);
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

class FNaniteIndirectMaterialVS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNaniteIndirectMaterialVS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float,   MaterialDepth)
		SHADER_PARAMETER(uint32,  MaterialSlot)
		SHADER_PARAMETER(uint32,  TileRemapCount)
	END_SHADER_PARAMETER_STRUCT()

	FNaniteIndirectMaterialVS() = default;

	FNaniteIndirectMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FNaniteGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		NaniteUniformBuffer.Bind(Initializer.ParameterMap, TEXT("Nanite"), SPF_Mandatory);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NANITE_MATERIAL_MULTIVIEW"), 0);
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
#if WITH_DEBUG_VIEW_MODES
	, InstructionCount(0)
#endif
	, bNeedUpload(false)
	{
	}

	FNaniteMaterialEntry(FNaniteMaterialEntry&& Other)
	: ReferenceCount(Other.ReferenceCount)
	, MaterialId(Other.MaterialId)
	, MaterialSlot(Other.MaterialSlot)
#if WITH_DEBUG_VIEW_MODES
	, InstructionCount(Other.InstructionCount)
#endif
	, bNeedUpload(false)
	{
		checkSlow(!Other.bNeedUpload);
	}

	uint32 ReferenceCount;
	uint32 MaterialId;
	int32 MaterialSlot;
#if WITH_DEBUG_VIEW_MODES
	uint32 InstructionCount;
#endif
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
public:
	typedef Experimental::FHashType FCommandHash;
	typedef Experimental::FHashElementId FCommandId;

public:
	FNaniteMaterialCommands(uint32 MaxMaterials = NANITE_MAX_MATERIALS);
	~FNaniteMaterialCommands();

	void Release();

	FNaniteCommandInfo Register(FMeshDrawCommand& Command, FCommandHash CommandHash, uint32 InstructionCount);
	FNaniteCommandInfo Register(FMeshDrawCommand& Command, uint32 InstructionCount) { return Register(Command, ComputeCommandHash(Command), InstructionCount); }
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
	void* GetMaterialSlotPtr(uint32 PrimitiveIndex, uint32 EntryCount);
#if WITH_EDITOR
	void* GetHitProxyTablePtr(uint32 PrimitiveIndex, uint32 EntryCount);
#endif
	void Finish(FRHICommandListImmediate& RHICmdList);

#if WITH_EDITOR
	FRHIShaderResourceView* GetHitProxyTableSRV() const { return HitProxyTableDataBuffer.SRV; }
#endif

	FRHIShaderResourceView* GetMaterialSlotSRV() const { return MaterialSlotDataBuffer.SRV; }
	FRHIShaderResourceView* GetMaterialDepthSRV() const { return MaterialDepthDataBuffer.SRV; }
#if WITH_DEBUG_VIEW_MODES
	FRHIShaderResourceView* GetMaterialEditorSRV() const { return MaterialEditorDataBuffer.SRV; }
#endif

	inline const int32 GetHighestMaterialSlot() const
	{
		return MaterialSlotAllocator.GetMaxSize();
	}

private:
	FNaniteMaterialEntryMap EntryMap;

	uint32 MaxMaterials = 0;
	uint32 NumPrimitiveUpdates = 0;
	uint32 NumHitProxyTableUpdates = 0;
	uint32 NumMaterialSlotUpdates = 0;
	uint32 NumMaterialDepthUpdates = 0;

	FScatterUploadBuffer MaterialSlotUploadBuffer;
	FRWByteAddressBuffer MaterialSlotDataBuffer;

	FScatterUploadBuffer HitProxyTableUploadBuffer;
	FRWByteAddressBuffer HitProxyTableDataBuffer;

	FGrowOnlySpanAllocator	MaterialSlotAllocator;

	FScatterUploadBuffer	MaterialDepthUploadBuffer; // 1 uint per slot (Depth Value)
	FRWByteAddressBuffer	MaterialDepthDataBuffer;

#if WITH_DEBUG_VIEW_MODES
	FScatterUploadBuffer	MaterialEditorUploadBuffer; // 1 uint per slot (VS and PS instruction count)
	FRWByteAddressBuffer	MaterialEditorDataBuffer;
#endif
};

extern bool UseComputeDepthExport();

namespace Nanite
{

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntVector4& PageConstants,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ViewsBuffer,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBuffer64,
	FRDGTextureRef VelocityBuffer,
	FRDGTextureRef& OutMaterialDepth,
	FRDGTextureRef& OutMaterialResolve,
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