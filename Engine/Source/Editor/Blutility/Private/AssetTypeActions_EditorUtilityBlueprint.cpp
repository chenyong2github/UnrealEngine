// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_EditorUtilityBlueprint.h"
#include "ToolMenus.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "EditorUtilityBlueprintFactory.h"
#include "GlobalEditorUtilityBase.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "IBlutilityModule.h"
#include "EditorUtilitySubsystem.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

/////////////////////////////////////////////////////
// FAssetTypeActions_EditorUtilityBlueprint

FText FAssetTypeActions_EditorUtilityBlueprint::GetName() const
{
	return LOCTEXT("AssetTypeActions_EditorUtilityBlueprintUpdate", "Editor Utility Blueprint");
}

FColor FAssetTypeActions_EditorUtilityBlueprint::GetTypeColor() const
{
	return FColor(0, 169, 255);
}

UClass* FAssetTypeActions_EditorUtilityBlueprint::GetSupportedClass() const
{
	return UEditorUtilityBlueprint::StaticClass();
}

bool FAssetTypeActions_EditorUtilityBlueprint::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FAssetTypeActions_EditorUtilityBlueprint::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	auto Blueprints = GetTypedWeakObjectPtrs<UEditorUtilityBlueprint>(InObjects);

	Section.AddMenuEntry(
		"EditorUtility_Run",
		LOCTEXT("EditorUtility_Run", "Run Editor Utility Blueprint"),
		LOCTEXT("EditorUtility_RunTooltip", "Runs this Editor Utility Blueprint."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_EditorUtilityBlueprint::ExecuteRun, Blueprints)
		)
	);
}

uint32 FAssetTypeActions_EditorUtilityBlueprint::GetCategories()
{
	IBlutilityModule* BlutilityModule = FModuleManager::GetModulePtr<IBlutilityModule>("Blutility");
	return BlutilityModule->GetAssetCategory();
}


void FAssetTypeActions_EditorUtilityBlueprint::ExecuteNewDerivedBlueprint(TWeakObjectPtr<UEditorUtilityBlueprint> InObject)
{
	if (auto Object = InObject.Get())
	{
		// The menu option should ONLY be available if there is only one blueprint selected, validated by the menu creation code
		UBlueprint* TargetBP = Object;
		UClass* TargetClass = TargetBP->GeneratedClass;

		if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(TargetClass))
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidClassToMakeBlueprintFrom", "Invalid class with which to make a Blueprint."));
			return;
		}

		FString Name;
		FString PackageName;
		CreateUniqueAssetName(Object->GetOutermost()->GetName(), TEXT("_Child"), PackageName, Name);
		const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

		UEditorUtilityBlueprintFactory* BlueprintFactory = NewObject<UEditorUtilityBlueprintFactory>();
		BlueprintFactory->ParentClass = TargetClass;

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().CreateNewAsset(Name, PackagePath, UEditorUtilityBlueprint::StaticClass(), BlueprintFactory);
	}
}

void FAssetTypeActions_EditorUtilityBlueprint::ExecuteRun(FWeakBlueprintPointerArray InObjects)
{
	UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	for (auto ObjIt = InObjects.CreateIterator(); ObjIt; ++ObjIt)
	{
		EditorUtilitySubsystem->TryRun(ObjIt->Get());
	}
}

#undef LOCTEXT_NAMESPACE
