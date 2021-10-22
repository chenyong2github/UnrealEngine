// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceEditorModule.h"
#include "NeuralNetworkAssetTypeActions.h"
#include "IAssetTypeActions.h"
#include "Interfaces/IPluginManager.h"



/* FNeuralNetworkInferenceEditorModule
 *****************************************************************************/

class FNeuralNetworkInferenceEditorModule : public INeuralNetworkInferenceEditorModule
{
public:
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Begin INeuralNetworkInferenceEditorModule
	virtual EAssetTypeCategories::Type GetMLAssetCategoryBit() const;

private:
	TSharedPtr<IAssetTypeActions> Action;
	EAssetTypeCategories::Type MLAssetCategoryBit;
};



/* FNeuralNetworkInferenceEditorModule public functions
 *****************************************************************************/

// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
void FNeuralNetworkInferenceEditorModule::StartupModule()
{
	// UNeuralNetwork - Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	Action = MakeShared<FNeuralNetworkAssetTypeActions>();
	AssetTools.RegisterAssetTypeActions(Action.ToSharedRef());
	// Register ML category so that ML assets can register to it
	MLAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("ML")), NSLOCTEXT("MLAssetCategory", "MLAssetCategory_ML", "Machine Learning"));
}

// This function may be called during shutdown to clean up your module. For modules that support dynamic reloading,
// we call this function before unloading the module.
void FNeuralNetworkInferenceEditorModule::ShutdownModule()
{
	// AssetTools module might have been already unloaded, so using LoadModulePtr() rather than LoadModuleChecked()
	if (FAssetToolsModule* ModuleInterface = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		IAssetTools& AssetTools = ModuleInterface->Get();
		// UNeuralNetwork - Unregister asset types
		if (Action.IsValid())
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
}

EAssetTypeCategories::Type FNeuralNetworkInferenceEditorModule::GetMLAssetCategoryBit() const
{
	return MLAssetCategoryBit;
}

IMPLEMENT_MODULE(FNeuralNetworkInferenceEditorModule, NeuralNetworkInferenceEditor);
