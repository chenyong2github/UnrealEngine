// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshPassProcessor.h
=============================================================================*/

#pragma once

#include "MeshMaterialShader.h"
#include "SceneUtils.h"
#include "MeshBatch.h"
#include "Hash/CityHash.h"
#include "Experimental/Containers/RobinHoodHashTable.h"

#define MESH_DRAW_COMMAND_DEBUG_DATA ((!UE_BUILD_SHIPPING && !UE_BUILD_TEST) || VALIDATE_MESH_COMMAND_BINDINGS || WANTS_DRAW_MESH_EVENTS)

class FRayTracingLocalShaderBindingWriter;

/** Mesh pass types supported. */
namespace EMeshPass
{
	enum Type
	{
		DepthPass,
		BasePass,
		AnisotropyPass,
		SkyPass,
		SingleLayerWaterPass,
		CSMShadowDepth,
		Distortion,
		Velocity,
		TranslucentVelocity,
		TranslucencyStandard,
		TranslucencyAfterDOF,
		TranslucencyAfterDOFModulate,
		TranslucencyAll, /** Drawing all translucency, regardless of separate or standard.  Used when drawing translucency outside of the main renderer, eg FRendererModule::DrawTile. */
		LightmapDensity,
		DebugViewMode, /** Any of EDebugViewShaderMode */
		CustomDepth,
		MobileBasePassCSM,  /** Mobile base pass with CSM shading enabled */
		MobileInverseOpacity,  /** Mobile specific scene capture, Non-cached */
		VirtualTexture,
		DitheredLODFadingOutMaskPass, /** A mini depth pass used to mark pixels with dithered LOD fading out. Currently only used by ray tracing shadows. */

#if WITH_EDITOR
		HitProxy,
		HitProxyOpaqueOnly,
		EditorSelection,
#endif

		Num,
		NumBits = 5,
	};
}
static_assert(EMeshPass::Num <= (1 << EMeshPass::NumBits), "EMeshPass::Num will not fit in EMeshPass::NumBits");

inline const TCHAR* GetMeshPassName(EMeshPass::Type MeshPass)
{
	switch (MeshPass)
	{
	case EMeshPass::DepthPass: return TEXT("DepthPass");
	case EMeshPass::BasePass: return TEXT("BasePass");
	case EMeshPass::AnisotropyPass: return TEXT("AnisotropyPass");
	case EMeshPass::SkyPass: return TEXT("SkyPass");
	case EMeshPass::SingleLayerWaterPass: return TEXT("SingleLayerWaterPass");
	case EMeshPass::CSMShadowDepth: return TEXT("CSMShadowDepth");
	case EMeshPass::Distortion: return TEXT("Distortion");
	case EMeshPass::Velocity: return TEXT("Velocity");
	case EMeshPass::TranslucentVelocity: return TEXT("TranslucentVelocity");
	case EMeshPass::TranslucencyStandard: return TEXT("TranslucencyStandard");
	case EMeshPass::TranslucencyAfterDOF: return TEXT("TranslucencyAfterDOF");
	case EMeshPass::TranslucencyAfterDOFModulate: return TEXT("TranslucencyAfterDOFModulate");
	case EMeshPass::TranslucencyAll: return TEXT("TranslucencyAll");
	case EMeshPass::LightmapDensity: return TEXT("LightmapDensity");
	case EMeshPass::DebugViewMode: return TEXT("DebugViewMode");
	case EMeshPass::CustomDepth: return TEXT("CustomDepth");
	case EMeshPass::MobileBasePassCSM: return TEXT("MobileBasePassCSM");
	case EMeshPass::MobileInverseOpacity: return TEXT("MobileInverseOpacity");
#if WITH_EDITOR
	case EMeshPass::HitProxy: return TEXT("HitProxy");
	case EMeshPass::HitProxyOpaqueOnly: return TEXT("HitProxyOpaqueOnly");
	case EMeshPass::EditorSelection: return TEXT("EditorSelection");
#endif
	}

	checkf(0, TEXT("Missing case for EMeshPass %u"), (uint32)MeshPass);
	return nullptr;
}

/** Mesh pass mask - stores one bit per mesh pass. */
class FMeshPassMask
{
public:
	FMeshPassMask()
		: Data(0)
	{
	}

	void Set(EMeshPass::Type Pass) 
	{ 
		Data |= (1 << Pass); 
	}

	bool Get(EMeshPass::Type Pass) const 
	{ 
		return !!(Data & (1 << Pass)); 
	}

	EMeshPass::Type SkipEmpty(EMeshPass::Type Pass) const 
	{
		uint32 Mask = 0xFFffFFff << Pass;
		return EMeshPass::Type(FMath::Min<uint32>(EMeshPass::Num, FMath::CountTrailingZeros(Data & Mask)));
	}

	int GetNum() 
	{ 
		return FMath::CountBits(Data); 
	}

	void AppendTo(FMeshPassMask& Mask) const 
	{ 
		Mask.Data |= Data; 
	}

	void Reset() 
	{ 
		Data = 0; 
	}

	bool IsEmpty() const 
	{ 
		return Data == 0; 
	}

	uint32 Data;
};

struct FMinimalBoundShaderStateInput
{
	inline FMinimalBoundShaderStateInput() {}

	FBoundShaderStateInput AsBoundShaderState() const
	{
		return FBoundShaderStateInput(VertexDeclarationRHI
			, VertexShaderResource ? static_cast<FRHIVertexShader*>(VertexShaderResource->GetShader(VertexShaderIndex)) : nullptr
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			, HullShaderResource ? static_cast<FRHIHullShader*>(HullShaderResource->GetShader(HullShaderIndex)) : nullptr
			, DomainShaderResource ? static_cast<FRHIDomainShader*>(DomainShaderResource->GetShader(DomainShaderIndex)) : nullptr
#endif
			, PixelShaderResource ? static_cast<FRHIPixelShader*>(PixelShaderResource->GetShader(PixelShaderIndex)) : nullptr
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			, GeometryShaderResource ? static_cast<FRHIGeometryShader*>(GeometryShaderResource->GetShader(GeometryShaderIndex)) : nullptr
#endif
		);
	}

	void LazilyInitShaders() const
	{
		AsBoundShaderState(); // querying shaders will initialize on demand
	}

	bool NeedsShaderInitialisation() const
	{
		if (VertexShaderResource && !VertexShaderResource->HasShader(VertexShaderIndex))
		{
			return true;
		}
		if (HullShaderResource && !HullShaderResource->HasShader(HullShaderIndex))
		{
			return true;
		}
		if (DomainShaderResource && !DomainShaderResource->HasShader(DomainShaderIndex))
		{
			return true;
		}
		if (PixelShaderResource && !PixelShaderResource->HasShader(PixelShaderIndex))
		{
			return true;
		}
		if (GeometryShaderResource && !GeometryShaderResource->HasShader(GeometryShaderIndex))
		{
			return true;
		}
		return false;
	}

	FRHIVertexDeclaration* VertexDeclarationRHI = nullptr;
	TRefCountPtr<FShaderMapResource> VertexShaderResource;
	TRefCountPtr<FShaderMapResource> HullShaderResource;
	TRefCountPtr<FShaderMapResource> DomainShaderResource;
	TRefCountPtr<FShaderMapResource> PixelShaderResource;
	TRefCountPtr<FShaderMapResource> GeometryShaderResource;
	int32 VertexShaderIndex = INDEX_NONE;
	int32 HullShaderIndex = INDEX_NONE;
	int32 DomainShaderIndex = INDEX_NONE;
	int32 PixelShaderIndex = INDEX_NONE;
	int32 GeometryShaderIndex = INDEX_NONE;
};


/**
 * Pipeline state without render target state
 * Useful for mesh passes where the render target state is not changing between draws.
 * Note: the size of this class affects rendering mesh pass traversal performance.
 */
class FGraphicsMinimalPipelineStateInitializer
{
public:
	// Can't use TEnumByte<EPixelFormat> as it changes the struct to be non trivially constructible, breaking memset
	using TRenderTargetFormats = TStaticArray<uint8/*EPixelFormat*/, MaxSimultaneousRenderTargets>;
	using TRenderTargetFlags = TStaticArray<uint32/*ETextureCreateFlags*/, MaxSimultaneousRenderTargets>;

	FGraphicsMinimalPipelineStateInitializer()
		: BlendState(nullptr)
		, RasterizerState(nullptr)
		, DepthStencilState(nullptr)
		, PrimitiveType(PT_Num)
	{
		static_assert(sizeof(EPixelFormat) != sizeof(uint8), "Change TRenderTargetFormats's uint8 to EPixelFormat");
		static_assert(PF_MAX < MAX_uint8, "TRenderTargetFormats assumes EPixelFormat can fit in a uint8!");
	}

	FGraphicsMinimalPipelineStateInitializer(
		FMinimalBoundShaderStateInput	InBoundShaderState,
		FRHIBlendState*					InBlendState,
		FRHIRasterizerState*			InRasterizerState,
		FRHIDepthStencilState*			InDepthStencilState,
		FImmutableSamplerState			InImmutableSamplerState,
		EPrimitiveType					InPrimitiveType
	)
		: BoundShaderState(InBoundShaderState)
		, BlendState(InBlendState)
		, RasterizerState(InRasterizerState)
		, DepthStencilState(InDepthStencilState)
		, ImmutableSamplerState(InImmutableSamplerState)
		, PrimitiveType(InPrimitiveType)
	{
	}

	FGraphicsMinimalPipelineStateInitializer(const FGraphicsMinimalPipelineStateInitializer& InMinimalState)
		: BoundShaderState(InMinimalState.BoundShaderState)
		, BlendState(InMinimalState.BlendState)
		, RasterizerState(InMinimalState.RasterizerState)
		, DepthStencilState(InMinimalState.DepthStencilState)
		, ImmutableSamplerState(InMinimalState.ImmutableSamplerState)
		, bDepthBounds(InMinimalState.bDepthBounds)
		, DrawShadingRate(InMinimalState.DrawShadingRate)
		, PrimitiveType(InMinimalState.PrimitiveType)
	{
	}

	FGraphicsPipelineStateInitializer AsGraphicsPipelineStateInitializer() const
	{	
		return FGraphicsPipelineStateInitializer
		(	BoundShaderState.AsBoundShaderState()
			, BlendState
			, RasterizerState
			, DepthStencilState
			, ImmutableSamplerState
			, PrimitiveType
			, 0
			, FGraphicsPipelineStateInitializer::TRenderTargetFormats(PF_Unknown)
			, FGraphicsPipelineStateInitializer::TRenderTargetFlags(0)
			, PF_Unknown
			, TexCreate_None
			, ERenderTargetLoadAction::ENoAction
			, ERenderTargetStoreAction::ENoAction
			, ERenderTargetLoadAction::ENoAction
			, ERenderTargetStoreAction::ENoAction
			, FExclusiveDepthStencil::DepthNop
			, 0
			, ESubpassHint::None
			, 0
			, 0
			, bDepthBounds
			, MultiViewCount
			, bHasFragmentDensityAttachment
			, DrawShadingRate
		);
	}

	inline bool operator==(const FGraphicsMinimalPipelineStateInitializer& rhs) const
	{
		if (BoundShaderState.VertexDeclarationRHI != rhs.BoundShaderState.VertexDeclarationRHI ||
			BoundShaderState.VertexShaderResource != rhs.BoundShaderState.VertexShaderResource ||
			BoundShaderState.PixelShaderResource != rhs.BoundShaderState.PixelShaderResource ||
			BoundShaderState.VertexShaderIndex != rhs.BoundShaderState.VertexShaderIndex ||
			BoundShaderState.PixelShaderIndex != rhs.BoundShaderState.PixelShaderIndex ||
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			BoundShaderState.GeometryShaderResource != rhs.BoundShaderState.GeometryShaderResource ||
			BoundShaderState.GeometryShaderIndex != rhs.BoundShaderState.GeometryShaderIndex ||
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			BoundShaderState.DomainShaderResource != rhs.BoundShaderState.DomainShaderResource ||
			BoundShaderState.HullShaderResource != rhs.BoundShaderState.HullShaderResource ||
			BoundShaderState.DomainShaderIndex != rhs.BoundShaderState.DomainShaderIndex ||
			BoundShaderState.HullShaderIndex != rhs.BoundShaderState.HullShaderIndex ||
#endif		
			BlendState != rhs.BlendState ||
			RasterizerState != rhs.RasterizerState ||
			DepthStencilState != rhs.DepthStencilState ||
			ImmutableSamplerState != rhs.ImmutableSamplerState ||
			bDepthBounds != rhs.bDepthBounds ||
			MultiViewCount != rhs.MultiViewCount ||
			bHasFragmentDensityAttachment != rhs.bHasFragmentDensityAttachment ||
			DrawShadingRate != rhs.DrawShadingRate ||
			PrimitiveType != rhs.PrimitiveType)
		{
			return false;
		}

		return true;
	}

	inline bool operator!=(const FGraphicsMinimalPipelineStateInitializer& rhs) const
	{
		return !(*this == rhs);
	}

	inline friend uint32 GetTypeHash(const FGraphicsMinimalPipelineStateInitializer& Initializer)
	{
		//add and initialize any leftover padding within the struct to avoid unstable key
		struct FHashKey
		{
			uint32 VertexDeclaration;
			uint32 VertexShader;
			uint32 PixelShader;
			uint32 RasterizerState;
		} HashKey;
		HashKey.VertexDeclaration = PointerHash(Initializer.BoundShaderState.VertexDeclarationRHI);
		HashKey.VertexShader = GetTypeHash(Initializer.BoundShaderState.VertexShaderIndex);
		HashKey.PixelShader = GetTypeHash(Initializer.BoundShaderState.PixelShaderIndex);
		HashKey.RasterizerState = PointerHash(Initializer.RasterizerState);

		return uint32(CityHash64((const char*)&HashKey, sizeof(FHashKey)));
	}

#define COMPARE_FIELD_BEGIN(Field) \
		if (Field != rhs.Field) \
		{ return Field COMPARE_OP rhs.Field; }

#define COMPARE_FIELD(Field) \
		else if (Field != rhs.Field) \
		{ return Field COMPARE_OP rhs.Field; }

#define COMPARE_FIELD_END \
		else { return false; }

	bool operator<(const FGraphicsMinimalPipelineStateInitializer& rhs) const
	{
#define COMPARE_OP <

		COMPARE_FIELD_BEGIN(BoundShaderState.VertexDeclarationRHI)
			COMPARE_FIELD(BoundShaderState.VertexShaderIndex)
			COMPARE_FIELD(BoundShaderState.PixelShaderIndex)
			COMPARE_FIELD(BoundShaderState.VertexShaderResource)
			COMPARE_FIELD(BoundShaderState.PixelShaderResource)
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			COMPARE_FIELD(BoundShaderState.GeometryShaderIndex)
			COMPARE_FIELD(BoundShaderState.GeometryShaderResource)
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			COMPARE_FIELD(BoundShaderState.DomainShaderIndex)
			COMPARE_FIELD(BoundShaderState.HullShaderIndex)
			COMPARE_FIELD(BoundShaderState.DomainShaderResource)
			COMPARE_FIELD(BoundShaderState.HullShaderResource)
#endif
			COMPARE_FIELD(BlendState)
			COMPARE_FIELD(RasterizerState)
			COMPARE_FIELD(DepthStencilState)
			COMPARE_FIELD(bDepthBounds)
			COMPARE_FIELD(MultiViewCount)
			COMPARE_FIELD(bHasFragmentDensityAttachment)
			COMPARE_FIELD(DrawShadingRate)
			COMPARE_FIELD(PrimitiveType)
		COMPARE_FIELD_END;

#undef COMPARE_OP
	}

	bool operator>(const FGraphicsMinimalPipelineStateInitializer& rhs) const
	{
#define COMPARE_OP >

		COMPARE_FIELD_BEGIN(BoundShaderState.VertexDeclarationRHI)
			COMPARE_FIELD(BoundShaderState.VertexShaderIndex)
			COMPARE_FIELD(BoundShaderState.PixelShaderIndex)
			COMPARE_FIELD(BoundShaderState.VertexShaderResource)
			COMPARE_FIELD(BoundShaderState.PixelShaderResource)
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			COMPARE_FIELD(BoundShaderState.GeometryShaderIndex)
			COMPARE_FIELD(BoundShaderState.GeometryShaderResource)
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			COMPARE_FIELD(BoundShaderState.DomainShaderIndex)
			COMPARE_FIELD(BoundShaderState.HullShaderIndex)
			COMPARE_FIELD(BoundShaderState.DomainShaderResource)
			COMPARE_FIELD(BoundShaderState.HullShaderResource)
#endif
			COMPARE_FIELD(BlendState)
			COMPARE_FIELD(RasterizerState)
			COMPARE_FIELD(DepthStencilState)
			COMPARE_FIELD(bDepthBounds)
			COMPARE_FIELD(MultiViewCount)
			COMPARE_FIELD(bHasFragmentDensityAttachment)
			COMPARE_FIELD(DrawShadingRate)
			COMPARE_FIELD(PrimitiveType)
			COMPARE_FIELD_END;

#undef COMPARE_OP
	}

#undef COMPARE_FIELD_BEGIN
#undef COMPARE_FIELD
#undef COMPARE_FIELD_END

	// TODO: [PSO API] - As we migrate reuse existing API objects, but eventually we can move to the direct initializers. 
	// When we do that work, move this to RHI.h as its more appropriate there, but here for now since dependent typdefs are here.
	FMinimalBoundShaderStateInput	BoundShaderState;
	FRHIBlendState*					BlendState;
	FRHIRasterizerState*			RasterizerState;
	FRHIDepthStencilState*			DepthStencilState;
	FImmutableSamplerState			ImmutableSamplerState;

	// Note: FGraphicsMinimalPipelineStateInitializer is 8-byte aligned and can't have any implicit padding,
	// as it is sometimes hashed and compared as raw bytes. Explicit padding is therefore required between
	// all data members and at the end of the structure.
	bool							bDepthBounds = false;
	uint8							MultiViewCount = 0;
	bool							bHasFragmentDensityAttachment = false;
	EVRSShadingRate					DrawShadingRate  = EVRSShadingRate::VRSSR_1x1;

	EPrimitiveType			PrimitiveType;
};

static_assert(sizeof(FMeshPassMask::Data) * 8 >= EMeshPass::Num, "FMeshPassMask::Data is too small to fit all mesh passes.");

/** Set of FGraphicsMinimalPipelineStateInitializer unique per MeshDrawCommandsPassContext */
typedef Experimental::TRobinHoodHashSet< FGraphicsMinimalPipelineStateInitializer > FGraphicsMinimalPipelineStateSet;

/** Uniquely represents a FGraphicsMinimalPipelineStateInitializer for fast compares. */
class FGraphicsMinimalPipelineStateId
{
public:
	FORCEINLINE_DEBUGGABLE uint32 GetId() const
	{
		checkSlow(IsValid());
		return PackedId;
	}

	inline bool IsValid() const 
	{
		return bValid != 0;
	}


	inline bool operator==(const FGraphicsMinimalPipelineStateId& rhs) const
	{
		return PackedId == rhs.PackedId;
	}

	inline bool operator!=(const FGraphicsMinimalPipelineStateId& rhs) const
	{
		return !(*this == rhs);
	}
	
	inline const FGraphicsMinimalPipelineStateInitializer& GetPipelineState(const FGraphicsMinimalPipelineStateSet& InPipelineSet) const
	{
		if (bComesFromLocalPipelineStateSet)
		{
			return InPipelineSet.GetByElementId(SetElementIndex);
		}

		{
			FScopeLock Lock(&PersistentIdTableLock);
			return PersistentIdTable.GetByElementId(SetElementIndex).Key;
		}
	}

	static void InitializePersistentIds();
	/**
	 * Get a ref counted persistent pipeline id, which needs to manually released.
	 */
	static FGraphicsMinimalPipelineStateId GetPersistentId(const FGraphicsMinimalPipelineStateInitializer& InPipelineState);

	/**
	 * Removes a persistent pipeline Id from the global persistent Id table.
	 */
	static void RemovePersistentId(FGraphicsMinimalPipelineStateId Id);
	
	/**
	 * Get a pipeline state id in this order: global persistent Id table. If not found, will lookup in PassSet argument. If not found in PassSet argument, create a blank pipeline set id and add it PassSet argument
	 */
	RENDERER_API static FGraphicsMinimalPipelineStateId GetPipelineStateId(const FGraphicsMinimalPipelineStateInitializer& InPipelineState, FGraphicsMinimalPipelineStateSet& InOutPassSet, bool& NeedsShaderInitialisation);

	static int32 GetLocalPipelineIdTableSize() 
	{ 
		FScopeLock Lock(&PersistentIdTableLock);
		return LocalPipelineIdTableSize; 
	}
	static void ResetLocalPipelineIdTableSize();
	static void AddSizeToLocalPipelineIdTableSize(SIZE_T Size);

	static SIZE_T GetPersistentIdTableSize() 
	{ 
		FScopeLock Lock(&PersistentIdTableLock);
		return PersistentIdTable.GetAllocatedSize(); 
	}
	static int32 GetPersistentIdNum() 
	{ 
		FScopeLock Lock(&PersistentIdTableLock);
		return PersistentIdTable.Num(); 
	}

private:
	union
	{
		uint32 PackedId = 0;

		struct
		{
			uint32 SetElementIndex				   : 30;
			uint32 bComesFromLocalPipelineStateSet : 1;
			uint32 bValid						   : 1;
		};
	};

	struct FRefCountedGraphicsMinimalPipelineState
	{
		FRefCountedGraphicsMinimalPipelineState() : RefNum(0)
		{
		}
		uint32 RefNum;
	};

	static FCriticalSection PersistentIdTableLock;
	using PersistentTableType = Experimental::TRobinHoodHashMap<FGraphicsMinimalPipelineStateInitializer, FRefCountedGraphicsMinimalPipelineState>;
	static PersistentTableType PersistentIdTable;

	static int32 LocalPipelineIdTableSize;
	static int32 CurrentLocalPipelineIdTableSize;
	static bool NeedsShaderInitialisation;
};

class FShaderBindingState
{
	enum { MAX_SRVS_PER_STAGE = 128 };
	enum { MAX_UNIFORM_BUFFERS_PER_STAGE = 14 };
	enum { MAX_SAMPLERS_PER_STAGE = 32 };

public:
	int32 MaxSRVUsed = -1;
	FRHIShaderResourceView* SRVs[MAX_SRVS_PER_STAGE] = {};
	int32 MaxUniformBufferUsed = -1;
	FRHIUniformBuffer* UniformBuffers[MAX_UNIFORM_BUFFERS_PER_STAGE] = {};
	int32 MaxTextureUsed = -1;
	FRHITexture* Textures[MAX_SRVS_PER_STAGE] = {};
	int32 MaxSamplerUsed = -1;
	FRHISamplerState* Samplers[MAX_SAMPLERS_PER_STAGE] = {};
};

struct FMeshProcessorShaders
{
	mutable TShaderRef<FMeshMaterialShader> VertexShader;
	mutable TShaderRef<FMeshMaterialShader> HullShader;
	mutable TShaderRef<FMeshMaterialShader> DomainShader;
	mutable TShaderRef<FMeshMaterialShader> PixelShader;
	mutable TShaderRef<FMeshMaterialShader> GeometryShader;
	mutable TShaderRef<FMeshMaterialShader> ComputeShader;
#if RHI_RAYTRACING
	mutable TShaderRef<FMeshMaterialShader> RayHitGroupShader;
#endif

	TShaderRef<FMeshMaterialShader> GetShader(EShaderFrequency Frequency) const
	{
		if (Frequency == SF_Vertex)
		{
			return VertexShader;
		}
		else if (Frequency == SF_Hull)
		{
			return HullShader;
		}
		else if (Frequency == SF_Domain)
		{
			return DomainShader;
		}
		else if (Frequency == SF_Pixel)
		{
			return PixelShader;
		}
		else if (Frequency == SF_Geometry)
		{
			return GeometryShader;
		}
		else if (Frequency == SF_Compute)
		{
			return ComputeShader;
		}
#if RHI_RAYTRACING
		else if (Frequency == SF_RayHitGroup)
		{
			return RayHitGroupShader;
		}
#endif // RHI_RAYTRACING

		checkf(0, TEXT("Unhandled shader frequency"));
		return TShaderRef<FMeshMaterialShader>();
	}
};

/** 
 * Number of resource bindings to allocate inline within a FMeshDrawCommand.
 * This is tweaked so that the bindings for BasePass shaders of an average material using a FLocalVertexFactory fit into the inline storage.
 * Overflow of the inline storage will cause a heap allocation per draw (and corresponding cache miss on traversal)
 */
const int32 NumInlineShaderBindings = 10;

/**
* Debug only data for being able to backtrack the origin of given FMeshDrawCommand.
*/
struct FMeshDrawCommandDebugData
{
#if MESH_DRAW_COMMAND_DEBUG_DATA
	const FPrimitiveSceneProxy* PrimitiveSceneProxyIfNotUsingStateBuckets;
	const FMaterialRenderProxy* MaterialRenderProxy;
	TShaderRef<FMeshMaterialShader> VertexShader;
	TShaderRef<FMeshMaterialShader> PixelShader;
	const FVertexFactory* VertexFactory;
	FName ResourceName;
	FString MaterialName;
#endif
};

/** 
 * Encapsulates shader bindings for a single FMeshDrawCommand.
 */
class FMeshDrawShaderBindings
{
public:

	FMeshDrawShaderBindings() 
	{
		static_assert(sizeof(ShaderFrequencyBits) * 8 > SF_NumFrequencies, "Please increase ShaderFrequencyBits size");
	}
	FMeshDrawShaderBindings(FMeshDrawShaderBindings&& Other)
	{
		if (!UsesInlineStorage())
		{
			delete[] Data.GetHeapData();
		}
		Size = Other.Size;
		ShaderFrequencyBits = Other.ShaderFrequencyBits;
		ShaderLayouts = MoveTemp(Other.ShaderLayouts);
		if (Other.UsesInlineStorage())
		{
			Data = MoveTemp(Other.Data);
		}
		else
		{		
			Data.SetHeapData(Other.Data.GetHeapData());
			Other.Data.SetHeapData(nullptr);
		}
		Other.Size = 0;	
	}

	FMeshDrawShaderBindings(const FMeshDrawShaderBindings& Other)
	{
		CopyFrom(Other);
	}
	RENDERER_API ~FMeshDrawShaderBindings();

	FMeshDrawShaderBindings& operator=(const FMeshDrawShaderBindings& Other)
	{
		CopyFrom(Other);
		return *this;
	}

	FMeshDrawShaderBindings& operator=(FMeshDrawShaderBindings&& Other)
	{
		if (!UsesInlineStorage())
		{
			delete[] Data.GetHeapData();
		}
		Size = Other.Size;
		ShaderFrequencyBits = Other.ShaderFrequencyBits;
		ShaderLayouts = MoveTemp(Other.ShaderLayouts);
		if (Other.UsesInlineStorage())
		{
			Data = MoveTemp(Other.Data);
		}
		else
		{	
			Data.SetHeapData(Other.Data.GetHeapData());
			Other.Data.SetHeapData(nullptr);
		}
		Other.Size = 0;
		return *this;
	}

	/** Allocates space for the bindings of all shaders. */
	void Initialize(FMeshProcessorShaders Shaders);

	/** Called once binding setup is complete. */
	void Finalize(const FMeshProcessorShaders* ShadersForDebugging);

	inline FMeshDrawSingleShaderBindings GetSingleShaderBindings(EShaderFrequency Frequency, int32& DataOffset)
	{
		int FrequencyIndex = FPlatformMath::CountBits(ShaderFrequencyBits & ((1 << (Frequency + 1)) - 1)) - 1;

#if DO_CHECK && !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
		int32 CheckedDataOffset = 0;
		for (int32 BindingIndex = 0; BindingIndex < FrequencyIndex; BindingIndex++)
		{
			CheckedDataOffset += ShaderLayouts[BindingIndex].GetDataSizeBytes();
		}
		checkf(CheckedDataOffset == DataOffset, TEXT("GetSingleShaderBindings was not called in the order of ShaderFrequencies"));
#endif
		if (FrequencyIndex >= 0)
		{
			int32 StartDataOffset = DataOffset;
			DataOffset += ShaderLayouts[FrequencyIndex].GetDataSizeBytes();
			return FMeshDrawSingleShaderBindings(ShaderLayouts[FrequencyIndex], GetData() + StartDataOffset);
		}

		checkf(0, TEXT("Invalid shader binding frequency requested"));
		return FMeshDrawSingleShaderBindings(FMeshDrawShaderBindingsLayout(TShaderRef<FShader>()), nullptr);
	}

	/** Set shader bindings on the commandlist, filtered by state cache. */
	void SetOnCommandList(FRHICommandList& RHICmdList, FBoundShaderStateInput Shaders, class FShaderBindingState* StateCacheShaderBindings) const;
	void SetOnCommandList(FRHIComputeCommandList& RHICmdList, FRHIComputeShader* Shader, class FShaderBindingState* StateCacheShaderBindings = nullptr) const;

#if RHI_RAYTRACING
	RENDERER_API void SetRayTracingShaderBindingsForHitGroup(FRayTracingLocalShaderBindingWriter* BindingWriter, uint32 InstanceIndex, uint32 SegmentIndex, uint32 HitGroupIndex, uint32 ShaderSlot) const;
#endif // RHI_RAYTRACING

	/** Returns whether this set of shader bindings can be merged into an instanced draw call with another. */
	bool RENDERER_API MatchesForDynamicInstancing(const FMeshDrawShaderBindings& Rhs) const;

	uint32 RENDERER_API GetDynamicInstancingHash() const;

	SIZE_T GetAllocatedSize() const
	{
		SIZE_T Bytes = ShaderLayouts.GetAllocatedSize();
		if (!UsesInlineStorage())
		{
			Bytes += Size;
		}

		return Bytes;
	}

	void GetShaderFrequencies(TArray<EShaderFrequency, TInlineAllocator<SF_NumFrequencies>>& OutShaderFrequencies) const
	{
		OutShaderFrequencies.Empty(ShaderLayouts.Num());

		for (int32 BindingIndex = 0; BindingIndex < SF_NumFrequencies; BindingIndex++)
		{
			if ((ShaderFrequencyBits & (1 << BindingIndex)) != 0)
			{
				OutShaderFrequencies.Add(EShaderFrequency(BindingIndex));
			}
		}
	}

	inline int32 GetDataSize() const { return Size; }

private:
	TArray<FMeshDrawShaderBindingsLayout, TInlineAllocator<2>> ShaderLayouts;
	struct FData
	{
		uint8* InlineStorage[NumInlineShaderBindings] = {};
		uint8* GetHeapData()
		{
			return InlineStorage[0];
		}
		const uint8* GetHeapData() const
		{
			return InlineStorage[0];
		}
		void SetHeapData(uint8* HeapData)
		{
			InlineStorage[0] = HeapData;
		}
	} Data = {};
	uint16 ShaderFrequencyBits = 0;
	uint16 Size = 0;

	void Allocate(uint16 InSize)
	{
		check(Size == 0 && Data.GetHeapData() == nullptr);

		Size = InSize;

		if (InSize > sizeof(FData))
		{
			Data.SetHeapData(new uint8[InSize]);
		}
	}

	void AllocateZeroed(uint32 InSize) 
	{
		Allocate(InSize);

		// Verify no type overflow
		check(Size == InSize);

		if (!UsesInlineStorage())
		{
			FPlatformMemory::Memzero(GetData(), InSize);
		}
	}

	inline bool UsesInlineStorage() const
	{
		return Size <= sizeof(FData);
	}

	uint8* GetData()
	{
		return UsesInlineStorage() ? reinterpret_cast<uint8*>(&Data.InlineStorage[0]) : Data.GetHeapData();
	}

	const uint8* GetData() const
	{
		return UsesInlineStorage() ? reinterpret_cast<const uint8*>(&Data.InlineStorage[0]) : Data.GetHeapData();
	}

	RENDERER_API void CopyFrom(const FMeshDrawShaderBindings& Other);

	RENDERER_API void Release();

	template<class RHICmdListType, class RHIShaderType>
	static void SetShaderBindings(
		RHICmdListType& RHICmdList,
		RHIShaderType Shader,
		const class FReadOnlyMeshDrawSingleShaderBindings& RESTRICT SingleShaderBindings,
		FShaderBindingState& RESTRICT ShaderBindingState);

	template<class RHICmdListType, class RHIShaderType>
	static void SetShaderBindings(
		RHICmdListType& RHICmdList,
		RHIShaderType Shader,
		const class FReadOnlyMeshDrawSingleShaderBindings& RESTRICT SingleShaderBindings);
};

/** 
 * FMeshDrawCommand fully describes a mesh pass draw call, captured just above the RHI.  
		FMeshDrawCommand should contain only data needed to draw.  For InitViews payloads, use FVisibleMeshDrawCommand.
 * FMeshDrawCommands are cached at Primitive AddToScene time for vertex factories that support it (no per-frame or per-view shader binding changes).
 * Dynamic Instancing operates at the FMeshDrawCommand level for robustness.  
		Adding per-command shader bindings will reduce the efficiency of Dynamic Instancing, but rendering will always be correct.
 * Any resources referenced by a command must be kept alive for the lifetime of the command.  FMeshDrawCommand is not responsible for lifetime management of resources.
		For uniform buffers referenced by cached FMeshDrawCommand's, RHIUpdateUniformBuffer makes it possible to access per-frame data in the shader without changing bindings.
 */
class FMeshDrawCommand
{
public:
	
	/**
	 * Resource bindings
	 */
	FMeshDrawShaderBindings ShaderBindings;
	FVertexInputStreamArray VertexStreams;
	FRHIIndexBuffer* IndexBuffer;

	/**
	 * PSO
	 */
	FGraphicsMinimalPipelineStateId CachedPipelineId;

	/**
	 * Draw command parameters
	 */
	uint32 FirstIndex;
	uint32 NumPrimitives;
	uint32 NumInstances;

	union
	{
		struct 
		{
			uint32 BaseVertexIndex;
			uint32 NumVertices;
		} VertexParams;
		
		struct  
		{
			FRHIVertexBuffer* Buffer;
			uint32 Offset;
		} IndirectArgs;
	};

	int8 PrimitiveIdStreamIndex;

	/** Non-pipeline state */
	uint8 StencilRef;

	FMeshDrawCommand() {};
	FMeshDrawCommand(FMeshDrawCommand&& Other) = default;
	FMeshDrawCommand(const FMeshDrawCommand& Other) = default;
	FMeshDrawCommand& operator=(const FMeshDrawCommand& Other) = default;
	FMeshDrawCommand& operator=(FMeshDrawCommand&& Other) = default; 

	bool MatchesForDynamicInstancing(const FMeshDrawCommand& Rhs) const
	{
		return CachedPipelineId == Rhs.CachedPipelineId
			&& StencilRef == Rhs.StencilRef
			&& ShaderBindings.MatchesForDynamicInstancing(Rhs.ShaderBindings)
			&& VertexStreams == Rhs.VertexStreams
			&& PrimitiveIdStreamIndex == Rhs.PrimitiveIdStreamIndex
			&& IndexBuffer == Rhs.IndexBuffer
			&& FirstIndex == Rhs.FirstIndex
			&& NumPrimitives == Rhs.NumPrimitives
			&& NumInstances == Rhs.NumInstances
			&& ((NumPrimitives > 0 && VertexParams.BaseVertexIndex == Rhs.VertexParams.BaseVertexIndex && VertexParams.NumVertices == Rhs.VertexParams.NumVertices)
				|| (NumPrimitives == 0 && IndirectArgs.Buffer == Rhs.IndirectArgs.Buffer && IndirectArgs.Offset == Rhs.IndirectArgs.Offset));
	}

	uint32 GetDynamicInstancingHash() const
	{
		//add and initialize any leftover padding within the struct to avoid unstable keys
		struct FHashKey
		{
			uint32 IndexBuffer;
			uint32 VertexBuffers = 0;
		    uint32 VertexStreams = 0;
			uint32 PipelineId;
			uint32 DynamicInstancingHash;
			uint32 FirstIndex;
			uint32 NumPrimitives;
			uint32 NumInstances;
			uint32 IndirectArgsBufferOrBaseVertexIndex;
			uint32 NumVertices;
			uint32 StencilRefAndPrimitiveIdStreamIndex;

			static inline uint32 PointerHash(const void* Key)
			{
#if PLATFORM_64BITS
				// Ignoring the lower 4 bits since they are likely zero anyway.
				// Higher bits are more significant in 64 bit builds.
				return reinterpret_cast<UPTRINT>(Key) >> 4;
#else
				return reinterpret_cast<UPTRINT>(Key);
#endif
			};

			static inline uint32 HashCombine(uint32 A, uint32 B)
			{
				return A ^ (B + 0x9e3779b9 + (A << 6) + (A >> 2));
			}
		} HashKey;

		HashKey.PipelineId = CachedPipelineId.GetId();
		HashKey.StencilRefAndPrimitiveIdStreamIndex = StencilRef | (PrimitiveIdStreamIndex << 8);
		HashKey.DynamicInstancingHash = ShaderBindings.GetDynamicInstancingHash();

		for (int index = 0; index < VertexStreams.Num(); index++)
		{
			const FVertexInputStream& VertexInputStream = VertexStreams[index];
			const uint32 StreamIndex = VertexInputStream.StreamIndex;
			const uint32 Offset = VertexInputStream.Offset;

			uint32 Packed = (StreamIndex << 28) | Offset;
			HashKey.VertexStreams = FHashKey::HashCombine(HashKey.VertexStreams, Packed);
			HashKey.VertexBuffers = FHashKey::HashCombine(HashKey.VertexBuffers, FHashKey::PointerHash(VertexInputStream.VertexBuffer));
		}

		HashKey.IndexBuffer = FHashKey::PointerHash(IndexBuffer);
		HashKey.FirstIndex = FirstIndex;
		HashKey.NumPrimitives = NumPrimitives;
		HashKey.NumInstances = NumInstances;

		if (NumPrimitives > 0)
		{
			HashKey.IndirectArgsBufferOrBaseVertexIndex = VertexParams.BaseVertexIndex;
			HashKey.NumVertices = VertexParams.NumVertices;
		}
		else
		{
			HashKey.IndirectArgsBufferOrBaseVertexIndex = FHashKey::PointerHash(IndirectArgs.Buffer);
			HashKey.NumVertices = IndirectArgs.Offset;
		}		

		return uint32(CityHash64((char*)&HashKey, sizeof(FHashKey)));
	}

	/** Sets shaders on the mesh draw command and allocates room for the shader bindings. */
	RENDERER_API void SetShaders(FRHIVertexDeclaration* VertexDeclaration, const FMeshProcessorShaders& Shaders, FGraphicsMinimalPipelineStateInitializer& PipelineState);

	inline void SetStencilRef(uint32 InStencilRef)
	{
		StencilRef = InStencilRef;
		// Verify no overflow
		checkSlow((uint32)StencilRef == InStencilRef);
	}

	/** Called when the mesh draw command is complete. */
	RENDERER_API void SetDrawParametersAndFinalize(
		const FMeshBatch& MeshBatch, 
		int32 BatchElementIndex,
		FGraphicsMinimalPipelineStateId PipelineId,
		const FMeshProcessorShaders* ShadersForDebugging);

	void Finalize(FGraphicsMinimalPipelineStateId PipelineId, const FMeshProcessorShaders* ShadersForDebugging)
	{
		CachedPipelineId = PipelineId;
		ShaderBindings.Finalize(ShadersForDebugging);	
	}

	/** Submits commands to the RHI Commandlist to draw the MeshDrawCommand. */
	static void SubmitDraw(
		const FMeshDrawCommand& RESTRICT MeshDrawCommand, 
		const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
		FRHIVertexBuffer* ScenePrimitiveIdsBuffer,
		int32 PrimitiveIdOffset,
		uint32 InstanceFactor,
		FRHICommandList& CommandList, 
		class FMeshDrawCommandStateCache& RESTRICT StateCache);

	FORCENOINLINE friend uint32 GetTypeHash( const FMeshDrawCommand& Other )
	{
		return Other.CachedPipelineId.GetId();
	}
#if MESH_DRAW_COMMAND_DEBUG_DATA
	RENDERER_API void SetDebugData(const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FMaterial* Material, const FMaterialRenderProxy* MaterialRenderProxy, const FMeshProcessorShaders& UntypedShaders, const FVertexFactory* VertexFactory);
#else
	void SetDebugData(const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FMaterial* Material, const FMaterialRenderProxy* MaterialRenderProxy, const FMeshProcessorShaders& UntypedShaders, const FVertexFactory* VertexFactory){}
#endif

	SIZE_T GetAllocatedSize() const
	{
		return ShaderBindings.GetAllocatedSize() + VertexStreams.GetAllocatedSize();
	}

	SIZE_T GetDebugDataSize() const
	{
#if MESH_DRAW_COMMAND_DEBUG_DATA
		return sizeof(DebugData);
#endif
		return 0;
	}

#if MESH_DRAW_COMMAND_DEBUG_DATA
	void ClearDebugPrimitiveSceneProxy() const
	{
		DebugData.PrimitiveSceneProxyIfNotUsingStateBuckets = nullptr;
	}
private:
	mutable FMeshDrawCommandDebugData DebugData;
#endif
};

/** FVisibleMeshDrawCommand sort key. */
class RENDERER_API FMeshDrawCommandSortKey
{
public:
	union 
	{
		uint64 PackedData;

		struct
		{
			uint64 VertexShaderHash		: 16; // Order by vertex shader's hash.
			uint64 PixelShaderHash		: 32; // Order by pixel shader's hash.
			uint64 Masked				: 16; // First order by masked.
		} BasePass;

		struct
		{
			uint64 MeshIdInPrimitive	: 16; // Order meshes belonging to the same primitive by a stable id.
			uint64 Distance				: 32; // Order by distance.
			uint64 Priority				: 16; // First order by priority.
		} Translucent;

		struct 
		{
			uint64 VertexShaderHash : 32;	// Order by vertex shader's hash.
			uint64 PixelShaderHash : 32;	// First order by pixel shader's hash.
		} Generic;
	};

	FORCEINLINE bool operator!=(FMeshDrawCommandSortKey B) const
	{
		return PackedData != B.PackedData;
	}

	FORCEINLINE bool operator<(FMeshDrawCommandSortKey B) const
	{
		return PackedData < B.PackedData;
	}

	static const FMeshDrawCommandSortKey Default;
};

/** Interface for the different types of draw lists. */
class FMeshPassDrawListContext
{
public:

	virtual ~FMeshPassDrawListContext() {}

	virtual FMeshDrawCommand& AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements) = 0;

	virtual void FinalizeCommand(
		const FMeshBatch& MeshBatch, 
		int32 BatchElementIndex,
		int32 DrawPrimitiveId,
		int32 ScenePrimitiveId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		FMeshDrawCommandSortKey SortKey,
		const FGraphicsMinimalPipelineStateInitializer& PipelineState,
		const FMeshProcessorShaders* ShadersForDebugging,
		FMeshDrawCommand& MeshDrawCommand) = 0;
};

/** Storage for Mesh Draw Commands built every frame. */
class FDynamicMeshDrawCommandStorage
{
public:
	// Using TChunkedArray to support growing without moving FMeshDrawCommand, since FVisibleMeshDrawCommand stores a pointer to these
	TChunkedArray<FMeshDrawCommand> MeshDrawCommands;
};

/** 
 * Stores information about a mesh draw command that has been determined to be visible, for further visibility processing. 
 * This class should only store data needed by InitViews operations (visibility, sorting) and not data needed for draw submission, which belongs in FMeshDrawCommand.
 */
class FVisibleMeshDrawCommand
{
public:

	// Note: no ctor as TChunkedArray::CopyToLinearArray requires POD types

	FORCEINLINE_DEBUGGABLE void Setup(
		const FMeshDrawCommand* InMeshDrawCommand,
		int32 InDrawPrimitiveId,
		int32 InScenePrimitiveId,
		int32 InStateBucketId,
		ERasterizerFillMode InMeshFillMode,
		ERasterizerCullMode InMeshCullMode,
		FMeshDrawCommandSortKey InSortKey)
	{
		MeshDrawCommand = InMeshDrawCommand;
		DrawPrimitiveId = InDrawPrimitiveId;
		ScenePrimitiveId = InScenePrimitiveId;
		PrimitiveIdBufferOffset = -1;
		StateBucketId = InStateBucketId;
		MeshFillMode = InMeshFillMode;
		MeshCullMode = InMeshCullMode;
		SortKey = InSortKey;
	}

	// Mesh Draw Command stored separately to avoid fetching its data during sorting
	const FMeshDrawCommand* MeshDrawCommand;

	// Sort key for non state based sorting (e.g. sort translucent draws by depth).
	FMeshDrawCommandSortKey SortKey;

	// Draw PrimitiveId this draw command is associated with - used by the shader to fetch primitive data from the PrimitiveSceneData SRV.
	// If it's < Scene->Primitives.Num() then it's a valid Scene PrimitiveIndex and can be used to backtrack to the FPrimitiveSceneInfo.
	int32 DrawPrimitiveId;

	// Scene PrimitiveId that generated this draw command, or -1 if no FPrimitiveSceneInfo. Can be used to backtrack to the FPrimitiveSceneInfo.
	int32 ScenePrimitiveId;

	// Offset into the buffer of PrimitiveIds built for this pass, in int32's.
	int32 PrimitiveIdBufferOffset;

	// Dynamic instancing state bucket ID.  
	// Any commands with the same StateBucketId can be merged into one draw call with instancing.
	// A value of -1 means the draw is not in any state bucket and should be sorted by other factors instead.
	int32 StateBucketId;

	// Needed for view overrides
	ERasterizerFillMode MeshFillMode : ERasterizerFillMode_NumBits + 1;
	ERasterizerCullMode MeshCullMode : ERasterizerCullMode_NumBits + 1;
};

template <>
struct TUseBitwiseSwap<FVisibleMeshDrawCommand>
{
	// Prevent Memcpy call overhead during FVisibleMeshDrawCommand sorting
	enum { Value = false };
};

typedef TArray<FVisibleMeshDrawCommand, SceneRenderingAllocator> FMeshCommandOneFrameArray;
typedef TMap<int32, FUniformBufferRHIRef, SceneRenderingSetAllocator> FTranslucentSelfShadowUniformBufferMap;

/** Context used when building FMeshDrawCommands for one frame only. */
class FDynamicPassMeshDrawListContext : public FMeshPassDrawListContext
{
public:
	FDynamicPassMeshDrawListContext
	(
		FDynamicMeshDrawCommandStorage& InDrawListStorage, 
		FMeshCommandOneFrameArray& InDrawList,
		FGraphicsMinimalPipelineStateSet& InPipelineStateSet,
		bool& InNeedsShaderInitialisation
	) :
		DrawListStorage(InDrawListStorage),
		DrawList(InDrawList),
		GraphicsMinimalPipelineStateSet(InPipelineStateSet),
		NeedsShaderInitialisation(InNeedsShaderInitialisation)
	{}

	virtual FMeshDrawCommand& AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements) override final
	{
		const int32 Index = DrawListStorage.MeshDrawCommands.AddElement(Initializer);
		FMeshDrawCommand& NewCommand = DrawListStorage.MeshDrawCommands[Index];
		return NewCommand;
	}

	virtual void FinalizeCommand(
		const FMeshBatch& MeshBatch, 
		int32 BatchElementIndex,
		int32 DrawPrimitiveId,
		int32 ScenePrimitiveId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		FMeshDrawCommandSortKey SortKey,
		const FGraphicsMinimalPipelineStateInitializer& PipelineState,
		const FMeshProcessorShaders* ShadersForDebugging,
		FMeshDrawCommand& MeshDrawCommand) override final
	{
		FGraphicsMinimalPipelineStateId PipelineId = FGraphicsMinimalPipelineStateId::GetPipelineStateId(PipelineState, GraphicsMinimalPipelineStateSet, NeedsShaderInitialisation);

		MeshDrawCommand.SetDrawParametersAndFinalize(MeshBatch, BatchElementIndex, PipelineId, ShadersForDebugging);

		FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;
		//@todo MeshCommandPipeline - assign usable state ID for dynamic path draws
		// Currently dynamic path draws will not get dynamic instancing, but they will be roughly sorted by state
		NewVisibleMeshDrawCommand.Setup(&MeshDrawCommand, DrawPrimitiveId, ScenePrimitiveId, -1, MeshFillMode, MeshCullMode, SortKey);
		DrawList.Add(NewVisibleMeshDrawCommand);
	}

private:
	FDynamicMeshDrawCommandStorage& DrawListStorage;
	FMeshCommandOneFrameArray& DrawList;
	FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet;
	bool& NeedsShaderInitialisation;
};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack (push,4)
#endif

/** 
 * Stores information about a mesh draw command which is cached in the scene. 
 * This is stored separately from the cached FMeshDrawCommand so that InitViews does not have to load the FMeshDrawCommand into cache.
 */
class FCachedMeshDrawCommandInfo
{
public:
	FCachedMeshDrawCommandInfo() : FCachedMeshDrawCommandInfo(EMeshPass::Num)
	{}

	explicit FCachedMeshDrawCommandInfo(EMeshPass::Type InMeshPass) :
		SortKey(FMeshDrawCommandSortKey::Default),
		CommandIndex(INDEX_NONE),
		StateBucketId(INDEX_NONE),
		MeshPass(InMeshPass),
		MeshFillMode(ERasterizerFillMode_Num),
		MeshCullMode(ERasterizerCullMode_Num)
	{}

	FMeshDrawCommandSortKey SortKey;

	// Stores the index into FScene::CachedDrawLists of the corresponding FMeshDrawCommand, or -1 if not stored there
	int32 CommandIndex;

	// Stores the index into FScene::CachedMeshDrawCommandStateBuckets of the corresponding FMeshDrawCommand, or -1 if not stored there
	int32 StateBucketId;

	// Needed for easier debugging and faster removal of cached mesh draw commands.
	EMeshPass::Type MeshPass : EMeshPass::NumBits + 1;

	// Needed for view overrides
	ERasterizerFillMode MeshFillMode : ERasterizerFillMode_NumBits + 1;
	ERasterizerCullMode MeshCullMode : ERasterizerCullMode_NumBits + 1;
};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack (pop)
#endif

class FCachedPassMeshDrawList
{
public:

	FCachedPassMeshDrawList() :
		LowestFreeIndexSearchStart(0)
	{}

	/** Indices held by FStaticMeshBatch::CachedMeshDrawCommands must be stable */
	TSparseArray<FMeshDrawCommand> MeshDrawCommands;
	int32 LowestFreeIndexSearchStart;
};

struct FMeshDrawCommandCount 
{
	uint32 Num = 0;
};

struct MeshDrawCommandKeyFuncs : TDefaultMapHashableKeyFuncs<FMeshDrawCommand, FMeshDrawCommandCount, false>
{
	/**
	 * @return True if the keys match.
	 */
	static inline bool Matches(KeyInitType A, KeyInitType B)
	{
		return A.MatchesForDynamicInstancing(B);
	}

	/** Calculates a hash index for a key. */
	static inline uint32 GetKeyHash(KeyInitType Key)
	{
		return Key.GetDynamicInstancingHash();
	}
};

using FDrawCommandIndices = TArray<int32, TInlineAllocator<5>>;
using FStateBucketMap = Experimental::TRobinHoodHashMap<FMeshDrawCommand, FMeshDrawCommandCount, MeshDrawCommandKeyFuncs>;

class FCachedPassMeshDrawListContext : public FMeshPassDrawListContext
{
public:
	FCachedPassMeshDrawListContext(FCachedMeshDrawCommandInfo& InCommandInfo, FCriticalSection& InCachedMeshDrawCommandLock, FCachedPassMeshDrawList& InCachedDrawLists, FStateBucketMap& InCachedMeshDrawCommandStateBuckets, const FScene& InScene);

	virtual FMeshDrawCommand& AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements) override final;

	virtual void FinalizeCommand(
		const FMeshBatch& MeshBatch, 
		int32 BatchElementIndex,
		int32 DrawPrimitiveId,
		int32 ScenePrimitiveId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		FMeshDrawCommandSortKey SortKey,
		const FGraphicsMinimalPipelineStateInitializer& PipelineState,
		const FMeshProcessorShaders* ShadersForDebugging,
		FMeshDrawCommand& MeshDrawCommand) override final;

private:
	FMeshDrawCommand MeshDrawCommandForStateBucketing;
	FCachedMeshDrawCommandInfo& CommandInfo;
	FCriticalSection& CachedMeshDrawCommandLock;
	FCachedPassMeshDrawList& CachedDrawLists;
	FStateBucketMap& CachedMeshDrawCommandStateBuckets;
	const FScene& Scene;
};

template<typename VertexType, typename HullType, typename DomainType, typename PixelType, typename GeometryType = FMeshMaterialShader, typename RayHitGroupType = FMeshMaterialShader, typename ComputeType = FMeshMaterialShader>
struct TMeshProcessorShaders
{
	TShaderRef<VertexType> VertexShader;
	TShaderRef<HullType> HullShader;
	TShaderRef<DomainType> DomainShader;
	TShaderRef<PixelType> PixelShader;
	TShaderRef<GeometryType> GeometryShader;
	TShaderRef<ComputeType> ComputeShader;
#if RHI_RAYTRACING
	TShaderRef<RayHitGroupType> RayHitGroupShader;
#endif

	TMeshProcessorShaders() = default;

	FMeshProcessorShaders GetUntypedShaders()
	{
		FMeshProcessorShaders Shaders;
		Shaders.VertexShader = VertexShader;
		Shaders.HullShader = HullShader;
		Shaders.DomainShader = DomainShader;
		Shaders.PixelShader = PixelShader;
		Shaders.GeometryShader = GeometryShader;
		Shaders.ComputeShader = ComputeShader;
#if RHI_RAYTRACING
		Shaders.RayHitGroupShader = RayHitGroupShader;
#endif
		return Shaders;
	}
};

enum class EMeshPassFeatures
{
	Default = 0,
	PositionOnly = 1 << 0,
	PositionAndNormalOnly = 1 << 1,
};
ENUM_CLASS_FLAGS(EMeshPassFeatures);

/**
 * A set of render state overrides passed into a Mesh Pass Processor, so it can be configured from the outside.
 */
struct FMeshPassProcessorRenderState
{
	FMeshPassProcessorRenderState(const FSceneView& SceneView, FRHIUniformBuffer* InPassUniformBuffer = nullptr) :
		  BlendState(nullptr)
		, DepthStencilState(nullptr)
		, DepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead)
		, ViewUniformBuffer(SceneView.ViewUniformBuffer)
		, InstancedViewUniformBuffer()
		, ReflectionCaptureUniformBuffer()
		, PassUniformBuffer(InPassUniformBuffer)
		, StencilRef(0)
	{
	}

	FMeshPassProcessorRenderState(const TUniformBufferRef<FViewUniformShaderParameters>& InViewUniformBuffer, FRHIUniformBuffer* InPassUniformBuffer = nullptr) :
		  BlendState(nullptr)
		, DepthStencilState(nullptr)
		, DepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead)
		, ViewUniformBuffer(InViewUniformBuffer)
		, InstancedViewUniformBuffer()
		, ReflectionCaptureUniformBuffer()
		, PassUniformBuffer(InPassUniformBuffer)
		, StencilRef(0)
	{
	}

	FMeshPassProcessorRenderState() :
		BlendState(nullptr)
		, DepthStencilState(nullptr)
		, ViewUniformBuffer()
		, InstancedViewUniformBuffer()
		, ReflectionCaptureUniformBuffer()
		, PassUniformBuffer(nullptr)
		, StencilRef(0)
	{
	}

	FORCEINLINE_DEBUGGABLE FMeshPassProcessorRenderState(const FMeshPassProcessorRenderState& DrawRenderState) :
		  BlendState(DrawRenderState.BlendState)
		, DepthStencilState(DrawRenderState.DepthStencilState)
		, DepthStencilAccess(DrawRenderState.DepthStencilAccess)
		, ViewUniformBuffer(DrawRenderState.ViewUniformBuffer)
		, InstancedViewUniformBuffer(DrawRenderState.InstancedViewUniformBuffer)
		, ReflectionCaptureUniformBuffer(DrawRenderState.ReflectionCaptureUniformBuffer)
		, PassUniformBuffer(DrawRenderState.PassUniformBuffer)
		, StencilRef(DrawRenderState.StencilRef)
	{
	}

	~FMeshPassProcessorRenderState()
	{
	}

public:
	FORCEINLINE_DEBUGGABLE void SetBlendState(FRHIBlendState* InBlendState)
	{
		BlendState = InBlendState;
	}

	FORCEINLINE_DEBUGGABLE FRHIBlendState* GetBlendState() const
	{
		return BlendState;
	}

	FORCEINLINE_DEBUGGABLE void SetDepthStencilState(FRHIDepthStencilState* InDepthStencilState)
	{
		DepthStencilState = InDepthStencilState;
		StencilRef = 0;
	}

	FORCEINLINE_DEBUGGABLE void SetStencilRef(uint32 InStencilRef)
		{
		StencilRef = InStencilRef;
	}

	FORCEINLINE_DEBUGGABLE FRHIDepthStencilState* GetDepthStencilState() const
	{
		return DepthStencilState;
	}

	FORCEINLINE_DEBUGGABLE void SetDepthStencilAccess(FExclusiveDepthStencil::Type InDepthStencilAccess)
	{
		DepthStencilAccess = InDepthStencilAccess;
	}

	FORCEINLINE_DEBUGGABLE FExclusiveDepthStencil::Type GetDepthStencilAccess() const
	{
		return DepthStencilAccess;
	}

	FORCEINLINE_DEBUGGABLE void SetViewUniformBuffer(const TUniformBufferRef<FViewUniformShaderParameters>& InViewUniformBuffer)
	{
		ViewUniformBuffer = InViewUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE const FRHIUniformBuffer* GetViewUniformBuffer() const
	{
		return ViewUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE void SetInstancedViewUniformBuffer(const TUniformBufferRef<FInstancedViewUniformShaderParameters>& InViewUniformBuffer)
	{
		InstancedViewUniformBuffer = InViewUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE const FRHIUniformBuffer* GetInstancedViewUniformBuffer() const
	{
		return InstancedViewUniformBuffer != nullptr ? InstancedViewUniformBuffer : ViewUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE void SetReflectionCaptureUniformBuffer(FRHIUniformBuffer* InUniformBuffer)
	{
		ReflectionCaptureUniformBuffer = InUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE const FRHIUniformBuffer* GetReflectionCaptureUniformBuffer() const
	{
		return ReflectionCaptureUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE void SetPassUniformBuffer(const FUniformBufferRHIRef& InPassUniformBuffer)
	{
		PassUniformBuffer = InPassUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE FRHIUniformBuffer* GetPassUniformBuffer() const
	{
		return PassUniformBuffer;
	}

	FORCEINLINE_DEBUGGABLE uint32 GetStencilRef() const
	{
		return StencilRef;
	}

	FORCEINLINE_DEBUGGABLE void ApplyToPSO(FGraphicsPipelineStateInitializer& GraphicsPSOInit) const
	{
		GraphicsPSOInit.BlendState = BlendState;
		GraphicsPSOInit.DepthStencilState = DepthStencilState;
	}

private:
	FRHIBlendState*					BlendState;
	FRHIDepthStencilState*			DepthStencilState;
	FExclusiveDepthStencil::Type	DepthStencilAccess;

	FRHIUniformBuffer*				ViewUniformBuffer;
	FRHIUniformBuffer*				InstancedViewUniformBuffer;

	/** Will be bound as reflection capture uniform buffer in case where scene is not available, typically set to dummy/empty buffer to avoid null binding */
	FRHIUniformBuffer*				ReflectionCaptureUniformBuffer;

	FRHIUniformBuffer*				PassUniformBuffer;
	uint32							StencilRef;
};

enum class EDrawingPolicyOverrideFlags
{
	None = 0,
	TwoSided = 1 << 0,
	DitheredLODTransition = 1 << 1,
	Wireframe = 1 << 2,
	ReverseCullMode = 1 << 3,
};
ENUM_CLASS_FLAGS(EDrawingPolicyOverrideFlags);

/** 
 * Base class of mesh processors, whose job is to transform FMeshBatch draw descriptions received from scene proxy implementations into FMeshDrawCommands ready for the RHI command list
 */
class FMeshPassProcessor
{
public:

	const FScene* RESTRICT Scene;
	ERHIFeatureLevel::Type FeatureLevel;
	const FSceneView* ViewIfDynamicMeshCommand;
	FMeshPassDrawListContext* DrawListContext;

	RENDERER_API FMeshPassProcessor(const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);

	virtual ~FMeshPassProcessor() {}

	void SetDrawListContext(FMeshPassDrawListContext* InDrawListContext)
	{
		DrawListContext = InDrawListContext;
	}

	// FMeshPassProcessor interface
	// Add a FMeshBatch to the pass
	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) = 0;

	static FORCEINLINE_DEBUGGABLE ERasterizerCullMode InverseCullMode(ERasterizerCullMode CullMode)
	{
		return CullMode == CM_None ? CM_None : (CullMode == CM_CCW ? CM_CW : CM_CCW);
	}

	struct FMeshDrawingPolicyOverrideSettings
	{
		EDrawingPolicyOverrideFlags	MeshOverrideFlags = EDrawingPolicyOverrideFlags::None;
		EPrimitiveType				MeshPrimitiveType = PT_TriangleList;
	};

	RENDERER_API static FMeshDrawingPolicyOverrideSettings ComputeMeshOverrideSettings(const FMeshBatch& Mesh);
	RENDERER_API static ERasterizerFillMode ComputeMeshFillMode(const FMeshBatch& Mesh, const FMaterial& InMaterialResource, const FMeshDrawingPolicyOverrideSettings& InOverrideSettings);
	RENDERER_API static ERasterizerCullMode ComputeMeshCullMode(const FMeshBatch& Mesh, const FMaterial& InMaterialResource, const FMeshDrawingPolicyOverrideSettings& InOverrideSettings);

	template<typename PassShadersType, typename ShaderElementDataType>
	void BuildMeshDrawCommands(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const FMeshPassProcessorRenderState& RESTRICT DrawRenderState,
		PassShadersType PassShaders,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		FMeshDrawCommandSortKey SortKey,
		EMeshPassFeatures MeshPassFeatures,
		const ShaderElementDataType& ShaderElementData);

protected:
	RENDERER_API void GetDrawCommandPrimitiveId(
		const FPrimitiveSceneInfo* RESTRICT PrimitiveSceneInfo,
		const FMeshBatchElement& BatchElement,
		int32& DrawPrimitiveId,
		int32& ScenePrimitiveId) const;
};

typedef FMeshPassProcessor* (*PassProcessorCreateFunction)(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);

enum class EMeshPassFlags
{
	None = 0,
	CachedMeshCommands = 1 << 0,
	MainView = 1 << 1
};
ENUM_CLASS_FLAGS(EMeshPassFlags);

class FPassProcessorManager
{
public:
	static PassProcessorCreateFunction GetCreateFunction(EShadingPath ShadingPath, EMeshPass::Type PassType)
	{
		check(ShadingPath < EShadingPath::Num && PassType < EMeshPass::Num);
		uint32 ShadingPathIdx = (uint32)ShadingPath;
		checkf(JumpTable[ShadingPathIdx][PassType], TEXT("Pass type %u create function was never registered for shading path %u.  Use a FRegisterPassProcessorCreateFunction to register a create function for this enum value."), (uint32)PassType, ShadingPathIdx);
		return JumpTable[ShadingPathIdx][PassType];
	}

	static EMeshPassFlags GetPassFlags(EShadingPath ShadingPath, EMeshPass::Type PassType)
	{
		check(ShadingPath < EShadingPath::Num && PassType < EMeshPass::Num);
		uint32 ShadingPathIdx = (uint32)ShadingPath;
		return Flags[ShadingPathIdx][PassType];
	}

private:
	RENDERER_API static PassProcessorCreateFunction JumpTable[(uint32)EShadingPath::Num][EMeshPass::Num];
	RENDERER_API static EMeshPassFlags Flags[(uint32)EShadingPath::Num][EMeshPass::Num];
	friend class FRegisterPassProcessorCreateFunction;
};

class FRegisterPassProcessorCreateFunction
{
public:
	FRegisterPassProcessorCreateFunction(PassProcessorCreateFunction CreateFunction, EShadingPath InShadingPath, EMeshPass::Type InPassType, EMeshPassFlags PassFlags) 
		: ShadingPath(InShadingPath)
		, PassType(InPassType)
	{
		uint32 ShadingPathIdx = (uint32)ShadingPath;
		FPassProcessorManager::JumpTable[ShadingPathIdx][PassType] = CreateFunction;
		FPassProcessorManager::Flags[ShadingPathIdx][PassType] = PassFlags;
	}

	~FRegisterPassProcessorCreateFunction()
	{
		uint32 ShadingPathIdx = (uint32)ShadingPath;
		FPassProcessorManager::JumpTable[ShadingPathIdx][PassType] = nullptr;
		FPassProcessorManager::Flags[ShadingPathIdx][PassType] = EMeshPassFlags::None;
	}

private:
	EShadingPath ShadingPath;
	EMeshPass::Type PassType;
};

extern void SubmitMeshDrawCommands(
	const FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet, 
	FRHIVertexBuffer* PrimitiveIdsBuffer,
	int32 BasePrimitiveIdsOffset,
	bool bDynamicInstancing,
	uint32 InstanceFactor,
	FRHICommandList& RHICmdList);

extern void SubmitMeshDrawCommandsRange(
	const FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	FRHIVertexBuffer* PrimitiveIdsBuffer,
	int32 BasePrimitiveIdsOffset,
	bool bDynamicInstancing,
	int32 StartIndex,
	int32 NumMeshDrawCommands,
	uint32 InstanceFactor,
	FRHICommandList& RHICmdList);

extern void ApplyViewOverridesToMeshDrawCommands(
	const FSceneView& View,
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& DynamicMeshDrawCommandStorage,
	FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	bool& NeedsShaderInitialisation);

RENDERER_API extern void DrawDynamicMeshPassPrivate(
	const FSceneView& View,
	FRHICommandList& RHICmdList,
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& DynamicMeshDrawCommandStorage,
	FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	bool& InNeedsShaderInitialisation,
	uint32 InstanceFactor);

RENDERER_API extern FMeshDrawCommandSortKey CalculateMeshStaticSortKey(const FMeshMaterialShader* VertexShader, const FMeshMaterialShader* PixelShader);

inline FMeshDrawCommandSortKey CalculateMeshStaticSortKey(const TShaderRef<FMeshMaterialShader>& VertexShader, const TShaderRef<FMeshMaterialShader>& PixelShader)
{
	return CalculateMeshStaticSortKey(VertexShader.GetShader(), PixelShader.GetShader());
}

class FRayTracingMeshCommand
{
public:
	FMeshDrawShaderBindings ShaderBindings;

	FRHIRayTracingShader* MaterialShader = nullptr;
	uint32 MaterialShaderIndex = UINT_MAX;

	uint32 GeometrySegmentIndex = ~0u;
	uint8 InstanceMask = 0xFF;

	bool bCastRayTracedShadows = true;
	bool bOpaque = true;
	bool bDecal = false;
	bool bTwoSided = false;

	/** Sets ray hit group shaders on the mesh command and allocates room for the shader bindings. */
	RENDERER_API void SetShaders(const FMeshProcessorShaders& Shaders);
};

class FVisibleRayTracingMeshCommand
{
public:
	const FRayTracingMeshCommand* RayTracingMeshCommand;

	uint32 InstanceIndex;
};

template <>
struct TUseBitwiseSwap<FVisibleRayTracingMeshCommand>
{
	// Prevent Memcpy call overhead during FVisibleRayTracingMeshCommand sorting
	enum { Value = false };
};

typedef TArray<FVisibleRayTracingMeshCommand> FRayTracingMeshCommandOneFrameArray;

class FRayTracingMeshCommandContext
{
public:

	virtual ~FRayTracingMeshCommandContext() {}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) = 0;

	virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) = 0;
};

struct FCachedRayTracingMeshCommandStorage
{
	TSparseArray<FRayTracingMeshCommand> RayTracingMeshCommands;
};

struct FDynamicRayTracingMeshCommandStorage
{
	TChunkedArray<FRayTracingMeshCommand> RayTracingMeshCommands;
};

class FCachedRayTracingMeshCommandContext : public FRayTracingMeshCommandContext
{
public:
	FCachedRayTracingMeshCommandContext(FCachedRayTracingMeshCommandStorage& InDrawListStorage) : DrawListStorage(InDrawListStorage) {}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) override final
	{
		CommandIndex = DrawListStorage.RayTracingMeshCommands.Add(Initializer);
		return DrawListStorage.RayTracingMeshCommands[CommandIndex];
	}

	virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) override final {}

	int32 CommandIndex = -1;

private:
	FCachedRayTracingMeshCommandStorage& DrawListStorage;
};

class FDynamicRayTracingMeshCommandContext : public FRayTracingMeshCommandContext
{
public:
	FDynamicRayTracingMeshCommandContext
	(
		FDynamicRayTracingMeshCommandStorage& InDynamicCommandStorage,
		FRayTracingMeshCommandOneFrameArray& InVisibleCommands,
		uint32 InGeometrySegmentIndex = ~0u,
		uint32 InRayTracingInstanceIndex = ~0u
	) :
		DynamicCommandStorage(InDynamicCommandStorage),
		VisibleCommands(InVisibleCommands),
		GeometrySegmentIndex(InGeometrySegmentIndex),
		RayTracingInstanceIndex(InRayTracingInstanceIndex)
	{}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) override final
	{
		const int32 Index = DynamicCommandStorage.RayTracingMeshCommands.AddElement(Initializer);
		FRayTracingMeshCommand& NewCommand = DynamicCommandStorage.RayTracingMeshCommands[Index];
		NewCommand.GeometrySegmentIndex = GeometrySegmentIndex;
		return NewCommand;
	}

	virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) override final
	{
		FVisibleRayTracingMeshCommand NewVisibleMeshCommand;
		NewVisibleMeshCommand.RayTracingMeshCommand = &RayTracingMeshCommand;
		NewVisibleMeshCommand.InstanceIndex = RayTracingInstanceIndex;
		VisibleCommands.Add(NewVisibleMeshCommand);
	}

private:
	FDynamicRayTracingMeshCommandStorage& DynamicCommandStorage;
	FRayTracingMeshCommandOneFrameArray& VisibleCommands;
	uint32 GeometrySegmentIndex;
	uint32 RayTracingInstanceIndex;
};
