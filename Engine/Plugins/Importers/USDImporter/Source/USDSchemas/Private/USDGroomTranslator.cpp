// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGroomTranslator.h"

#if USE_USD_SDK && WITH_EDITOR

#include "GroomAsset.h"
#include "GroomBuilder.h"
#include "GroomComponent.h"
#include "GroomImportOptions.h"
#include "HairDescription.h"
#include "HairStrandsImporter.h"
#include "Misc/ArchiveMD5.h"

#include "UnrealUSDWrapper.h"
#include "USDAssetImportData.h"
#include "USDGroomConversion.h"
#include "USDIntegrationUtils.h"
#include "USDTypesConversion.h"

#include "USDIncludesStart.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/curves.h"
#include "USDIncludesEnd.h"

namespace UE::UsdGroomTranslator::Private
{
	UGroomImportOptions* CreateGroomImportOptions(const FHairDescriptionGroups& GroupsDescription, const TArray<FHairGroupsInterpolation>& BuildSettings)
	{
		// Create a new groom import options and populate the interpolation settings based on the group count
		UGroomImportOptions* ImportOptions = NewObject<UGroomImportOptions>();
		const uint32 GroupCount = GroupsDescription.HairGroups.Num();
		if (GroupCount != uint32(ImportOptions->InterpolationSettings.Num()))
		{
			ImportOptions->InterpolationSettings.Init(FHairGroupsInterpolation(), GroupCount);
		}

		TOptional<FHairGroupsInterpolation> LastBuildSettings;
		if (BuildSettings.Num() > 0)
		{
			LastBuildSettings = BuildSettings.Last();
		}

		// If there are less build settings than groups, use the last one that was specified by the user
		for (uint32 Index = 0; Index < GroupCount; ++Index)
		{
			if (BuildSettings.IsValidIndex(Index))
			{
				ImportOptions->InterpolationSettings[Index] = BuildSettings[Index];
			}
			else if (LastBuildSettings.IsSet())
			{
				ImportOptions->InterpolationSettings[Index] = LastBuildSettings.GetValue();
			}
		}
		return ImportOptions;
	}

	FSHAHash ComputeHairDescriptionHash(FHairDescription& HairDescription, TArray<FHairGroupsInterpolation>& BuildSettings)
	{
		// The computed hash takes the hair description and group build settings into account since the groom builder
		// will use those settings to build the groom asset from the description
		FArchiveMD5 ArMD5;
		HairDescription.Serialize(ArMD5);

		for (FHairGroupsInterpolation& GroupSettings : BuildSettings)
		{
			GroupSettings.BuildDDCKey(ArMD5);
		}

		FMD5Hash MD5Hash;
		ArMD5.GetHash(MD5Hash);

		FSHA1 SHA1;
		SHA1.Update(MD5Hash.GetBytes(), MD5Hash.GetSize());
		SHA1.Final();

		FSHAHash SHAHash;
		SHA1.GetHash(SHAHash.Hash);

		return SHAHash;
	}
}

class FUsdGroomCreateAssetsTaskChain : public FUsdSchemaTranslatorTaskChain
{
public:
	explicit FUsdGroomCreateAssetsTaskChain(const TSharedRef<FUsdSchemaTranslationContext>& InContext, const UE::FSdfPath& InPrimPath)
		: PrimPath(InPrimPath)
		, Context(InContext)
	{
		SetupTasks();
	}

protected:

	UE::FSdfPath PrimPath;
	TSharedRef<FUsdSchemaTranslationContext> Context;

	FHairDescription HairDescription;

protected:
	UE::FUsdPrim GetPrim() const { return Context->Stage.GetPrimAtPath(PrimPath); }

protected:
	void SetupTasks()
	{
		FScopedUnrealAllocs UnrealAllocs;

		// Create hair description (Async)
		Do(ESchemaTranslationLaunchPolicy::Async,
			[this]() -> bool
			{
				const bool bSuccess = UsdToUnreal::ConvertGroomHierarchy(GetPrim(), pxr::UsdTimeCode::EarliestTime(), FTransform::Identity, HairDescription);

				return bSuccess && HairDescription.IsValid();
			});
		// Build groom asset from hair description (Sync)
		Then(ESchemaTranslationLaunchPolicy::Sync,
			[this]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FUsdGroomCreateAssetsTaskChain::Build);

				// Extract the groom groups info from the hair description to get the number of groups
				FHairDescriptionGroups GroupsDescription;
				FGroomBuilder::BuildHairDescriptionGroups(HairDescription, GroupsDescription);

				UGroomImportOptions* ImportOptions = UE::UsdGroomTranslator::Private::CreateGroomImportOptions(GroupsDescription, Context->GroomInterpolationSettings);

				FSHAHash SHAHash = UE::UsdGroomTranslator::Private::ComputeHairDescriptionHash(HairDescription, ImportOptions->InterpolationSettings);

				const FString PrimPathString = PrimPath.GetString();
				UGroomAsset* GroomAsset = Cast<UGroomAsset>(Context->AssetCache->GetCachedAsset(SHAHash.ToString()));
				if (!GroomAsset)
				{
					FName AssetName = MakeUniqueObjectName(GetTransientPackage(), UGroomAsset::StaticClass(), *FPaths::GetBaseFilename(PrimPathString));

					FHairImportContext HairImportContext(ImportOptions, GetTransientPackage(), UGroomAsset::StaticClass(), AssetName, Context->ObjectFlags | EObjectFlags::RF_Public);
					UGroomAsset* ExistingAsset = nullptr;
					GroomAsset = FHairStrandsImporter::ImportHair(HairImportContext, HairDescription, ExistingAsset);
					if (GroomAsset)
					{
						Context->AssetCache->CacheAsset(SHAHash.ToString(), GroomAsset);
#if WITH_EDITOR
						UUsdAssetImportData* ImportData = NewObject<UUsdAssetImportData>(GroomAsset, TEXT("UUSDAssetImportData"));
						ImportData->PrimPath = PrimPathString;
						ImportData->ImportOptions = ImportOptions;

						GroomAsset->AssetImportData = ImportData;
#endif // WITH_EDITOR
					}
				}

				if (GroomAsset)
				{
					Context->AssetCache->LinkAssetToPrim(PrimPathString, GroomAsset);
				}

				return true;
			});
	}
};

bool FUsdGroomTranslator::IsGroomPrim() const
{
	return UsdUtils::PrimHasSchema(GetPrim(), UnrealIdentifiers::GroomAPI);
}

void FUsdGroomTranslator::CreateAssets()
{
	if (!IsGroomPrim())
	{
		return Super::CreateAssets();
	}

	Context->TranslatorTasks.Add(MakeShared<FUsdGroomCreateAssetsTaskChain>(Context, PrimPath));
}

USceneComponent* FUsdGroomTranslator::CreateComponents()
{
	if (!IsGroomPrim())
	{
		return Super::CreateComponents();
	}

	// Display the groom as a standalone actor only if the stage loads the matching purpose.
	// The groom asset is processed regardless of the purpose so that it can be bound to mesh prims.
	if (!EnumHasAllFlags(Context->PurposesToLoad, IUsdPrim::GetPurpose(GetPrim())))
	{
		return nullptr;
	}

	bool bNeedsActor = true;
	USceneComponent* Component = CreateComponentsEx(TSubclassOf<USceneComponent>(UGroomComponent::StaticClass()), bNeedsActor);
	UpdateComponents(Component);

	return Component;
}

void FUsdGroomTranslator::UpdateComponents(USceneComponent* SceneComponent)
{
	if (!IsGroomPrim())
	{
		Super::UpdateComponents(SceneComponent);
	}

	if (UGroomComponent* GroomComponent = Cast<UGroomComponent>(SceneComponent))
	{
		GroomComponent->Modify();

		UGroomAsset* Groom = Cast<UGroomAsset>(Context->AssetCache->GetAssetForPrim(PrimPath.GetString()));

		bool bShouldRegister = false;
		if (Groom != GroomComponent->GroomAsset.Get())
		{
			bShouldRegister = true;

			if (GroomComponent->IsRegistered())
			{
				GroomComponent->UnregisterComponent();
			}

			GroomComponent->SetGroomAsset(Groom);
		}

		// Use the prim purpose in conjunction with the prim's computed visibility to toggle the visibility of the groom component
		// since the component itself cannot be removed if the groom shouldn't be displayed
		const bool bShouldRender = UsdUtils::IsVisible(GetPrim()) && EnumHasAllFlags(Context->PurposesToLoad, IUsdPrim::GetPurpose(GetPrim()));
		GroomComponent->SetVisibility(bShouldRender);

		if (bShouldRegister && !GroomComponent->IsRegistered())
		{
			GroomComponent->RegisterComponent();
		}
	}
}

bool FUsdGroomTranslator::CollapsesChildren(ECollapsingType CollapsingType) const
{
	if (!IsGroomPrim())
	{
		return Super::CollapsesChildren(CollapsingType);
	}

	return true;
}

bool FUsdGroomTranslator::CanBeCollapsed(ECollapsingType CollapsingType) const
{
	if (!IsGroomPrim())
	{
		return Super::CanBeCollapsed(CollapsingType);
	}

	return true;
}

#endif // #if USE_USD_SDK