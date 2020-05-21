// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorModule.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Game/DMXComponent.h"
#include "DMXEditor.h"
#include "DMXEditorStyle.h"
#include "DMXProtocolTypes.h"
#include "DMXAttribute.h"
#include "AssetTools/AssetTypeActions_DMXEditorLibrary.h"
#include "Customizations/DMXEditorPropertyEditorCustomization.h"
#include "Sequencer/DMXLibraryTrackEditor.h"
#include "Sequencer/TakeRecorderDMXLibrarySource.h"
#include "Sequencer/Customizations/TakeRecorderDMXLibrarySourceEditorCustomization.h"

#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ISequencerModule.h"

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
	
	// Register our custom Sequencer track
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	DMXLibraryTrackCreateHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FDMXLibraryTrackEditor::CreateTrackEditor));

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

	if (FModuleManager::Get().IsModuleLoaded("Sequencer"))
	{
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.UnRegisterTrackEditor(DMXLibraryTrackCreateHandle);
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
	// Customizations for the name lists of our custom types, like Fixture Categories
	RegisterCustomPropertyTypeLayout(FDMXProtocolName::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXProtocolName>>));
	RegisterCustomPropertyTypeLayout(FDMXFixtureCategory::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXFixtureCategory>>));
	RegisterCustomPropertyTypeLayout(FDMXAttributeName::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXAttributeName>>));

	// Customizations for the Entity Reference types
	RegisterCustomPropertyTypeLayout(FDMXEntityControllerRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixtureTypeRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixturePatchRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	
	// DMXLibrary TakeRecorder AddAllPatchesButton customization
	RegisterCustomPropertyTypeLayout(FAddAllPatchesButton::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXLibraryRecorderAddAllPatchesButtonCustomization>));
}

void FDMXEditorModule::RegisterObjectCustomizations()
{}

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
