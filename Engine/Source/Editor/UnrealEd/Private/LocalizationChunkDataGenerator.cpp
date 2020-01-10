// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizationChunkDataGenerator.h"
#include "LocTextHelper.h"
#include "TextLocalizationResourceGenerator.h"
#include "Internationalization/TextLocalizationResource.h"

#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "IPlatformFileSandboxWrapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogLocalizationChunkDataGenerator, Log, All);

FLocalizationChunkDataGenerator::FLocalizationChunkDataGenerator(TArray<FString> InLocalizationTargetsToChunk, TArray<FString> InAllCulturesToCook)
	: LocalizationTargetsToChunk(MoveTemp(InLocalizationTargetsToChunk))
	, AllCulturesToCook(MoveTemp(InAllCulturesToCook))
{
}

void FLocalizationChunkDataGenerator::GenerateChunkDataFiles(const int32 InChunkId, const TSet<FName>& InPackagesInChunk, const FString& InPlatformName, FSandboxPlatformFile* InSandboxFile, TArray<FString>& OutChunkFilenames)
{
	// We can skip this chunk if it's empty
	if (InPackagesInChunk.Num() == 0)
	{
		return;
	}

	ConditionalCacheLocalizationTargetData();

	// We can skip this if we're not actually chunking any localization data
	if (CachedLocalizationTargetHelpers.Num() == 0)
	{
		return;
	}

	for (const TSharedPtr<FLocTextHelper>& SourceLocTextHelper : CachedLocalizationTargetHelpers)
	{
		// If this target has no helper, then it was either an invalid target or failed to load when caching - skip it here
		if (!SourceLocTextHelper)
		{
			continue;
		}

		// Chunk 0 is the only chunk that can contain non-asset localization data, as it acts as the "catch-all" since it's always available
		// It is also the only chunk that doesn't gain a suffix to make it unique (as it is replacing the offline localization data that is usually staged verbatim)
		const bool bIsPrimaryChunk = InChunkId == 0;

		// Prepare to produce the localization target for this chunk
		const TArray<FString> AvailableCulturesToCook = SourceLocTextHelper->GetAllCultures();
		const FString ChunkTargetName = TextLocalizationResourceUtil::GetLocalizationTargetNameForChunkId(SourceLocTextHelper->GetTargetName(), InChunkId);
		const FString ChunkTargetRoot = (InSandboxFile->GetSandboxDirectory() / InSandboxFile->GetGameSandboxDirectoryName() / TEXT("Content") / TEXT("Localization") / ChunkTargetName).Replace(TEXT("[Platform]"), *InPlatformName);

		// Produce a filtered set of data that can be used to produce the LocRes for each chunk
		bool bChunkHasText = false;
		FLocTextHelper ChunkLocTextHelper(ChunkTargetRoot, FString::Printf(TEXT("%s.manifest"), *ChunkTargetName), FString::Printf(TEXT("%s.archive"), *ChunkTargetName), SourceLocTextHelper->GetNativeCulture(), SourceLocTextHelper->GetForeignCultures(), nullptr);
		ChunkLocTextHelper.LoadAll(ELocTextHelperLoadFlags::Create); // Create the in-memory manifest and archives
		SourceLocTextHelper->EnumerateSourceTexts([bIsPrimaryChunk, SourceLocTextHelper, &bChunkHasText, &ChunkLocTextHelper, &InPackagesInChunk, &AvailableCulturesToCook](TSharedRef<FManifestEntry> InManifestEntry)
		{
			for (const FManifestContext& ManifestContext : InManifestEntry->Contexts)
			{
				bool bIncludeInChunk = false;
				if (FPackageName::IsValidObjectPath(ManifestContext.SourceLocation))
				{
					const FName SourcePackageName = *FPackageName::ObjectPathToPackageName(ManifestContext.SourceLocation);
					bIncludeInChunk = InPackagesInChunk.Contains(SourcePackageName);
				}
				else
				{
					bIncludeInChunk = bIsPrimaryChunk;
				}

				if (bIncludeInChunk)
				{
					bChunkHasText = true;
					ChunkLocTextHelper.AddSourceText(InManifestEntry->Namespace, InManifestEntry->Source, ManifestContext);
					for (const FString& CultureToCook : AvailableCulturesToCook)
					{
						TSharedPtr<FArchiveEntry> SourceTranslationEntry = SourceLocTextHelper->FindTranslation(CultureToCook, InManifestEntry->Namespace, ManifestContext.Key, ManifestContext.KeyMetadataObj);
						if (SourceTranslationEntry)
						{
							ChunkLocTextHelper.AddTranslation(CultureToCook, SourceTranslationEntry.ToSharedRef());
						}
					}
				}
			}

			return true; // continue enumeration
		}, false);

		// If this chunk has no localization data then we can skip generating the LocRes (unless it's the primary chunk)
		if (!bIsPrimaryChunk && !bChunkHasText)
		{
			continue;
		}

		// Save the manifest and archives for debug purposes, but don't add them to the build
		// We don't care if this fails as it's only for debugging
		ChunkLocTextHelper.SaveAll();

		// Produce the LocMeta file for the chunk target
		{
			const FString ChunkLocMetaFilename = ChunkTargetRoot / FString::Printf(TEXT("%s.locmeta"), *ChunkTargetName);
			
			FTextLocalizationMetaDataResource ChunkLocMeta;
			if (FTextLocalizationResourceGenerator::GenerateLocMeta(ChunkLocTextHelper, FString::Printf(TEXT("%s.locres"), *ChunkTargetName), ChunkLocMeta) && ChunkLocMeta.SaveToFile(ChunkLocMetaFilename))
			{
				OutChunkFilenames.Add(ChunkLocMetaFilename);
			}
			else
			{
				UE_LOG(LogLocalizationChunkDataGenerator, Error, TEXT("Failed to generate meta-data for localization target '%s' when chunking localization data."), *ChunkTargetName);
			}
		}

		// Produce the LocRes files for each culture of the chunk target
		for (const FString& CultureToCook : AvailableCulturesToCook)
		{
			// This is an extra sanity check as the native culture of the target may not be being cooked
			if (!AllCulturesToCook.Contains(CultureToCook))
			{
				continue;
			}

			const FString ChunkLocResFilename = ChunkTargetRoot / CultureToCook / FString::Printf(TEXT("%s.locres"), *ChunkTargetName);

			FTextLocalizationResource ChunkLocRes;
			TMap<FName, TSharedRef<FTextLocalizationResource>> UnusedPerPlatformLocRes;
			if (FTextLocalizationResourceGenerator::GenerateLocRes(ChunkLocTextHelper, CultureToCook, EGenerateLocResFlags::None, ChunkLocResFilename, ChunkLocRes, UnusedPerPlatformLocRes) && ChunkLocRes.SaveToFile(ChunkLocResFilename))
			{
				OutChunkFilenames.Add(ChunkLocResFilename);
			}
			else
			{
				UE_LOG(LogLocalizationChunkDataGenerator, Error, TEXT("Failed to generate resource data for localization target '%s' when chunking localization data."), *ChunkTargetName);
			}
		}
	}
}

void FLocalizationChunkDataGenerator::ConditionalCacheLocalizationTargetData()
{
	// We can skip this if we're not actually chunking or staging any localization data
	if (LocalizationTargetsToChunk.Num() == 0 || AllCulturesToCook.Num() == 0)
	{
		return;
	}

	// Already cached?
	if (LocalizationTargetsToChunk.Num() == CachedLocalizationTargetHelpers.Num())
	{
		return;
	}

	CachedLocalizationTargetHelpers.Reset(LocalizationTargetsToChunk.Num());
	for (const FString& LocalizationTarget : LocalizationTargetsToChunk)
	{
		TSharedPtr<FLocTextHelper>& SourceLocTextHelper = CachedLocalizationTargetHelpers.AddDefaulted_GetRef();

		// Does this target exist?
		// Note: We only allow game localization targets to be chunked, and the layout is assumed to follow our standard pattern (as used by the localization dashboard and FLocTextHelper)
		const FString SourceRootPath = FPaths::ProjectContentDir() / TEXT("Localization") / LocalizationTarget;
		if (!FPaths::DirectoryExists(SourceRootPath))
		{
			UE_LOG(LogLocalizationChunkDataGenerator, Warning, TEXT("Failed to find localization target for '%s' when chunking localization data. Is it a valid project localization target? - %s"), *LocalizationTarget, *SourceRootPath);
			continue;
		}

		// Work out what the native culture is
		FString SourceNativeCulture;
		{
			const FString SourceLocMetaFilename = SourceRootPath / FString::Printf(TEXT("%s.locmeta"), *LocalizationTarget);

			FTextLocalizationMetaDataResource SourceLocMeta;
			if (!SourceLocMeta.LoadFromFile(SourceLocMetaFilename))
			{
				UE_LOG(LogLocalizationChunkDataGenerator, Error, TEXT("Failed to load meta-data for localization target '%s' when chunking localization data. Re-compile the localization target to generate the LocMeta file."), *LocalizationTarget);
				continue;
			}

			SourceNativeCulture = SourceLocMeta.NativeCulture;
		}

		// Work out which of the desired cultures this target actually supports
		TArray<FString> SourceForeignCulturesToCook;
		for (const FString& CultureToCook : AllCulturesToCook)
		{
			if (CultureToCook == SourceNativeCulture)
			{
				continue;
			}

			const FString LocalizationTargetCulturePath = SourceRootPath / CultureToCook;
			if (FPaths::DirectoryExists(LocalizationTargetCulturePath))
			{
				SourceForeignCulturesToCook.Add(CultureToCook);
			}
		}

		// Load the data for this target
		SourceLocTextHelper = MakeShared<FLocTextHelper>(SourceRootPath, FString::Printf(TEXT("%s.manifest"), *LocalizationTarget), FString::Printf(TEXT("%s.archive"), *LocalizationTarget), SourceNativeCulture, SourceForeignCulturesToCook, nullptr);
		{
			FText LoadError;
			if (!SourceLocTextHelper->LoadAll(ELocTextHelperLoadFlags::Load, &LoadError))
			{
				SourceLocTextHelper.Reset();
				UE_LOG(LogLocalizationChunkDataGenerator, Error, TEXT("Failed to load data for localization target '%s' when chunking localization data: %s"), *LocalizationTarget, *LoadError.ToString());
				continue;
			}
		}
	}
}
