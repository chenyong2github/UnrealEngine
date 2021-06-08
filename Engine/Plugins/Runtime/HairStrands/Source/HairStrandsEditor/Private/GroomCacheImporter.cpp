// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomCacheImporter.h"
#include "GroomAsset.h"
#include "GroomBuilder.h"
#include "GroomCache.h"
#include "GroomImportOptions.h"
#include "HairStrandsImporter.h"
#include "HairStrandsTranslator.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PackageTools.h"

#define LOCTEXT_NAMESPACE "GroomCacheImporter"

static UGroomCache* CreateGroomCache(EGroomCacheType Type, UObject*& InParent, const FString& ObjectName, const EObjectFlags Flags)
{
	// Setup package name and create one accordingly
	FString NewPackageName = InParent->GetOutermost()->GetName();
	if (!NewPackageName.EndsWith(ObjectName))
	{
		// Remove the cache suffix if the parent is from a reimport
		if (NewPackageName.EndsWith("_strands_cache"))
		{
			NewPackageName.RemoveFromEnd("_strands_cache");
		}
		else if (NewPackageName.EndsWith("_guides_cache"))
		{
			NewPackageName.RemoveFromEnd("_guides_cache");
		}

		// Append the correct suffix
		NewPackageName += TEXT("_") + ObjectName;
	}

	NewPackageName = UPackageTools::SanitizePackageName(NewPackageName);

	// Parent package to place new GroomCache
	UPackage* Package = CreatePackage(*NewPackageName);

	FString CompoundObjectName = FPackageName::GetShortName(NewPackageName);
	const FString SanitizedObjectName = ObjectTools::SanitizeObjectName(CompoundObjectName);

	UGroomCache* ExistingTypedObject = FindObject<UGroomCache>(Package, *SanitizedObjectName);
	UObject* ExistingObject = FindObject<UObject>(Package, *SanitizedObjectName);

	if (ExistingTypedObject != nullptr)
	{
		ExistingTypedObject->PreEditChange(nullptr);
		return ExistingTypedObject;
	}
	else if (ExistingObject != nullptr)
	{
		// Replacing an object.  Here we go!
		// Delete the existing object
		const bool bDeleteSucceeded = ObjectTools::DeleteSingleObject(ExistingObject);

		if (bDeleteSucceeded)
		{
			// Force GC so we can cleanly create a new asset (and not do an 'in place' replacement)
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

			// Create a package for each mesh
			Package = CreatePackage(*NewPackageName);
			InParent = Package;
		}
		else
		{
			// failed to delete
			return nullptr;
		}
	}

	UGroomCache* GroomCache = NewObject<UGroomCache>(Package, FName(*SanitizedObjectName), Flags | RF_Public);
	GroomCache->Initialize(Type);

	return GroomCache;
}

UGroomCache* ProcessToGroomCache(FGroomCacheProcessor& Processor, const FGroomAnimationInfo& AnimInfo, FHairImportContext& ImportContext, const FString& ObjectNameSuffix)
{
	if (UGroomCache* GroomCache = CreateGroomCache(Processor.GetType(), ImportContext.Parent, ObjectNameSuffix, ImportContext.Flags))
	{
		Processor.TransferChunks(GroomCache);
		GroomCache->SetGroomAnimationInfo(AnimInfo);

		GroomCache->MarkPackageDirty();
		GroomCache->PostEditChange();
		return GroomCache;
	}
	return nullptr;
}

TArray<UGroomCache*> FGroomCacheImporter::ImportGroomCache(const FString& SourceFilename, TSharedPtr<IGroomTranslator> Translator, const FGroomAnimationInfo& AnimInfo, FHairImportContext& HairImportContext, UGroomAsset* GroomAssetForCache)
{
	bool bSuccess = true;
	FGroomCacheProcessor StrandsProcessor(EGroomCacheType::Strands, AnimInfo.Attributes);
	FGroomCacheProcessor GuidesProcessor(EGroomCacheType::Guides, AnimInfo.Attributes);
	if (Translator->BeginTranslation(SourceFilename))
	{
		const int32 NumFrames = AnimInfo.NumFrames;
		FScopedSlowTask SlowTask(NumFrames, LOCTEXT("ImportGroomCache", "Importing GroomCache frames"));
		SlowTask.MakeDialog();

		// Each frame is translated into a HairDescription and processed into HairGroupData
		for (int32 FrameIndex = AnimInfo.StartFrame; FrameIndex < AnimInfo.EndFrame + 1; ++FrameIndex)
		{
			const uint32 CurrentFrame = FrameIndex - AnimInfo.StartFrame;

			FTextBuilder TextBuilder;
			TextBuilder.AppendLineFormat(LOCTEXT("ImportGroomCacheFrame", "Importing GroomCache frame {0} of {1}"), FText::AsNumber(CurrentFrame), FText::AsNumber(NumFrames));
			SlowTask.EnterProgressFrame(1.f, TextBuilder.ToText());

			FHairDescription FrameHairDescription;
			if (Translator->Translate(FrameIndex, FrameHairDescription, HairImportContext.ImportOptions->ConversionSettings))
			{
				FHairDescriptionGroups HairDescriptionGroups;
				if (!FGroomBuilder::BuildHairDescriptionGroups(FrameHairDescription, HairDescriptionGroups))
				{
					bSuccess = false;
					break;
				}

				const uint32 GroupCount = HairDescriptionGroups.HairGroups.Num();

				TArray<FHairGroupInfoWithVisibility> HairGroupsInfo = GroomAssetForCache->HairGroupsInfo;
				TArray<FHairGroupData> HairGroupsData;
				HairGroupsData.SetNum(GroupCount);
				for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
				{
					//FGroomBuilder::BuildHairGroupData(ProcessedHairDescription, GroomAssetForCache->HairGroupsInterpolation[GroupIndex], GroupIndex, HairGroupsData[GroupIndex]);

					const FHairDescriptionGroup& HairGroup = HairDescriptionGroups.HairGroups[GroupIndex];

					FHairStrandsDatas StrandsData;
					FHairStrandsDatas GuidesData;
					FGroomBuilder::BuildData(HairGroup, GroomAssetForCache->HairGroupsInterpolation[GroupIndex], HairGroupsInfo[GroupIndex], StrandsData, GuidesData);

					FGroomBuilder::BuildBulkData(HairGroup.Info, GuidesData, HairGroupsData[GroupIndex].Guides.BulkData);
					FGroomBuilder::BuildBulkData(HairGroup.Info, StrandsData, HairGroupsData[GroupIndex].Strands.BulkData);

					FHairStrandsInterpolationDatas InterpolationData;
					FGroomBuilder::BuildInterplationData(HairGroup.Info, StrandsData, GuidesData, GroomAssetForCache->HairGroupsInterpolation[GroupIndex].InterpolationSettings, InterpolationData);
					FGroomBuilder::BuildInterplationBulkData(GuidesData, InterpolationData, HairGroupsData[GroupIndex].Strands.InterpolationBulkData);

					FGroomBuilder::BuildClusterBulkData(StrandsData, HairDescriptionGroups.BoundRadius, GroomAssetForCache->HairGroupsLOD[GroupIndex], HairGroupsData[GroupIndex].Strands.ClusterCullingBulkData);
				}

				// The HairGroupData is converted into animated groom data by the GroomCacheProcessor
				StrandsProcessor.AddGroomSample(MoveTemp(HairGroupsData));
				GuidesProcessor.AddGroomSample(MoveTemp(HairGroupsData));
			}
		}
	}
	else
	{
		bSuccess = false;
	}
	Translator->EndTranslation();

	TArray<UGroomCache*> GroomCaches;
	if (bSuccess)
	{
		// Once the processing has completed successfully, the data is transferred to the GroomCache
		UGroomCache* GroomCache = ProcessToGroomCache(StrandsProcessor, AnimInfo, HairImportContext, "strands_cache");
		if (GroomCache)
		{
			GroomCaches.Add(GroomCache);
		}

		GroomCache = ProcessToGroomCache(GuidesProcessor, AnimInfo, HairImportContext, "guides_cache");
		if (GroomCache)
		{
			GroomCaches.Add(GroomCache);
		}
	}
	return GroomCaches;
}

#undef LOCTEXT_NAMESPACE