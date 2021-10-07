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


#define LOCTEXT_NAMESPACE "MassEntityEditor"

IMPLEMENT_MODULE(FMassEntityEditorModule, MassEntityEditor)

void FMassEntityEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FMassEntityEditorStyle::Initialize();

	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TSharedPtr<FAssetTypeActions_MassSchematic> MassAssetTypeAction = MakeShareable(new FAssetTypeActions_MassSchematic);
	ItemDataAssetTypeActions.Add(MassAssetTypeAction);
	AssetTools.RegisterAssetTypeActions(MassAssetTypeAction.ToSharedRef());

	// Register the details customizers
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	//PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeVariableDesc", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeVariableDescDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FMassEntityEditorModule::ShutdownModule()
{
	ProcessorClassCache.Reset();
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	FMassEntityEditorStyle::Shutdown();

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

TSharedRef<IMassEntityEditor> FMassEntityEditorModule::CreateMassEntityEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UMassSchematic* MassSchematic)
{
	if (!ProcessorClassCache.IsValid())
	{
		ProcessorClassCache = MakeShareable(new FGraphNodeClassHelper(UMassProcessor::StaticClass()));
		ProcessorClassCache->UpdateAvailableBlueprintClasses();
	}

	TSharedRef<FMassEntityEditor> NewEditor(new FMassEntityEditor());
	if (ensure(MassSchematic))
	{
		NewEditor->InitEditor(Mode, InitToolkitHost, *MassSchematic);
	}
	return NewEditor;
}
#undef LOCTEXT_NAMESPACE
