// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"

struct FGLTFTextureUtility
{
	static bool IsCubemap(const UTexture* Texture);

	static TextureFilter GetDefaultFilter(TextureGroup Group);

	static TTuple<TextureAddress, TextureAddress> GetAddressXY(const UTexture* Texture);

	static UTexture2D* CreateTransientTexture(const void* RawData, int64 ByteLength, const FIntPoint& Size, EPixelFormat Format, bool bUseSRGB = false);
};
