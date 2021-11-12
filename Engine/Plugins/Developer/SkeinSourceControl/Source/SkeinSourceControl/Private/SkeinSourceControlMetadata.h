// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace SkeinSourceControlMetadata
{

/// <summary>
/// Extracts the metadata Skein requires from the package to disk
/// </summary>
/// <param name="InPackagePath">Path to the package</param>
/// <param name="InMetadataPath">Path where to store the metadata. If this is an empty string, no metadata will be extracted</param>
/// <param name="InThumbnailPath">Path where to store the thumbnail. If this is an empty string, no thumbnail will be extracted</param>
/// <param name="InThumbnailSize">Dimensions to use for the thumbnail.</param>
/// <returns></returns>
bool ExtractMetadata(const FString& InPackagePath, const FString& InMetadataPath, const FString& InThumbnailPath, int InThumbnailSize = 256);

}