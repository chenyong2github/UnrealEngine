// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "HAL/UnrealMemory.h"
#include "Containers/Array.h"
#include "Misc/Crc.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "Math/Color.h"
#include "Containers/StaticArray.h"
#include "HAL/ThreadSafeCounter.h"
#include "RHIDefinitions.h"
#include "Templates/RefCounting.h"
#include "PixelFormat.h"
#include "Containers/LockFreeList.h"
#include "Misc/SecureHash.h"
#include "Hash/CityHash.h"
#include "Async/TaskGraphInterfaces.h"
#include "Serialization/MemoryImage.h"

#define DISABLE_RHI_DEFFERED_DELETE 0

struct FClearValueBinding;
struct FRHIResourceInfo;
struct FGenerateMipsStruct;
enum class EClearBinding;

/** The base type of RHI resources. */
class RHI_API FRHIResource
{
public:
	FRHIResource(bool InbDoNotDeferDelete = false)
		: MarkedForDelete(0)
		, bDoNotDeferDelete(InbDoNotDeferDelete)
		, bCommitted(true)
	{
	}
	virtual ~FRHIResource() 
	{
		check(PlatformNeedsExtraDeletionLatency() || (NumRefs.GetValue() == 0 && (CurrentlyDeleting == this || bDoNotDeferDelete || Bypass()))); // this should not have any outstanding refs
	}
	FORCEINLINE_DEBUGGABLE uint32 AddRef() const
	{
		int32 NewValue = NumRefs.Increment();
		checkSlow(NewValue > 0); 
		return uint32(NewValue);
	}
	FORCEINLINE_DEBUGGABLE uint32 Release() const
	{
		int32 NewValue = NumRefs.Decrement();
		if (NewValue == 0)
		{
			if (!DeferDelete())
			{ 
				delete this;
			}
			else
			{
				if (FPlatformAtomics::InterlockedCompareExchange(&MarkedForDelete, 1, 0) == 0)
				{
					PendingDeletes.Push(const_cast<FRHIResource*>(this));
				}
			}
		}
		checkSlow(NewValue >= 0);
		return uint32(NewValue);
	}
	FORCEINLINE_DEBUGGABLE uint32 GetRefCount() const
	{
		int32 CurrentValue = NumRefs.GetValue();
		checkSlow(CurrentValue >= 0); 
		return uint32(CurrentValue);
	}
	void DoNoDeferDelete()
	{
		check(!MarkedForDelete);
		bDoNotDeferDelete = true;
		FPlatformMisc::MemoryBarrier();
		check(!MarkedForDelete);
	}

	static void FlushPendingDeletes(bool bFlushDeferredDeletes = false);

	FORCEINLINE static bool PlatformNeedsExtraDeletionLatency()
	{
		return GRHINeedsExtraDeletionLatency && GIsRHIInitialized;
	}

	static bool Bypass();

	// Transient resource tracking
	// We do this at a high level so we can catch errors even when transient resources are not supported
	void SetCommitted(bool bInCommitted) 
	{ 
		check(IsInRenderingThread()); 
		bCommitted = bInCommitted;
	}
	bool IsCommitted() const 
	{ 
		check(IsInRenderingThread());
		return bCommitted;
	}

	bool IsValid() const
	{
		return !MarkedForDelete && NumRefs.GetValue() > 0;
	}

private:
	mutable FThreadSafeCounter NumRefs;
	mutable int32 MarkedForDelete;
	bool bDoNotDeferDelete;
	bool bCommitted;

	static TLockFreePointerListUnordered<FRHIResource, PLATFORM_CACHE_LINE_SIZE> PendingDeletes;
	static FRHIResource* CurrentlyDeleting;

	FORCEINLINE bool DeferDelete() const
	{
#if DISABLE_RHI_DEFFERED_DELETE
		checkf(!GRHIValidationEnabled, TEXT("RHI validation is not supported when DISABLE_RHI_DEFERRED_DELETE flag is set."));
		return false;
#else
		// Defer if GRHINeedsExtraDeletionLatency or we are doing threaded rendering (unless otherwise requested).
		return !bDoNotDeferDelete && (GRHINeedsExtraDeletionLatency || !Bypass());
#endif
	}

	// Some APIs don't do internal reference counting, so we have to wait an extra couple of frames before deleting resources
	// to ensure the GPU has completely finished with them. This avoids expensive fences, etc.
	struct ResourcesToDelete
	{
		ResourcesToDelete(uint32 InFrameDeleted = 0)
			: FrameDeleted(InFrameDeleted)
		{

		}

		TArray<FRHIResource*>	Resources;
		uint32					FrameDeleted;
	};

	static TArray<ResourcesToDelete> DeferredDeletionQueue;
	static uint32 CurrentFrame;
};

class FExclusiveDepthStencil
{
public:
	enum Type
	{
		// don't use those directly, use the combined versions below
		// 4 bits are used for depth and 4 for stencil to make the hex value readable and non overlapping
		DepthNop = 0x00,
		DepthRead = 0x01,
		DepthWrite = 0x02,
		DepthMask = 0x0f,
		StencilNop = 0x00,
		StencilRead = 0x10,
		StencilWrite = 0x20,
		StencilMask = 0xf0,

		// use those:
		DepthNop_StencilNop = DepthNop + StencilNop,
		DepthRead_StencilNop = DepthRead + StencilNop,
		DepthWrite_StencilNop = DepthWrite + StencilNop,
		DepthNop_StencilRead = DepthNop + StencilRead,
		DepthRead_StencilRead = DepthRead + StencilRead,
		DepthWrite_StencilRead = DepthWrite + StencilRead,
		DepthNop_StencilWrite = DepthNop + StencilWrite,
		DepthRead_StencilWrite = DepthRead + StencilWrite,
		DepthWrite_StencilWrite = DepthWrite + StencilWrite,
	};

private:
	Type Value;

public:
	// constructor
	FExclusiveDepthStencil(Type InValue = DepthNop_StencilNop)
		: Value(InValue)
	{
	}

	inline bool IsUsingDepthStencil() const
	{
		return Value != DepthNop_StencilNop;
	}
	inline bool IsUsingDepth() const
	{
		return (ExtractDepth() != DepthNop);
	}
	inline bool IsUsingStencil() const
	{
		return (ExtractStencil() != StencilNop);
	}
	inline bool IsDepthWrite() const
	{
		return ExtractDepth() == DepthWrite;
	}
	inline bool IsDepthRead() const
	{
		return ExtractDepth() == DepthRead;
	}
	inline bool IsStencilWrite() const
	{
		return ExtractStencil() == StencilWrite;
	}
	inline bool IsStencilRead() const
	{
		return ExtractStencil() == StencilRead;
	}

	inline bool IsAnyWrite() const
	{
		return IsDepthWrite() || IsStencilWrite();
	}

	inline void SetDepthWrite()
	{
		Value = (Type)(ExtractStencil() | DepthWrite);
	}
	inline void SetStencilWrite()
	{
		Value = (Type)(ExtractDepth() | StencilWrite);
	}
	inline void SetDepthStencilWrite(bool bDepth, bool bStencil)
	{
		Value = DepthNop_StencilNop;

		if (bDepth)
		{
			SetDepthWrite();
		}
		if (bStencil)
		{
			SetStencilWrite();
		}
	}
	bool operator==(const FExclusiveDepthStencil& rhs) const
	{
		return Value == rhs.Value;
	}

	bool operator != (const FExclusiveDepthStencil& RHS) const
	{
		return Value != RHS.Value;
	}

	inline bool IsValid(FExclusiveDepthStencil& Current) const
	{
		Type Depth = ExtractDepth();

		if (Depth != DepthNop && Depth != Current.ExtractDepth())
		{
			return false;
		}

		Type Stencil = ExtractStencil();

		if (Stencil != StencilNop && Stencil != Current.ExtractStencil())
		{
			return false;
		}

		return true;
	}

	inline void GetAccess(ERHIAccess& DepthAccess, ERHIAccess& StencilAccess) const
	{
		DepthAccess = ERHIAccess::None;

		// SRV access is allowed whilst a depth stencil target is "readable".
		constexpr ERHIAccess DSVReadOnlyMask =
			ERHIAccess::DSVRead |
			ERHIAccess::SRVGraphics |
			ERHIAccess::SRVCompute;

		// If write access is required, only the depth block can access the resource.
		constexpr ERHIAccess DSVReadWriteMask =
			ERHIAccess::DSVRead |
			ERHIAccess::DSVWrite;

		if (IsUsingDepth())
		{
			DepthAccess = IsDepthWrite() ? DSVReadWriteMask : DSVReadOnlyMask;
		}

		StencilAccess = ERHIAccess::None;

		if (IsUsingStencil())
		{
			StencilAccess = IsStencilWrite() ? DSVReadWriteMask : DSVReadOnlyMask;
		}
	}

	template <typename TFunction>
	inline void EnumerateSubresources(TFunction Function) const
	{
		if (!IsUsingDepthStencil())
		{
			return;
		}

		ERHIAccess DepthAccess = ERHIAccess::None;
		ERHIAccess StencilAccess = ERHIAccess::None;
		GetAccess(DepthAccess, StencilAccess);

		// Same depth / stencil state; single subresource.
		if (DepthAccess == StencilAccess)
		{
			Function(DepthAccess, FRHITransitionInfo::kAllSubresources);
		}
		// Separate subresources for depth / stencil.
		else
		{
			if (DepthAccess != ERHIAccess::None)
			{
				Function(DepthAccess, FRHITransitionInfo::kDepthPlaneSlice);
			}
			if (StencilAccess != ERHIAccess::None)
			{
				Function(StencilAccess, FRHITransitionInfo::kStencilPlaneSlice);
			}
		}
	}

	/**
	* Returns a new FExclusiveDepthStencil to be used to transition a depth stencil resource to readable.
	* If the depth or stencil is already in a readable state, that particular component is returned as Nop,
	* to avoid unnecessary subresource transitions.
	*/
	inline FExclusiveDepthStencil GetReadableTransition() const
	{
		FExclusiveDepthStencil::Type NewDepthState = IsDepthWrite()
			? FExclusiveDepthStencil::DepthRead
			: FExclusiveDepthStencil::DepthNop;

		FExclusiveDepthStencil::Type NewStencilState = IsStencilWrite()
			? FExclusiveDepthStencil::StencilRead
			: FExclusiveDepthStencil::StencilNop;

		return (FExclusiveDepthStencil::Type)(NewDepthState | NewStencilState);
	}

	/**
	* Returns a new FExclusiveDepthStencil to be used to transition a depth stencil resource to readable.
	* If the depth or stencil is already in a readable state, that particular component is returned as Nop,
	* to avoid unnecessary subresource transitions.
	*/
	inline FExclusiveDepthStencil GetWritableTransition() const
	{
		FExclusiveDepthStencil::Type NewDepthState = IsDepthRead()
			? FExclusiveDepthStencil::DepthWrite
			: FExclusiveDepthStencil::DepthNop;

		FExclusiveDepthStencil::Type NewStencilState = IsStencilRead()
			? FExclusiveDepthStencil::StencilWrite
			: FExclusiveDepthStencil::StencilNop;

		return (FExclusiveDepthStencil::Type)(NewDepthState | NewStencilState);
	}

	uint32 GetIndex() const
	{
		// Note: The array to index has views created in that specific order.

		// we don't care about the Nop versions so less views are needed
		// we combine Nop and Write
		switch (Value)
		{
		case DepthWrite_StencilNop:
		case DepthNop_StencilWrite:
		case DepthWrite_StencilWrite:
		case DepthNop_StencilNop:
			return 0; // old DSAT_Writable

		case DepthRead_StencilNop:
		case DepthRead_StencilWrite:
			return 1; // old DSAT_ReadOnlyDepth

		case DepthNop_StencilRead:
		case DepthWrite_StencilRead:
			return 2; // old DSAT_ReadOnlyStencil

		case DepthRead_StencilRead:
			return 3; // old DSAT_ReadOnlyDepthAndStencil
		}
		// should never happen
		check(0);
		return -1;
	}
	static const uint32 MaxIndex = 4;

private:
	inline Type ExtractDepth() const
	{
		return (Type)(Value & DepthMask);
	}
	inline Type ExtractStencil() const
	{
		return (Type)(Value & StencilMask);
	}
};

//
// State blocks
//

class FRHISamplerState : public FRHIResource 
{
public:
	virtual bool IsImmutable() const { return false; }
};

class FRHIRasterizerState : public FRHIResource
{
public:
	virtual bool GetInitializer(struct FRasterizerStateInitializerRHI& Init) { return false; }
};
class FRHIDepthStencilState : public FRHIResource
{
public:
#if ENABLE_RHI_VALIDATION
	FExclusiveDepthStencil ActualDSMode;
#endif
	virtual bool GetInitializer(struct FDepthStencilStateInitializerRHI& Init) { return false; }
};
class FRHIBlendState : public FRHIResource
{
public:
	virtual bool GetInitializer(class FBlendStateInitializerRHI& Init) { return false; }
};

//
// Shader bindings
//

typedef TArray<struct FVertexElement,TFixedAllocator<MaxVertexElementCount> > FVertexDeclarationElementList;
class FRHIVertexDeclaration : public FRHIResource
{
public:
	virtual bool GetInitializer(FVertexDeclarationElementList& Init) { return false; }
};

class FRHIBoundShaderState : public FRHIResource {};

//
// Shaders
//

class FRHIShader : public FRHIResource
{
public:
	void SetHash(FSHAHash InHash) { Hash = InHash; }
	FSHAHash GetHash() const { return Hash; }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// for debugging only e.g. MaterialName:ShaderFile.usf or ShaderFile.usf/EntryFunc
	FString ShaderName;
	FORCEINLINE const TCHAR* GetShaderName() const { return *ShaderName; }
#else
	FORCEINLINE const TCHAR* GetShaderName() const { return TEXT(""); }
#endif

	explicit FRHIShader(EShaderFrequency InFrequency)
		: Frequency(InFrequency)
	{
	}

	inline EShaderFrequency GetFrequency() const
	{
		return Frequency;
	}

private:
	FSHAHash Hash;
	EShaderFrequency Frequency;
};

class FRHIGraphicsShader : public FRHIShader
{
public:
	explicit FRHIGraphicsShader(EShaderFrequency InFrequency) : FRHIShader(InFrequency) {}
};

class FRHIVertexShader : public FRHIGraphicsShader
{
public:
	FRHIVertexShader() : FRHIGraphicsShader(SF_Vertex) {}
};

class FRHIHullShader : public FRHIGraphicsShader
{
public:
	FRHIHullShader() : FRHIGraphicsShader(SF_Hull) {}
};

class FRHIDomainShader : public FRHIGraphicsShader
{
public:
	FRHIDomainShader() : FRHIGraphicsShader(SF_Domain) {}
};

class FRHIPixelShader : public FRHIGraphicsShader
{
public:
	FRHIPixelShader() : FRHIGraphicsShader(SF_Pixel) {}
};

class FRHIGeometryShader : public FRHIGraphicsShader
{
public:
	FRHIGeometryShader() : FRHIGraphicsShader(SF_Geometry) {}
};

class FRHIRayTracingShader : public FRHIShader
{
public:
	explicit FRHIRayTracingShader(EShaderFrequency InFrequency) : FRHIShader(InFrequency) {}
};

class RHI_API FRHIComputeShader : public FRHIShader
{
public:
	FRHIComputeShader() : FRHIShader(SF_Compute), Stats(nullptr) {}
	
	inline void SetStats(struct FPipelineStateStats* Ptr) { Stats = Ptr; }
	void UpdateStats();
	
private:
	struct FPipelineStateStats* Stats;
};

//
// Pipeline States
//

class FRHIGraphicsPipelineState : public FRHIResource 
{
#if ENABLE_RHI_VALIDATION
	friend class FValidationContext;
	friend class FValidationRHI;
	FExclusiveDepthStencil DSMode;
#endif
};
class FRHIComputePipelineState : public FRHIResource {};
class FRHIRayTracingPipelineState : public FRHIResource {};

//
// Buffers
//

// Whether to assert in cases where the layout is released before uniform buffers created with that layout
#define VALIDATE_UNIFORM_BUFFER_LAYOUT_LIFETIME 0

// Whether to assert when a uniform buffer is being deleted while still referenced by a mesh draw command
// Enabling this requires -norhithread to work correctly since FRHIResource lifetime is managed by both the RT and RHIThread
#define VALIDATE_UNIFORM_BUFFER_LIFETIME 0

/** The layout of a uniform buffer in memory. */
struct FRHIUniformBufferLayout
{
public:
	static const uint16 kInvalidOffset = TNumericLimits<uint16>::Max();

	/** Data structure to store information about resource parameter in a shader parameter structure. */
	struct FResourceParameter
	{
		DECLARE_EXPORTED_TYPE_LAYOUT(FResourceParameter, RHI_API, NonVirtual);
	public:
		friend inline FArchive& operator<<(FArchive& Ar, FResourceParameter& Ref)
		{
			uint8 Type = (uint8)Ref.MemberType;
			Ar << Ref.MemberOffset;
			Ar << Type;
			Ref.MemberType = (EUniformBufferBaseType)Type;
			return Ar;
		}

		/** Byte offset to each resource in the uniform buffer memory. */
		LAYOUT_FIELD(uint16, MemberOffset);
		/** Type of the member that allow (). */
		LAYOUT_FIELD(EUniformBufferBaseType, MemberType);
	};

	DECLARE_EXPORTED_TYPE_LAYOUT(FRHIUniformBufferLayout, RHI_API, NonVirtual);
public:

	inline uint32 GetHash() const
	{
		checkSlow(Hash != 0);
		return Hash;
	}

	void ComputeHash()
	{
		uint32 TmpHash = ConstantBufferSize << 16 | static_cast<uint32>(StaticSlot);

		for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ResourceIndex++)
		{
			// Offset and therefore hash must be the same regardless of pointer size
			checkSlow(Resources[ResourceIndex].MemberOffset == Align(Resources[ResourceIndex].MemberOffset, SHADER_PARAMETER_POINTER_ALIGNMENT));
			TmpHash ^= Resources[ResourceIndex].MemberOffset;
		}

		uint32 N = Resources.Num();
		while (N >= 4)
		{
			TmpHash ^= (Resources[--N].MemberType << 0);
			TmpHash ^= (Resources[--N].MemberType << 8);
			TmpHash ^= (Resources[--N].MemberType << 16);
			TmpHash ^= (Resources[--N].MemberType << 24);
		}
		while (N >= 2)
		{
			TmpHash ^= Resources[--N].MemberType << 0;
			TmpHash ^= Resources[--N].MemberType << 16;
		}
		while (N > 0)
		{
			TmpHash ^= Resources[--N].MemberType;
		}
		Hash = TmpHash;
	}

	FRHIUniformBufferLayout() = default;

	explicit FRHIUniformBufferLayout(const TCHAR* InName)
		: Name(InName)
	{}

#if VALIDATE_UNIFORM_BUFFER_LAYOUT_LIFETIME
	~FRHIUniformBufferLayout()
	{
		check(NumUsesForDebugging == 0 || IsEngineExitRequested());
	}
#endif

	void CopyFrom(const FRHIUniformBufferLayout& Source)
	{
		ConstantBufferSize = Source.ConstantBufferSize;
		StaticSlot = Source.StaticSlot;
		Resources = Source.Resources;
		Name = Source.Name;
		Hash = Source.Hash;
	}

	const FMemoryImageString& GetDebugName() const
	{
		return Name;
	}

	bool HasRenderTargets() const
	{
		return RenderTargetsOffset != kInvalidOffset;
	}

	bool HasExternalOutputs() const
	{
		return bHasNonGraphOutputs;
	}

	bool HasStaticSlot() const
	{
		return IsUniformBufferStaticSlotValid(StaticSlot);
	}

	friend FArchive& operator<<(FArchive& Ar, FRHIUniformBufferLayout& Ref)
	{
		Ar << Ref.ConstantBufferSize;
		Ar << Ref.StaticSlot;
		Ar << Ref.RenderTargetsOffset;
		Ar << Ref.bHasNonGraphOutputs;
		Ar << Ref.Resources;
		Ar << Ref.GraphResources;
		Ar << Ref.GraphTextures;
		Ar << Ref.GraphBuffers;
		Ar << Ref.GraphUniformBuffers;
		Ar << Ref.UniformBuffers;
		Ar << Ref.Name;
		Ar << Ref.Hash;
		return Ar;
	}

	/** The size of the constant buffer in bytes. */
	LAYOUT_FIELD_INITIALIZED(uint32, ConstantBufferSize, 0);

	/** The static slot (if applicable). */
	LAYOUT_FIELD_INITIALIZED(FUniformBufferStaticSlot, StaticSlot, MAX_UNIFORM_BUFFER_STATIC_SLOTS);

	/** The render target binding slots offset, if it exists. */
	LAYOUT_FIELD_INITIALIZED(uint16, RenderTargetsOffset, kInvalidOffset);

	/** Whether this layout may contain non-render-graph outputs (e.g. RHI UAVs). */
	LAYOUT_FIELD_INITIALIZED(bool, bHasNonGraphOutputs, false);

	/** The list of all resource inlined into the shader parameter structure. */
	LAYOUT_FIELD(TMemoryImageArray<FResourceParameter>, Resources);

	/** The list of all RDG resource references inlined into the shader parameter structure. */
	LAYOUT_FIELD(TMemoryImageArray<FResourceParameter>, GraphResources);

	/** The list of all RDG texture references inlined into the shader parameter structure. */
	LAYOUT_FIELD(TMemoryImageArray<FResourceParameter>, GraphTextures);

	/** The list of all RDG buffer references inlined into the shader parameter structure. */
	LAYOUT_FIELD(TMemoryImageArray<FResourceParameter>, GraphBuffers);

	/** The list of all RDG uniform buffer references inlined into the shader parameter structure. */
	LAYOUT_FIELD(TMemoryImageArray<FResourceParameter>, GraphUniformBuffers);

	/** The list of all non-RDG uniform buffer references inlined into the shader parameter structure. */
	LAYOUT_FIELD(TMemoryImageArray<FResourceParameter>, UniformBuffers);

	LAYOUT_MUTABLE_FIELD_INITIALIZED(int32, NumUsesForDebugging, 0);

private:
	// for debugging / error message
	LAYOUT_FIELD(FMemoryImageString, Name);
	LAYOUT_FIELD_INITIALIZED(uint32, Hash, 0);
};

/** Compare two uniform buffer layouts. */
inline bool operator==(const FRHIUniformBufferLayout::FResourceParameter& A, const FRHIUniformBufferLayout::FResourceParameter& B)
{
	return A.MemberOffset == B.MemberOffset
		&& A.MemberType == B.MemberType;
}

/** Compare two uniform buffer layouts. */
inline bool operator==(const FRHIUniformBufferLayout& A, const FRHIUniformBufferLayout& B)
{
	return A.ConstantBufferSize == B.ConstantBufferSize
		&& A.StaticSlot == B.StaticSlot
		&& A.Resources == B.Resources;
}

class FRHIUniformBuffer : public FRHIResource
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FUniformBufferResource
#endif
{
public:

	/** Initialization constructor. */
	FRHIUniformBuffer(const FRHIUniformBufferLayout& InLayout)
	: Layout(&InLayout)
	, LayoutConstantBufferSize(InLayout.ConstantBufferSize)
	{}

	FORCEINLINE_DEBUGGABLE uint32 AddRef() const
	{
#if VALIDATE_UNIFORM_BUFFER_LAYOUT_LIFETIME
		if (GetRefCount() == 0)
		{
			Layout->NumUsesForDebugging++;
		}
#endif
		return FRHIResource::AddRef();
	}

	FORCEINLINE_DEBUGGABLE uint32 Release() const
	{
		const FRHIUniformBufferLayout* LocalLayout = Layout;

#if VALIDATE_UNIFORM_BUFFER_LIFETIME
		int32 LocalNumMeshCommandReferencesForDebugging = NumMeshCommandReferencesForDebugging;
#endif

		uint32 NewRefCount = FRHIResource::Release();

		if (NewRefCount == 0)
		{
#if VALIDATE_UNIFORM_BUFFER_LAYOUT_LIFETIME
			LocalLayout->NumUsesForDebugging--;
			check(LocalLayout->NumUsesForDebugging >= 0);
#endif
#if VALIDATE_UNIFORM_BUFFER_LIFETIME
			check(LocalNumMeshCommandReferencesForDebugging == 0 || IsEngineExitRequested());
#endif
		}

		return NewRefCount;
	}

	/** @return The number of bytes in the uniform buffer. */
	uint32 GetSize() const
	{
		check(LayoutConstantBufferSize == Layout->ConstantBufferSize);
		return LayoutConstantBufferSize;
	}
	const FRHIUniformBufferLayout& GetLayout() const { return *Layout; }

	bool IsGlobal() const
	{
		return IsUniformBufferStaticSlotValid(Layout->StaticSlot);
	}

#if VALIDATE_UNIFORM_BUFFER_LIFETIME
	mutable int32 NumMeshCommandReferencesForDebugging = 0;
#endif

	virtual int32 GetPatchingFrameNumber() const { return -1; }
	virtual void SetPatchingFrameNumber(int32 FrameNumber) { }

private:
	/** Layout of the uniform buffer. */
	const FRHIUniformBufferLayout* Layout;

	uint32 LayoutConstantBufferSize;
};

class FRHIIndexBuffer : public FRHIResource
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FBufferResource
#endif
{
public:

	/** Initialization constructor. */
	FRHIIndexBuffer(uint32 InStride,uint32 InSize,uint32 InUsage)
	: Stride(InStride)
	, Size(InSize)
	, Usage(InUsage)
	{}

	/** @return The stride in bytes of the index buffer; must be 2 or 4. */
	uint32 GetStride() const { return Stride; }

	/** @return The number of bytes in the index buffer. */
	uint32 GetSize() const { return Size; }

	/** @return The usage flags used to create the index buffer. */
	uint32 GetUsage() const { return Usage; }

protected:
	FRHIIndexBuffer()
		: Stride(0)
		, Size(0)
		, Usage(0)
	{}

	void Swap(FRHIIndexBuffer& Other)
	{
		::Swap(Stride, Other.Stride);
		::Swap(Size, Other.Size);
		::Swap(Usage, Other.Usage);
	}

	void ReleaseUnderlyingResource()
	{
		Stride = Size = Usage = 0;
	}

private:
	uint32 Stride;
	uint32 Size;
	uint32 Usage;
};

class FRHIVertexBuffer : public FRHIResource
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FBufferResource
#endif
{
public:

	/**
	 * Initialization constructor.
	 * @apram InUsage e.g. BUF_UnorderedAccess
	 */
	FRHIVertexBuffer(uint32 InSize,uint32 InUsage)
	: Size(InSize)
	, Usage(InUsage)
	{}

	/** @return The number of bytes in the vertex buffer. */
	uint32 GetSize() const { return Size; }

	/** @return The usage flags used to create the vertex buffer. e.g. BUF_UnorderedAccess */
	uint32 GetUsage() const { return Usage; }

protected:
	FRHIVertexBuffer()
		: Size(0)
		, Usage(0)
	{}

	void Swap(FRHIVertexBuffer& Other)
	{
		::Swap(Size, Other.Size);
		::Swap(Usage, Other.Usage);
	}

	void ReleaseUnderlyingResource()
	{
		Size = 0;
		Usage = 0;
	}

private:
	uint32 Size;
	// e.g. BUF_UnorderedAccess
	uint32 Usage;
};

class FRHIStructuredBuffer : public FRHIResource
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FBufferResource
#endif
{
public:

	/** Initialization constructor. */
	FRHIStructuredBuffer(uint32 InStride,uint32 InSize,uint32 InUsage)
	: Stride(InStride)
	, Size(InSize)
	, Usage(InUsage)
	{}

	/** @return The stride in bytes of the structured buffer; must be 2 or 4. */
	uint32 GetStride() const { return Stride; }

	/** @return The number of bytes in the structured buffer. */
	uint32 GetSize() const { return Size; }

	/** @return The usage flags used to create the structured buffer. */
	uint32 GetUsage() const { return Usage; }

private:
	uint32 Stride;
	uint32 Size;
	uint32 Usage;
};

//
// Textures
//

class RHI_API FLastRenderTimeContainer
{
public:
	FLastRenderTimeContainer() : LastRenderTime(-FLT_MAX) {}

	double GetLastRenderTime() const { return LastRenderTime; }
	FORCEINLINE_DEBUGGABLE void SetLastRenderTime(double InLastRenderTime) 
	{ 
		// avoid dirty caches from redundant writes
		if (LastRenderTime != InLastRenderTime)
		{
			LastRenderTime = InLastRenderTime;
		}
	}

private:
	/** The last time the resource was rendered. */
	double LastRenderTime;
};

class RHI_API FRHITexture : public FRHIResource
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FTextureResource
#endif
{
public:
	
	/** Initialization constructor. */
	FRHITexture(uint32 InNumMips, uint32 InNumSamples, EPixelFormat InFormat, ETextureCreateFlags InFlags, FLastRenderTimeContainer* InLastRenderTime, const FClearValueBinding& InClearValue)
		: ClearValue(InClearValue)
		, NumMips(InNumMips)
		, NumSamples(InNumSamples)
		, Format(InFormat)
		, Flags(InFlags)
	, LastRenderTime(InLastRenderTime ? *InLastRenderTime : DefaultLastRenderTime)	
	{}

	// Dynamic cast methods.
	virtual class FRHITexture2D* GetTexture2D() { return NULL; }
	virtual class FRHITexture2DArray* GetTexture2DArray() { return NULL; }
	virtual class FRHITexture3D* GetTexture3D() { return NULL; }
	virtual class FRHITextureCube* GetTextureCube() { return NULL; }
	virtual class FRHITextureReference* GetTextureReference() { return NULL; }
	
	// Slower method to get Size X, Y & Z information. Prefer sub-classes' GetSizeX(), etc
	virtual FIntVector GetSizeXYZ() const = 0;

	/**
	 * Returns access to the platform-specific native resource pointer.  This is designed to be used to provide plugins with access
	 * to the underlying resource and should be used very carefully or not at all.
	 *
	 * @return	The pointer to the native resource or NULL if it not initialized or not supported for this resource type for some reason
	 */
	virtual void* GetNativeResource() const
	{
		// Override this in derived classes to expose access to the native texture resource
		return nullptr;
	}

	/**
	 * Returns access to the platform-specific native shader resource view pointer.  This is designed to be used to provide plugins with access
	 * to the underlying resource and should be used very carefully or not at all.
	 *
	 * @return	The pointer to the native resource or NULL if it not initialized or not supported for this resource type for some reason
	 */
	virtual void* GetNativeShaderResourceView() const
	{
		// Override this in derived classes to expose access to the native texture resource
		return nullptr;
	}
	
	/**
	 * Returns access to the platform-specific RHI texture baseclass.  This is designed to provide the RHI with fast access to its base classes in the face of multiple inheritance.
	 * @return	The pointer to the platform-specific RHI texture baseclass or NULL if it not initialized or not supported for this RHI
	 */
	virtual void* GetTextureBaseRHI()
	{
		// Override this in derived classes to expose access to the native texture resource
		return nullptr;
	}

	/** @return The number of mip-maps in the texture. */
	uint32 GetNumMips() const { return NumMips; }

	/** @return The format of the pixels in the texture. */
	EPixelFormat GetFormat() const { return Format; }

	/** @return The flags used to create the texture. */
	ETextureCreateFlags GetFlags() const { return Flags; }

	/* @return the number of samples for multi-sampling. */
	uint32 GetNumSamples() const { return NumSamples; }

	/** @return Whether the texture is multi sampled. */
	bool IsMultisampled() const { return NumSamples > 1; }		

	FRHIResourceInfo ResourceInfo;

	/** sets the last time this texture was cached in a resource table. */
	FORCEINLINE_DEBUGGABLE void SetLastRenderTime(float InLastRenderTime)
	{
		LastRenderTime.SetLastRenderTime(InLastRenderTime);
	}

	/** Returns the last render time container, or NULL if none were specified at creation. */
	FLastRenderTimeContainer* GetLastRenderTimeContainer()
	{
		if (&LastRenderTime == &DefaultLastRenderTime)
		{
			return NULL;
		}
		return &LastRenderTime;
	}

	FORCEINLINE_DEBUGGABLE void SetDefaultLastRenderTimeContainer()
	{
		LastRenderTime = DefaultLastRenderTime;
	}

	void SetName(const FName& InName)
	{
		TextureName = InName;
	}

	FName GetName() const
	{
		return TextureName;
	}

	bool HasClearValue() const
	{
		return ClearValue.ColorBinding != EClearBinding::ENoneBound;
	}

	FLinearColor GetClearColor() const
	{
		return ClearValue.GetClearColor();
	}

	void GetDepthStencilClearValue(float& OutDepth, uint32& OutStencil) const
	{
		return ClearValue.GetDepthStencil(OutDepth, OutStencil);
	}

	float GetDepthClearValue() const
	{
		float Depth;
		uint32 Stencil;
		ClearValue.GetDepthStencil(Depth, Stencil);
		return Depth;
	}

	uint32 GetStencilClearValue() const
	{
		float Depth;
		uint32 Stencil;
		ClearValue.GetDepthStencil(Depth, Stencil);
		return Stencil;
	}

	const FClearValueBinding GetClearBinding() const
	{
		return ClearValue;
	}

	virtual void GetWriteMaskProperties(void*& OutData, uint32& OutSize)
	{
		OutData = nullptr;
		OutSize = 0;
	}

private:
	FClearValueBinding ClearValue;
	uint32 NumMips;
	uint32 NumSamples;
	EPixelFormat Format;
	ETextureCreateFlags Flags;
	FLastRenderTimeContainer& LastRenderTime;
	FLastRenderTimeContainer DefaultLastRenderTime;	
	FName TextureName;
};

class RHI_API FRHITexture2D : public FRHITexture
{
public:
	
	/** Initialization constructor. */
	FRHITexture2D(uint32 InSizeX,uint32 InSizeY,uint32 InNumMips,uint32 InNumSamples,EPixelFormat InFormat,ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture(InNumMips, InNumSamples, InFormat, InFlags, NULL, InClearValue)
	, SizeX(InSizeX)
	, SizeY(InSizeY)
	{}
	
	// Dynamic cast methods.
	virtual FRHITexture2D* GetTexture2D() { return this; }

	/** @return The width of the texture. */
	uint32 GetSizeX() const { return SizeX; }
	
	/** @return The height of the texture. */
	uint32 GetSizeY() const { return SizeY; }

	inline FIntPoint GetSizeXY() const
	{
		return FIntPoint(SizeX, SizeY);
	}

	virtual FIntVector GetSizeXYZ() const override
	{
		return FIntVector(SizeX, SizeY, 1);
	}

private:

	uint32 SizeX;
	uint32 SizeY;
};

class RHI_API FRHITexture2DArray : public FRHITexture2D
{
public:
	
	/** Initialization constructor. */
	FRHITexture2DArray(uint32 InSizeX,uint32 InSizeY,uint32 InSizeZ,uint32 InNumMips,uint32 NumSamples, EPixelFormat InFormat,ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture2D(InSizeX, InSizeY, InNumMips,NumSamples,InFormat,InFlags, InClearValue)
	, SizeZ(InSizeZ)
	{
		check(InSizeZ != 0);
	}
	
	// Dynamic cast methods.
	virtual FRHITexture2DArray* GetTexture2DArray() { return this; }

	virtual FRHITexture2D* GetTexture2D() { return NULL; }

	/** @return The number of textures in the array. */
	uint32 GetSizeZ() const { return SizeZ; }

	virtual FIntVector GetSizeXYZ() const final override
	{
		return FIntVector(GetSizeX(), GetSizeY(), SizeZ);
	}

private:

	uint32 SizeZ;
};

class RHI_API FRHITexture3D : public FRHITexture
{
public:
	
	/** Initialization constructor. */
	FRHITexture3D(uint32 InSizeX,uint32 InSizeY,uint32 InSizeZ,uint32 InNumMips,EPixelFormat InFormat,ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture(InNumMips,1,InFormat,InFlags,NULL, InClearValue)
	, SizeX(InSizeX)
	, SizeY(InSizeY)
	, SizeZ(InSizeZ)
	{}
	
	// Dynamic cast methods.
	virtual FRHITexture3D* GetTexture3D() { return this; }
	
	/** @return The width of the texture. */
	uint32 GetSizeX() const { return SizeX; }
	
	/** @return The height of the texture. */
	uint32 GetSizeY() const { return SizeY; }

	/** @return The depth of the texture. */
	uint32 GetSizeZ() const { return SizeZ; }

	virtual FIntVector GetSizeXYZ() const final override
	{
		return FIntVector(SizeX, SizeY, SizeZ);
	}

private:

	uint32 SizeX;
	uint32 SizeY;
	uint32 SizeZ;
};

class RHI_API FRHITextureCube : public FRHITexture
{
public:
	
	/** Initialization constructor. */
	FRHITextureCube(uint32 InSize,uint32 InNumMips,EPixelFormat InFormat,ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture(InNumMips,1,InFormat,InFlags,NULL, InClearValue)
	, Size(InSize)
	{}
	
	// Dynamic cast methods.
	virtual FRHITextureCube* GetTextureCube() { return this; }
	
	/** @return The width and height of each face of the cubemap. */
	uint32 GetSize() const { return Size; }

	virtual FIntVector GetSizeXYZ() const final override
	{
		return FIntVector(Size, Size, 1);
	}

private:

	uint32 Size;
};

class RHI_API FRHITextureReference : public FRHITexture
{
public:
	explicit FRHITextureReference(FLastRenderTimeContainer* InLastRenderTime)
		: FRHITexture(0,0,PF_Unknown,TexCreate_None,InLastRenderTime, FClearValueBinding())
	{}

	virtual FRHITextureReference* GetTextureReference() override { return this; }
	inline FRHITexture* GetReferencedTexture() const { return ReferencedTexture.GetReference(); }

	void SetReferencedTexture(FRHITexture* InTexture)
	{
		ReferencedTexture = InTexture;
	}

	virtual FIntVector GetSizeXYZ() const final override
	{
		if (ReferencedTexture)
		{
			return ReferencedTexture->GetSizeXYZ();
		}
		return FIntVector(0, 0, 0);
	}

#if ENABLE_RHI_VALIDATION
	virtual RHIValidation::FResource* GetTrackerResource() final override
	{
		check(ReferencedTexture);
		return ReferencedTexture->GetTrackerResource();
	}
#endif

private:
	TRefCountPtr<FRHITexture> ReferencedTexture;
};

class RHI_API FRHITextureReferenceNullImpl : public FRHITextureReference
{
public:
	FRHITextureReferenceNullImpl()
		: FRHITextureReference(NULL)
	{}

	void SetReferencedTexture(FRHITexture* InTexture)
	{
		FRHITextureReference::SetReferencedTexture(InTexture);
	}
};

//
// Misc
//

class RHI_API FRHITimestampCalibrationQuery : public FRHIResource
{
public:
	uint64 GPUMicroseconds[MAX_NUM_GPUS] = {};
	uint64 CPUMicroseconds[MAX_NUM_GPUS] = {};
};

/*
* Generic GPU fence class.
* Granularity differs depending on backing RHI - ie it may only represent command buffer granularity.
* RHI specific fences derive from this to implement real GPU->CPU fencing.
* The default implementation always returns false for Poll until the next frame from the frame the fence was inserted
* because not all APIs have a GPU/CPU sync object, we need to fake it.
*/
class RHI_API FRHIGPUFence : public FRHIResource
{
public:
	FRHIGPUFence(FName InName) : FenceName(InName) {}
	virtual ~FRHIGPUFence() {}

	virtual void Clear() = 0;

	/**
	 * Poll the fence to see if the GPU has signaled it.
	 * @returns True if and only if the GPU fence has been inserted and the GPU has signaled the fence.
	 */
	virtual bool Poll() const = 0;

	/**
	 * Poll on a subset of the GPUs that this fence supports.
	 */
	virtual bool Poll(FRHIGPUMask GPUMask) const { return Poll(); }

	const FName& GetFName() const { return FenceName; }

	FThreadSafeCounter NumPendingWriteCommands;

protected:
	FName FenceName;
};

// Generic implementation of FRHIGPUFence
class RHI_API FGenericRHIGPUFence : public FRHIGPUFence
{
public:
	FGenericRHIGPUFence(FName InName);

	virtual void Clear() final override;

	/** @discussion RHI implementations must be thread-safe and must correctly handle being called before RHIInsertFence if an RHI thread is active. */
	virtual bool Poll() const final override;

	void WriteInternal();

private:
	uint32 InsertedFrameNumber;
};

class FRHIRenderQuery : public FRHIResource {};

class FRHIRenderQueryPool;
class RHI_API FRHIPooledRenderQuery
{
	TRefCountPtr<FRHIRenderQuery> Query;
	FRHIRenderQueryPool* QueryPool = nullptr;

public:
	FRHIPooledRenderQuery() = default;
	FRHIPooledRenderQuery(FRHIRenderQueryPool* InQueryPool, TRefCountPtr<FRHIRenderQuery>&& InQuery);
	~FRHIPooledRenderQuery();

	FRHIPooledRenderQuery(const FRHIPooledRenderQuery&) = delete;
	FRHIPooledRenderQuery& operator=(const FRHIPooledRenderQuery&) = delete;
	FRHIPooledRenderQuery(FRHIPooledRenderQuery&&) = default;
	FRHIPooledRenderQuery& operator=(FRHIPooledRenderQuery&&) = default;

	bool IsValid() const
	{
		return Query.IsValid();
	}

	FRHIRenderQuery* GetQuery() const
	{
		return Query;
	}

	void ReleaseQuery();
};

class RHI_API FRHIRenderQueryPool : public FRHIResource
{
public:
	virtual ~FRHIRenderQueryPool() {};
	virtual FRHIPooledRenderQuery AllocateQuery() = 0;

private:
	friend class FRHIPooledRenderQuery;
	virtual void ReleaseQuery(TRefCountPtr<FRHIRenderQuery>&& Query) = 0;
};

inline FRHIPooledRenderQuery::FRHIPooledRenderQuery(FRHIRenderQueryPool* InQueryPool, TRefCountPtr<FRHIRenderQuery>&& InQuery) 
	: Query(MoveTemp(InQuery))
	, QueryPool(InQueryPool)
{
	check(IsInRenderingThread());
}

inline void FRHIPooledRenderQuery::ReleaseQuery()
{
	if (QueryPool && Query.IsValid())
	{
		QueryPool->ReleaseQuery(MoveTemp(Query));
		QueryPool = nullptr;
	}
	check(!Query.IsValid());
}

inline FRHIPooledRenderQuery::~FRHIPooledRenderQuery()
{
	check(IsInRenderingThread());
	ReleaseQuery();
}

class FRHIComputeFence final : public FRHIResource
{
public:

	FRHIComputeFence(FName InName)
		: Name(InName)
	{}

	FORCEINLINE FName GetName() const
	{
		return Name;
	}

	FORCEINLINE bool GetWriteEnqueued() const
	{
		return Transition != nullptr;
	}

private:
	//debug name of the label.
	FName Name;

public:
	const FRHITransition* Transition = nullptr;
};

class FRHIViewport : public FRHIResource 
{
public:
	/**
	 * Returns access to the platform-specific native resource pointer.  This is designed to be used to provide plugins with access
	 * to the underlying resource and should be used very carefully or not at all.
	 *
	 * @return	The pointer to the native resource or NULL if it not initialized or not supported for this resource type for some reason
	 */
	virtual void* GetNativeSwapChain() const { return nullptr; }
	/**
	 * Returns access to the platform-specific native resource pointer to a backbuffer texture.  This is designed to be used to provide plugins with access
	 * to the underlying resource and should be used very carefully or not at all.
	 *
	 * @return	The pointer to the native resource or NULL if it not initialized or not supported for this resource type for some reason
	 */
	virtual void* GetNativeBackBufferTexture() const { return nullptr; }
	/**
	 * Returns access to the platform-specific native resource pointer to a backbuffer rendertarget. This is designed to be used to provide plugins with access
	 * to the underlying resource and should be used very carefully or not at all.
	 *
	 * @return	The pointer to the native resource or NULL if it not initialized or not supported for this resource type for some reason
	 */
	virtual void* GetNativeBackBufferRT() const { return nullptr; }

	/**
	 * Returns access to the platform-specific native window. This is designed to be used to provide plugins with access
	 * to the underlying resource and should be used very carefully or not at all. 
	 *
	 * @return	The pointer to the native resource or NULL if it not initialized or not supported for this resource type for some reason.
	 * AddParam could represent any additional platform-specific data (could be null).
	 */
	virtual void* GetNativeWindow(void** AddParam = nullptr) const { return nullptr; }

	/**
	 * Sets custom Present handler on the viewport
	 */
	virtual void SetCustomPresent(class FRHICustomPresent*) {}

	/**
	 * Returns currently set custom present handler.
	 */
	virtual class FRHICustomPresent* GetCustomPresent() const { return nullptr; }


	/**
	 * Ticks the viewport on the Game thread
	 */
	virtual void Tick(float DeltaTime) {}

	virtual void WaitForFrameEventCompletion() { }

	virtual void IssueFrameEvent() { }
};

//
// Views
//

class FRHIUnorderedAccessView : public FRHIResource
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FUnorderedAccessView
#endif
{};

class FRHIShaderResourceView : public FRHIResource 
#if ENABLE_RHI_VALIDATION
	, public RHIValidation::FShaderResourceView
#endif
{};


typedef TRefCountPtr<FRHISamplerState> FSamplerStateRHIRef;
typedef TRefCountPtr<FRHIRasterizerState> FRasterizerStateRHIRef;
typedef TRefCountPtr<FRHIDepthStencilState> FDepthStencilStateRHIRef;
typedef TRefCountPtr<FRHIBlendState> FBlendStateRHIRef;
typedef TRefCountPtr<FRHIVertexDeclaration> FVertexDeclarationRHIRef;
typedef TRefCountPtr<FRHIVertexShader> FVertexShaderRHIRef;
typedef TRefCountPtr<FRHIHullShader> FHullShaderRHIRef;
typedef TRefCountPtr<FRHIDomainShader> FDomainShaderRHIRef;
typedef TRefCountPtr<FRHIPixelShader> FPixelShaderRHIRef;
typedef TRefCountPtr<FRHIGeometryShader> FGeometryShaderRHIRef;
typedef TRefCountPtr<FRHIComputeShader> FComputeShaderRHIRef;
typedef TRefCountPtr<FRHIRayTracingShader>          FRayTracingShaderRHIRef;
typedef TRefCountPtr<FRHIComputeFence>	FComputeFenceRHIRef;
typedef TRefCountPtr<FRHIBoundShaderState> FBoundShaderStateRHIRef;
typedef TRefCountPtr<FRHIUniformBuffer> FUniformBufferRHIRef;
typedef TRefCountPtr<FRHIIndexBuffer> FIndexBufferRHIRef;
typedef TRefCountPtr<FRHIVertexBuffer> FVertexBufferRHIRef;
typedef TRefCountPtr<FRHIStructuredBuffer> FStructuredBufferRHIRef;
typedef TRefCountPtr<FRHITexture> FTextureRHIRef;
typedef TRefCountPtr<FRHITexture2D> FTexture2DRHIRef;
typedef TRefCountPtr<FRHITexture2DArray> FTexture2DArrayRHIRef;
typedef TRefCountPtr<FRHITexture3D> FTexture3DRHIRef;
typedef TRefCountPtr<FRHITextureCube> FTextureCubeRHIRef;
typedef TRefCountPtr<FRHITextureReference> FTextureReferenceRHIRef;
typedef TRefCountPtr<FRHIRenderQuery> FRenderQueryRHIRef;
typedef TRefCountPtr<FRHIRenderQueryPool> FRenderQueryPoolRHIRef;
typedef TRefCountPtr<FRHITimestampCalibrationQuery> FTimestampCalibrationQueryRHIRef;
typedef TRefCountPtr<FRHIGPUFence>	FGPUFenceRHIRef;
typedef TRefCountPtr<FRHIViewport> FViewportRHIRef;
typedef TRefCountPtr<FRHIUnorderedAccessView> FUnorderedAccessViewRHIRef;
typedef TRefCountPtr<FRHIShaderResourceView> FShaderResourceViewRHIRef;
typedef TRefCountPtr<FRHIGraphicsPipelineState> FGraphicsPipelineStateRHIRef;
typedef TRefCountPtr<FRHIRayTracingPipelineState> FRayTracingPipelineStateRHIRef;


//
// Ray tracing resources
//

/** Bottom level ray tracing acceleration structure (contains triangles). */
class FRHIRayTracingGeometry : public FRHIResource {};

typedef TRefCountPtr<FRHIRayTracingGeometry>     FRayTracingGeometryRHIRef;

/** Top level ray tracing acceleration structure (contains instances of meshes). */
class FRHIRayTracingScene : public FRHIResource
{
public:
	FRHIShaderResourceView* GetShaderResourceView() { return ShaderResourceView; }
protected:
	FShaderResourceViewRHIRef ShaderResourceView;
};

typedef TRefCountPtr<FRHIRayTracingScene>        FRayTracingSceneRHIRef;


/* Generic staging buffer class used by FRHIGPUMemoryReadback
* RHI specific staging buffers derive from this
*/
class RHI_API FRHIStagingBuffer : public FRHIResource
{
public:
	FRHIStagingBuffer()
		: bIsLocked(false)
	{}

	virtual ~FRHIStagingBuffer() {}

	virtual void *Lock(uint32 Offset, uint32 NumBytes) = 0;
	virtual void Unlock() = 0;
protected:
	bool bIsLocked;
};

class RHI_API FGenericRHIStagingBuffer : public FRHIStagingBuffer
{
public:
	FGenericRHIStagingBuffer()
		: FRHIStagingBuffer()
	{}

	~FGenericRHIStagingBuffer() {}

	virtual void* Lock(uint32 Offset, uint32 NumBytes) final override;
	virtual void Unlock() final override;
	FVertexBufferRHIRef ShadowBuffer;
	uint32 Offset;
};

typedef TRefCountPtr<FRHIStagingBuffer>	FStagingBufferRHIRef;

class FRHIRenderTargetView
{
public:
	FRHITexture* Texture;
	uint32 MipIndex;

	/** Array slice or texture cube face.  Only valid if texture resource was created with TexCreate_TargetArraySlicesIndependently! */
	uint32 ArraySliceIndex;
	
	ERenderTargetLoadAction LoadAction;
	ERenderTargetStoreAction StoreAction;

	FRHIRenderTargetView() : 
		Texture(NULL),
		MipIndex(0),
		ArraySliceIndex(-1),
		LoadAction(ERenderTargetLoadAction::ENoAction),
		StoreAction(ERenderTargetStoreAction::ENoAction)
	{}

	FRHIRenderTargetView(const FRHIRenderTargetView& Other) :
		Texture(Other.Texture),
		MipIndex(Other.MipIndex),
		ArraySliceIndex(Other.ArraySliceIndex),
		LoadAction(Other.LoadAction),
		StoreAction(Other.StoreAction)
	{}

	//common case
	explicit FRHIRenderTargetView(FRHITexture* InTexture, ERenderTargetLoadAction InLoadAction) :
		Texture(InTexture),
		MipIndex(0),
		ArraySliceIndex(-1),
		LoadAction(InLoadAction),
		StoreAction(ERenderTargetStoreAction::EStore)
	{}

	//common case
	explicit FRHIRenderTargetView(FRHITexture* InTexture, ERenderTargetLoadAction InLoadAction, uint32 InMipIndex, uint32 InArraySliceIndex) :
		Texture(InTexture),
		MipIndex(InMipIndex),
		ArraySliceIndex(InArraySliceIndex),
		LoadAction(InLoadAction),
		StoreAction(ERenderTargetStoreAction::EStore)
	{}
	
	explicit FRHIRenderTargetView(FRHITexture* InTexture, uint32 InMipIndex, uint32 InArraySliceIndex, ERenderTargetLoadAction InLoadAction, ERenderTargetStoreAction InStoreAction) :
		Texture(InTexture),
		MipIndex(InMipIndex),
		ArraySliceIndex(InArraySliceIndex),
		LoadAction(InLoadAction),
		StoreAction(InStoreAction)
	{}

	bool operator==(const FRHIRenderTargetView& Other) const
	{
		return 
			Texture == Other.Texture &&
			MipIndex == Other.MipIndex &&
			ArraySliceIndex == Other.ArraySliceIndex &&
			LoadAction == Other.LoadAction &&
			StoreAction == Other.StoreAction;
	}
};

class FRHIDepthRenderTargetView
{
public:
	FRHITexture* Texture;

	ERenderTargetLoadAction		DepthLoadAction;
	ERenderTargetStoreAction	DepthStoreAction;
	ERenderTargetLoadAction		StencilLoadAction;

private:
	ERenderTargetStoreAction	StencilStoreAction;
	FExclusiveDepthStencil		DepthStencilAccess;
public:

	// accessor to prevent write access to StencilStoreAction
	ERenderTargetStoreAction GetStencilStoreAction() const { return StencilStoreAction; }
	// accessor to prevent write access to DepthStencilAccess
	FExclusiveDepthStencil GetDepthStencilAccess() const { return DepthStencilAccess; }

	explicit FRHIDepthRenderTargetView() :
		Texture(nullptr),
		DepthLoadAction(ERenderTargetLoadAction::ENoAction),
		DepthStoreAction(ERenderTargetStoreAction::ENoAction),
		StencilLoadAction(ERenderTargetLoadAction::ENoAction),
		StencilStoreAction(ERenderTargetStoreAction::ENoAction),
		DepthStencilAccess(FExclusiveDepthStencil::DepthNop_StencilNop)
	{
		Validate();
	}

	//common case
	explicit FRHIDepthRenderTargetView(FRHITexture* InTexture, ERenderTargetLoadAction InLoadAction, ERenderTargetStoreAction InStoreAction) :
		Texture(InTexture),
		DepthLoadAction(InLoadAction),
		DepthStoreAction(InStoreAction),
		StencilLoadAction(InLoadAction),
		StencilStoreAction(InStoreAction),
		DepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		Validate();
	}

	explicit FRHIDepthRenderTargetView(FRHITexture* InTexture, ERenderTargetLoadAction InLoadAction, ERenderTargetStoreAction InStoreAction, FExclusiveDepthStencil InDepthStencilAccess) :
		Texture(InTexture),
		DepthLoadAction(InLoadAction),
		DepthStoreAction(InStoreAction),
		StencilLoadAction(InLoadAction),
		StencilStoreAction(InStoreAction),
		DepthStencilAccess(InDepthStencilAccess)
	{
		Validate();
	}

	explicit FRHIDepthRenderTargetView(FRHITexture* InTexture, ERenderTargetLoadAction InDepthLoadAction, ERenderTargetStoreAction InDepthStoreAction, ERenderTargetLoadAction InStencilLoadAction, ERenderTargetStoreAction InStencilStoreAction) :
		Texture(InTexture),
		DepthLoadAction(InDepthLoadAction),
		DepthStoreAction(InDepthStoreAction),
		StencilLoadAction(InStencilLoadAction),
		StencilStoreAction(InStencilStoreAction),
		DepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		Validate();
	}

	explicit FRHIDepthRenderTargetView(FRHITexture* InTexture, ERenderTargetLoadAction InDepthLoadAction, ERenderTargetStoreAction InDepthStoreAction, ERenderTargetLoadAction InStencilLoadAction, ERenderTargetStoreAction InStencilStoreAction, FExclusiveDepthStencil InDepthStencilAccess) :
		Texture(InTexture),
		DepthLoadAction(InDepthLoadAction),
		DepthStoreAction(InDepthStoreAction),
		StencilLoadAction(InStencilLoadAction),
		StencilStoreAction(InStencilStoreAction),
		DepthStencilAccess(InDepthStencilAccess)
	{
		Validate();
	}

	void Validate() const
	{
		// VK and Metal MAY leave the attachment in an undefined state if the StoreAction is DontCare. So we can't assume read-only implies it should be DontCare unless we know for sure it will never be used again.
		// ensureMsgf(DepthStencilAccess.IsDepthWrite() || DepthStoreAction == ERenderTargetStoreAction::ENoAction, TEXT("Depth is read-only, but we are performing a store.  This is a waste on mobile.  If depth can't change, we don't need to store it out again"));
		/*ensureMsgf(DepthStencilAccess.IsStencilWrite() || StencilStoreAction == ERenderTargetStoreAction::ENoAction, TEXT("Stencil is read-only, but we are performing a store.  This is a waste on mobile.  If stencil can't change, we don't need to store it out again"));*/
	}

	bool operator==(const FRHIDepthRenderTargetView& Other) const
	{
		return
			Texture == Other.Texture &&
			DepthLoadAction == Other.DepthLoadAction &&
			DepthStoreAction == Other.DepthStoreAction &&
			StencilLoadAction == Other.StencilLoadAction &&
			StencilStoreAction == Other.StencilStoreAction &&
			DepthStencilAccess == Other.DepthStencilAccess;
	}
};

class FRHISetRenderTargetsInfo
{
public:
	// Color Render Targets Info
	FRHIRenderTargetView ColorRenderTarget[MaxSimultaneousRenderTargets];	
	int32 NumColorRenderTargets;
	bool bClearColor;

	// Color Render Targets Info
	FRHIRenderTargetView ColorResolveRenderTarget[MaxSimultaneousRenderTargets];	
	bool bHasResolveAttachments;

	// Depth/Stencil Render Target Info
	FRHIDepthRenderTargetView DepthStencilRenderTarget;	
	bool bClearDepth;
	bool bClearStencil;

	FRHITexture* ShadingRateTexture;
	EVRSRateCombiner ShadingRateTextureCombiner;

	uint8 MultiViewCount;

	FRHISetRenderTargetsInfo() :
		NumColorRenderTargets(0),
		bClearColor(false),
		bHasResolveAttachments(false),
		bClearDepth(false),
		ShadingRateTexture(nullptr),
		MultiViewCount(0)
	{}

	FRHISetRenderTargetsInfo(int32 InNumColorRenderTargets, const FRHIRenderTargetView* InColorRenderTargets, const FRHIDepthRenderTargetView& InDepthStencilRenderTarget) :
		NumColorRenderTargets(InNumColorRenderTargets),
		bClearColor(InNumColorRenderTargets > 0 && InColorRenderTargets[0].LoadAction == ERenderTargetLoadAction::EClear),
		bHasResolveAttachments(false),
		DepthStencilRenderTarget(InDepthStencilRenderTarget),		
		bClearDepth(InDepthStencilRenderTarget.Texture && InDepthStencilRenderTarget.DepthLoadAction == ERenderTargetLoadAction::EClear),
		ShadingRateTexture(nullptr),
		ShadingRateTextureCombiner(VRSRB_Passthrough)
	{
		check(InNumColorRenderTargets <= 0 || InColorRenderTargets);
		for (int32 Index = 0; Index < InNumColorRenderTargets; ++Index)
		{
			ColorRenderTarget[Index] = InColorRenderTargets[Index];			
		}
	}
	// @todo metal mrt: This can go away after all the cleanup is done
	void SetClearDepthStencil(bool bInClearDepth, bool bInClearStencil = false)
	{
		if (bInClearDepth)
		{
			DepthStencilRenderTarget.DepthLoadAction = ERenderTargetLoadAction::EClear;
		}
		if (bInClearStencil)
		{
			DepthStencilRenderTarget.StencilLoadAction = ERenderTargetLoadAction::EClear;
		}
		bClearDepth = bInClearDepth;		
		bClearStencil = bInClearStencil;		
	}

	uint32 CalculateHash() const
	{
		// Need a separate struct so we can memzero/remove dependencies on reference counts
		struct FHashableStruct
		{
			// *2 for color and resolves, depth goes in the second-to-last slot, shading rate goes in the last slot
			FRHITexture* Texture[MaxSimultaneousRenderTargets*2 + 2];
			uint32 MipIndex[MaxSimultaneousRenderTargets];
			uint32 ArraySliceIndex[MaxSimultaneousRenderTargets];
			ERenderTargetLoadAction LoadAction[MaxSimultaneousRenderTargets];
			ERenderTargetStoreAction StoreAction[MaxSimultaneousRenderTargets];

			ERenderTargetLoadAction		DepthLoadAction;
			ERenderTargetStoreAction	DepthStoreAction;
			ERenderTargetLoadAction		StencilLoadAction;
			ERenderTargetStoreAction	StencilStoreAction;
			FExclusiveDepthStencil		DepthStencilAccess;

			bool bClearDepth;
			bool bClearStencil;
			bool bClearColor;
			bool bHasResolveAttachments;
			FRHIUnorderedAccessView* UnorderedAccessView[MaxSimultaneousUAVs];
			uint8 MultiViewCount;

			void Set(const FRHISetRenderTargetsInfo& RTInfo)
			{
				FMemory::Memzero(*this);
				for (int32 Index = 0; Index < RTInfo.NumColorRenderTargets; ++Index)
				{
					Texture[Index] = RTInfo.ColorRenderTarget[Index].Texture;
					Texture[MaxSimultaneousRenderTargets+Index] = RTInfo.ColorResolveRenderTarget[Index].Texture;
					MipIndex[Index] = RTInfo.ColorRenderTarget[Index].MipIndex;
					ArraySliceIndex[Index] = RTInfo.ColorRenderTarget[Index].ArraySliceIndex;
					LoadAction[Index] = RTInfo.ColorRenderTarget[Index].LoadAction;
					StoreAction[Index] = RTInfo.ColorRenderTarget[Index].StoreAction;
				}

				Texture[MaxSimultaneousRenderTargets] = RTInfo.DepthStencilRenderTarget.Texture;
				Texture[MaxSimultaneousRenderTargets + 1] = RTInfo.ShadingRateTexture;
				DepthLoadAction = RTInfo.DepthStencilRenderTarget.DepthLoadAction;
				DepthStoreAction = RTInfo.DepthStencilRenderTarget.DepthStoreAction;
				StencilLoadAction = RTInfo.DepthStencilRenderTarget.StencilLoadAction;
				StencilStoreAction = RTInfo.DepthStencilRenderTarget.GetStencilStoreAction();
				DepthStencilAccess = RTInfo.DepthStencilRenderTarget.GetDepthStencilAccess();

				bClearDepth = RTInfo.bClearDepth;
				bClearStencil = RTInfo.bClearStencil;
				bClearColor = RTInfo.bClearColor;
				bHasResolveAttachments = RTInfo.bHasResolveAttachments;
				MultiViewCount = RTInfo.MultiViewCount;
			}
		};

		FHashableStruct RTHash;
		FMemory::Memzero(RTHash);
		RTHash.Set(*this);
		return FCrc::MemCrc32(&RTHash, sizeof(RTHash));
	}
};

class FRHICustomPresent : public FRHIResource
{
public:
	FRHICustomPresent() {}
	
	virtual ~FRHICustomPresent() {} // should release any references to D3D resources.
	
	// Called when viewport is resized.
	virtual void OnBackBufferResize() = 0;

	// Called from render thread to see if a native present will be requested for this frame.
	// @return	true if native Present will be requested for this frame; false otherwise.  Must
	// match value subsequently returned by Present for this frame.
	virtual bool NeedsNativePresent() = 0;
	// In come cases we want to use custom present but still let the native environment handle 
	// advancement of the backbuffer indices.
	// @return true if backbuffer index should advance independently from CustomPresent.
	virtual bool NeedsAdvanceBackbuffer() { return false; };

	// Called from RHI thread when the engine begins drawing to the viewport.
	virtual void BeginDrawing() {};

	// Called from RHI thread to perform custom present.
	// @param InOutSyncInterval - in out param, indicates if vsync is on (>0) or off (==0).
	// @return	true if native Present should be also be performed; false otherwise. If it returns
	// true, then InOutSyncInterval could be modified to switch between VSync/NoVSync for the normal 
	// Present.  Must match value previously returned by NeedsNormalPresent for this frame.
	virtual bool Present(int32& InOutSyncInterval) = 0;

	// Called from RHI thread after native Present has been called
	virtual void PostPresent() {};

	// Called when rendering thread is acquired
	virtual void OnAcquireThreadOwnership() {}
	// Called when rendering thread is released
	virtual void OnReleaseThreadOwnership() {}
};


typedef TRefCountPtr<FRHICustomPresent> FCustomPresentRHIRef;

// Template magic to convert an FRHI*Shader to its enum
template<typename TRHIShader> struct TRHIShaderToEnum {};
template<> struct TRHIShaderToEnum<FRHIVertexShader>		{ enum { ShaderFrequency = SF_Vertex }; };
template<> struct TRHIShaderToEnum<FRHIHullShader>			{ enum { ShaderFrequency = SF_Hull }; };
template<> struct TRHIShaderToEnum<FRHIDomainShader>		{ enum { ShaderFrequency = SF_Domain }; };
template<> struct TRHIShaderToEnum<FRHIPixelShader>			{ enum { ShaderFrequency = SF_Pixel }; };
template<> struct TRHIShaderToEnum<FRHIGeometryShader>		{ enum { ShaderFrequency = SF_Geometry }; };
template<> struct TRHIShaderToEnum<FRHIComputeShader>		{ enum { ShaderFrequency = SF_Compute }; };
template<> struct TRHIShaderToEnum<FRHIVertexShader*>		{ enum { ShaderFrequency = SF_Vertex }; };
template<> struct TRHIShaderToEnum<FRHIHullShader*>			{ enum { ShaderFrequency = SF_Hull }; };
template<> struct TRHIShaderToEnum<FRHIDomainShader*>		{ enum { ShaderFrequency = SF_Domain }; };
template<> struct TRHIShaderToEnum<FRHIPixelShader*>		{ enum { ShaderFrequency = SF_Pixel }; };
template<> struct TRHIShaderToEnum<FRHIGeometryShader*>		{ enum { ShaderFrequency = SF_Geometry }; };
template<> struct TRHIShaderToEnum<FRHIComputeShader*>		{ enum { ShaderFrequency = SF_Compute }; };
template<> struct TRHIShaderToEnum<FVertexShaderRHIRef>		{ enum { ShaderFrequency = SF_Vertex }; };
template<> struct TRHIShaderToEnum<FHullShaderRHIRef>		{ enum { ShaderFrequency = SF_Hull }; };
template<> struct TRHIShaderToEnum<FDomainShaderRHIRef>		{ enum { ShaderFrequency = SF_Domain }; };
template<> struct TRHIShaderToEnum<FPixelShaderRHIRef>		{ enum { ShaderFrequency = SF_Pixel }; };
template<> struct TRHIShaderToEnum<FGeometryShaderRHIRef>	{ enum { ShaderFrequency = SF_Geometry }; };
template<> struct TRHIShaderToEnum<FComputeShaderRHIRef>	{ enum { ShaderFrequency = SF_Compute }; };

struct FBoundShaderStateInput
{
	inline FBoundShaderStateInput() {}

	inline FBoundShaderStateInput
	(
		FRHIVertexDeclaration* InVertexDeclarationRHI
		, FRHIVertexShader* InVertexShaderRHI
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		, FRHIHullShader* InHullShaderRHI
		, FRHIDomainShader* InDomainShaderRHI
#endif
		, FRHIPixelShader* InPixelShaderRHI
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		, FRHIGeometryShader* InGeometryShaderRHI
#endif
	)
		: VertexDeclarationRHI(InVertexDeclarationRHI)
		, VertexShaderRHI(InVertexShaderRHI)
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		, HullShaderRHI(InHullShaderRHI)
		, DomainShaderRHI(InDomainShaderRHI)
#endif
		, PixelShaderRHI(InPixelShaderRHI)
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		, GeometryShaderRHI(InGeometryShaderRHI)
#endif
	{
	}

	void AddRefResources()
	{
		check(VertexDeclarationRHI);
		VertexDeclarationRHI->AddRef();

		check(VertexShaderRHI);
		VertexShaderRHI->AddRef();

		if (HullShaderRHI)
		{
			HullShaderRHI->AddRef();
		}

		if (DomainShaderRHI)
		{
			DomainShaderRHI->AddRef();
		}

		if (PixelShaderRHI)
		{
			PixelShaderRHI->AddRef();
		}

		if (GeometryShaderRHI)
		{
			GeometryShaderRHI->AddRef();
		}
	}

	void ReleaseResources()
	{
		check(VertexDeclarationRHI);
		VertexDeclarationRHI->Release();

		check(VertexShaderRHI);
		VertexShaderRHI->Release();

		if (HullShaderRHI)
		{
			HullShaderRHI->Release();
		}

		if (DomainShaderRHI)
		{
			DomainShaderRHI->Release();
		}

		if (PixelShaderRHI)
		{
			PixelShaderRHI->Release();
		}

		if (GeometryShaderRHI)
		{
			GeometryShaderRHI->Release();
		}
	}

	FRHIVertexDeclaration* VertexDeclarationRHI = nullptr;
	FRHIVertexShader* VertexShaderRHI = nullptr;
	FRHIHullShader* HullShaderRHI = nullptr;
	FRHIDomainShader* DomainShaderRHI = nullptr;
	FRHIPixelShader* PixelShaderRHI = nullptr;
	FRHIGeometryShader* GeometryShaderRHI = nullptr;
};

struct FImmutableSamplerState
{
	using TImmutableSamplers = TStaticArray<FRHISamplerState*, MaxImmutableSamplers>;

	FImmutableSamplerState()
		: ImmutableSamplers(nullptr)
	{}

	void Reset()
	{
		for (uint32 Index = 0; Index < MaxImmutableSamplers; ++Index)
		{
			ImmutableSamplers[Index] = nullptr;
		}
	}

	bool operator==(const FImmutableSamplerState& rhs) const
	{
		return ImmutableSamplers == rhs.ImmutableSamplers;
	}

	bool operator!=(const FImmutableSamplerState& rhs) const
	{
		return ImmutableSamplers != rhs.ImmutableSamplers;
	}

	TImmutableSamplers ImmutableSamplers;
};

// Hints for some RHIs that support subpasses
enum class ESubpassHint : uint8
{
	// Regular rendering
	None,

	// Render pass has depth reading subpass
	DepthReadSubpass,

	// Mobile defferred shading subpass
	DeferredShadingSubpass,
};

class FGraphicsPipelineStateInitializer
{
public:
	// Can't use TEnumByte<EPixelFormat> as it changes the struct to be non trivially constructible, breaking memset
	using TRenderTargetFormats		= TStaticArray<uint8/*EPixelFormat*/, MaxSimultaneousRenderTargets>;
	using TRenderTargetFlags		= TStaticArray<uint32/*ETextureCreateFlags*/, MaxSimultaneousRenderTargets>;

	FGraphicsPipelineStateInitializer()
		: BlendState(nullptr)
		, RasterizerState(nullptr)
		, DepthStencilState(nullptr)
		, RenderTargetsEnabled(0)
		, RenderTargetFormats(PF_Unknown)
		, RenderTargetFlags(0)
		, DepthStencilTargetFormat(PF_Unknown)
		, DepthStencilTargetFlag(0)
		, DepthTargetLoadAction(ERenderTargetLoadAction::ENoAction)
		, DepthTargetStoreAction(ERenderTargetStoreAction::ENoAction)
		, StencilTargetLoadAction(ERenderTargetLoadAction::ENoAction)
		, StencilTargetStoreAction(ERenderTargetStoreAction::ENoAction)
		, NumSamples(0)
		, SubpassHint(ESubpassHint::None)
		, SubpassIndex(0)
		, bDepthBounds(false)
		, MultiViewCount(0)
		, bHasFragmentDensityAttachment(false)
		, ShadingRate(EVRSShadingRate::VRSSR_1x1)
		, Flags(0)
	{
#if PLATFORM_WINDOWS
		static_assert(sizeof(TRenderTargetFormats::ElementType) == sizeof(uint8/*EPixelFormat*/), "Change TRenderTargetFormats's uint8 to EPixelFormat's size!");
		static_assert(sizeof(TRenderTargetFlags::ElementType) == sizeof(uint32/*ETextureCreateFlags*/), "Change TRenderTargetFlags's uint32 to ETextureCreateFlags's size!");
#endif
		static_assert(PF_MAX < MAX_uint8, "TRenderTargetFormats assumes EPixelFormat can fit in a uint8!");
	}

	FGraphicsPipelineStateInitializer(
		FBoundShaderStateInput		InBoundShaderState,
		FRHIBlendState*				InBlendState,
		FRHIRasterizerState*		InRasterizerState,
		FRHIDepthStencilState*		InDepthStencilState,
		FImmutableSamplerState		InImmutableSamplerState,
		EPrimitiveType				InPrimitiveType,
		uint32						InRenderTargetsEnabled,
		const TRenderTargetFormats&	InRenderTargetFormats,
		const TRenderTargetFlags&	InRenderTargetFlags,
		EPixelFormat				InDepthStencilTargetFormat,
		ETextureCreateFlags			InDepthStencilTargetFlag,
		ERenderTargetLoadAction		InDepthTargetLoadAction,
		ERenderTargetStoreAction	InDepthTargetStoreAction,
		ERenderTargetLoadAction		InStencilTargetLoadAction,
		ERenderTargetStoreAction	InStencilTargetStoreAction,
		FExclusiveDepthStencil		InDepthStencilAccess,
		uint32						InNumSamples,
		ESubpassHint				InSubpassHint,
		uint8						InSubpassIndex,
		uint16						InFlags,
		bool						bInDepthBounds,
		uint8						InMultiViewCount,
		bool						bInHasFragmentDensityAttachment,
		EVRSShadingRate				InShadingRate)
		: BoundShaderState(InBoundShaderState)
		, BlendState(InBlendState)
		, RasterizerState(InRasterizerState)
		, DepthStencilState(InDepthStencilState)
		, ImmutableSamplerState(InImmutableSamplerState)
		, PrimitiveType(InPrimitiveType)
		, RenderTargetsEnabled(InRenderTargetsEnabled)
		, RenderTargetFormats(InRenderTargetFormats)
		, RenderTargetFlags(InRenderTargetFlags)
		, DepthStencilTargetFormat(InDepthStencilTargetFormat)
		, DepthStencilTargetFlag(InDepthStencilTargetFlag)
		, DepthTargetLoadAction(InDepthTargetLoadAction)
		, DepthTargetStoreAction(InDepthTargetStoreAction)
		, StencilTargetLoadAction(InStencilTargetLoadAction)
		, StencilTargetStoreAction(InStencilTargetStoreAction)
		, DepthStencilAccess(InDepthStencilAccess)
		, NumSamples(InNumSamples)
		, SubpassHint(InSubpassHint)
		, SubpassIndex(InSubpassIndex)
		, bDepthBounds(bInDepthBounds)
		, MultiViewCount(InMultiViewCount)
		, bHasFragmentDensityAttachment(bInHasFragmentDensityAttachment)
		, ShadingRate(InShadingRate)
		, Flags(InFlags)
	{
	}

	bool operator==(const FGraphicsPipelineStateInitializer& rhs) const
	{
		if (BoundShaderState.VertexDeclarationRHI != rhs.BoundShaderState.VertexDeclarationRHI ||
			BoundShaderState.VertexShaderRHI != rhs.BoundShaderState.VertexShaderRHI ||
			BoundShaderState.PixelShaderRHI != rhs.BoundShaderState.PixelShaderRHI ||
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			BoundShaderState.GeometryShaderRHI != rhs.BoundShaderState.GeometryShaderRHI ||
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			BoundShaderState.DomainShaderRHI != rhs.BoundShaderState.DomainShaderRHI ||
			BoundShaderState.HullShaderRHI != rhs.BoundShaderState.HullShaderRHI ||
#endif
			BlendState != rhs.BlendState ||
			RasterizerState != rhs.RasterizerState ||
			DepthStencilState != rhs.DepthStencilState ||
			ImmutableSamplerState != rhs.ImmutableSamplerState ||
			PrimitiveType != rhs.PrimitiveType ||
			bDepthBounds != rhs.bDepthBounds ||
			MultiViewCount != rhs.MultiViewCount ||
			ShadingRate != rhs.ShadingRate ||
			bHasFragmentDensityAttachment != rhs.bHasFragmentDensityAttachment ||
			RenderTargetsEnabled != rhs.RenderTargetsEnabled ||
			RenderTargetFormats != rhs.RenderTargetFormats || 
			RenderTargetFlags != rhs.RenderTargetFlags || 
			DepthStencilTargetFormat != rhs.DepthStencilTargetFormat || 
			DepthStencilTargetFlag != rhs.DepthStencilTargetFlag ||
			DepthTargetLoadAction != rhs.DepthTargetLoadAction ||
			DepthTargetStoreAction != rhs.DepthTargetStoreAction ||
			StencilTargetLoadAction != rhs.StencilTargetLoadAction ||
			StencilTargetStoreAction != rhs.StencilTargetStoreAction || 
			DepthStencilAccess != rhs.DepthStencilAccess ||
			NumSamples != rhs.NumSamples ||
			SubpassHint != rhs.SubpassHint ||
			SubpassIndex != rhs.SubpassIndex)
		{
			return false;
		}

		return true;
	}

	uint32 ComputeNumValidRenderTargets() const
	{
		// Get the count of valid render targets (ignore those at the end of the array with PF_Unknown)
		if (RenderTargetsEnabled > 0)
		{
			int32 LastValidTarget = -1;
			for (int32 i = (int32)RenderTargetsEnabled - 1; i >= 0; i--)
			{
				if (RenderTargetFormats[i] != PF_Unknown)
				{
					LastValidTarget = i;
					break;
				}
			}
			return uint32(LastValidTarget + 1);
		}
		return RenderTargetsEnabled;
	}

	FBoundShaderStateInput			BoundShaderState;
	FRHIBlendState*					BlendState;
	FRHIRasterizerState*			RasterizerState;
	FRHIDepthStencilState*			DepthStencilState;
	FImmutableSamplerState			ImmutableSamplerState;

	EPrimitiveType					PrimitiveType;
	uint32							RenderTargetsEnabled;
	TRenderTargetFormats			RenderTargetFormats;
	TRenderTargetFlags				RenderTargetFlags;
	EPixelFormat					DepthStencilTargetFormat;
	uint32							DepthStencilTargetFlag;
	ERenderTargetLoadAction			DepthTargetLoadAction;
	ERenderTargetStoreAction		DepthTargetStoreAction;
	ERenderTargetLoadAction			StencilTargetLoadAction;
	ERenderTargetStoreAction		StencilTargetStoreAction;
	FExclusiveDepthStencil			DepthStencilAccess;
	uint16							NumSamples;
	ESubpassHint					SubpassHint;
	uint8							SubpassIndex;
	bool							bDepthBounds;
	uint8							MultiViewCount;
	bool							bHasFragmentDensityAttachment;
	EVRSShadingRate					ShadingRate;
	
	// Note: these flags do NOT affect compilation of this PSO.
	// The resulting object is invariant with respect to whatever is set here, they are
	// behavior hints.
	// They do not participate in equality comparisons or hashing.
	union
	{
		struct
		{
			uint16					Reserved			: 15;
			uint16					bFromPSOFileCache	: 1;
		};
		uint16						Flags;
	};
};

class FRayTracingPipelineStateSignature
{
public:

	uint32 MaxPayloadSizeInBytes = 24; // sizeof FDefaultPayload declared in RayTracingCommon.ush
	bool bAllowHitGroupIndexing = true;

	// NOTE: GetTypeHash(const FRayTracingPipelineStateInitializer& Initializer) should also be updated when changing this function
	bool operator==(const FRayTracingPipelineStateSignature& rhs) const
	{
		return MaxPayloadSizeInBytes == rhs.MaxPayloadSizeInBytes
			&& bAllowHitGroupIndexing == rhs.bAllowHitGroupIndexing
			&& RayGenHash == rhs.RayGenHash
			&& MissHash == rhs.MissHash
			&& HitGroupHash == rhs.HitGroupHash
			&& CallableHash == rhs.CallableHash;
	}

	friend uint32 GetTypeHash(const FRayTracingPipelineStateSignature& Initializer)
	{
		return GetTypeHash(Initializer.MaxPayloadSizeInBytes) ^
			GetTypeHash(Initializer.bAllowHitGroupIndexing) ^
			GetTypeHash(Initializer.GetRayGenHash()) ^
			GetTypeHash(Initializer.GetRayMissHash()) ^
			GetTypeHash(Initializer.GetHitGroupHash()) ^
			GetTypeHash(Initializer.GetCallableHash());
	}

	uint64 GetHitGroupHash() const { return HitGroupHash; }
	uint64 GetRayGenHash()   const { return RayGenHash; }
	uint64 GetRayMissHash()  const { return MissHash; }
	uint64 GetCallableHash() const { return CallableHash; }

protected:

	uint64 RayGenHash = 0;
	uint64 MissHash = 0;
	uint64 HitGroupHash = 0;
	uint64 CallableHash = 0;
};

class FRayTracingPipelineStateInitializer : public FRayTracingPipelineStateSignature
{
public:

	FRayTracingPipelineStateInitializer() = default;

	// Partial ray tracing pipelines can be used for run-time asynchronous shader compilation, but not for rendering.
	// Any number of shaders for any stage may be provided when creating partial pipelines, but 
	// at least one shader must be present in total (completely empty pipelines are not allowed).
	bool bPartial = false;

	// Ray tracing pipeline may be created by deriving from the existing base.
	// Base pipeline will be extended by adding new shaders into it, potentially saving substantial amount of CPU time.
	// Depends on GRHISupportsRayTracingPSOAdditions support at runtime (base pipeline is simply ignored if it is unsupported).
	FRayTracingPipelineStateRHIRef BasePipeline;

	const TArrayView<FRHIRayTracingShader*>& GetRayGenTable()   const { return RayGenTable; }
	const TArrayView<FRHIRayTracingShader*>& GetMissTable()     const { return MissTable; }
	const TArrayView<FRHIRayTracingShader*>& GetHitGroupTable() const { return HitGroupTable; }
	const TArrayView<FRHIRayTracingShader*>& GetCallableTable() const { return CallableTable; }

	// Shaders used as entry point to ray tracing work. At least one RayGen shader must be provided.
	void SetRayGenShaderTable(const TArrayView<FRHIRayTracingShader*>& InRayGenShaders, uint64 Hash = 0)
	{
		RayGenTable = InRayGenShaders;
		RayGenHash = Hash ? Hash : ComputeShaderTableHash(InRayGenShaders);
	}

	// Shaders that will be invoked if a ray misses all geometry.
	// If this table is empty, then a built-in default miss shader will be used that sets HitT member of FMinimalPayload to -1.
	// Desired miss shader can be selected by providing MissShaderIndex to TraceRay() function.
	void SetMissShaderTable(const TArrayView<FRHIRayTracingShader*>& InMissShaders, uint64 Hash = 0)
	{
		MissTable = InMissShaders;
		MissHash = Hash ? Hash : ComputeShaderTableHash(InMissShaders);
	}

	// Shaders that will be invoked when ray intersects geometry.
	// If this table is empty, then a built-in default shader will be used for all geometry, using FDefaultPayload.
	void SetHitGroupTable(const TArrayView<FRHIRayTracingShader*>& InHitGroups, uint64 Hash = 0)
	{
		HitGroupTable = InHitGroups;
		HitGroupHash = Hash ? Hash : ComputeShaderTableHash(HitGroupTable);
	}

	// Shaders that can be explicitly invoked from RayGen shaders by their Shader Binding Table (SBT) index.
	// SetRayTracingCallableShader() command must be used to fill SBT slots before a shader can be called.
	void SetCallableTable(const TArrayView<FRHIRayTracingShader*>& InCallableShaders, uint64 Hash = 0)
	{
		CallableTable = InCallableShaders;
		CallableHash = Hash ? Hash : ComputeShaderTableHash(CallableTable);
	}

private:

	uint64 ComputeShaderTableHash(const TArrayView<FRHIRayTracingShader*>& ShaderTable, uint64 InitialHash = 5699878132332235837ull)
	{
		uint64 CombinedHash = InitialHash;
		for (FRHIRayTracingShader* ShaderRHI : ShaderTable)
		{
			uint64 ShaderHash; // 64 bits from the shader SHA1
			FMemory::Memcpy(&ShaderHash, ShaderRHI->GetHash().Hash, sizeof(ShaderHash));

			// 64 bit hash combination as per boost::hash_combine_impl
			CombinedHash ^= ShaderHash + 0x9e3779b9 + (CombinedHash << 6) + (CombinedHash >> 2);
		}

		return CombinedHash;
	}

	TArrayView<FRHIRayTracingShader*> RayGenTable;
	TArrayView<FRHIRayTracingShader*> MissTable;
	TArrayView<FRHIRayTracingShader*> HitGroupTable;
	TArrayView<FRHIRayTracingShader*> CallableTable;
};

// This PSO is used as a fallback for RHIs that dont support PSOs. It is used to set the graphics state using the legacy state setting APIs
class FRHIGraphicsPipelineStateFallBack : public FRHIGraphicsPipelineState
{
public:
	FRHIGraphicsPipelineStateFallBack() {}

	FRHIGraphicsPipelineStateFallBack(const FGraphicsPipelineStateInitializer& Init)
		: Initializer(Init)
	{
	}

	FGraphicsPipelineStateInitializer Initializer;
};

class FRHIComputePipelineStateFallback : public FRHIComputePipelineState
{
public:
	FRHIComputePipelineStateFallback(FRHIComputeShader* InComputeShader)
		: ComputeShader(InComputeShader)
	{
		check(InComputeShader);
	}

	FRHIComputeShader* GetComputeShader()
	{
		return ComputeShader;
	}

protected:
	TRefCountPtr<FRHIComputeShader> ComputeShader;
};

//
// Shader Library
//

class FRHIShaderLibrary : public FRHIResource
{
public:
	FRHIShaderLibrary(EShaderPlatform InPlatform, FString const& InName) : Platform(InPlatform), LibraryName(InName), LibraryId(GetTypeHash(InName)) {}
	virtual ~FRHIShaderLibrary() {}
	
	FORCEINLINE EShaderPlatform GetPlatform(void) const { return Platform; }
	FORCEINLINE const FString& GetName(void) const { return LibraryName; }
	FORCEINLINE uint32 GetId(void) const { return LibraryId; }
	
	virtual bool IsNativeLibrary() const = 0;
	virtual int32 GetNumShaderMaps() const = 0;
	virtual int32 GetNumShaders() const = 0;
	virtual int32 GetNumShadersForShaderMap(int32 ShaderMapIndex) const = 0;
	virtual int32 GetShaderIndex(int32 ShaderMapIndex, int32 i) const = 0;
	virtual int32 FindShaderMapIndex(const FSHAHash& Hash) = 0;
	virtual int32 FindShaderIndex(const FSHAHash& Hash) = 0;
	virtual bool PreloadShader(int32 ShaderIndex, FGraphEventArray& OutCompletionEvents) { return false; }
	virtual bool PreloadShaderMap(int32 ShaderMapIndex, FGraphEventArray& OutCompletionEvents) { return false; }
	virtual void ReleasePreloadedShader(int32 ShaderIndex) {}

	virtual TRefCountPtr<FRHIShader> CreateShader(int32 ShaderIndex) { return nullptr; }
	virtual void Teardown() {};

protected:
	EShaderPlatform Platform;
	FString LibraryName;
	uint32 LibraryId;
};

typedef TRefCountPtr<FRHIShaderLibrary>	FRHIShaderLibraryRef;

class FRHIPipelineBinaryLibrary : public FRHIResource
{
public:
	FRHIPipelineBinaryLibrary(EShaderPlatform InPlatform, FString const& FilePath) : Platform(InPlatform) {}
	virtual ~FRHIPipelineBinaryLibrary() {}
	
	FORCEINLINE EShaderPlatform GetPlatform(void) const { return Platform; }
	
protected:
	EShaderPlatform Platform;
};

typedef TRefCountPtr<FRHIPipelineBinaryLibrary>	FRHIPipelineBinaryLibraryRef;

enum class ERenderTargetActions : uint8
{
	LoadOpMask = 2,

#define RTACTION_MAKE_MASK(Load, Store) (((uint8)ERenderTargetLoadAction::Load << (uint8)LoadOpMask) | (uint8)ERenderTargetStoreAction::Store)

	DontLoad_DontStore =	RTACTION_MAKE_MASK(ENoAction, ENoAction),

	DontLoad_Store =		RTACTION_MAKE_MASK(ENoAction, EStore),
	Clear_Store =			RTACTION_MAKE_MASK(EClear, EStore),
	Load_Store =			RTACTION_MAKE_MASK(ELoad, EStore),

	Clear_DontStore =		RTACTION_MAKE_MASK(EClear, ENoAction),
	Load_DontStore =		RTACTION_MAKE_MASK(ELoad, ENoAction),
	Clear_Resolve =			RTACTION_MAKE_MASK(EClear, EMultisampleResolve),
	Load_Resolve =			RTACTION_MAKE_MASK(ELoad, EMultisampleResolve),

#undef RTACTION_MAKE_MASK
};

inline ERenderTargetActions MakeRenderTargetActions(ERenderTargetLoadAction Load, ERenderTargetStoreAction Store)
{
	return (ERenderTargetActions)(((uint8)Load << (uint8)ERenderTargetActions::LoadOpMask) | (uint8)Store);
}

inline ERenderTargetLoadAction GetLoadAction(ERenderTargetActions Action)
{
	return (ERenderTargetLoadAction)((uint8)Action >> (uint8)ERenderTargetActions::LoadOpMask);
}

inline ERenderTargetStoreAction GetStoreAction(ERenderTargetActions Action)
{
	return (ERenderTargetStoreAction)((uint8)Action & ((1 << (uint8)ERenderTargetActions::LoadOpMask) - 1));
}

enum class EDepthStencilTargetActions : uint8
{
	DepthMask = 4,

#define RTACTION_MAKE_MASK(Depth, Stencil) (((uint8)ERenderTargetActions::Depth << (uint8)DepthMask) | (uint8)ERenderTargetActions::Stencil)

	DontLoad_DontStore =						RTACTION_MAKE_MASK(DontLoad_DontStore, DontLoad_DontStore),
	DontLoad_StoreDepthStencil =				RTACTION_MAKE_MASK(DontLoad_Store, DontLoad_Store),
	DontLoad_StoreStencilNotDepth =				RTACTION_MAKE_MASK(DontLoad_DontStore, DontLoad_Store),
	ClearDepthStencil_StoreDepthStencil =		RTACTION_MAKE_MASK(Clear_Store, Clear_Store),
	LoadDepthStencil_StoreDepthStencil =		RTACTION_MAKE_MASK(Load_Store, Load_Store),
	LoadDepthNotStencil_StoreDepthNotStencil =	RTACTION_MAKE_MASK(Load_Store, DontLoad_DontStore),
	LoadDepthNotStencil_DontStore =				RTACTION_MAKE_MASK(Load_DontStore, DontLoad_DontStore),
	LoadDepthStencil_StoreStencilNotDepth =		RTACTION_MAKE_MASK(Load_DontStore, Load_Store),

	ClearDepthStencil_DontStoreDepthStencil =	RTACTION_MAKE_MASK(Clear_DontStore, Clear_DontStore),
	LoadDepthStencil_DontStoreDepthStencil =	RTACTION_MAKE_MASK(Load_DontStore, Load_DontStore),
	ClearDepthStencil_StoreDepthNotStencil =	RTACTION_MAKE_MASK(Clear_Store, Clear_DontStore),
	ClearDepthStencil_StoreStencilNotDepth =	RTACTION_MAKE_MASK(Clear_DontStore, Clear_Store),
	ClearDepthStencil_ResolveDepthNotStencil =	RTACTION_MAKE_MASK(Clear_Resolve, Clear_DontStore),
	ClearDepthStencil_ResolveStencilNotDepth =	RTACTION_MAKE_MASK(Clear_DontStore, Clear_Resolve),
	LoadDepthClearStencil_StoreDepthStencil  =  RTACTION_MAKE_MASK(Load_Store, Clear_Store),

	ClearStencilDontLoadDepth_StoreStencilNotDepth = RTACTION_MAKE_MASK(DontLoad_DontStore, Clear_Store),

#undef RTACTION_MAKE_MASK
};

inline constexpr EDepthStencilTargetActions MakeDepthStencilTargetActions(const ERenderTargetActions Depth, const ERenderTargetActions Stencil)
{
	return (EDepthStencilTargetActions)(((uint8)Depth << (uint8)EDepthStencilTargetActions::DepthMask) | (uint8)Stencil);
}

inline ERenderTargetActions GetDepthActions(EDepthStencilTargetActions Action)
{
	return (ERenderTargetActions)((uint8)Action >> (uint8)EDepthStencilTargetActions::DepthMask);
}

inline ERenderTargetActions GetStencilActions(EDepthStencilTargetActions Action)
{
	return (ERenderTargetActions)((uint8)Action & ((1 << (uint8)EDepthStencilTargetActions::DepthMask) - 1));
}

struct FRHIRenderPassInfo
{
	struct FColorEntry
	{
		FRHITexture* RenderTarget;
		FRHITexture* ResolveTarget;
		int32 ArraySlice;
		uint8 MipIndex;
		ERenderTargetActions Action;
	};
	FColorEntry ColorRenderTargets[MaxSimultaneousRenderTargets];

	struct FDepthStencilEntry
	{
		FRHITexture* DepthStencilTarget;
		FRHITexture* ResolveTarget;
		EDepthStencilTargetActions Action;
		FExclusiveDepthStencil ExclusiveDepthStencil;
	};
	FDepthStencilEntry DepthStencilRenderTarget;

	FResolveParams ResolveParameters;

	// Some RHIs can use a texture to control the sampling and/or shading resolution of different areas 
	FTextureRHIRef ShadingRateTexture = nullptr;
	EVRSRateCombiner ShadingRateTextureCombiner = VRSRB_Passthrough;

	// Some RHIs require a hint that occlusion queries will be used in this render pass
	uint32 NumOcclusionQueries = 0;
	bool bOcclusionQueries = false;

	// Some RHIs need to know if this render pass is going to be reading and writing to the same texture in the case of generating mip maps for partial resource transitions
	bool bGeneratingMips = false;

	// if this renderpass should be multiview, and if so how many views are required
	uint8 MultiViewCount = 0;

	// Hint for some RHI's that renderpass will have specific sub-passes 
	ESubpassHint SubpassHint = ESubpassHint::None;

	// TODO: Remove once FORT-162640 is solved
	bool bTooManyUAVs = false;


	// Color, no depth, optional resolve, optional mip, optional array slice
	explicit FRHIRenderPassInfo(FRHITexture* ColorRT, ERenderTargetActions ColorAction, FRHITexture* ResolveRT = nullptr, uint32 InMipIndex = 0, int32 InArraySlice = -1)
	{
		check(ColorRT);
		ColorRenderTargets[0].RenderTarget = ColorRT;
		ColorRenderTargets[0].ResolveTarget = ResolveRT;
		ColorRenderTargets[0].ArraySlice = InArraySlice;
		ColorRenderTargets[0].MipIndex = InMipIndex;
		ColorRenderTargets[0].Action = ColorAction;
		DepthStencilRenderTarget.DepthStencilTarget = nullptr;
		DepthStencilRenderTarget.Action = EDepthStencilTargetActions::DontLoad_DontStore;
		DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilNop;
		DepthStencilRenderTarget.ResolveTarget = nullptr;
		bIsMSAA = ColorRT->GetNumSamples() > 1;
		FMemory::Memzero(&ColorRenderTargets[1], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - 1));
	}

	// Color MRTs, no depth
	explicit FRHIRenderPassInfo(int32 NumColorRTs, FRHITexture* ColorRTs[], ERenderTargetActions ColorAction)
	{
		check(NumColorRTs > 0);
		for (int32 Index = 0; Index < NumColorRTs; ++Index)
		{
			check(ColorRTs[Index]);
			ColorRenderTargets[Index].RenderTarget = ColorRTs[Index];
			ColorRenderTargets[Index].ResolveTarget = nullptr;
			ColorRenderTargets[Index].ArraySlice = -1;
			ColorRenderTargets[Index].MipIndex = 0;
			ColorRenderTargets[Index].Action = ColorAction;
		}
		DepthStencilRenderTarget.DepthStencilTarget = nullptr;
		DepthStencilRenderTarget.Action = EDepthStencilTargetActions::DontLoad_DontStore;
		DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilNop;
		DepthStencilRenderTarget.ResolveTarget = nullptr;
		if (NumColorRTs < MaxSimultaneousRenderTargets)
		{
			FMemory::Memzero(&ColorRenderTargets[NumColorRTs], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - NumColorRTs));
		}
	}

	// Color MRTs, no depth
	explicit FRHIRenderPassInfo(int32 NumColorRTs, FRHITexture* ColorRTs[], ERenderTargetActions ColorAction, FRHITexture* ResolveTargets[])
	{
		check(NumColorRTs > 0);
		for (int32 Index = 0; Index < NumColorRTs; ++Index)
		{
			check(ColorRTs[Index]);
			ColorRenderTargets[Index].RenderTarget = ColorRTs[Index];
			ColorRenderTargets[Index].ResolveTarget = ResolveTargets[Index];
			ColorRenderTargets[Index].ArraySlice = -1;
			ColorRenderTargets[Index].MipIndex = 0;
			ColorRenderTargets[Index].Action = ColorAction;
		}
		DepthStencilRenderTarget.DepthStencilTarget = nullptr;
		DepthStencilRenderTarget.Action = EDepthStencilTargetActions::DontLoad_DontStore;
		DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilNop;
		DepthStencilRenderTarget.ResolveTarget = nullptr;
		if (NumColorRTs < MaxSimultaneousRenderTargets)
		{
			FMemory::Memzero(&ColorRenderTargets[NumColorRTs], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - NumColorRTs));
		}
	}

	// Color MRTs and depth
	explicit FRHIRenderPassInfo(int32 NumColorRTs, FRHITexture* ColorRTs[], ERenderTargetActions ColorAction, FRHITexture* DepthRT, EDepthStencilTargetActions DepthActions, FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		check(NumColorRTs > 0);
		for (int32 Index = 0; Index < NumColorRTs; ++Index)
		{
			check(ColorRTs[Index]);
			ColorRenderTargets[Index].RenderTarget = ColorRTs[Index];
			ColorRenderTargets[Index].ResolveTarget = nullptr;
			ColorRenderTargets[Index].ArraySlice = -1;
			ColorRenderTargets[Index].MipIndex = 0;
			ColorRenderTargets[Index].Action = ColorAction;
		}
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = nullptr;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		bIsMSAA = DepthRT->GetNumSamples() > 1;
		if (NumColorRTs < MaxSimultaneousRenderTargets)
		{
			FMemory::Memzero(&ColorRenderTargets[NumColorRTs], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - NumColorRTs));
		}
	}

	// Color MRTs and depth
	explicit FRHIRenderPassInfo(int32 NumColorRTs, FRHITexture* ColorRTs[], ERenderTargetActions ColorAction, FRHITexture* ResolveRTs[], FRHITexture* DepthRT, EDepthStencilTargetActions DepthActions, FRHITexture* ResolveDepthRT, FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		check(NumColorRTs > 0);
		for (int32 Index = 0; Index < NumColorRTs; ++Index)
		{
			check(ColorRTs[Index]);
			ColorRenderTargets[Index].RenderTarget = ColorRTs[Index];
			ColorRenderTargets[Index].ResolveTarget = ResolveRTs[Index];
			ColorRenderTargets[Index].ArraySlice = -1;
			ColorRenderTargets[Index].MipIndex = 0;
			ColorRenderTargets[Index].Action = ColorAction;
		}
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = ResolveDepthRT;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		bIsMSAA = DepthRT->GetNumSamples() > 1;
		if (NumColorRTs < MaxSimultaneousRenderTargets)
		{
			FMemory::Memzero(&ColorRenderTargets[NumColorRTs], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - NumColorRTs));
		}
	}

	// Depth, no color
	explicit FRHIRenderPassInfo(FRHITexture* DepthRT, EDepthStencilTargetActions DepthActions, FRHITexture* ResolveDepthRT = nullptr, FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = ResolveDepthRT;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		bIsMSAA = DepthRT->GetNumSamples() > 1;
		FMemory::Memzero(ColorRenderTargets, sizeof(FColorEntry) * MaxSimultaneousRenderTargets);
	}

	// Depth, no color, occlusion queries
	explicit FRHIRenderPassInfo(FRHITexture* DepthRT, uint32 InNumOcclusionQueries, EDepthStencilTargetActions DepthActions, FRHITexture* ResolveDepthRT = nullptr, FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
		: NumOcclusionQueries(InNumOcclusionQueries)
		, bOcclusionQueries(true)
	{
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = ResolveDepthRT;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		bIsMSAA = DepthRT->GetNumSamples() > 1;
		FMemory::Memzero(ColorRenderTargets, sizeof(FColorEntry) * MaxSimultaneousRenderTargets);
	}

	// Color and depth
	explicit FRHIRenderPassInfo(FRHITexture* ColorRT, ERenderTargetActions ColorAction, FRHITexture* DepthRT, EDepthStencilTargetActions DepthActions, FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		check(ColorRT);
		ColorRenderTargets[0].RenderTarget = ColorRT;
		ColorRenderTargets[0].ResolveTarget = nullptr;
		ColorRenderTargets[0].ArraySlice = -1;
		ColorRenderTargets[0].MipIndex = 0;
		ColorRenderTargets[0].Action = ColorAction;
		bIsMSAA = ColorRT->GetNumSamples() > 1;
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = nullptr;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		FMemory::Memzero(&ColorRenderTargets[1], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - 1));
	}

	// Color and depth with resolve
	explicit FRHIRenderPassInfo(FRHITexture* ColorRT, ERenderTargetActions ColorAction, FRHITexture* ResolveColorRT,
		FRHITexture* DepthRT, EDepthStencilTargetActions DepthActions, FRHITexture* ResolveDepthRT, FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		check(ColorRT);
		ColorRenderTargets[0].RenderTarget = ColorRT;
		ColorRenderTargets[0].ResolveTarget = ResolveColorRT;
		ColorRenderTargets[0].ArraySlice = -1;
		ColorRenderTargets[0].MipIndex = 0;
		ColorRenderTargets[0].Action = ColorAction;
		bIsMSAA = ColorRT->GetNumSamples() > 1;
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = ResolveDepthRT;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		FMemory::Memzero(&ColorRenderTargets[1], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - 1));
	}

	// Color and depth with resolve and optional sample density
	explicit FRHIRenderPassInfo(FRHITexture* ColorRT, ERenderTargetActions ColorAction, FRHITexture* ResolveColorRT,
		FRHITexture* DepthRT, EDepthStencilTargetActions DepthActions, FRHITexture* ResolveDepthRT, 
		FRHITexture* InShadingRateTexture, EVRSRateCombiner InShadingRateTextureCombiner,
		FExclusiveDepthStencil InEDS = FExclusiveDepthStencil::DepthWrite_StencilWrite)
	{
		check(ColorRT);
		ColorRenderTargets[0].RenderTarget = ColorRT;
		ColorRenderTargets[0].ResolveTarget = ResolveColorRT;
		ColorRenderTargets[0].ArraySlice = -1;
		ColorRenderTargets[0].MipIndex = 0;
		ColorRenderTargets[0].Action = ColorAction;
		bIsMSAA = ColorRT->GetNumSamples() > 1;
		check(DepthRT);
		DepthStencilRenderTarget.DepthStencilTarget = DepthRT;
		DepthStencilRenderTarget.ResolveTarget = ResolveDepthRT;
		DepthStencilRenderTarget.Action = DepthActions;
		DepthStencilRenderTarget.ExclusiveDepthStencil = InEDS;
		ShadingRateTexture = InShadingRateTexture;
		ShadingRateTextureCombiner = InShadingRateTextureCombiner;
		FMemory::Memzero(&ColorRenderTargets[1], sizeof(FColorEntry) * (MaxSimultaneousRenderTargets - 1));
	}

	enum ENoRenderTargets
	{
		NoRenderTargets,
	};
	explicit FRHIRenderPassInfo(ENoRenderTargets Dummy)
	{
		(void)Dummy;
		FMemory::Memzero(*this);
	}

	inline int32 GetNumColorRenderTargets() const
	{
		int32 ColorIndex = 0;
		for (; ColorIndex < MaxSimultaneousRenderTargets; ++ColorIndex)
		{
			const FColorEntry& Entry = ColorRenderTargets[ColorIndex];
			if (!Entry.RenderTarget)
			{
				break;
			}
		}

		return ColorIndex;
	}

	explicit FRHIRenderPassInfo()
	{
		FMemory::Memzero(*this);
	}

	inline bool IsMSAA() const
	{
		return bIsMSAA;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	RHI_API void Validate() const;
#else
	RHI_API void Validate() const {}
#endif
	RHI_API void ConvertToRenderTargetsInfo(FRHISetRenderTargetsInfo& OutRTInfo) const;

#if 0 // FORT-162640
	FRHIRenderPassInfo& operator = (const FRHIRenderPassInfo& In)
	{
		FMemory::Memcpy(*this, In);
		return *this;
	}
#endif

	bool bIsMSAA = false;

private:
	RHI_API void OnVerifyNumUAVsFailed(int32 InNumUAVs);
};
