// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "ISourceControlModule.h"
#include "ObjectTools.h"
#include "ImageUtils.h"

namespace SkeinSourceControlThumbnail
{

bool WriteThumbnailToDisk(const FString& InAssetPath, const FString& InThumbnailPath, int32 InSize /* = 256*/)
{
	FString AssetPackageName = FPackageName::FilenameToLongPackageName(InAssetPath);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		
	// Make sure to call this function using bIncludeOnlyOnDiskAssets=true as iterating in-memory assets can only happen on the main thread and
	// this function gets called by the Skein plugin from a worker thread
	TArray<FAssetData> AssetDatas;
	if (!AssetRegistry.GetAssetsByPackageName(FName(*AssetPackageName), AssetDatas, true) || AssetDatas.Num() == 0)
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("WriteThumbnailToDisk failed because no assets could be found for %s."), *InAssetPath);
#endif
		return false;
	}

	FName FullAssetName = FName(*(AssetDatas[0].GetFullName()));

	FThumbnailMap ThumbnailMap;
	TArray<FName> ObjectNames;
	ObjectNames.Add(FullAssetName);
	if (!ThumbnailTools::ConditionallyLoadThumbnailsForObjects(ObjectNames, ThumbnailMap) || ThumbnailMap.Num() == 0)
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("WriteThumbnailToDisk failed because thumbnails could not be loaded for %s."), *InAssetPath);
#endif
		return false;
	}

	const FObjectThumbnail* ObjectThumbnail = ThumbnailMap.Find(FullAssetName);
	check(ObjectThumbnail != nullptr);

	// Grab the - uncompressed - bytes
	const TArray<uint8>& Bytes = ObjectThumbnail->GetUncompressedImageData();

	// Convert the bytes to colors
	TArray<FColor> Colors;
	Colors.SetNumUninitialized(Bytes.Num() / sizeof(FColor));
	FMemory::Memcpy(Colors.GetData(), Bytes.GetData(), Bytes.Num());

	// Resize if needed
	int32 Width = ObjectThumbnail->GetImageWidth();
	int32 Height = ObjectThumbnail->GetImageHeight();
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
	FImageUtils::PNGCompressImageArray(ObjectThumbnail->GetImageWidth(), ObjectThumbnail->GetImageHeight(), TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), CompressedBitmap);
		
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

}