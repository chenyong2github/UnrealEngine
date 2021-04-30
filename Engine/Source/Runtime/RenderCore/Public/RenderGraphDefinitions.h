// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RendererInterface.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

/** DEFINES */

/** Whether render graph debugging is enabled. */
#define RDG_ENABLE_DEBUG (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

/** Performs the operation if RDG_ENABLE_DEBUG is enabled. Useful for one-line checks without explicitly wrapping in #if. */
#if RDG_ENABLE_DEBUG
	#define IF_RDG_ENABLE_DEBUG(Op) Op
#else
	#define IF_RDG_ENABLE_DEBUG(Op)
#endif

/** Whether render graph debugging is enabled and we are compiling with the engine. */
#define RDG_ENABLE_DEBUG_WITH_ENGINE (RDG_ENABLE_DEBUG && WITH_ENGINE)

/** The type of GPU events the render graph system supports.
 *  RDG_EVENTS == 0 means there is no string processing at all.
 *  RDG_EVENTS == 1 means the format component of the event name is stored as a const TCHAR*.
 *  RDG_EVENTS == 2 means string formatting is evaluated and stored in an FString.
 */
#define RDG_EVENTS_NONE 0
#define RDG_EVENTS_STRING_REF 1
#define RDG_EVENTS_STRING_COPY 2

/** Whether render graph GPU events are enabled. */
#if WITH_PROFILEGPU
	#define RDG_EVENTS RDG_EVENTS_STRING_COPY
#else
	#define RDG_EVENTS RDG_EVENTS_NONE
#endif

#define RDG_GPU_SCOPES (RDG_EVENTS || HAS_GPU_STATS)

#if RDG_GPU_SCOPES
	#define IF_RDG_GPU_SCOPES(Op) Op
#else
	#define IF_RDG_GPU_SCOPES(Op)
#endif

#define RDG_CPU_SCOPES (CSV_PROFILER)

#if RDG_CPU_SCOPES
	#define IF_RDG_CPU_SCOPES(Op) Op
#else
	#define IF_RDG_CPU_SCOPES(Op)
#endif

/** ENUMS */

/** Flags to annotate a pass with when calling AddPass. */
enum class ERDGPassFlags : uint8
{
	/** Pass doesn't have any inputs or outputs tracked by the graph. This may only be used by the parameterless AddPass function. */
	None = 0,

	/** Pass uses rasterization on the graphics pipe. */
	Raster = 1 << 0,

	/** Pass uses compute on the graphics pipe. */
	Compute = 1 << 1,

	/** Pass uses compute on the async compute pipe. */
	AsyncCompute = 1 << 2,

	/** Pass uses copy commands on the graphics pipe. */
	Copy = 1 << 3,

	/** Pass (and its producers) will never be culled. Necessary if outputs cannot be tracked by the graph. */
	NeverCull = 1 << 4,

	/** Render pass begin / end is skipped and left to the user. Only valid when combined with 'Raster'. Disables render pass merging for the pass. */
	SkipRenderPass = 1 << 5,

	/** Pass accesses raw RHI resources which may be registered with the graph, but all resources are kept in their current state. This flag prevents
	 *  the graph from scheduling split barriers across the pass. Any splitting is deferred until after the pass executes. The resource may not change
	 *  state within the pass execution. Affects barrier performance. May not be combined with Async Compute.
	 */
	UntrackedAccess = 1 << 6,

	/** Pass uses copy commands but writes to a staging resource. */
	Readback = Copy | NeverCull,

	/** Mask of flags denoting the kinds of RHI commands submitted to a pass. */
	CommandMask = Raster | Compute | AsyncCompute | Copy,

	/** Mask of flags which can used by a pass flag scope. */
	ScopeMask = NeverCull | UntrackedAccess
};
ENUM_CLASS_FLAGS(ERDGPassFlags);

/** Flags to annotate a render graph buffer. */
enum class ERDGBufferFlags : uint8
{
	None = 0,

	/** Tag the buffer to survive through frame, that is important for multi GPU alternate frame rendering. */
	MultiFrame = 1 << 0
};
ENUM_CLASS_FLAGS(ERDGBufferFlags);

/** Flags to annotate a render graph texture. */
enum class ERDGTextureFlags : uint8
{
	None = 0,

	/** Tag the texture to survive through frame, that is important for multi GPU alternate frame rendering. */
	MultiFrame = 1 << 0,

	/** Prevents metadata decompression on this texture. */
	MaintainCompression = 1 << 1
};
ENUM_CLASS_FLAGS(ERDGTextureFlags);

/** Flags to annotate a view with when calling CreateUAV. */
enum class ERDGUnorderedAccessViewFlags : uint8
{
	None = 0,

	// The view will not perform UAV barriers between consecutive usage.
	SkipBarrier = 1 << 0
};
ENUM_CLASS_FLAGS(ERDGUnorderedAccessViewFlags);

/** The set of concrete parent resource types. */
enum class ERDGParentResourceType : uint8
{
	Texture,
	Buffer,
	MAX
};

/** The set of concrete view types. */
enum class ERDGViewType : uint8
{
	TextureUAV,
	TextureSRV,
	BufferUAV,
	BufferSRV,
	MAX
};

/** Returns the equivalent parent resource type for a view type. */
inline ERDGParentResourceType GetParentResourceType(ERDGViewType ViewType)
{
	switch (ViewType)
	{
	case ERDGViewType::TextureUAV:
	case ERDGViewType::TextureSRV:
		return ERDGParentResourceType::Texture;
	case ERDGViewType::BufferUAV:
	case ERDGViewType::BufferSRV:
		return ERDGParentResourceType::Buffer;
	default:
		checkNoEntry();
		return ERDGParentResourceType::MAX;
	}
}

/** Used to specify a texture metadata plane when creating a view. */
enum class ERDGTextureMetaDataAccess : uint8
{
	/** The primary plane is used with default compression behavior. */
	None = 0,

	/** The primary plane is used without decompressing it. */
	CompressedSurface,

	/** The depth plane is used with default compression behavior. */
	Depth,

	/** The stencil plane is used with default compression behavior. */
	Stencil,

	/** The HTile plane is used. */
	HTile,

	/** the FMask plane is used. */
	FMask,

	/** the CMask plane is used. */
	CMask
};

/** Returns the associated FRHITransitionInfo plane index. */
inline int32 GetResourceTransitionPlaneForMetadataAccess(ERDGTextureMetaDataAccess Metadata)
{
	switch (Metadata)
	{
	case ERDGTextureMetaDataAccess::CompressedSurface:
	case ERDGTextureMetaDataAccess::HTile:
	case ERDGTextureMetaDataAccess::Depth:
		return FRHITransitionInfo::kDepthPlaneSlice;
	case ERDGTextureMetaDataAccess::Stencil:
		return FRHITransitionInfo::kStencilPlaneSlice;
	default:
		return 0;
	}
}

/** Simple C++ object allocator which tracks and destructs objects allocated using the MemStack allocator. */
class FRDGAllocator final
{
public:
	FRDGAllocator()
		: MemStack(0)
	{
		TrackedAllocs.Reserve(16);
	}

	~FRDGAllocator()
	{
		checkSlow(MemStack.IsEmpty());
	}

	/** Allocates raw memory. */
	FORCEINLINE void* Alloc(uint32 SizeInBytes, uint32 AlignInBytes)
	{
		return MemStack.Alloc(SizeInBytes, AlignInBytes);
	}

	/** Allocates POD memory without destructor tracking. */
	template <typename PODType>
	FORCEINLINE PODType* AllocPOD()
	{
		return reinterpret_cast<PODType*>(Alloc(sizeof(PODType), alignof(PODType)));
	}

	/** Allocates a C++ object with destructor tracking. */
	template <typename ObjectType, typename... TArgs>
	FORCEINLINE ObjectType* AllocObject(TArgs&&... Args)
	{
		TTrackedAlloc<ObjectType>* TrackedAlloc = new(MemStack) TTrackedAlloc<ObjectType>(Forward<TArgs&&>(Args)...);
		check(TrackedAlloc);
		TrackedAllocs.Add(TrackedAlloc);
		return TrackedAlloc->Get();
	}

	/** Allocates a C++ object with no destructor tracking (dangerous!). */
	template <typename ObjectType, typename... TArgs>
	FORCEINLINE ObjectType* AllocNoDestruct(TArgs&&... Args)
	{
		return new (MemStack) ObjectType(Forward<TArgs&&>(Args)...);
	}

	/** Releases all allocations. */
	void ReleaseAll()
	{
		for (int32 Index = TrackedAllocs.Num() - 1; Index >= 0; --Index)
		{
			TrackedAllocs[Index]->~FTrackedAlloc();
		}
		TrackedAllocs.Empty();
		MemStack.Flush();
	}

	FORCEINLINE int32 GetByteCount() const
	{
		return MemStack.GetByteCount();
	}

private:
	class FTrackedAlloc
	{
	public:
		virtual ~FTrackedAlloc() = default;
	};

	template <typename ObjectType>
	class TTrackedAlloc : public FTrackedAlloc
	{
	public:
		template <typename... TArgs>
		FORCEINLINE TTrackedAlloc(TArgs&&... Args) : Object(Forward<TArgs&&>(Args)...) {}

		FORCEINLINE ObjectType* Get() { return &Object; }

	private:
		ObjectType Object;
	};

	FMemStackBase MemStack;
	TArray<FTrackedAlloc*, SceneRenderingAllocator> TrackedAllocs;
};

/** HANDLE UTILITIES */

/** Handle helper class for internal tracking of RDG types. */
template <typename LocalObjectType, typename LocalIndexType>
class TRDGHandle
{
public:
	using ObjectType = LocalObjectType;
	using IndexType = LocalIndexType;

	static const TRDGHandle Null;

	TRDGHandle() = default;

	explicit inline TRDGHandle(int32 InIndex)
	{
		check(InIndex >= 0 && InIndex < kNullIndex);
		Index = InIndex;
	}

	FORCEINLINE IndexType GetIndex() const { check(IsValid()); return Index; }
	FORCEINLINE bool IsNull()  const { return Index == kNullIndex; }
	FORCEINLINE bool IsValid() const { return Index != kNullIndex; }
	FORCEINLINE bool operator==(TRDGHandle Other) const { return Index == Other.Index; }
	FORCEINLINE bool operator!=(TRDGHandle Other) const { return Index != Other.Index; }
	FORCEINLINE bool operator<=(TRDGHandle Other) const { check(IsValid() && Other.IsValid()); return Index <= Other.Index; }
	FORCEINLINE bool operator>=(TRDGHandle Other) const { check(IsValid() && Other.IsValid()); return Index >= Other.Index; }
	FORCEINLINE bool operator< (TRDGHandle Other) const { check(IsValid() && Other.IsValid()); return Index <  Other.Index; }
	FORCEINLINE bool operator> (TRDGHandle Other) const { check(IsValid() && Other.IsValid()); return Index >  Other.Index; }

	FORCEINLINE TRDGHandle& operator++()
	{
		check(IsValid());
		++Index;
		return *this;
	}

	FORCEINLINE TRDGHandle& operator--()
	{
		check(IsValid());
		--Index;
		return *this;
	}

private:
	static const IndexType kNullIndex = TNumericLimits<IndexType>::Max();
	IndexType Index = kNullIndex;
};

template <typename ObjectType, typename IndexType>
FORCEINLINE uint32 GetTypeHash(TRDGHandle<ObjectType, IndexType> Handle)
{
	return Handle.GetIndex();
}

/** Helper handle registry class for internal tracking of RDG types. */
template <typename LocalHandleType>
class TRDGHandleRegistry
{
public:
	using HandleType = LocalHandleType;
	using ObjectType = typename HandleType::ObjectType;
	using IndexType = typename HandleType::IndexType;

	void Insert(ObjectType* Object)
	{
		Array.Emplace(Object);
		Object->Handle = Last();
	}

	template<typename DerivedType = ObjectType, class ...TArgs>
	DerivedType* Allocate(FRDGAllocator& Allocator, TArgs&&... Args)
	{
		static_assert(TIsDerivedFrom<DerivedType, ObjectType>::Value, "You must specify a type that derives from ObjectType");
		DerivedType* Object = Allocator.AllocObject<DerivedType>(Forward<TArgs>(Args)...);
		Insert(Object);
		return Object;
	}

	void Clear()
	{
		Array.Empty();
	}

	FORCEINLINE const ObjectType* Get(HandleType Handle) const
	{
		return Array[Handle.GetIndex()];
	}

	FORCEINLINE ObjectType* Get(HandleType Handle)
	{
		return Array[Handle.GetIndex()];
	}

	FORCEINLINE const ObjectType* operator[] (HandleType Handle) const
	{
		return Get(Handle);
	}

	FORCEINLINE ObjectType* operator[] (HandleType Handle)
	{
		return Get(Handle);
	}

	FORCEINLINE HandleType Begin() const
	{
		return HandleType(0);
	}

	FORCEINLINE HandleType End() const
	{
		return HandleType(Array.Num());
	}

	FORCEINLINE HandleType Last() const
	{
		return HandleType(Array.Num() - 1);
	}

	FORCEINLINE int32 Num() const
	{
		return Array.Num();
	}

private:
	TArray<ObjectType*, SceneRenderingAllocator> Array;
};

/** Specialization of bit array with compile-time type checking for handles and a pre-configured allocator. */
template <typename HandleType>
class TRDGHandleBitArray : public TBitArray<SceneRenderingBitArrayAllocator>
{
	using Base = TBitArray<SceneRenderingBitArrayAllocator>;
public:
	TRDGHandleBitArray() = default;

	explicit TRDGHandleBitArray(bool bValue, int32 InNumBits)
		: Base(bValue, InNumBits)
	{}

	FORCEINLINE FBitReference operator[](HandleType Handle)
	{
		return Base::operator[](Handle.GetIndex());
	}

	FORCEINLINE const FConstBitReference operator[](HandleType Handle) const
	{
		return Base::operator[](Handle.GetIndex());
	}
};

/** Esoteric helper class which accumulates handles and will return a valid handle only if a single unique
 *  handle was added. Otherwise, it returns null until reset. This helper is chiefly used to track UAVs
 *  tagged as 'no UAV barrier'; such that a UAV barrier is issued only if a unique no-barrier UAV is used
 *  on a pass. Intended for internal use only.
 */
template <typename HandleType>
class TRDGHandleUniqueFilter
{
public:
	TRDGHandleUniqueFilter() = default;

	TRDGHandleUniqueFilter(HandleType InHandle)
	{
		AddHandle(InHandle);
	}

	void Reset()
	{
		Handle = HandleType::Null;
		bUnique = false;
	}

	void AddHandle(HandleType InHandle)
	{
		if (Handle != InHandle && InHandle.IsValid())
		{
			bUnique = Handle.IsNull();
			Handle = InHandle;
		}
	}

	HandleType GetUniqueHandle() const
	{
		return bUnique ? Handle : HandleType::Null;
	}

private:
	HandleType Handle;
	bool bUnique = false;
};

template <typename ObjectType, typename IndexType>
const TRDGHandle<ObjectType, IndexType> TRDGHandle<ObjectType, IndexType>::Null;

/** FORWARD DECLARATIONS */

struct FRDGTextureDesc;

class FRDGBlackboard;

class FRDGPassFlagsScopeGuard;
class FRDGAsyncComputeBudgetScopeGuard;
class FRDGEventScopeGuard;
class FRDGGPUStatScopeGuard;
class FRDGScopedCsvStatExclusive;
class FRDGScopedCsvStatExclusiveConditional;

class FRDGBarrierBatch;
class FRDGBarrierBatchBegin;
class FRDGBarrierBatchEnd;
class FRDGBarrierValidation;
class FRDGBuilder;
class FRDGEventName;
class FRDGUserValidation;
class FRenderGraphResourcePool;

class FRDGResource;
using FRDGResourceRef = FRDGResource*;

class FRDGParentResource;
using FRDGParentResourceRef = FRDGParentResource*;

class FRDGShaderResourceView;
using FRDGShaderResourceViewRef = FRDGShaderResourceView*;

class FRDGUnorderedAccessView;
using FRDGUnorderedAccessViewRef = FRDGUnorderedAccessView*;

class FRDGTextureSRV;
using FRDGTextureSRVRef = FRDGTextureSRV*;

class FRDGTextureUAV;
using FRDGTextureUAVRef = FRDGTextureUAV*;

class FRDGBufferSRV;
using FRDGBufferSRVRef = FRDGBufferSRV*;

class FRDGBufferUAV;
using FRDGBufferUAVRef = FRDGBufferUAV*;

class FRDGPass;
using FRDGPassRef = const FRDGPass*;
using FRDGPassHandle = TRDGHandle<FRDGPass, uint16>;
using FRDGPassRegistry = TRDGHandleRegistry<FRDGPassHandle>;
using FRDGPassHandleArray = TArray<FRDGPassHandle, TInlineAllocator<4, SceneRenderingAllocator>>;
using FRDGPassBitArray = TRDGHandleBitArray<FRDGPassHandle>;

class FRDGUniformBuffer;
using FRDGUniformBufferRef = FRDGUniformBuffer*;
using FRDGUniformBufferHandle = TRDGHandle<FRDGUniformBuffer, uint16>;
using FRDGUniformBufferRegistry = TRDGHandleRegistry<FRDGUniformBufferHandle>;
using FRDGUniformBufferBitArray = TRDGHandleBitArray<FRDGUniformBufferHandle>;

class FRDGView;
using FRDGViewRef = FRDGView*;
using FRDGViewHandle = TRDGHandle<FRDGView, uint16>;
using FRDGViewRegistry = TRDGHandleRegistry<FRDGViewHandle>;
using FRDGViewUniqueFilter = TRDGHandleUniqueFilter<FRDGViewHandle>;

class FRDGTexture;
using FRDGTextureRef = FRDGTexture*;
using FRDGTextureHandle = TRDGHandle<FRDGTexture, uint16>;
using FRDGTextureRegistry = TRDGHandleRegistry<FRDGTextureHandle>;
using FRDGTextureBitArray = TRDGHandleBitArray<FRDGTextureHandle>;

class FRDGBuffer;
using FRDGBufferRef = FRDGBuffer*;
using FRDGBufferHandle = TRDGHandle<FRDGBuffer, uint16>;
using FRDGBufferRegistry = TRDGHandleRegistry<FRDGBufferHandle>;
using FRDGBufferBitArray = TRDGHandleBitArray<FRDGBufferHandle>;

template <typename TUniformStruct> class TRDGUniformBuffer;
template <typename TUniformStruct> using TRDGUniformBufferRef = TRDGUniformBuffer<TUniformStruct>*;

template <typename InElementType, typename InAllocatorType = FDefaultAllocator>
using TRDGTextureSubresourceArray = TArray<InElementType, TInlineAllocator<1, InAllocatorType>>;