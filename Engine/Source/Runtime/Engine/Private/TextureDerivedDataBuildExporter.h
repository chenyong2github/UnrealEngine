// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/IoHash.h"

#if WITH_EDITOR

struct FTextureSource;
struct FTextureBuildSettings;
struct FTexturePlatformData;
class UTexture;


/** Collects input/output about the build of textures (i.e. compression) and exports the data to a file for testing of remote texture compression */
class FTextureDerivedDataBuildExporter
{
public:
	void Init(const FString& InKeySuffix);

	void ExportTextureSourceBulkData(FTextureSource& TextureSource);
	void ExportCompositeTextureSourceBulkData(FTextureSource& TextureSource);

	void ExportTextureBuild(const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 MaxInlineMips);
	void ExportTextureOutput(FTexturePlatformData& PlatformData, const FTextureBuildSettings& BuildSettings);
private:
	FString KeySuffix;
	FString ExportRoot;
	FIoHash ExportedTextureBulkDataHash;
	FIoHash ExportedCompositeTextureBulkDataHash;
	bool bEnabled = false;
};

#endif // WITH_EDITOR
