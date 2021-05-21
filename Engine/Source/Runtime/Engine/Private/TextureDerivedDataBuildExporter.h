// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/IoHash.h"

#if WITH_EDITOR

struct FTextureSource;
struct FTextureBuildSettings;
struct FTexturePlatformData;
class UTexture;

namespace UE::DerivedData { class IBuild; }

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
	UE::DerivedData::IBuild* DerivedDataBuild;
	FString KeySuffix;
	FString TexturePath;
	FString ExportRoot;
	FString BuildFunctionName;
	FIoHash ExportedTextureBulkDataHash;
	uint64 ExportedTextureBulkDataSize {0};
	FIoHash ExportedCompositeTextureBulkDataHash;
	uint64 ExportedCompositeTextureBulkDataSize {0};
	bool bEnabled = false;
};

#endif // WITH_EDITOR
