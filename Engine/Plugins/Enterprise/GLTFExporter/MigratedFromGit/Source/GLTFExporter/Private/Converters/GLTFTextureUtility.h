// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"

struct FGLTFTextureUtility
{
	static TextureFilter GetDefaultFilter(TextureGroup Group);

	static TextureAddress GetAddressX(const UTexture* Texture);
	static TextureAddress GetAddressY(const UTexture* Texture);

	static UTexture2D* CreateTransientTexture(const void* RawData, int64 ByteLength, const FIntPoint& Size, EPixelFormat Format, bool bUseSRGB = false);
};
