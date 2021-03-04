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
	virtual void Freeze() override final;

private:

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

		FString DebugName;
		EType ResourceType;
		bool bMemoryAllocated;
	};
	TMap<FRHIResource*, AllocatedResourceData> AllocatedResourceMap;
};

#endif	// ENABLE_RHI_VALIDATION
