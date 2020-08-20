// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#if TEXTURESHARECORE_RHI
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#endif

class ITextureShareItemRHI
{
public:
	virtual ~ITextureShareItemRHI() = 0
	{}

#if TEXTURESHARECORE_RHI
	/** RHI texture access */
	virtual bool LockRHITexture_RenderThread(const FString& TextureName, FTexture2DRHIRef& OutRHITexture) = 0;
	virtual bool TransferTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName) = 0;
	/** check texture formats that can be copied */
	virtual bool IsFormatResampleRequired(const FRHITexture* Texture1, const FRHITexture* Texture2) = 0;
#endif
};
