// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThumbnailExternalCache.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "AssetThumbnail.h"
#include "Misc/ObjectThumbnail.h"
#include "ObjectTools.h"
#include "Serialization/Archive.h"
#include "AssetRegistryModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogThumbnailExternalCache, Log, All);

const int64 FThumbnailExternalCache::LatestVersion = 0;
const uint64 FThumbnailExternalCache::ExpectedHeaderId = 0x424d5548545f4555; // "UE_THUMB"
const FString FThumbnailExternalCache::ThumbnailFilenamePart(TEXT("CachedEditorThumbnails.bin"));
const FString FThumbnailExternalCache::ThumbnailImageFormatName(TEXT("PNG"));

FThumbnailExternalCache::FThumbnailExternalCache()
{
}

FThumbnailExternalCache::~FThumbnailExternalCache()
{
	Cleanup();
}

FThumbnailExternalCache& FThumbnailExternalCache::Get()
{
	static FThumbnailExternalCache ThumbnailExternalCache;
	return ThumbnailExternalCache;
}

void FThumbnailExternalCache::Init()
{
	if (!bHasInit)
	{
		bHasInit = true;

		// Load file for project
		LoadCacheFileIndex(FPaths::ProjectDir() / ThumbnailFilenamePart);

		// Load any thumbnail files for content plugins
		TArray<TSharedRef<IPlugin>> ContentPlugins = IPluginManager::Get().GetEnabledPluginsWithContent();
		for (const TSharedRef<IPlugin>& ContentPlugin : ContentPlugins)
		{
			LoadCacheFileIndexForPlugin(ContentPlugin);
		}

		// Look for cache file when a new path is mounted
		FPackageName::OnContentPathMounted().AddRaw(this, &FThumbnailExternalCache::OnContentPathMounted);

		// Unload cache file when path is unmounted
		FPackageName::OnContentPathDismounted().AddRaw(this, &FThumbnailExternalCache::OnContentPathDismounted);
	}
}

void FThumbnailExternalCache::Cleanup()
{
	if (bHasInit)
	{
		FPackageName::OnContentPathMounted().RemoveAll(this);
		FPackageName::OnContentPathDismounted().RemoveAll(this);
	}
}

bool FThumbnailExternalCache::LoadThumbnailsFromExternalCache(const TSet<FName>& InObjectFullNames, FThumbnailMap& InOutThumbnails)
{
	if (bIsSavingCache)
	{
		return false;
	}

	Init();

	if (CacheFiles.Num() == 0)
	{
		return false;
	}

	static const FString BlueprintGeneratedClassPrefix = TEXT("BlueprintGeneratedClass ");

	int32 NumLoaded = 0;
	for (const FName ObjectFullName : InObjectFullNames)
	{
		FName ThumbnailName = ObjectFullName;

		FNameBuilder NameBuilder(ObjectFullName);
		FStringView NameView(NameBuilder);

		// BlueprintGeneratedClass assets can be displayed in content browser but thumbnails are usually not saved to package file for them
		if (NameView.StartsWith(BlueprintGeneratedClassPrefix) && NameView.EndsWith(TEXT("_C")))
		{
			// Look for the thumbnail of the Blueprint version of this object instead
			FNameBuilder ModifiedNameBuilder;
			ModifiedNameBuilder.Append(TEXT("Blueprint "));
			FStringView ViewToAppend = NameView;
			ViewToAppend.RightChopInline(BlueprintGeneratedClassPrefix.Len());
			ViewToAppend.LeftChopInline(2);
			ModifiedNameBuilder.Append(ViewToAppend);
			ThumbnailName = FName(ModifiedNameBuilder.ToView());
		}

		for (TPair<FString, TSharedPtr<FThumbnailCacheFile>>& It : CacheFiles)
		{
			TSharedPtr<FThumbnailCacheFile>& ThumbnailCacheFile = It.Value;
			if (FThumbnailEntry* Found = ThumbnailCacheFile->NameToEntry.Find(ThumbnailName))
			{
				if (ThumbnailCacheFile->bUnableToOpenFile == false)
				{
					if (TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*ThumbnailCacheFile->Filename)))
					{
						FileReader->Seek(Found->Offset);

						if (ensure(!FileReader->IsError()))
						{
							FObjectThumbnail ObjectThumbnail;
							(*FileReader) << ObjectThumbnail;
							
							InOutThumbnails.Add(ObjectFullName, ObjectThumbnail);
							++NumLoaded;
						}
					}
					else
					{
						// Avoid retrying if file no longer exists
						ThumbnailCacheFile->bUnableToOpenFile = true;
					}
				}
			}
		}
	}

	return NumLoaded > 0;
}

bool FThumbnailExternalCache::SaveExternalCache(const FString& InFilename, TArray<FAssetData>& AssetDatas)
{
	bIsSavingCache = true;
	if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InFilename)))
	{
		SaveExternalCache(*FileWriter, AssetDatas);
		bIsSavingCache = false;
		return true;
	}

	bIsSavingCache = false;

	return false;
}

void FThumbnailExternalCache::SaveExternalCache(FArchive& Ar, const TArray<FAssetData>& AssetDatas) const
{
	FThumbnailExternalCacheHeader Header;
	Header.HeaderId = ExpectedHeaderId;
	Header.Version = LatestVersion;
	Header.Flags = 0;
	Header.ImageFormatName = FThumbnailExternalCache::ThumbnailImageFormatName;
	Header.Serialize(Ar);
	const int64 ThumbnailTableOffsetPos = Ar.Tell() - sizeof(int64);

	struct FPackageThumbnailRecord
	{
		FName Name;
		int64 Offset = 0;
	};

	const int32 NumAssetDatas = AssetDatas.Num();

	FScopedSlowTask SlowTask(NumAssetDatas / 5000.0, FText::FromString("Saving Thumbnail Cache"));
	SlowTask.MakeDialog(/*bShowCancelButton*/ true);

	TArray<FPackageThumbnailRecord> PackageThumbnailRecords;
	PackageThumbnailRecords.Reserve(NumAssetDatas);

	FString CustomThumbnailTagValue;
	FString ObjectFullName;
	int64 TotalCompressedBytes = 0;
	int32 Counter = 0;
	for (const FAssetData& AssetData : AssetDatas)
	{
		FAssetData CustomThumbnailAsset;
		CustomThumbnailTagValue.Reset();
		if (AssetData.GetTagValue(FAssetThumbnailPool::CustomThumbnailTagName, CustomThumbnailTagValue))
		{
			if (FPackageName::IsValidObjectPath(CustomThumbnailTagValue))
			{
				CustomThumbnailAsset = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get().GetAssetByObjectPath(*CustomThumbnailTagValue);
			}
		}

		FThumbnailMap ThumbnailMap;
		FObjectThumbnail* LoadedThumbnail = nullptr;
		const FAssetData* AssetDataToUse = nullptr;

		if (CustomThumbnailAsset.IsValid())
		{
			AssetDataToUse = &CustomThumbnailAsset;
			LoadedThumbnail = LoadThumbnailFromPackage(CustomThumbnailAsset, ThumbnailMap);
		}

		if (!LoadedThumbnail)
		{
			AssetDataToUse = &AssetData;
			LoadedThumbnail = LoadThumbnailFromPackage(AssetData, ThumbnailMap);
		}

		if (LoadedThumbnail)
		{
			AssetDataToUse->GetFullName(ObjectFullName);

			FPackageThumbnailRecord& PackageThumbnailRecord = PackageThumbnailRecords.AddDefaulted_GetRef();
			PackageThumbnailRecord.Name = FName(ObjectFullName);
			PackageThumbnailRecord.Offset = Ar.Tell();

			if (LoadedThumbnail->GetCompressedDataSize() == 0)
			{
				LoadedThumbnail->CompressImageData();
			}

			LoadedThumbnail->Serialize(Ar);

			TotalCompressedBytes += LoadedThumbnail->GetCompressedDataSize();
		}

		if ((++Counter % 5000) == 0)
		{
			SlowTask.EnterProgressFrame(1.f);

			if (SlowTask.ShouldCancel())
			{
				break;
			}
		}
	}

	// Table of contents
	int64 NewThumbnailTableOffset = Ar.Tell();

	int64 NumPackages = PackageThumbnailRecords.Num();
	Ar << NumPackages;

	FString ThumbnailNameString;
	for (FPackageThumbnailRecord& PackageThumbnailRecord : PackageThumbnailRecords)
	{
		ThumbnailNameString.Reset();
		PackageThumbnailRecord.Name.AppendString(ThumbnailNameString);
		Ar << ThumbnailNameString;
		Ar << PackageThumbnailRecord.Offset;
	}

	// Modify top of archive to know where table of contents is located
	Ar.Seek(ThumbnailTableOffsetPos);
	Ar << NewThumbnailTableOffset;

	UE_LOG(LogThumbnailExternalCache, Log, TEXT("Thumbnail cache saved. Thumbnails: %d, %f MB"), PackageThumbnailRecords.Num(), (TotalCompressedBytes / (1024.0 * 1024.0)));
}

FObjectThumbnail* FThumbnailExternalCache::LoadThumbnailFromPackage(const FAssetData& AssetData, FThumbnailMap& ThumbnailMap) const
{
	FString PackageFilename;
	if (FPackageName::DoesPackageExist(AssetData.PackageName.ToString(), NULL, &PackageFilename))
	{
		TSet<FName> ObjectFullNames;
		FName ObjectFullName = FName(*AssetData.GetFullName());
		ObjectFullNames.Add(ObjectFullName);

		ThumbnailTools::LoadThumbnailsFromPackage(PackageFilename, ObjectFullNames, ThumbnailMap);

		return ThumbnailMap.Find(ObjectFullName);
	}

	return nullptr;
}

void FThumbnailExternalCache::OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	if (TSharedPtr<IPlugin> FoundPlugin = IPluginManager::Get().FindPluginFromPath(InAssetPath))
	{
		LoadCacheFileIndexForPlugin(FoundPlugin);
	}
}

void FThumbnailExternalCache::OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	if (TSharedPtr<IPlugin> FoundPlugin = IPluginManager::Get().FindPluginFromPath(InAssetPath))
	{
		if (FoundPlugin->CanContainContent())
		{
			FString Filename = FoundPlugin->GetBaseDir() / ThumbnailFilenamePart;
			CacheFiles.Remove(Filename);
		}
	}
}

void FThumbnailExternalCache::LoadCacheFileIndexForPlugin(const TSharedPtr<IPlugin> InPlugin)
{
	if (InPlugin && InPlugin->CanContainContent())
	{
		FString Filename = InPlugin->GetBaseDir() / ThumbnailFilenamePart;
		if (IFileManager::Get().FileExists(*Filename))
		{
			LoadCacheFileIndex(Filename);
		}
	}
}

bool FThumbnailExternalCache::LoadCacheFileIndex(const FString& Filename)
{
	// Stop if attempt to load already made
	if (CacheFiles.Contains(Filename))
	{
		return true;
	}

	// Track file
	TSharedPtr<FThumbnailCacheFile> ThumbnailCacheFile = MakeShared<FThumbnailCacheFile>();
	ThumbnailCacheFile->Filename = Filename;
	ThumbnailCacheFile->bUnableToOpenFile = true;
	CacheFiles.Add(Filename, ThumbnailCacheFile);

	// Attempt load index of file
	if (TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*Filename)))
	{
		if (LoadCacheFileIndex(*FileReader, ThumbnailCacheFile))
		{
			ThumbnailCacheFile->bUnableToOpenFile = false;
			return true;
		}
	}

	return false;
}

bool FThumbnailExternalCache::LoadCacheFileIndex(FArchive& Ar, const TSharedPtr<FThumbnailCacheFile>& CacheFile)
{
	FThumbnailExternalCacheHeader& Header = CacheFile->Header;
	Header.Serialize(Ar);

	if (Header.HeaderId != ExpectedHeaderId)
	{
		return false;
	}

	if (Header.Version != 0)
	{
		return false;
	}

	Ar.Seek(Header.ThumbnailTableOffset);

	int64 NumPackages = 0;
	Ar << NumPackages;

	CacheFile->NameToEntry.Reserve(NumPackages);

	FString PackageNameString;
	for (int64 i=0; i < NumPackages; ++i)
	{
		PackageNameString.Reset();
		Ar << PackageNameString;

		FThumbnailEntry NewEntry;
		Ar << NewEntry.Offset;

		CacheFile->NameToEntry.Add(FName(PackageNameString), NewEntry);
	}

	return true;
}
