#pragma once

#include "IOptimusEditorModule.h"

class IAssetTypeActions;

class FOptimusEditorModule : public IOptimusEditorModule
{
public:

	// IModuleInterface implementations
	void StartupModule() override;
	void ShutdownModule() override;

	// IOptimusEditorModule implementations
	TSharedRef<IOptimusEditor> CreateEditor(
		const EToolkitMode::Type Mode, 
		const TSharedPtr<IToolkitHost>& InitToolkitHost, 
		UOptimusDeformer* DeformerObject
	) override;

private:
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};
