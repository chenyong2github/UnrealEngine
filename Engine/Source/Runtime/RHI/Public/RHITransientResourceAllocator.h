// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHIResources.h"

class FRHICommandListImmediate;

enum class ERHITransientResourceType
{
	Texture,
	Buffer
};

class FRHITransientResource
{
public:
	FRHITransientResource(FRHIResource* InResource, uint64 InHash, ERHITransientResourceType InType)
		: Resource(InResource)
		, Hash(InHash)
		, Type(InType)
	{}

	virtual ~FRHITransientResource() = default;

	// (Internal) Initializes the transient resource with a new allocation / name.
	virtual void Init(const TCHAR* InName, uint32 InAllocationIndex, uint32 InAcquirePassIndex)
	{
		Name = InName;
		AllocationIndex = InAllocationIndex;
		AcquirePasses = TInterval<uint32>(0, InAcquirePassIndex);
		DiscardPasses = TInterval<uint32>(0, TNumericLimits<uint32>::Max());
		AliasingOverlaps.Reset();
	}

	// (Internal) Assigns the discard pass index.
	FORCEINLINE void SetDiscardPass(uint32 PassIndex) { DiscardPasses.Min = PassIndex; }

	// (Internal) Adds a new transient resource overlap.
	FORCEINLINE void AddAliasingOverlap(FRHITransientResource* InResource)
	{
		AliasingOverlaps.Emplace(InResource->GetRHI(), InResource->IsTexture() ? FRHITransientAliasingOverlap::EType::Texture : FRHITransientAliasingOverlap::EType::Buffer);

		InResource->DiscardPasses.Max = FMath::Min(InResource->DiscardPasses.Max,             AcquirePasses.Max);
		            AcquirePasses.Min = FMath::Max(            AcquirePasses.Min, InResource->DiscardPasses.Min);
	}

	// Returns the underlying RHI resource.
	FORCEINLINE FRHIResource* GetRHI() const { return Resource; }

	// Returns the name assigned to the transient resource at allocation time.
	FORCEINLINE const TCHAR* GetName() const { return Name; }

	// (Internal)Returns the hash used to uniquely identify this resource if cached.
	FORCEINLINE uint64 GetHash() const { return Hash; }

	// (Internal) Returns the platform-specific allocation index.
	FORCEINLINE uint32 GetAllocationIndex() const { return AllocationIndex; }

	// Returns the pass index which may end acquiring this resource.
	FORCEINLINE TInterval<uint32> GetAcquirePasses() const { return AcquirePasses; }

	// Returns the pass index which discarded this resource.
	FORCEINLINE TInterval<uint32> GetDiscardPasses() const { return DiscardPasses; }

	// Returns the aliasing overlaps for this resource.
	FORCEINLINE TConstArrayView<FRHITransientAliasingOverlap> GetAliasingOverlaps() const { return AliasingOverlaps; }

	FORCEINLINE ERHITransientResourceType GetType() const { return Type; }
	FORCEINLINE bool IsTexture() const { return Type == ERHITransientResourceType::Texture; }
	FORCEINLINE bool IsBuffer() const { return Type == ERHITransientResourceType::Buffer; }

private:
	// Underlying RHI resource.
	TRefCountPtr<FRHIResource> Resource;

	// The hash used to uniquely identify this resource if cached.
	uint64 Hash;

	// Debug name of the resource. Updated with each allocation.
	const TCHAR* Name{};

	// List of aliasing resources overlapping with this one.
	TArray<FRHITransientAliasingOverlap> AliasingOverlaps;

	// Type of the underlying RHI resource.
	ERHITransientResourceType Type;

	// An index to the underlying allocation info on the internal platform allocator.
	uint32 AllocationIndex = ~0u;
	TInterval<uint32> AcquirePasses = TInterval<uint32>(0, 0);
	TInterval<uint32> DiscardPasses = TInterval<uint32>(0, 0);
};

class RHI_API FRHITransientTexture final : public FRHITransientResource
{
public:
	FRHITransientTexture(FRHITexture* InTexture, uint64 InHash, const FRHITextureCreateInfo& InCreateInfo)
		: FRHITransientResource(InTexture, InHash, ERHITransientResourceType::Texture)
		, CreateInfo(InCreateInfo)
	{}

	void Init(const TCHAR* InName, uint32 InAllocationIndex, uint32 InPassIndex) override;

	// Returns the underlying RHI texture.
	FRHITexture* GetRHI() const { return static_cast<FRHITexture*>(FRHITransientResource::GetRHI()); }

	// Returns the create info struct used when creating this texture.
	FORCEINLINE const FRHITextureCreateInfo& GetCreateInfo() const { return CreateInfo; }

	// Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache.
	FORCEINLINE FRHIUnorderedAccessView* GetOrCreateUAV(const FRHITextureUAVCreateInfo& InCreateInfo) { return ViewCache.GetOrCreateUAV(GetRHI(), InCreateInfo); }

	// Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache.
	FORCEINLINE FRHIShaderResourceView* GetOrCreateSRV(const FRHITextureSRVCreateInfo& InCreateInfo) { return ViewCache.GetOrCreateSRV(GetRHI(), InCreateInfo); }

	// The create info describing the texture.
	const FRHITextureCreateInfo CreateInfo;

	// The persistent view cache containing all views created for this texture.
	FRHITextureViewCache ViewCache;
};

class RHI_API FRHITransientBuffer final : public FRHITransientResource
{
public:
	FORCEINLINE FRHITransientBuffer(FRHIBuffer* InBuffer, uint64 InHash, const FRHIBufferCreateInfo& InCreateInfo)
		: FRHITransientResource(InBuffer, InHash, ERHITransientResourceType::Buffer)
		, CreateInfo(InCreateInfo)
	{}

	void Init(const TCHAR* InName, uint32 InAllocationIndex, uint32 InPassIndex) override;

	// Returns the underlying RHI buffer.
	FORCEINLINE FRHIBuffer* GetRHI() const { return static_cast<FRHIBuffer*>(FRHITransientResource::GetRHI()); }

	// Returns the create info used when creating this buffer.
	FORCEINLINE const FRHIBufferCreateInfo& GetCreateInfo() const { return CreateInfo; }

	// Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache.
	FORCEINLINE FRHIUnorderedAccessView* GetOrCreateUAV(const FRHIBufferUAVCreateInfo& InCreateInfo) { return ViewCache.GetOrCreateUAV(GetRHI(), InCreateInfo); }

	// Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache.
	FORCEINLINE FRHIShaderResourceView* GetOrCreateSRV(const FRHIBufferSRVCreateInfo& InCreateInfo) { return ViewCache.GetOrCreateSRV(GetRHI(), InCreateInfo); }

	// The create info describing the texture.
	const FRHIBufferCreateInfo CreateInfo;

	// The persistent view cache containing all views created for this buffer.
	FRHIBufferViewCache ViewCache;
};

class IRHITransientResourceAllocator
{
public:
	virtual ~IRHITransientResourceAllocator() = default;

	// Allocates a new transient resource with memory backed by the transient allocator.
	virtual FRHITransientTexture* CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName, uint32 InPassIndex) = 0;
	virtual FRHITransientBuffer* CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName, uint32 InPassIndex) = 0;

	// Deallocates the underlying memory for use by a future resource creation call.
	virtual void DeallocateMemory(FRHITransientTexture* InTexture, uint32 InPassIndex) = 0;
	virtual void DeallocateMemory(FRHITransientBuffer* InBuffer, uint32 InPassIndex) = 0;

	// Flushes any pending allocations in preparation for rendering. Resources are not required to be deallocated yet. Use when interleaving execution with allocation.
	virtual void Flush(FRHICommandListImmediate& RHICmdList) {}

	// Freezes all allocations and validates that all resources have their memory deallocated.
	virtual void Freeze(FRHICommandListImmediate& RHICmdList) = 0;

	// Releases the transient allocator and deletes the instance. Any FRHITransientResource* access after this call is not allowed.
	virtual void Release(FRHICommandListImmediate& RHICmdList)
	{
		delete this;
	}
};