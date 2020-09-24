// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareD3D11.h"

class FTextureShareD3D11
	: public ITextureShareD3D11
{
public:
	FTextureShareD3D11() {}
	virtual ~FTextureShareD3D11() {}

	virtual bool CreateRHITexture(ID3D11Texture2D* OpenedSharedResource, EPixelFormat Format, FTexture2DRHIRef& DstTexture) override;
	virtual bool CreateSharedTexture(FIntPoint& Size, EPixelFormat Format, FTexture2DRHIRef& OutRHITexture, void*& OutHandle) override;
};
