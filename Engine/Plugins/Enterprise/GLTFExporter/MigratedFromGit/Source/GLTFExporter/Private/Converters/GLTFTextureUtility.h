// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"

struct FGLTFTextureUtility
{
	static TextureAddress GetAddressX(const UTexture* Texture);
	static TextureAddress GetAddressY(const UTexture* Texture);
};
