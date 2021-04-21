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
	void Init(const TCHAR* InName, uint32 InAllocationIndex)
	{
		Name = InName;
		AllocationIndex = InAllocationIndex;
		AliasingOverlaps.Reset();
	}

	// (Internal) Adds a new transient resource overlap.
	FORCEINLINE void AddAliasingOverlap(FRHITransientResource* InResource)
	{
		AliasingOverlaps.Emplace(InResource->GetRHI(), InResource->IsTexture() ? FRHITransientAliasingOverlap::EType::Texture : FRHITransientAliasingOverlap::EType::Buffer);
	}

	// Returns the underlying RHI resource.
	FORCEINLINE FRHIResource* GetRHI() const { return Resource; }

	// Returns the name assigned to the transient resource at allocation time.
	FORCEINLINE const TCHAR* GetName() const { return Name; }

	// (Internal)Returns the hash used to uniquely identify this resource if cached.
	FORCEINLINE uint64 GetHash() const { return Hash; }

	// (Internal) Returns the platform-specific allocation index.
	FORCEINLINE uint32 GetAllocationIndex() const { return AllocationIndex; }

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
};

class RHI_API FRHITransientTexture final : public FRHITransientResource
{
public:
	FRHITransientTexture(FRHITexture* InTexture, uint64 InHash, const FRHITextureCreateInfo& InCreateInfo)
		: FRHITransientResource(InTexture, InHash, ERHITransientResourceType::Texture)
		, CreateInfo(InCreateInfo)
	{}

	// Returns the underlying RHI texture.
	FRHITexture* GetRHI() const { return static_cast<FRHITexture*>(FRHITransientResource::GetRHI()); }

	// Returns the create info struct used when creating this texture.
	FORCEINLINE const FRHITextureCreateInfo& GetCreateInfo() const { return CreateInfo; }

	// Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache.
	FRHIUnorderedAccessView* GetOrCreateUAV(const FRHITextureUAVCreateInfo& CreateInfo);

	// Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache.
	FRHIShaderResourceView* GetOrCreateSRV(const FRHITextureSRVCreateInfo& CreateInfo);

private:
	FRHITextureCreateInfo CreateInfo;
	TArray<TPair<FRHITextureUAVCreateInfo, FUnorderedAccessViewRHIRef>, TInlineAllocator<1>> UAVs;
	TArray<TPair<FRHITextureSRVCreateInfo, FShaderResourceViewRHIRef>, TInlineAllocator<1>> SRVs;
};

class RHI_API FRHITransientBuffer final : public FRHITransientResource
{
public:
	FORCEINLINE FRHITransientBuffer(FRHIBuffer* InBuffer, uint64 InHash, const FRHIBufferCreateInfo& InCreateInfo)
		: FRHITransientResource(InBuffer, InHash, ERHITransientResourceType::Buffer)
		, CreateInfo(InCreateInfo)
	{}

	// Returns the underlying RHI buffer.
	FORCEINLINE FRHIBuffer* GetRHI() const { return static_cast<FRHIBuffer*>(FRHITransientResource::GetRHI()); }

	// Returns the create info used when creating this buffer.
	FORCEINLINE const FRHIBufferCreateInfo& GetCreateInfo() const { return CreateInfo; }

	// Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache.
	FRHIUnorderedAccessView* GetOrCreateUAV(const FRHIBufferUAVCreateInfo& CreateInfo);

	// Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache.
	FRHIShaderResourceView* GetOrCreateSRV(const FRHIBufferSRVCreateInfo& CreateInfo);

private:
	FRHIBufferCreateInfo CreateInfo;
	TArray<TPair<FRHIBufferUAVCreateInfo, FUnorderedAccessViewRHIRef>, TInlineAllocator<1>> UAVs;
	TArray<TPair<FRHIBufferSRVCreateInfo, FShaderResourceViewRHIRef>, TInlineAllocator<1>> SRVs;
};

class RHI_API IRHITransientResourceAllocator
{
public:
	virtual ~IRHITransientResourceAllocator() = default;

	// Allocates a new transient resource with memory backed by the transient allocator.
	virtual FRHITransientTexture* CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName) = 0;
	virtual FRHITransientBuffer* CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName) = 0;

	// Deallocates the underlying memory for use by a future resource creation call.
	virtual void DeallocateMemory(FRHITransientTexture* InTexture) = 0;
	virtual void DeallocateMemory(FRHITransientBuffer* InBuffer) = 0;

	// Freezes all allocations and validates that all resources have their memory deallocated.
	virtual void Freeze(FRHICommandListImmediate& RHICmdList) = 0;

	// Releases the transient allocator and deletes the instance. Any FRHITransientResource* access after this call is not allowed.
	virtual void Release(FRHICommandListImmediate& RHICmdList);
};