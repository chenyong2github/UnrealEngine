// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "RHI.h"

//-------------------------------------------------------------------------------------------------
// FXRSwapChain
//-------------------------------------------------------------------------------------------------

class HEADMOUNTEDDISPLAY_API FXRSwapChain : public TSharedFromThis<FXRSwapChain, ESPMode::ThreadSafe>
{
public:
	FXRSwapChain(FRHITexture* InRHITexture, TArray<FTextureRHIRef>&& InRHITextureSwapChain);
	virtual ~FXRSwapChain();

	FRHITexture* GetTexture() const { return RHITexture.GetReference(); }
	FRHITexture2D* GetTexture2D() const { return RHITexture->GetTexture2D(); }
	FRHITexture2DArray* GetTexture2DArray() const { return RHITexture->GetTexture2DArray(); }
	FRHITextureCube* GetTextureCube() const { return RHITexture->GetTextureCube(); }
	uint32 GetSwapChainLength() const { return (uint32)RHITextureSwapChain.Num(); }

	void GenerateMips_RenderThread(FRHICommandListImmediate& RHICmdList);
	uint32 GetSwapChainIndex_RHIThread() { return SwapChainIndex_RHIThread; }

	virtual void IncrementSwapChainIndex_RHIThread();
	virtual void ReleaseCurrentImage_RHIThread() {}		

protected:
	virtual void ReleaseResources_RHIThread();

	FTextureRHIRef RHITexture;
	TArray<FTextureRHIRef> RHITextureSwapChain;
	uint32 SwapChainIndex_RHIThread;
};

typedef TSharedPtr<FXRSwapChain, ESPMode::ThreadSafe> FXRSwapChainPtr;

