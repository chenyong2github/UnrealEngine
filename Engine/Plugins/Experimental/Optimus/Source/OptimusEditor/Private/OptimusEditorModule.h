// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusEditorModule.h"


class FOptimusEditorGraphNodeFactory;
class FOptimusEditorGraphPinFactory;
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
	void RegisterPropertyCustomizations();
	void UnregisterPropertyCustomizations();

	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;

	TSharedPtr<FOptimusEditorGraphNodeFactory> GraphNodeFactory;
	TSharedPtr<FOptimusEditorGraphPinFactory> GraphPinFactory;
};
