// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreTypes.h"
#include "Containers/StringFwd.h"

class FCbObject;
class FString;
class UTexture;
struct FTextureBuildSettings;

bool TryFindTextureBuildFunction(FStringBuilderBase& OutFunctionName, const class FName& TextureFormatName);
FCbObject SaveTextureBuildSettings(const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips);

#endif // WITH_EDITOR
