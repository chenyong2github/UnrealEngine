// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "Misc/ObjectThumbnail.h"

class IPlugin;

class FThumbnailExternalCache
{
public:

	FThumbnailExternalCache();
	~FThumbnailExternalCache();
	
	/** Get thumbnail external cache */
	UNREALED_API static FThumbnailExternalCache& Get();

	/** Load thumbnails for the given object names from external cache */
	UNREALED_API bool LoadThumbnailsFromExternalCache(const TSet<FName>& InObjectFullNames, FThumbnailMap& InOutThumbnails);

	/** Save thumbnails for the given assets to an external file */
	UNREALED_API bool SaveExternalCache(const FString& InFilename, TArray<FAssetData>& AssetDatas);

private:

	enum class EThumbnailExternalCacheHeaderFlags : uint64
	{
		None = 0,
	};

	struct FThumbnailExternalCacheHeader
	{
		uint64 HeaderId = 0;
		uint64 Version = 0;
		uint64 Flags = 0;
		FString ImageFormatName;
		int64 ThumbnailTableOffset = 0;

		void Serialize(FArchive& Ar)
		{
			Ar << HeaderId;
			Ar << Version;
			Ar << Flags;
			Ar << ImageFormatName;
			Ar << ThumbnailTableOffset; // Offset must be serialized last
		}

		bool HasAnyFlags(EThumbnailExternalCacheHeaderFlags FlagsToCheck) const
		{
			return (Flags & (uint64)FlagsToCheck) != 0;
		}
	};

	struct FThumbnailEntry
	{
		int64 Offset = 0;
	};

	struct FThumbnailCacheFile
	{
		bool bUnableToOpenFile = false;
		FString Filename;
		FThumbnailExternalCacheHeader Header;
		TMap<FName, FThumbnailEntry> NameToEntry;
	};
	
	void SaveExternalCache(FArchive& Ar, const TArray<FAssetData>& AssetDatas) const;

	FObjectThumbnail* LoadThumbnailFromPackage(const FAssetData& AssetData, FThumbnailMap& ThumbnailMap) const;

	void Init();

	void Cleanup();

	void OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath);

	void OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath);

	void LoadCacheFileIndexForPlugin(const TSharedPtr<IPlugin> InPlugin);

	bool LoadCacheFileIndex(const FString& Filename);

	bool LoadCacheFileIndex(FArchive& Ar, const TSharedPtr<FThumbnailCacheFile>& CacheFile);

	TMap<FString, TSharedPtr<FThumbnailCacheFile>> CacheFiles;

	bool bHasInit = false;
	bool bIsSavingCache = false;
	static const int64 LatestVersion;
	static const uint64 ExpectedHeaderId;
	static const FString ThumbnailFilenamePart;
	static const FString ThumbnailImageFormatName;
};
