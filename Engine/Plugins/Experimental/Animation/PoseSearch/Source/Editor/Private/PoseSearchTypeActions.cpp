// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTypeActions.h"
#include "PoseSearchDatabaseEditor.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchSchema.h"

#define LOCTEXT_NAMESPACE "PoseSearchTypeActions"

namespace UE::PoseSearch
{
	static const FText PoseSearchSubMenuName = LOCTEXT("PoseSearchSubMenuName", "Motion Matching");
	static const FColor PoseSearchAssetColor(29, 96, 125);

	//////////////////////////////////////////////////////////////////////////
	// FDatabaseTypeActions

	FText FDatabaseTypeActions::GetName() const
	{
		return LOCTEXT("PoseSearchDatabaseTypeActionsName", "Motion Database");
	}

	FColor FDatabaseTypeActions::GetTypeColor() const
	{
		return PoseSearchAssetColor;
	}

	UClass* FDatabaseTypeActions::GetSupportedClass() const
	{
		return UPoseSearchDatabase::StaticClass();
	}

	void FDatabaseTypeActions::OpenAssetEditor(
		const TArray<UObject*>& InObjects,
		TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
	{
		const EToolkitMode::Type Mode =
			EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

		for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
		{
			if (UPoseSearchDatabase* PoseSearchDb = Cast<UPoseSearchDatabase>(*ObjIt))
			{
				TSharedRef<FDatabaseEditor> NewEditor(new FDatabaseEditor());
				NewEditor->InitAssetEditor(Mode, EditWithinLevelEditor, PoseSearchDb);
			}
		}
	}

	uint32 FDatabaseTypeActions::GetCategories()
	{
		return EAssetTypeCategories::Animation;
	}

	const TArray<FText>& FDatabaseTypeActions::GetSubMenus() const
	{
		static const TArray<FText> SubMenus
		{
			PoseSearchSubMenuName
		};
		return SubMenus;
	}

	//////////////////////////////////////////////////////////////////////////
	// FSchemaTypeActions

	FText FSchemaTypeActions::GetName() const
	{
		return LOCTEXT("PoseSearchSchemaTypeActionsName", "Motion Database Config");
	}

	FColor FSchemaTypeActions::GetTypeColor() const
	{
		return PoseSearchAssetColor;
	}

	UClass* FSchemaTypeActions::GetSupportedClass() const
	{
		return UPoseSearchSchema::StaticClass();
	}

	uint32 FSchemaTypeActions::GetCategories()
	{
		return EAssetTypeCategories::Animation;
	}

	const TArray<FText>& FSchemaTypeActions::GetSubMenus() const
	{
		static const TArray<FText> SubMenus
		{
			PoseSearchSubMenuName
		};
		return SubMenus;
	}

	//////////////////////////////////////////////////////////////////////////
	// FNormalizationSetTypeActions

	FText FNormalizationSetTypeActions::GetName() const
	{
		return LOCTEXT("PoseSearchNormalizationSetTypeActionsName", "Normalization Set");
	}

	FColor FNormalizationSetTypeActions::GetTypeColor() const
	{
		return PoseSearchAssetColor;
	}

	UClass* FNormalizationSetTypeActions::GetSupportedClass() const
	{
		return UPoseSearchNormalizationSet::StaticClass();
	}

	uint32 FNormalizationSetTypeActions::GetCategories()
	{
		return EAssetTypeCategories::Animation;
	}

	const TArray<FText>& FNormalizationSetTypeActions::GetSubMenus() const
	{
		static const TArray<FText> SubMenus
		{
			PoseSearchSubMenuName
		};
		return SubMenus;
	}
}

#undef LOCTEXT_NAMESPACE
