// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIResources.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

struct ID3D11Texture2D;

class ITextureShareD3D11
	: public IModuleInterface
{
	static constexpr auto ModuleName = TEXT("TextureShareD3D11");

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline ITextureShareD3D11& Get()
	{
		return FModuleManager::LoadModuleChecked<ITextureShareD3D11>(ITextureShareD3D11::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(ITextureShareD3D11::ModuleName);
	}

public:
	/**
	 * Create RHI for opened D3D11 shared texture
	 *
	 */
	virtual bool CreateRHITexture(ID3D11Texture2D* OpenedSharedResource, EPixelFormat Format, FTexture2DRHIRef& DstTexture) = 0;

	/*
	 * Create shared texture and handle
	*/
	virtual bool CreateSharedTexture(FIntPoint& Size, EPixelFormat Format, FTexture2DRHIRef& OutRHITexture, void*& OutHandle) = 0;
};
