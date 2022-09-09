// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabAssetTypeActions.h"
#include "PrefabUncooked.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "PrefabAssetTypeActions"

//////////////////////////////////////////////////////////////////////////
// FPrefabAssetTypeActions

FPrefabAssetTypeActions::FPrefabAssetTypeActions(EAssetTypeCategories::Type InAssetCategory)
	: MyAssetCategory(InAssetCategory)
{
}

FText FPrefabAssetTypeActions::GetName() const
{
	return LOCTEXT("FPrefabAssetTypeActionsName", "Prefab");
}

FColor FPrefabAssetTypeActions::GetTypeColor() const
{
	return FColorList::Orange;
}

UClass* FPrefabAssetTypeActions::GetSupportedClass() const
{
	return UPrefabUncooked::StaticClass();
}

void FPrefabAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
}

uint32 FPrefabAssetTypeActions::GetCategories()
{
	return MyAssetCategory;
}

void FPrefabAssetTypeActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	auto Prefabs = GetTypedWeakObjectPtrs<UPrefabUncooked>(InObjects);

	Section.AddMenuEntry(
		"CreatePrefab",
		LOCTEXT("CreatePrefab", "Create Prefab"),
		LOCTEXT("CreatePrefabTooltip", "Create an empty Prefab."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Prefab"),
		FUIAction(
			FExecuteAction::CreateLambda([] {}),
			FCanExecuteAction()
		)
	);
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
