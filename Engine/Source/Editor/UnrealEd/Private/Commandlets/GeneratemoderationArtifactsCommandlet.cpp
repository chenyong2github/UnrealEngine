// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GenerateModerationArtifactsCommandlet.cpp: Commandlet provides basic package iteration functionaliy for derived commandlets
=============================================================================*/
#include "Commandlets/GenerateModerationArtifactsCommandlet.h"
#include "CoreMinimal.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/Texture.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"

/**-----------------------------------------------------------------------------
 *	UGenerateModerationArtifactsCommandlet commandlet.
 *
 * This commandlet exposes some functionality for iterating packages 
 *
 *
----------------------------------------------------------------------------**/

DEFINE_LOG_CATEGORY(LogModerationArtifactsCommandlet);

UGenerateModerationArtifactsCommandlet::UGenerateModerationArtifactsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UGenerateModerationArtifactsCommandlet::InitializeParameters( const TArray<FString>& Tokens, TArray<FString>& PackageNames )
{
	

	for (int32 SwitchIdx = 0; SwitchIdx < Switches.Num(); SwitchIdx++)
	{
		const FString& CurrentSwitch = Switches[SwitchIdx];
		if (FParse::Value(*CurrentSwitch, TEXT("OutputDir="), OutputPath))
		{
			continue;
		}
	}

	return Super::InitializeParameters(Tokens, PackageNames);
}

void UGenerateModerationArtifactsCommandlet::PerformAdditionalOperations(class UPackage* Package, bool& bSavePackage)
{

	// need to process localization, fstrings in structs, probably need to do something special with datatables... 

	GatherLocalizationFromPackage(Package);
}



void UGenerateModerationArtifactsCommandlet::GatherFStringsFromObject(class UObject* Object)
{
	
}


void UGenerateModerationArtifactsCommandlet::GatherLocalizationFromPackage(class UPackage* Package)
{

/*
	TArray<FGatherableTextData> GatherableTextDataArray;

	// Gathers from the given package
	EPropertyLocalizationGathererResultFlags GatherableTextResultFlags = EPropertyLocalizationGathererResultFlags::Empty;
	FPropertyLocalizationDataGatherer(GatherableTextDataArray, Package, GatherableTextResultFlags);

	if (PackagePendingGather.PackageLocCacheState != EPackageLocCacheState::Cached)
	{
		ProcessGatherableTextDataArray(GatherableTextDataArray);
	}
*/

#if 0
	int32 NumPackagesFailed = 0;
	TArray<FName> PackagesWithStaleGatherCache;
	TArray<FGatherableTextData> GatherableTextDataArray;
	while (PackagesPendingGather.Num() > 0)
	{
		const FPackagePendingGather PackagePendingGather = PackagesPendingGather.Pop(/ *bAllowShrinking* /false);
		const FNameBuilder PackageNameStr(PackagePendingGather.PackageName);

		const int32 CurrentPackageNum = ++NumPackagesProcessed + NumPackagesFailed;
		const float PercentageComplete = (((float)CurrentPackageNum / (float)PackageCount) * 100.0f);
		UE_LOG(LogGatherTextFromAssetsCommandlet, Display, TEXT("[%6.2f%%] Loading package: '%s'..."), PercentageComplete, *PackageNameStr);

		UPackage* Package = nullptr;
		{
			FLoadPackageLogOutputRedirector::FScopedCapture ScopedCapture(&LogOutputRedirector, *PackageNameStr);
			Package = LoadPackage(nullptr, *PackageNameStr, LOAD_NoWarn | LOAD_Quiet);
		}

		if (!Package)
		{
			UE_LOG(LogGatherTextFromAssetsCommandlet, Warning, TEXT("Failed to load package: '%s'."), *PackageNameStr);
			++NumPackagesFailed;
			continue;
		}

		// Tick background tasks
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->ProcessAsyncResults(true, false);
		}
		if (GDistanceFieldAsyncQueue)
		{
			GDistanceFieldAsyncQueue->ProcessAsyncTasks();
		}

		// Because packages may not have been resaved after this flagging was implemented, we may have added packages to load that weren't flagged - potential false positives.
		// The loading process should have reflagged said packages so that only true positives will have this flag.
		if (Package->RequiresLocalizationGather())
		{
			UE_LOG(LogGatherTextFromAssetsCommandlet, Display, TEXT("[%6.2f%%] Gathering package: '%s'..."), PercentageComplete, *PackageNameStr);

			// Gathers from the given package
			EPropertyLocalizationGathererResultFlags GatherableTextResultFlags = EPropertyLocalizationGathererResultFlags::Empty;
			FPropertyLocalizationDataGatherer(GatherableTextDataArray, Package, GatherableTextResultFlags);

			bool bSavePackage = false;

			// Optionally check to see whether the clean gather we did is in-sync with the gather cache and deal with it accordingly
			if ((bReportStaleGatherCache || bFixStaleGatherCache) && PackagePendingGather.PackageLocCacheState == EPackageLocCacheState::Cached)
			{
				// Look for any structurally significant changes (missing, added, or changed texts) in the cache
				// Ignore insignificant things (like source changes caused by assets moving or being renamed)
				if (EnumHasAnyFlags(GatherableTextResultFlags, EPropertyLocalizationGathererResultFlags::HasTextWithInvalidPackageLocalizationID)
					|| !IsGatherableTextDataIdentical(GatherableTextDataArray, PackagePendingGather.GatherableTextDataArray))
				{
					PackagesWithStaleGatherCache.Add(PackagePendingGather.PackageName);

					if (bFixStaleGatherCache)
					{
						bSavePackage = true;
					}
				}
			}

			// Optionally save the package if it is missing a gather cache
			if (bFixMissingGatherCache && PackagePendingGather.PackageLocCacheState == EPackageLocCacheState::Uncached_TooOld)
			{
				bSavePackage = true;
			}

			// Re-save the package to attempt to fix it?
			if (bSavePackage)
			{
				UE_LOG(LogGatherTextFromAssetsCommandlet, Display, TEXT("Resaving package: '%s'..."), *PackageNameStr);

				bool bSavedPackage = false;
				{
					FLoadPackageLogOutputRedirector::FScopedCapture ScopedCapture(&LogOutputRedirector, PackageNameStr);
					bSavedPackage = FLocalizedAssetSCCUtil::SavePackageWithSCC(SourceControlInfo, Package, PackagePendingGather.PackageFilename);
				}

				if (!bSavedPackage)
				{
					UE_LOG(LogGatherTextFromAssetsCommandlet, Warning, TEXT("Failed to resave package: '%s'."), *PackageNameStr);
				}
			}

			// This package may have already been cached in cases where we're reporting or fixing assets with a stale gather cache
			// This check prevents it being gathered a second time
			if (PackagePendingGather.PackageLocCacheState != EPackageLocCacheState::Cached)
			{
				ProcessGatherableTextDataArray(GatherableTextDataArray);
			}

			GatherableTextDataArray.Reset();
		}
	#endif
}


void UGenerateModerationArtifactsCommandlet::PerformAdditionalOperations(class UObject* Object, bool& bSavePackage)
{
	bSavePackage = false;

	GatherFStringsFromObject(Object);

	if (Object->GetClass()->IsChildOf(UTexture::StaticClass()))
	{
		GenerateArtifact(StaticCast<UTexture*>(Object));
	}
	else if (Object->GetClass()->IsChildOf(UStaticMeshComponent::StaticClass()))
	{
		GenerateArtifact(StaticCast<UStaticMeshComponent*>(Object));
	}
#if 0 // todo add suppport for datatables 
	else if (Object->GetClass()->IsChildOf(UDataTable::StaticClass()))
	{
		GenerateArtifact(StaticCast<UDataTable*>(Object));
	}
#endif
}

FString UGenerateModerationArtifactsCommandlet::CreateOutputFileName(class UObject* Object, const FString& Extension)
{
	UPackage* Package = Object->GetOutermost();
	const FPackagePath& PackagePath = Package->GetLoadedPath();
	
	FMD5Hash FileHash = FMD5Hash::HashFile(*PackagePath.GetLocalFullPath());
	FString FileName = FString::Printf(TEXT("%s-%s.%s"), *Object->GetClass()->GetName(), *LexToString(FileHash), *Extension);
	FString FullPath = FPaths::Combine(OutputPath, FileName);
	UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Created moderation file %s for asset %s"), *FullPath, *Object->GetPathName());
	return FullPath;
}

void UGenerateModerationArtifactsCommandlet::GenerateArtifact(UTexture* Texture)
{
	UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found texture %s"), *Texture->GetFullName());

	if (Texture->Source.IsValid())
	{
		FString OutputFileName = CreateOutputFileName(Texture, TEXT("png"));


		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);



		FImage Image;
		if (ImageWrapper.IsValid() && Texture->Source.GetMipImage(Image, 0))
		{
			ERGBFormat RGBFormat;
			int BitsPerChannel;
			switch (Image.Format)
			{
			case ERawImageFormat::G8:
				RGBFormat = ERGBFormat::Gray;
				BitsPerChannel = 8;
				break;
			case ERawImageFormat::BGRA8:
				RGBFormat = ERGBFormat::BGRA;
				BitsPerChannel = 8;
				break;
			case ERawImageFormat::BGRE8:
				RGBFormat = ERGBFormat::BGRE;
				BitsPerChannel = 8;
				break;
			case ERawImageFormat::RGBA16:
				RGBFormat = ERGBFormat::RGBA;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::RGBA16F:
				RGBFormat = ERGBFormat::RGBAF;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::RGBA32F:
				RGBFormat = ERGBFormat::RGBAF;
				BitsPerChannel = 32;
				break;
			case ERawImageFormat::G16:
				RGBFormat = ERGBFormat::Gray;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::R16F:
				RGBFormat = ERGBFormat::GrayF;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::R32F:
				RGBFormat = ERGBFormat::GrayF;
				BitsPerChannel = 32;
				break;
			default:
				UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Texture %s source image format %s is unsupported"), *Texture->GetFullName(), ERawImageFormat::GetName(Image.Format));
				return;
			}
			
			if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Image.RawData.GetData(), Image.RawData.Num(), Image.GetWidth(), Image.GetHeight(), RGBFormat, BitsPerChannel))
			{
				EImageCompressionQuality PngQuality = EImageCompressionQuality::Default; // 0 means default 
				TArray64<uint8> CompressedData = ImageWrapper->GetCompressed((int32)PngQuality);
				if (CompressedData.Num() > 0)
				{
					FFileHelper::SaveArrayToFile(CompressedData, *OutputFileName);
				}
			}
		}

	}
	/*

	ImageWrapper->SetRaw(SurfaceData.GetData(), SurfaceData.GetAllocatedSize(), UnprojectedAtlasWidth, UnprojectedAtlasHeight, ERGBFormat::BGRA, 32);
	const TArray64<uint8> PNGDataUnprojected = ImageWrapper->GetCompressed(100);
	FFileHelper::SaveArrayToFile(PNGDataUnprojected, *AtlasNameUnprojected);
	ImageWrapper.Reset();*/
	
}

/*
void UGenerateModerationArtifactsCommandlet::GenerateArtifact(UDataTable* DataTable)
{
	check(0);  // not implemented yet
	// FString UDataTable::GetTableAsString(const EDataTableExportFlags InDTExportFlags) const
}*/
void UGenerateModerationArtifactsCommandlet::GenerateArtifact(UStaticMeshComponent* StaticMesh)
{
	UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found staticmesh %s"), *StaticMesh->GetFullName());
}