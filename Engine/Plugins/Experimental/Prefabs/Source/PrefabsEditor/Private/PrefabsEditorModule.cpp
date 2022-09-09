// Copyright Epic Games, Inc. All Rights Reserved.

#include "IPrefabsEditorModule.h"
#include "AssetToolsModule.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "PrefabAssetTypeActions.h"
#include "Templates/Function.h"

DECLARE_LOG_CATEGORY_EXTERN(PrefabsEditor, Log, All);

/**
 * The Plugin Warden is a simple module used to verify a user has purchased a plug-in.  This
 * module won't prevent a determined user from avoiding paying for a plug-in, it is merely to
 * prevent accidental violation of a per-seat license on a plug-in, and to direct those users
 * to the marketplace page where they may purchase the plug-in.
 */
class FPrefabsEditorModule : public IPrefabsEditorModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnPostEngineInit();
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);

	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;
};

IMPLEMENT_MODULE( FPrefabsEditorModule, PrefabsEditor);

#define LOCTEXT_NAMESPACE "PrefabsEditor"

TSet<FString> AuthorizedPlugins;

void FPrefabsEditorModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPrefabsEditorModule::OnPostEngineInit);
}

void FPrefabsEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (int32 Index = 0; Index < CreatedAssetTypeActions.Num(); ++Index)
		{
			AssetTools.UnregisterAssetTypeActions(CreatedAssetTypeActions[Index].ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
}

void FPrefabsEditorModule::OnPostEngineInit()
{
	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	
	EAssetTypeCategories::Type Category = AssetTools.RegisterAdvancedAssetCategory(
		FName(TEXT("Prefabs")), LOCTEXT("PrefabsAssetCategory", "Prefabs"));

	RegisterAssetTypeAction(AssetTools, MakeShareable(new FPrefabAssetTypeActions(Category)));
}

void FPrefabsEditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

#undef LOCTEXT_NAMESPACE
