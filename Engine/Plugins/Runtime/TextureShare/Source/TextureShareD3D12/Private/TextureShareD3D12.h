// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareD3D12.h"

#include "Platform/TextureShareD3D12PlatformWindows.h"

class FTextureShareD3D12
	: public ITextureShareD3D12
{
public:
	FTextureShareD3D12() {};
	virtual ~FTextureShareD3D12() {};

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	virtual bool CreateRHITexture(ID3D12Resource* OpenedSharedResource, EPixelFormat Format, FTexture2DRHIRef& DstTexture) override;
	virtual bool CreateSharedTexture(FIntPoint& Size, EPixelFormat Format, FTexture2DRHIRef& OutRHITexture, void*& OutHandle, FGuid& OutSharedHandleGuid) override;
	virtual bool GetCrossGPUHeap(TSharedPtr<ID3D12CrossGPUHeap>& OutCrossGPUHeap) override;

private:
	FTextureShareD3D12SharedResourceSecurityAttributes SharedResourceSecurityAttributes;
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
};
