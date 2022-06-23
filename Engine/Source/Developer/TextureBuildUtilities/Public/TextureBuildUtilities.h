// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImageCore.h"
#include "TextureCompressorModule.h" // for FTextureBuildSettings

/***

TextureBuildUtilities is a utility module for shared code that Engine and TextureBuildWorker (no Engine) can both see

for Texture-related functions that don't need Texture.h/UTexture

TextureBuildUtilities is hard-linked to the Engine so no LoadModule/ModuleInterface is needed

***/

namespace UE
{
namespace TextureBuildUtilities
{

TEXTUREBUILDUTILITIES_API bool TextureFormatIsHdr(FName const& InName);

// Removes platform and other custom prefixes from the name.
// Returns plain format name and the non-platform prefix (with trailing underscore).
// i.e. PLAT_BLAH_AutoDXT returns AutoDXT and writes BLAH_ to OutPrefix.
TEXTUREBUILDUTILITIES_API const FName TextureFormatRemovePrefixFromName(FName const& InName, FName& OutPrefix);

FORCEINLINE const FName TextureFormatRemovePrefixFromName(FName const& InName)
{
	FName OutPrefix;
	return TextureFormatRemovePrefixFromName(InName,OutPrefix);
}

TEXTUREBUILDUTILITIES_API ERawImageFormat::Type GetVirtualTextureBuildIntermediateFormat(const FTextureBuildSettings& BuildSettings);

}
}
