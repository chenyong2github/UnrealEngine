// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareItem.h"

#if TEXTURESHARELIB_USE_D3D12

struct ID3D12Device;
struct ID3D12Resource;

class ITextureShareItemD3D12
{
public:
	virtual ~ITextureShareItemD3D12() = 0
	{}

	/*
	* Synchronized access to shared texture by name Lock()+Unlock()
	*/
	virtual ID3D12Resource* LockTexture_RenderThread(ID3D12Device* pD3D12Device, const FString& TextureName) = 0;
};

#endif // TEXTURESHARELIB_USE_D3D12
