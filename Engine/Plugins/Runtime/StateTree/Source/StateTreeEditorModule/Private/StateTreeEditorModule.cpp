// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "AssetTypeActions_StateTree.h"
#include "Customizations/StateTreeVariableDescDetails.h"
#include "Customizations/StateTreeVariableLayoutDetails.h"
#include "Customizations/StateTreeVariableDetails.h"
#include "Customizations/StateTreeParameterLayoutDetails.h"
#include "Customizations/StateTreeConditionDetails.h"
#include "Customizations/StateTreeTransitionDetails.h"
#include "Customizations/StateTreeStateLinkDetails.h"
#include "Customizations/StateTreeTransition2Details.h"
#include "Customizations/StateTreeStateDetails.h"
#include "Customizations/StateTreeBindableItemDetails.h"
#include "Customizations/StateTreeEditorPropertyPathDetails.h"
#include "Customizations/StateTreeConditionItemDetails.h"
#include "Customizations/StateTreeAnyEnumDetails.h"
#include "AIGraphTypes.h" // Class cache
#include "StateTreeEditor.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEditorStyle.h"


#define LOCTEXT_NAMESPACE "StateTreeEditor"

IMPLEMENT_MODULE(FStateTreeEditorModule, StateTreeEditorModule)

void FStateTreeEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FStateTreeEditorStyle::Initialize();

	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TSharedPtr<FAssetTypeActions_StateTree> StateTreeAssetTypeAction = MakeShareable(new FAssetTypeActions_StateTree);
	ItemDataAssetTypeActions.Add(StateTreeAssetTypeAction);
	AssetTools.RegisterAssetTypeActions(StateTreeAssetTypeAction.ToSharedRef());

	// Register the details customizer
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeVariableDesc", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeVariableDescDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeVariableLayout", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeVariableLayoutDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeVariable", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeVariableDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeParameterLayout", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeParameterLayoutDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeCondition", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeConditionDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeTransition", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeTransitionDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeStateLink", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeStateLinkDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeTransition2", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeTransition2Details::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEvaluatorItem", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeBindableItemDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeTaskItem", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeBindableItemDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEditorPropertyPath", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeEditorPropertyPathDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeConditionItem", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeConditionItemDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeAnyEnum", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeAnyEnumDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("StateTreeState", FOnGetDetailCustomizationInstance::CreateStatic(&FStateTreeStateDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FStateTreeEditorModule::ShutdownModule()
{
	TaskClassCache.Reset();
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	FStateTreeEditorStyle::Shutdown();

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
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeVariableDesc");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeVariableLayout");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeVariable");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeParameterLayout");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeCondition");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeTransition");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeTransition2");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeStateLink");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeEvaluatorItem");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeTaskItem");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeEditorPropertyPath");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeConditionItem");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeAnyEnum");
		PropertyModule.NotifyCustomizationModuleChanged();
	}

}

TSharedRef<IStateTreeEditor> FStateTreeEditorModule::CreateStateTreeEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UStateTree* StateTree)
{
	if (!TaskClassCache.IsValid())
	{
		TaskClassCache = MakeShareable(new FGraphNodeClassHelper(UStateTreeTaskBase::StaticClass()));
		TaskClassCache->UpdateAvailableBlueprintClasses();
	}

	TSharedRef<FStateTreeEditor> NewEditor(new FStateTreeEditor());
	NewEditor->InitEditor(Mode, InitToolkitHost, StateTree);
	return NewEditor;
}

#undef LOCTEXT_NAMESPACE
