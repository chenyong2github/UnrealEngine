// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorModule.h"

#include "OptimusDataType.h"
#include "OptimusDeformerAssetActions.h"
#include "OptimusDetailsCustomization.h"
#include "OptimusEditor.h"
#include "OptimusEditorCommands.h"
#include "OptimusEditorGraphNodeFactory.h"
#include "OptimusEditorGraphPinFactory.h"
#include "OptimusEditorStyle.h"
#include "SOptimusEditorGraphExplorer.h"
#include "OptimusDetailsCustomization.h"
#include "OptimusComputeComponentBroker.h"

#include "Types/OptimusType_ShaderText.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "EdGraphUtilities.h"
#include "PropertyEditorModule.h"
#include "ComputeFramework/ComputeGraphComponent.h"

#define LOCTEXT_NAMESPACE "OptimusEditorModule"

DEFINE_LOG_CATEGORY(LogOptimusEditor);

void FOptimusEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	
	TSharedRef<IAssetTypeActions> OptimusDeformerAssetAction = MakeShared<FOptimusDeformerAssetActions>();
	AssetTools.RegisterAssetTypeActions(OptimusDeformerAssetAction);
	RegisteredAssetTypeActions.Add(OptimusDeformerAssetAction);

	ComputeGraphComponentBroker = MakeShareable(new FOptimusComputeComponentBroker);
	FComponentAssetBrokerage::RegisterBroker(ComputeGraphComponentBroker, UComputeGraphComponent::StaticClass(), true, true);
	
	FOptimusEditorCommands::Register();
	FOptimusEditorGraphExplorerCommands::Register();
	FOptimusEditorStyle::Register();

	GraphNodeFactory = MakeShared<FOptimusEditorGraphNodeFactory>();
	FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);

	GraphPinFactory = MakeShared<FOptimusEditorGraphPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(GraphPinFactory);

	RegisterPropertyCustomizations();
}

void FOptimusEditorModule::ShutdownModule()
{
	UnregisterPropertyCustomizations();

	FEdGraphUtilities::UnregisterVisualPinFactory(GraphPinFactory);
	FEdGraphUtilities::UnregisterVisualNodeFactory(GraphNodeFactory);

	FOptimusEditorStyle::Unregister();
	FOptimusEditorGraphExplorerCommands::Unregister();
	FOptimusEditorCommands::Unregister();
	
	FComponentAssetBrokerage::UnregisterBroker(ComputeGraphComponentBroker);

	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
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



void FOptimusEditorModule::RegisterPropertyCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	auto RegisterPropertyCustomization = [&](FName InStructName, auto InCustomizationFactory)
	{
		PropertyModule.RegisterCustomPropertyTypeLayout(
			InStructName, 
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(InCustomizationFactory)
			);
		CustomizedProperties.Add(InStructName);
	};

	RegisterPropertyCustomization(FOptimusDataTypeRef::StaticStruct()->GetFName(), &FOptimusDataTypeRefCustomization::MakeInstance);
	RegisterPropertyCustomization(FOptimusType_ShaderText::StaticStruct()->GetFName(), &FOptimusType_ShaderTextCustomization::MakeInstance);
}


void FOptimusEditorModule::UnregisterPropertyCustomizations()
{
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		for (const FName& PropertyName: CustomizedProperties)
		{
			PropertyModule->UnregisterCustomPropertyTypeLayout(PropertyName);
		}
	}
}


IMPLEMENT_MODULE(FOptimusEditorModule, OptimusEditor)


#undef LOCTEXT_NAMESPACE
