// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorModule.h"

#include "OptimusDeformerAssetActions.h"
#include "OptimusEditor.h"
#include "OptimusEditorCommands.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"

#define LOCTEXT_NAMESPACE "OptimusEditorModule"

DEFINE_LOG_CATEGORY(LogOptimusEditor);

void FOptimusEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	
	TSharedRef<IAssetTypeActions> OptimusDeformerAssetAction = MakeShared<FOptimusDeformerAssetActions>();
	AssetTools.RegisterAssetTypeActions(OptimusDeformerAssetAction);
	RegisteredAssetTypeActions.Add(OptimusDeformerAssetAction);

	FOptimusEditorCommands::Register();
}

void FOptimusEditorModule::ShutdownModule()
{
	FOptimusEditorCommands::Unregister();

	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AssetToolsModule)
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();

		for (const TSharedRef<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}
}

TSharedRef<IOptimusEditor> FOptimusEditorModule::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UOptimusDeformer* DeformerObject)
{
	TSharedRef<FOptimusEditor> OptimusEditor = MakeShared<FOptimusEditor>();
	OptimusEditor->Construct(Mode, InitToolkitHost, DeformerObject);
	return OptimusEditor;
}


IMPLEMENT_MODULE(FOptimusEditorModule, OptimusEditor)

#undef LOCTEXT_NAMESPACE
