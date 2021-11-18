// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_ConsoleVariables.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditor/Private/ConsoleVariablesEditorModule.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

FText FAssetTypeActions_ConsoleVariables::GetName() const
{
	return LOCTEXT("AssetTypeActions_ConsoleVariable_Name", "Console Variable Collection");
}

UClass* FAssetTypeActions_ConsoleVariables::GetSupportedClass() const
{
	return UConsoleVariablesAsset::StaticClass();
}

void FAssetTypeActions_ConsoleVariables::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);

	const TArray<TWeakObjectPtr<UConsoleVariablesAsset>> ConsoleVariableAssets = GetTypedWeakObjectPtrs<UConsoleVariablesAsset>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AssetTypeActions_OpenVariableCollection", "Open Variable Collection in Editor"),
		LOCTEXT("AssetTypeActions_OpenVariableCollectionToolTip", "Open this console variable collection in the Console Variables Editor. Select only one asset at a time."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "SystemWideCommands.SummonOpenAssetDialog"),
		FUIAction(
			FExecuteAction::CreateLambda([=] {
					OpenAssetEditor(InObjects, TSharedPtr<IToolkitHost>());
			}),
			FCanExecuteAction::CreateLambda([=] {
					// We only want to open a single Variable Collection asset at a time, so let's ensure
					// the number of selected assets is exactly one.
					return ConsoleVariableAssets.Num() == 1;
				})
			)
	);
}

void FAssetTypeActions_ConsoleVariables::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	if (InObjects.Num() && InObjects[0])
	{
		FConsoleVariablesEditorModule& Module = FConsoleVariablesEditorModule::Get();
					
		Module.OpenConsoleVariablesDialogWithAssetSelected(FAssetData(InObjects[0]));
	}
}

#undef LOCTEXT_NAMESPACE
