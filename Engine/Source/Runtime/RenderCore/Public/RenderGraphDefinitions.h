// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RendererInterface.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

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

#if RDG_EVENTS || HAS_GPU_STATS
#define IF_RDG_SCOPES(Op) Op
#define RDG_SCOPES 1
#else
#define IF_RDG_SCOPES(Op)
#define RDG_SCOPES 0
#endif

/** Flags to annotate passes. */
enum class ERDGPassFlags : uint8
{
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

	/** Pass uses copy commands but writes to an untracked staging resource. */
	Readback = Copy | NeverCull
};
ENUM_CLASS_FLAGS(ERDGPassFlags);

/** Render graph specific flags for resources. */
enum class ERDGParentResourceFlags : uint8
{
	None = 0,

	// Tag the resource to survive through frame, that is important for multi GPU alternate frame rendering.
	MultiFrame = 1 << 1,
};
ENUM_CLASS_FLAGS(ERDGParentResourceFlags);

typedef ERDGParentResourceFlags ERDGResourceFlags;

enum class ERDGChildResourceFlags : uint8
{
	None = 0,

	// The view will not perform UAV barriers between consecutive usage. This flag is only valid on UAVs.
	NoUAVBarrier = 1 << 1
};
ENUM_CLASS_FLAGS(ERDGChildResourceFlags);

enum class ERDGBuilderFlags
{
	None,

	/** Forces on / off async compute if the RHI supports it. */
	ForceAsyncComputeEnable = 1 << 0,
	ForceAsyncComputeDisable = 1 << 1
};
ENUM_CLASS_FLAGS(ERDGBuilderFlags);

enum class ERDGParentResourceType : uint8
{
	Texture,
	Buffer,
	MAX
};

enum class ERDGChildResourceType : uint8
{
	TextureUAV,
	TextureSRV,
	BufferUAV,
	BufferSRV,
	MAX
};

/** Used to specify a particular texture meta data plane instead of the default resource */
enum class ERDGTextureMetaDataAccess : uint8
{
	None = 0,

	// Creates a UAV for the primary resource plane (i.e. depth or color). This is used to access
	// raw depth or color buffer data from compute shaders without decompression.
	CompressedSurface,

	// Creates a UAV for the stencil plane of the depth buffer.
	Stencil,

	HTile,
};

inline int32 GetResourceTransitionPlaneForMetadataAccess(ERDGTextureMetaDataAccess Metadata)
{
	switch (Metadata)
	{
	case ERDGTextureMetaDataAccess::CompressedSurface:
	case ERDGTextureMetaDataAccess::HTile:
		return FRHITransitionInfo::kDepthPlaneSlice;
	case ERDGTextureMetaDataAccess::Stencil:
		return FRHITransitionInfo::kStencilPlaneSlice;
	default:
		return 0;
	}
}

enum class ERDGPipeline : uint8
{
	Graphics,
	AsyncCompute,
	MAX
};

const uint32 kRDGPipelineCount = static_cast<uint32>(ERDGPipeline::MAX);

template <typename LocalClassType, typename LocalIndexType>
class TRDGHandle
{
public:
	using ClassType = LocalClassType;
	using IndexType = LocalIndexType;

	static const TRDGHandle Null;

	TRDGHandle() = default;

	explicit inline TRDGHandle(int32 InIndex)
	{
		check(InIndex >= 0 && InIndex < kNullIndex);
		Index = InIndex;
	}

	FORCEINLINE bool IsNull() const
	{
		return Index == kNullIndex;
	}

	FORCEINLINE bool IsValid() const
	{
		return Index != kNullIndex;
	}

	FORCEINLINE IndexType GetIndex() const
	{
		check(IsValid());
		return Index;
	}

	FORCEINLINE bool operator == (TRDGHandle Other) const
	{
		return Index == Other.Index;
	}

	FORCEINLINE bool operator != (TRDGHandle Other) const
	{
		return Index != Other.Index;
	}

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

template <typename ClassType, typename IndexType>
FORCEINLINE uint32 GetTypeHash(TRDGHandle<ClassType, IndexType> Handle)
{
	return Handle.GetIndex();
}

template <typename LocalHandleType>
class TRDGRegistry
{
public:
	using HandleType = LocalHandleType;
	using ClassType = typename HandleType::ClassType;
	using IndexType = typename HandleType::IndexType;

	void DestructAndClear()
	{
		for (int32 Index = Array.Num() - 1; Index >= 0; --Index)
		{
			Array[Index]->~ClassType();
		}
		Array.Empty();
	}

	HandleType Insert(ClassType* Object)
	{
		check(Object);
		Array.Emplace(Object);
		return Last();
	}

	FORCEINLINE const ClassType* Get(HandleType Handle) const
	{
		return Array[Handle.GetIndex()];
	}

	FORCEINLINE ClassType* Get(HandleType Handle)
	{
		return Array[Handle.GetIndex()];
	}

	FORCEINLINE const ClassType* operator[] (HandleType Handle) const
	{
		return Get(Handle);
	}

	FORCEINLINE ClassType* operator[] (HandleType Handle)
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
	TArray<ClassType*, SceneRenderingAllocator> Array;
};

template <typename HandleType>
class TRDGUniqueFilter
{
public:
	TRDGUniqueFilter() = default;

	TRDGUniqueFilter(HandleType InHandle)
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

template <typename ClassType, typename IndexType>
const TRDGHandle<ClassType, IndexType> TRDGHandle<ClassType, IndexType>::Null;

class FRDGPass;
using FRDGPassRef = const FRDGPass*;

using FRDGPassHandle = TRDGHandle<FRDGPass, uint16>;
using FRDGPassRegistry = TRDGRegistry<FRDGPassHandle>;
using FRDGPassHandleArray = TArray<FRDGPassHandle, TInlineAllocator<8, SceneRenderingAllocator>>;
using FRDGPassArray = TArray<FRDGPass, TInlineAllocator<8, SceneRenderingAllocator>>;

using FRDGResourceHandle = TRDGHandle<class FRDGResource, uint16>;
using FRDGResourceRegistry = TRDGRegistry<FRDGResourceHandle>;
using FRDGResourceUniqueFilter = TRDGUniqueFilter<FRDGResourceHandle>;
