// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIValidation.h: Public Valdation RHI definitions.
=============================================================================*/

#pragma once 

#include "RHIResources.h"

#if ENABLE_RHI_VALIDATION

class FValidationTransientResourceAllocator : public IRHITransientResourceAllocator
{
public:
	FValidationTransientResourceAllocator(IRHITransientResourceAllocator* InRHIAllocator) : RHIAllocator(InRHIAllocator) {}
	virtual ~FValidationTransientResourceAllocator();

	// Implementation of FRHITransientResourceAllocator interface
	virtual FRHITexture* CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName) override final;
	virtual FRHIBuffer* CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName) override final;
	virtual void DeallocateMemory(FRHITexture* InTexture) override final;
	virtual void DeallocateMemory(FRHIBuffer* InBuffer) override final;
	virtual void Freeze(FRHICommandListImmediate&) override final;

private:

	friend class FValidationContext;
	void InitBarrierTracking();

	// Actual RHI transient allocator which will get all functions forwarded
	IRHITransientResourceAllocator* RHIAllocator = nullptr;

	// Allocator already frozen?
	bool bFrozen = false;

	// All the allocated resources on the transient allocator
	struct AllocatedResourceData
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
	TMap<FRHIResource*, AllocatedResourceData> AllocatedResourceMap;
};

#endif	// ENABLE_RHI_VALIDATION
