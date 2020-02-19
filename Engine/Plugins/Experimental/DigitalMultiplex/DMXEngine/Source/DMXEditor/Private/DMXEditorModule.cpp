// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorModule.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Game/DMXComponent.h"
#include "DMXEditor.h"
#include "DMXEditorStyle.h"
#include "DMXProtocolTypes.h"
#include "AssetTools/AssetTypeActions_DMXEditorLibrary.h"
#include "Customizations/DMXEditorPropertyEditorCustomization.h"

#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"

const FName FDMXEditorModule::DMXEditorAppIdentifier(TEXT("DMXEditorApp"));
EAssetTypeCategories::Type FDMXEditorModule::DMXEditorAssetCategory;

const FName FDMXEditorModule::ModuleName = TEXT("DMXEditor");

#define LOCTEXT_NAMESPACE "DMXEditorModule"

void FDMXEditorModule::StartupModule()
{
    static const FName AssetRegistryName(TEXT("AssetRegistry"));
    static const FName AssetToolsModuleame(TEXT("AssetTools"));

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

    // Make sure AssetRegistry is loaded and get reference to the module
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryName);
    FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleame);

    FDMXEditorStyle::Initialize();

	MenuExtensibilityManager = MakeShared<FExtensibilityManager>();
	ToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();
	SharedDMXEditorCommands = MakeShared<FUICommandList>();

	RegisterPropertyTypeCustomizations();
	RegisterObjectCustomizations();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	DMXEditorAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("DMX")), LOCTEXT("DmxCategory", "DMX"));
	RegisterAssetTypeAction(AssetTools, MakeShared<FAssetTypeActions_DMXEditorLibrary>());

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FDMXEditorModule::ShutdownModule()
{
	FDMXEditorStyle::Shutdown();

	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();
	SharedDMXEditorCommands.Reset();

	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (TSharedPtr<IAssetTypeActions>& AssetIt : CreatedAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(AssetIt.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();


	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);


	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		// Unregister all classes customized by name
		for (const FName& ClassName : RegisteredClassNames)
		{
			PropertyModule.UnregisterCustomClassLayout(ClassName);
		}

		// Unregister all structures
		for (const FName& PropertyTypeName : RegisteredPropertyTypes)
		{
			PropertyModule.UnregisterCustomPropertyTypeLayout(PropertyTypeName);
		}

		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

void FDMXEditorModule::RegisterCustomClassLayout(FName ClassName, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	check(ClassName != NAME_None);

	RegisteredClassNames.Add(ClassName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomClassLayout(ClassName, DetailLayoutDelegate);
}


void FDMXEditorModule::RegisterCustomPropertyTypeLayout(FName PropertyTypeName, FOnGetPropertyTypeCustomizationInstance PropertyTypeLayoutDelegate)
{
	check(PropertyTypeName != NAME_None);

	RegisteredPropertyTypes.Add(PropertyTypeName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomPropertyTypeLayout(PropertyTypeName, PropertyTypeLayoutDelegate);
}

void FDMXEditorModule::RegisterPropertyTypeCustomizations()
{
	RegisterCustomPropertyTypeLayout(FDMXProtocolName::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXProtocolName>>, MakeAttributeLambda(&FDMXProtocolName::GetPossibleValues), (FSimpleMulticastDelegate*)nullptr));
	RegisterCustomPropertyTypeLayout(FDMXFixtureCategory::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXFixtureCategory>>, MakeAttributeLambda(&FDMXFixtureCategory::GetPossibleValues), &FDMXFixtureCategory::OnPossibleValuesUpdated));

	// Customizations for the Entity Reference types
	RegisterCustomPropertyTypeLayout(FDMXEntityControllerRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixtureTypeRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixturePatchRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
}

void FDMXEditorModule::RegisterObjectCustomizations()
{
}

void FDMXEditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

FDMXEditorModule& FDMXEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXEditorModule>(ModuleName);
}

TSharedRef<FDMXEditor> FDMXEditorModule::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXLibrary * DMXLibrary)
{
	TSharedRef<FDMXEditor> DMXEditor(new FDMXEditor());
	DMXEditor->InitEditor(Mode, InitToolkitHost, DMXLibrary);

	return DMXEditor;
}

IMPLEMENT_MODULE(FDMXEditorModule, DMXEditor)

#undef LOCTEXT_NAMESPACE
