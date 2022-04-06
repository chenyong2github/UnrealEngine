// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTypeActions.h"
#include "PoseSearchDatabaseEditorToolkit.h"
#include "PoseSearch/PoseSearch.h"

#define LOCTEXT_NAMESPACE "PoseSearchTypeActions"

FText FPoseSearchDatabaseTypeActions::GetName() const
{
	return LOCTEXT("PoseSearchDatabaseTypeActionsName", "Pose Search Database");
}

FColor FPoseSearchDatabaseTypeActions::GetTypeColor() const
{
	return FColor(129, 196, 115);
}

UClass* FPoseSearchDatabaseTypeActions::GetSupportedClass() const
{
	return UPoseSearchDatabase::StaticClass();
}

void FPoseSearchDatabaseTypeActions::OpenAssetEditor(
	const TArray<UObject*>& InObjects, 
	TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = 
		EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UPoseSearchDatabase* PoseSearchDb = Cast<UPoseSearchDatabase>(*ObjIt))
		{
			TSharedRef<FPoseSearchDatabaseEditorToolkit> NewEditor(new FPoseSearchDatabaseEditorToolkit());
			NewEditor->InitAssetEditor(Mode, EditWithinLevelEditor, PoseSearchDb);
		}
	}
}

uint32 FPoseSearchDatabaseTypeActions::GetCategories()
{
	return EAssetTypeCategories::Animation;
}

#undef LOCTEXT_NAMESPACE
