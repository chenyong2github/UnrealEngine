// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphResources.h"
#include "RenderGraphEvent.h"
#include "ShaderParameterMacros.h"

class FRDGPass;
class FRDGBuilder;

/** A helper class for identifying and accessing a render graph pass parameter. */
class FRDGPassParameter final
{
	friend class FRDGPassParameterStruct;
public:
	FRDGPassParameter() = default;

	bool IsResource() const
	{
		return IsRDGResourceReferenceShaderParameterType(MemberType);
	}

	bool IsSRV() const
	{
		return MemberType == UBMT_RDG_TEXTURE_SRV || MemberType == UBMT_RDG_BUFFER_SRV;
	}

	bool IsUAV() const
	{
		return MemberType == UBMT_RDG_TEXTURE_UAV || MemberType == UBMT_RDG_BUFFER_UAV;
	}

	bool IsParentResource() const
	{
		return
			MemberType == UBMT_RDG_TEXTURE ||
			MemberType == UBMT_RDG_TEXTURE_COPY_DEST ||
			MemberType == UBMT_RDG_BUFFER ||
			MemberType == UBMT_RDG_BUFFER_COPY_DEST;
	}

	bool IsChildResource() const
	{
		return IsSRV() || IsUAV();
	}

	bool IsRenderTargetBindingSlots() const
	{
		return MemberType == UBMT_RENDER_TARGET_BINDING_SLOTS;
	}

	EUniformBufferBaseType GetType() const
	{
		return MemberType;
	}

	FRDGResourceRef GetAsResource() const
	{
		check(IsResource());
		return *GetAs<FRDGResourceRef>();
	}

	FRDGParentResourceRef GetAsParentResource() const
	{
		check(IsParentResource());
		return *GetAs<FRDGParentResourceRef>();
	}

	FRDGChildResourceRef GetAsChildResource() const
	{
		check(IsChildResource());
		return *GetAs<FRDGChildResourceRef>();
	}

	FRDGShaderResourceViewRef GetAsSRV() const
	{
		check(IsSRV());
		return *GetAs<FRDGShaderResourceViewRef>();
	}

	FRDGUnorderedAccessViewRef GetAsUAV() const
	{
		check(IsUAV());
		return *GetAs<FRDGUnorderedAccessViewRef>();
	}

	FRDGTextureRef GetAsTexture() const
	{
		check(MemberType == UBMT_RDG_TEXTURE || MemberType == UBMT_RDG_TEXTURE_COPY_DEST);
		return *GetAs<FRDGTextureRef>();
	}

	FRDGBufferRef GetAsBuffer() const
	{
		check(MemberType == UBMT_RDG_BUFFER || MemberType == UBMT_RDG_BUFFER_COPY_DEST);
		return *GetAs<FRDGBufferRef>();
	}

	FRDGTextureSRVRef GetAsTextureSRV() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_SRV);
		return *GetAs<FRDGTextureSRVRef>();
	}

	FRDGBufferSRVRef GetAsBufferSRV() const
	{
		check(MemberType == UBMT_RDG_BUFFER_SRV);
		return *GetAs<FRDGBufferSRVRef>();
	}

	FRDGTextureUAVRef GetAsTextureUAV() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_UAV);
		return *GetAs<FRDGTextureUAVRef>();
	}

	FRDGBufferUAVRef GetAsBufferUAV() const
	{
		check(MemberType == UBMT_RDG_BUFFER_UAV);
		return *GetAs<FRDGBufferUAVRef>();
	}

	const FRenderTargetBindingSlots& GetAsRenderTargetBindingSlots() const
	{
		check(IsRenderTargetBindingSlots());
		return *GetAs<FRenderTargetBindingSlots>();
	}

private:
	FRDGPassParameter(EUniformBufferBaseType InMemberType, void* InMemberPtr)
		: MemberType(InMemberType)
		, MemberPtr(InMemberPtr)
	{}

	template <typename T>
	T* GetAs() const
	{
		return reinterpret_cast<T*>(MemberPtr);
	}

	const EUniformBufferBaseType MemberType = UBMT_INVALID;
	void* MemberPtr = nullptr;
};

/** Wraps a pass parameter struct payload and provides helpers for traversing members. */
class FRDGPassParameterStruct final
{
public:
	explicit FRDGPassParameterStruct(void* InContents, const FRHIUniformBufferLayout* InLayout)
		: Contents(reinterpret_cast<uint8*>(InContents))
		, Layout(InLayout)
	{
		checkf(Contents && Layout, TEXT("Pass parameter struct created with null inputs."));
	}

	template <typename FParameterStruct>
	explicit FRDGPassParameterStruct(FParameterStruct* Parameters)
		: FRDGPassParameterStruct(Parameters, &FParameterStruct::FTypeInfo::GetStructMetadata()->GetLayout())
	{}

	uint32 GetParameterCount() const
	{
		return Layout->Resources.Num();
	}

	FRDGPassParameter GetParameter(uint32 ParameterIndex) const
	{
		const auto& Resources = Layout->Resources;
		const EUniformBufferBaseType MemberType = Resources[ParameterIndex].MemberType;
		const uint16 MemberOffset = Resources[ParameterIndex].MemberOffset;

		return FRDGPassParameter(MemberType, Contents + MemberOffset);
	}

	const void* GetContents() const
	{
		return Contents;
	}

	const FRHIUniformBufferLayout& GetLayout() const
	{
		return *Layout;
	}

private:
	/** Releases all active uniform buffer references held inside the struct. */
	RENDERCORE_API void ClearUniformBuffers() const;

	uint8* Contents;
	const FRHIUniformBufferLayout* Layout;

	friend FRDGPass;
};

/** Flags to annotate passes. */
enum class ERDGPassFlags : uint8
{
	/** Pass uses raster pipeline. */
	Raster = 0x1,

	/** Pass uses compute only */
	Compute = 0x2,

	/** Pass uses RHI copy commands only. */
	Copy = 0x4,

	//#todo-rco: Remove this when we can do split/per mip layout transitions.
	/** Hint to some RHIs this pass will be generating mips to optimize transitions. */
	GenerateMips = 0x8
};
ENUM_CLASS_FLAGS(ERDGPassFlags);

/** Base class of a render graph pass. */
class RENDERCORE_API FRDGPass
{
public:
	FRDGPass(
		FRDGEventName&& InName,
		FRDGPassParameterStruct InParameterStruct,
		ERDGPassFlags InPassFlags);

	FRDGPass(const FRDGPass&) = delete;

	virtual ~FRDGPass() = default;

	const TCHAR* GetName() const
	{
		return Name.GetTCHAR();
	}

	ERDGPassFlags GetFlags() const
	{
		return PassFlags;
	}

	bool IsRaster() const
	{
		return (PassFlags & ERDGPassFlags::Raster) == ERDGPassFlags::Raster;
	}

	bool IsCompute() const
	{
		return (PassFlags & ERDGPassFlags::Compute) == ERDGPassFlags::Compute;
	}

	bool IsCopy() const
	{
		return (PassFlags & ERDGPassFlags::Copy) == ERDGPassFlags::Copy;
	}

	bool IsGenerateMips() const
	{
		return (PassFlags & ERDGPassFlags::GenerateMips) == ERDGPassFlags::GenerateMips;
	}

	FRDGPassParameterStruct GetParameters() const
	{
		return ParameterStruct;
	}

	const FRDGEventScope* GetEventScope() const
	{
		return EventScope;
	}

	const FRDGStatScope* GetStatScope() const
	{
		return StatScope;
	}

private:
	//////////////////////////////////////////////////////////////////////////
	//! User Methods to Override

	virtual void ExecuteImpl(FRHICommandListImmediate& RHICmdList) const = 0;

	//////////////////////////////////////////////////////////////////////////

	void Execute(FRHICommandListImmediate& RHICmdList) const;

	const FRDGEventName Name;
	const FRDGEventScope* EventScope = nullptr;
	const FRDGStatScope* StatScope = nullptr;
	FRDGPassParameterStruct ParameterStruct;
	const ERDGPassFlags PassFlags;

	friend FRDGBuilder;
};

/** Render graph pass with lambda execute function. */
template <typename ParameterStructType, typename ExecuteLambdaType>
class TRDGLambdaPass final
	: public FRDGPass
{
public:
	TRDGLambdaPass(
		FRDGEventName&& InName,
		FRDGPassParameterStruct InParameterStruct,
		ERDGPassFlags InPassFlags,
		ExecuteLambdaType&& InExecuteLambda)
		: FRDGPass(MoveTemp(InName), InParameterStruct, InPassFlags)
		, ExecuteLambda(MoveTemp(InExecuteLambda))
	{}

private:
	void ExecuteImpl(FRHICommandListImmediate& RHICmdList) const override
	{
		ExecuteLambda(RHICmdList);
	}

	ExecuteLambdaType ExecuteLambda;
};
