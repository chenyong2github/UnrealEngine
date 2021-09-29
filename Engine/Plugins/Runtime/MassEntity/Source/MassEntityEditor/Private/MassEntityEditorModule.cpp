// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "AssetTypeActions_MassSchematic.h"
#include "MassEditorStyle.h"
#include "MassEntityEditor.h"
#include "AIGraphTypes.h" // Class cache
#include "MassSchematic.h"
#include "MassProcessor.h"


#define LOCTEXT_NAMESPACE "PipeEditor"

IMPLEMENT_MODULE(FPipeEditorModule, PipeEditor)

void FPipeEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FPipeEditorStyle::Initialize();

	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TSharedPtr<FAssetTypeActions_PipeSchematic> PipeAssetTypeAction = MakeShareable(new FAssetTypeActions_PipeSchematic);
	ItemDataAssetTypeActions.Add(PipeAssetTypeAction);
	AssetTools.RegisterAssetTypeActions(PipeAssetTypeAction.ToSharedRef());

	// Register the details customizers
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	//PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeVariableDesc", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeVariableDescDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FPipeEditorModule::ShutdownModule()
{
	ProcessorClassCache.Reset();
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	FPipeEditorStyle::Shutdown();

	// Unregister the data asset type actions
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (int i = 0; i < ItemDataAssetTypeActions.Num(); i++)
		{
			if (ItemDataAssetTypeActions[i].IsValid())
			{
				AssetToolsModule.UnregisterAssetTypeActions(ItemDataAssetTypeActions[i].ToSharedRef());
			}
		}
	}
	ItemDataAssetTypeActions.Empty();

	// Unregister the details customization
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		//PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeVariableDesc");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

TSharedRef<IPipeEditor> FPipeEditorModule::CreatePipeEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UPipeSchematic* PipeSchematic)
{
	if (!ProcessorClassCache.IsValid())
	{
		ProcessorClassCache = MakeShareable(new FGraphNodeClassHelper(UPipeProcessor::StaticClass()));
		ProcessorClassCache->UpdateAvailableBlueprintClasses();
	}

	TSharedRef<FPipeEditor> NewEditor(new FPipeEditor());
	if (ensure(PipeSchematic))
	{
		NewEditor->InitEditor(Mode, InitToolkitHost, *PipeSchematic);
	}
	return NewEditor;
}
#undef LOCTEXT_NAMESPACE
