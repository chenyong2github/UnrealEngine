// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace SkeinSourceControlThumbnail
{

bool WriteThumbnailToDisk(const FString& InAssetPath, const FString& InThumbnailPath, int32 InSize = 256);

}