// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorModule.h"

#include "SOptimusEditorGraphExplorer.h"
#include "OptimusDeformerAssetActions.h"
#include "OptimusDetailsCustomization.h"
#include "OptimusEditor.h"
#include "OptimusEditorClipboard.h"
#include "OptimusEditorCommands.h"
#include "OptimusEditorGraphCommands.h"
#include "OptimusEditorGraphNodeFactory.h"
#include "OptimusEditorGraphPinFactory.h"
#include "OptimusEditorStyle.h"

#include "OptimusDataType.h"
#include "OptimusResourceDescription.h"
#include "OptimusShaderText.h"

#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"
#include "EdGraphUtilities.h"
#include "IAssetTools.h"

#define LOCTEXT_NAMESPACE "OptimusEditorModule"

DEFINE_LOG_CATEGORY(LogOptimusEditor);

FOptimusEditorModule::FOptimusEditorModule() :
	Clipboard(MakeShared<FOptimusEditorClipboard>())
{
}

void FOptimusEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	
	TSharedRef<IAssetTypeActions> OptimusDeformerAssetAction = MakeShared<FOptimusDeformerAssetActions>();
	AssetTools.RegisterAssetTypeActions(OptimusDeformerAssetAction);
	RegisteredAssetTypeActions.Add(OptimusDeformerAssetAction);

	FOptimusEditorCommands::Register();
	FOptimusEditorGraphCommands::Register();
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
	FOptimusEditorGraphCommands::Unregister();
	FOptimusEditorCommands::Unregister();
	
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

FOptimusEditorClipboard& FOptimusEditorModule::GetClipboard() const
{
	return Clipboard.Get();
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
	RegisterPropertyCustomization(FOptimusDataDomain::StaticStruct()->GetFName(), &FOptimusDataDomainCustomization::MakeInstance);
	RegisterPropertyCustomization(FOptimusMultiLevelDataDomain::StaticStruct()->GetFName(), &FOptimusMultiLevelDataDomainCustomization::MakeInstance);
	RegisterPropertyCustomization(FOptimusShaderText::StaticStruct()->GetFName(), &FOptimusShaderTextCustomization::MakeInstance);
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
