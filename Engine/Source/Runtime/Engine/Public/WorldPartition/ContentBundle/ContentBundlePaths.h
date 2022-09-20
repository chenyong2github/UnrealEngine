// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"

namespace ContentBundlePaths
{
	constexpr FStringView ContentBundleFolder = TEXTVIEW("/ContentBundle/");

	// Returns "/ContentBundle/"
	constexpr FStringView GetContentBundleFolder()
	{
		return ContentBundleFolder;
	}
	
	// Returns "ContentBundle"
	constexpr FStringView GetContentBundleFolderName()
	{
		// Start at index 1 to remove the first "/", Lenght - 2 to remove the trailing "/"
		return FStringView(&GetContentBundleFolder()[1], GetContentBundleFolder().Len() - 2);
	}

#if WITH_EDITOR
	// return an ExternalActor path following the format : /{MountPoint}/{ExternalActorFolder}/{ContentBundleFolder}/{ContentBundleUID}/{LevelPath}/{ExternalActorPackagePath}
	ENGINE_API FString MakeExternalActorPackagePath(const FString& ContentBundleExternalActorFolder, const FString& ActorName);

	// return true if InPackage follow format : //{MountPoint}/{ExternalActorFolder}/{ContentBundleFolder}/{ContentBundleUID}/, false otherwise
	ENGINE_API bool IsAContentBundlePackagePath(FStringView InPackagePath);

	// InContentBundleExternalActorPackagePath format is : /{MountPoint}/{ExternalActorFolder}/{ContentBundleFolder}/{ContentBundleUID}/{LevelPath}/{ExternalActorPackagePath}
	// return format is /{LevelPath}/{ExternalActorPackagePath}, empty otherwise
	ENGINE_API FStringView GetRelativeExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath);

	// InContentBundleExternalActorPackagePath format is : /{MountPoint}/{ExternalActorFolder}/{ContentBundleFolder}/{ContentBundleUID}/{LevelPath}/{ExternalActorPackagePath}
	// return format is {ContentBundleUID}, 0 otherwise
	ENGINE_API FGuid GetContentBundleGuidFromExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath);

	// InExternalActorPath format is : /{MountPoint}/{ExternalActorFolder}/{ContentBundleFolder}/{ContentBundleUID}/{LevelPath}/{ExternalActorPackagePath}
	// return format is /{ContentBundleFolder}/{ContentBundleUID}/{LevelPath}/{ExternalActorPackagePath}, empty otherwise
	ENGINE_API FStringView GetActorPathRelativeToExternalActors(FStringView InContentBundleExternalActorPackagePath);
#endif
}