// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIResources.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

struct ID3D12Resource;
class ID3D12CrossGPUHeap;

class ITextureShareD3D12
	: public IModuleInterface
{
	static constexpr auto ModuleName = TEXT("TextureShareD3D12");

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline ITextureShareD3D12& Get()
	{
		return FModuleManager::LoadModuleChecked<ITextureShareD3D12>(ITextureShareD3D12::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(ITextureShareD3D12::ModuleName);
	}

public:
	/**
	 * Create RHI texture for opened D3D12 shared resource
	 *
	 */
	virtual bool CreateRHITexture(ID3D12Resource* OpenedSharedResource, EPixelFormat Format, FTexture2DRHIRef& DstTexture) = 0;

	/*
	 * Create DX12 shared texture and handle
	 */
	virtual bool CreateSharedTexture(FIntPoint& Size, EPixelFormat Format, FTexture2DRHIRef& OutRHITexture, void*& OutHandle, FGuid& OutSharedHandleGuid) = 0;

	/*
	 * Get DX12 Cross GPU heap resource API (experimental)
	 */
	virtual bool GetCrossGPUHeap(TSharedPtr<ID3D12CrossGPUHeap>& OutCrossGPUHeap) = 0;
};
