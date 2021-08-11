// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module.h"
#include "NeuralNetworkInferenceQAAssetTypeActions.h"
#include "Interfaces/IPluginManager.h"



/* FNeuralNetworkInferenceQAEditorModule public functions
 *****************************************************************************/

// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
void FNeuralNetworkInferenceQAEditorModule::StartupModule()
{
	// NeuralNetworkInferenceQAAsset - Register asset types
	Action = MakeShared<FNeuralNetworkInferenceQAAssetTypeActions>();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.RegisterAssetTypeActions(Action.ToSharedRef());
}

// This function may be called during shutdown to clean up your module. For modules that support dynamic reloading,
// we call this function before unloading the module.
void FNeuralNetworkInferenceQAEditorModule::ShutdownModule()
{
	// NeuralNetworkInferenceQAAsset - Unregister asset types
	if (Action.IsValid())
	{
		// AssetTools module might have been already unloaded, so using LoadModulePtr() rather than LoadModuleChecked()
		if (FAssetToolsModule* ModuleInterface = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
		{
			IAssetTools& AssetTools = ModuleInterface->Get();
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
}

IMPLEMENT_MODULE(FNeuralNetworkInferenceQAEditorModule, NeuralNetworkInferenceEditor);
