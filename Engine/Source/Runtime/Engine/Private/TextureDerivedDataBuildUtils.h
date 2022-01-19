// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreTypes.h"
#include "DerivedDataSharedStringFwd.h"

class FCbObject;
class FName;
class UTexture;
struct FTextureBuildSettings;

UE::DerivedData::FUtf8SharedString FindTextureBuildFunction(FName TextureFormatName);
FCbObject SaveTextureBuildSettings(const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips, bool bUseCompositeTexture);

#endif // WITH_EDITOR
