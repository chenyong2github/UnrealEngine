// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareItem.h"

#if TEXTURESHARELIB_USE_D3D11

struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

class ITextureShareItemD3D11
{
public:
	virtual ~ITextureShareItemD3D11() = 0
	{}

	/*
	* Synchronized access to shared texture by name Lock()+Unlock()
	*/
	virtual ID3D11Texture2D*  LockTexture_RenderThread(ID3D11Device* pD3D11Device, const FString& TextureName) = 0;
};

#endif // TEXTURESHARELIB_USE_D3D11
