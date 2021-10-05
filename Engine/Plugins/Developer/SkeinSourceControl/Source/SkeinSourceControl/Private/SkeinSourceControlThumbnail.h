#pragma once

#include "CoreMinimal.h"

namespace SkeinSourceControlThumbnail
{

bool WriteThumbnailToDisk(const FString& InAssetPath, const FString& InThumbnailPath, int32 InSize = 256);

}