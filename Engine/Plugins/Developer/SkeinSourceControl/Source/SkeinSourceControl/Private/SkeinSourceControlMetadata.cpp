// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlMetadata.h"
#include "SkeinSourceControlModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/FileHelper.h"
#include "Algo/Transform.h"
#include "ISourceControlModule.h"
#include "ObjectTools.h"
#include "ImageUtils.h"

namespace SkeinSourceControlMetadata
{
	
// Tags are somewhat odd. If the asset is loaded, it's expected that the Tags are gathered dynamically
// through the UObject::GetAssetRegistryTags function. If it's not loaded, the TagsAndValues member
// on the FAssetData contains the latest serialized data, which appears to get pruned once the UObject
// is loaded.
static bool ExtractTags(IAssetRegistry& InAssetRegistry, FAssetData& InAssetData, TMap<FName, FString>& OutTags)
{
	if (InAssetData.IsAssetLoaded())
	{
		// UObject::GetAssetRegistryTags might access render resources so we need to tag this thread
		// as mentioned in HAL/ThreadingBase to avoid triggering an assert.
		FTaskTagScope Scope(ETaskTag::EParallelRenderingThread);

		UObject* Asset = InAssetData.GetAsset();

		// UWorld::GetAssetRegistryTags broadcasts and one of the registered delegates asserts on
		// a game thread requirement in FActorIteratorState.
		static FName NAME_World("World");
		if (Asset->GetClass()->GetFName() != NAME_World)
		{
			TArray<UObject::FAssetRegistryTag> AssetRegistryTags;
			Asset->GetAssetRegistryTags(AssetRegistryTags);

			for (TArray<UObject::FAssetRegistryTag>::TConstIterator TagIter(AssetRegistryTags); TagIter; ++TagIter)
			{
				OutTags.Add(TagIter->Name, TagIter->Value);
			}
		}
	}
	else
	{
		auto IsValid = [](const TTuple<FName, FString>& InTuple)
		{
			static FName NAME_FiBData("FiBData");
			static FName NAME_ClassFlags("ClassFlags");
			static FName NAME_AssetImportData("AssetImportData");

			return InTuple.Key != NAME_FiBData
				&& InTuple.Key != NAME_ClassFlags
				&& InTuple.Key != NAME_AssetImportData;
		};

		Algo::TransformIf(InAssetData.TagsAndValues.CopyMap(), OutTags, IsValid, [](const TTuple<FName, FString>& InTuple) { return InTuple; });
	}

	return true;
}

// Dependencies are straightforward. The AssetRegistry provides a nice interface for them.
// We do reformat to the project root relative asset name format that's being used by Skein and its WebUI.
// For example:
//   Dep In : /Game/Environment/Textures/T_Vista_Grass_N
//   Dep Out: /Content/Environment/Textures/T_Vista_Grass_N.uasset
static bool ExtractDependencies(IAssetRegistry& InAssetRegistry, FAssetData& InAssetData, TArray<FString>& OutDependencies)
{
	FSkeinSourceControlModule& SkeinSourceControl = FModuleManager::LoadModuleChecked<FSkeinSourceControlModule>("SkeinSourceControl");
	FString SkeinProjectRoot = SkeinSourceControl.GetProvider().GetSkeinProjectRoot() + TEXT("/");

	TArray<FName> Dependencies;
	if (InAssetRegistry.GetDependencies(InAssetData.PackageName, Dependencies))
	{
		for (const FName& Dependency : Dependencies)
		{
			FString RelativeDepFileName;
			if (FPackageName::TryConvertLongPackageNameToFilename(Dependency.ToString(), RelativeDepFileName))
			{
				FString DepFileName;

				if (DepFileName.IsEmpty())
				{
					FString MapFileName = RelativeDepFileName + FPackageName::GetMapPackageExtension();
					if (IFileManager::Get().FileExists(*MapFileName))
					{
						DepFileName = FPaths::ConvertRelativePathToFull(MapFileName);
					}
				}

				if (DepFileName.IsEmpty())
				{
					FString AssetFileName = RelativeDepFileName + FPackageName::GetAssetPackageExtension();
					if (IFileManager::Get().FileExists(*AssetFileName))
					{
						DepFileName = FPaths::ConvertRelativePathToFull(AssetFileName);
					}
				}

				if (!DepFileName.IsEmpty())
				{
					if (FPaths::IsUnderDirectory(DepFileName, SkeinProjectRoot))
					{
						if (FPaths::MakePathRelativeTo(DepFileName, *SkeinProjectRoot))
						{
							OutDependencies.Add(DepFileName);
						}
					}
				}
			}
		}

		return true;
	}
	return false;
}

// ThumbnailTools provides some nice features for us. The Thumbnail could be missing though.
static bool ExtractThumbnail(IAssetRegistry& InAssetRegistry, FAssetData& InAssetData, FObjectThumbnail& OutThumbnail)
{
	FString ObjectFullName = InAssetData.GetFullName();

	// Check if there's one cached already
	const FObjectThumbnail* ExistingThumbnail = ThumbnailTools::FindCachedThumbnail(ObjectFullName);
	if (ExistingThumbnail != nullptr)
	{
		OutThumbnail = *ExistingThumbnail;
		return true;
	}

	// Load from disk instead
	TArray< FName > ObjectFullNames;
	FName ObjectFullNameFName(*ObjectFullName);
	ObjectFullNames.Add(ObjectFullNameFName);

	FThumbnailMap LoadedThumbnails;
	if (ThumbnailTools::ConditionallyLoadThumbnailsForObjects(ObjectFullNames, LoadedThumbnails))
	{
		const FObjectThumbnail* LoadedThumbnail = LoadedThumbnails.Find(ObjectFullNameFName);
		if (LoadedThumbnail != nullptr)
		{
			OutThumbnail = *LoadedThumbnail;
			return true;
		}
	}

	return false;
}

static bool WriteThumbnailToDisk(const FString& InThumbnailPath, const FObjectThumbnail& InObjectThumbnail, int32 InSize = 256)
{
	int32 Width = InObjectThumbnail.GetImageWidth();
	int32 Height = InObjectThumbnail.GetImageHeight();
	if (Width == 0 || Height == 0)
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("WriteThumbnailToDisk failed because dimensions are invalid for %s."), *InThumbnailPath);
#endif
		return false;
	}

	// Grab the - uncompressed - bytes
	const TArray<uint8>& Bytes = InObjectThumbnail.GetUncompressedImageData();

	// Convert the bytes to colors
	TArray<FColor> Colors;
	Colors.SetNumUninitialized(Bytes.Num() / sizeof(FColor));
	FMemory::Memcpy(Colors.GetData(), Bytes.GetData(), Bytes.Num());

	// Resize if needed
	if (Width != InSize || Height != InSize)
	{
		TArray<FColor> ResizedColors;
		ResizedColors.SetNum(InSize * InSize);

		const bool bLinearSpace = false;
		const bool bForceOpaqueOutput = false;
		FImageUtils::ImageResize(Width, Height, Colors, InSize, InSize, ResizedColors, bLinearSpace, bForceOpaqueOutput);

		Colors = MoveTemp(ResizedColors);
	}

	// Compress the image data
	TArray64<uint8> CompressedBitmap;
	FImageUtils::PNGCompressImageArray(InSize, InSize, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), CompressedBitmap);
		
	// Write to disk as a PNG to maintain transparency		
	if (!FFileHelper::SaveArrayToFile(CompressedBitmap, *InThumbnailPath))
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("WriteThumbnailToDisk failed because image data could not be written to %s."), *InThumbnailPath);
#endif
		return false;
	}

	return true;
}

static bool WriteMetadataToDisk(const FString& InMetadataPath, const TMap<FName, FString>& InTags, const TArray<FString>& InDependencies)
{
	// Build JsonObject
	TArray<TSharedPtr<FJsonValue>> JsonTagsArray;
	JsonTagsArray.Reserve(InTags.Num());
	for (const TPair<FName, FString>& Tag : InTags)
	{
		TSharedRef<FJsonObject> JsonTag = MakeShareable(new FJsonObject);
		JsonTag->SetStringField("name", Tag.Key.ToString());
		JsonTag->SetStringField("value", Tag.Value);
		JsonTagsArray.Add(MakeShareable(new FJsonValueObject(JsonTag)));
	}

	TArray<TSharedPtr<FJsonValue>> JsonDepsArray;
	JsonDepsArray.Reserve(InDependencies.Num());
	for (const FString& Dependency : InDependencies)
	{
		JsonDepsArray.Add(MakeShareable(new FJsonValueString(Dependency)));
	}

	TSharedRef<FJsonObject> JsonData = MakeShareable(new FJsonObject);
	JsonData->SetArrayField("tags", JsonTagsArray);
	JsonData->SetArrayField("deps", JsonDepsArray);

	// Write Json to disk
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(JsonData, JsonWriter))
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("WriteMetadataToDisk failed because FJsonSerializer::Serialize failed."));
#endif
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonString, *InMetadataPath))
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("WriteMetadataToDisk failed because SaveStringToFile %s failed."), *InMetadataPath);
#endif
		return false;
	}

	return true;
}

bool ExtractMetadata(const FString& InPackagePath, const FString& InMetadataPath, const FString& InThumbnailPath, int InThumbnailSize /* = 256 */)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FString PackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(InPackagePath, PackageName))
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("ExtractMetadata failed because TryConvertFilenameToLongPackageName %s failed."), *InPackagePath);
#endif
		return false;
	}

	TArray<FAssetData> AssetDatas;
	if (!AssetRegistry.GetAssetsByPackageName(*PackageName, AssetDatas, true) || AssetDatas.Num() == 0)
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("ExtractMetadata failed because GetAssetsByPackageName %s failed."), *InPackagePath);
#endif
		return false;
	}

	FAssetData& AssetData = AssetDatas[0];

	TMap<FName, FString> Tags;
	ExtractTags(AssetRegistry, AssetData, Tags);

	TArray<FString> Dependencies;
	ExtractDependencies(AssetRegistry, AssetData, Dependencies);

	FObjectThumbnail Thumbnail;
	ExtractThumbnail(AssetRegistry, AssetData, Thumbnail);

	bool bErrorWritingMetadata = false;
	if (!WriteMetadataToDisk(InMetadataPath, Tags, Dependencies))
	{
		bErrorWritingMetadata = true;
	}

	bool bErrorWritingThumbnail = false;
	if (!WriteThumbnailToDisk(InThumbnailPath, Thumbnail, InThumbnailSize))
	{
		bErrorWritingThumbnail = true;
	}

	return !bErrorWritingMetadata && !bErrorWritingThumbnail;
}

}