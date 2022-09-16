// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"

namespace ContentBundlePaths
{
	constexpr TCHAR ContentBundleFolder[] = TEXT("ContentBundle");

#if WITH_EDITOR
	// InContentBundleExternalActorPackagePath format is : //{Plugin}/{ExternalActorFolder}/{ContentBundleFolder}/{ContentBundleUID}/{LevelPath}/{ExternalActorPackagePath}
	// return format is {LevelPath}/{ExternalActorPackagePath}, empty otherwise
	ENGINE_API FStringView GetRelativeExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath);

	ENGINE_API FGuid GetGuidFromPath(FStringView InPath);
#endif
}