// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "RHITransientResourceAllocator.h"

#if ENABLE_RHI_VALIDATION

class FValidationTransientResourceAllocator : public IRHITransientResourceAllocator
{
public:
	FValidationTransientResourceAllocator(IRHITransientResourceAllocator* InRHIAllocator)
		: RHIAllocator(InRHIAllocator)
	{}

	virtual ~FValidationTransientResourceAllocator();

	// Implementation of FRHITransientResourceAllocator interface
	virtual FRHITransientTexture* CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName) override final;
	virtual FRHITransientBuffer* CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName) override final;
	virtual void DeallocateMemory(FRHITransientTexture* InTexture) override final;
	virtual void DeallocateMemory(FRHITransientBuffer* InBuffer) override final;
	virtual void Freeze(FRHICommandListImmediate&) override final;
	virtual void Release(FRHICommandListImmediate&) override final;

private:

	friend class FValidationContext;
	void InitBarrierTracking();

	// Actual RHI transient allocator which will get all functions forwarded
	IRHITransientResourceAllocator* RHIAllocator = nullptr;

	bool bFrozen = false;
	bool bReleased = false;

	// All the allocated resources on the transient allocator
	struct FAllocatedResourceData
	{
		enum class EType
		{
			Texture,
			Buffer,
		};

		const TCHAR* DebugName = nullptr;
		EType ResourceType = EType::Texture;
		bool bMemoryAllocated = false;
		bool bReinitializeBarrierTracking = false;

		struct FTexture
		{
			ETextureCreateFlags Flags = TexCreate_None;
			EPixelFormat Format = PF_Unknown;
			uint16 ArraySize = 0;
			uint8 NumMips = 0;
		} Texture;
	};
	TMap<FRHIResource*, FAllocatedResourceData> AllocatedResourceMap;
};

#endif	// ENABLE_RHI_VALIDATION
