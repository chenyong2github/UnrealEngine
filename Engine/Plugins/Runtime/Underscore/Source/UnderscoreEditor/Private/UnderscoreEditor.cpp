// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnderscoreEditor.h"
#include "AssetToolsModule.h"
// #include "UnderscoreSection.h"
// #include "AssetTypeActions_UnderscoreSection.h"

void FUnderscoreEditorModule::StartupModule()
{
	// Register the audio editor asset type actions.
 // 	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

//	UnderscoreSectionAssetTypeActions = MakeShareable(new FAssetTypeActions_UnderscoreSection);
//	AssetTools.RegisterAssetTypeActions(UnderscoreSectionAssetTypeActions.ToSharedRef()); 

}

void FUnderscoreEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	 
/*	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		if (UnderscoreSectionAssetTypeActions.IsValid())
		{
			AssetTools.UnregisterAssetTypeActions(UnderscoreSectionAssetTypeActions.ToSharedRef());
		}
	}
	*/
}

IMPLEMENT_MODULE(FUnderscoreEditorModule, UnderscoreEditor)