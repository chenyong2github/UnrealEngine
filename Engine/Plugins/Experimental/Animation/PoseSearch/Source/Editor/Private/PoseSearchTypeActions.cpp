// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTypeActions.h"
#include "PoseSearchDatabaseEditorToolkit.h"
#include "PoseSearch/PoseSearch.h"

#define LOCTEXT_NAMESPACE "PoseSearchTypeActions"

namespace UE::PoseSearch
{
	FText FDatabaseTypeActions::GetName() const
	{
		return LOCTEXT("PoseSearchDatabaseTypeActionsName", "Pose Search Database");
	}

	FColor FDatabaseTypeActions::GetTypeColor() const
	{
		return FColor(129, 196, 115);
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
				TSharedRef<FDatabaseEditorToolkit> NewEditor(new FDatabaseEditorToolkit());
				NewEditor->InitAssetEditor(Mode, EditWithinLevelEditor, PoseSearchDb);
			}
		}
	}

	uint32 FDatabaseTypeActions::GetCategories()
	{
		return EAssetTypeCategories::Animation;
	}
}

#undef LOCTEXT_NAMESPACE
