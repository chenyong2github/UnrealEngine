// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Containers/StringView.h"
#include "Templates/Function.h"

namespace UE::DerivedData { class FBuildActionBuilder; }

class FCbObject;
class UTexture;
struct FTextureBuildSettings;

using FTextureConstantOperator = TFunctionRef<void (FStringView Key, const FCbObject& Value)>;

// Suitable for use with an action builder or in the future a definition builder
FString GetTextureBuildFunctionName(const FTextureBuildSettings& BuildSettings);
void ComposeTextureBuildFunctionConstants(const FString& KeySuffix, const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips, FTextureConstantOperator Operator);

#endif // WITH_EDITOR
