// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreTypes.h"

class FCbObject;
class FString;
class UTexture;
struct FTextureBuildSettings;

FString GetTextureBuildFunctionName(const FTextureBuildSettings& BuildSettings);
FCbObject SaveTextureBuildSettings(const FString& KeySuffix, const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips);

#endif // WITH_EDITOR
