// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorModule.h"

#include "DMXAttribute.h"
#include "DMXEditor.h"
#include "DMXEditorStyle.h"
#include "DMXPIEManager.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolBlueprintLibrary.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Game/DMXComponent.h"
#include "Commands/DMXEditorCommands.h"
#include "AssetTools/AssetTypeActions_DMXEditorLibrary.h"
#include "Customizations/DMXEditorPropertyEditorCustomization.h"
#include "Sequencer/DMXLibraryTrackEditor.h"
#include "Sequencer/TakeRecorderDMXLibrarySource.h"
#include "Sequencer/Customizations/TakeRecorderDMXLibrarySourceEditorCustomization.h"
#include "Widgets/Monitors/SDMXActivityMonitor.h"
#include "Widgets/Monitors/SDMXChannelsMonitor.h"
#include "Widgets/OutputConsole/SDMXOutputConsole.h"

#include "Templates/SharedPointer.h"
#include "LevelEditor.h"
#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ISequencerModule.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"


#define LOCTEXT_NAMESPACE "DMXEditorModule"

const FName FDMXEditorModule::DMXEditorAppIdentifier(TEXT("DMXEditorApp"));

const FName FDMXEditorTabNames::ChannelsMonitorTabName(TEXT("ChannelsMonitorTabName"));
const FName FDMXEditorTabNames::ActivityMonitorTabName( TEXT("ActivityMonitorTabName") );
const FName FDMXEditorTabNames::OutputConsoleTabName( TEXT("OutputConsoleTabName") );

EAssetTypeCategories::Type FDMXEditorModule::DMXEditorAssetCategory;

void FDMXEditorModule::StartupModule()
{
    static const FName AssetRegistryName( TEXT("AssetRegistry") );
    static const FName AssetToolsModuleame( TEXT("AssetTools") );
	static const FName LevelEditorName( TEXT("LevelEditor") );

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

	// Set up the Level Editor DMX button menu
	FDMXEditorCommands::Register();

	DMXLevelEditorMenuCommands = MakeShared<FUICommandList>();
	DMXLevelEditorMenuCommands->MapAction(
		FDMXEditorCommands::Get().OpenChannelsMonitor,
		FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnOpenChannelsMonitor),
		FCanExecuteAction()
		);
	DMXLevelEditorMenuCommands->MapAction(
		FDMXEditorCommands::Get().OpenActivityMonitor,
		FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnOpenActivityMonitor),
		FCanExecuteAction()
	);
	DMXLevelEditorMenuCommands->MapAction(
		FDMXEditorCommands::Get().OpenOutputConsole,
		FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnOpenOutputConsole),
		FCanExecuteAction()
	);
	DMXLevelEditorMenuCommands->MapAction(
		FDMXEditorCommands::Get().ToggleReceiveDMX,
		FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnToggleReceiveDMX),
		FCanExecuteAction()
	);
	DMXLevelEditorMenuCommands->MapAction(
		FDMXEditorCommands::Get().ToggleReceiveDMX,
		FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnToggleSendDMX),
		FCanExecuteAction()
	);
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LevelEditorName);

	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, 
		DMXLevelEditorMenuCommands,
		FToolBarExtensionDelegate::CreateRaw(this, &FDMXEditorModule::AddToolbarExtension));
	
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FDMXEditorTabNames::ChannelsMonitorTabName,
		FOnSpawnTab::CreateRaw(this, &FDMXEditorModule::OnSpawnChannelsMonitorTab))
		.SetDisplayName(LOCTEXT("ChannelsMonitorTabTitle", "DMX Channel Monitor"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FDMXEditorTabNames::ActivityMonitorTabName,
		FOnSpawnTab::CreateRaw(this, &FDMXEditorModule::OnSpawnActivityMonitorTab))
		.SetDisplayName(LOCTEXT("ActivityMonitorTabTitle", "DMX Activity Monitor"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FDMXEditorTabNames::OutputConsoleTabName,
		FOnSpawnTab::CreateRaw(this, &FDMXEditorModule::OnSpawnOutputConsoleTab))
		.SetDisplayName(LOCTEXT("OutputConsoleTabTitle", "DMX Output Console"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	PIEManager = MakeShared<FDMXPIEManager>();
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
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FDMXEditorTabNames::ActivityMonitorTabName);

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
	RegisterCustomPropertyTypeLayout("EDMXPixelMappingDistribution", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXPixelMappingDistributionCustomization>));

	// Customizations for the Entity Reference types
	RegisterCustomPropertyTypeLayout(FDMXEntityControllerRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixtureTypeRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixturePatchRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	
	// DMXLibrary TakeRecorder AddAllPatchesButton customization
	RegisterCustomPropertyTypeLayout(FAddAllPatchesButton::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXLibraryRecorderAddAllPatchesButtonCustomization>));

}

void FDMXEditorModule::RegisterObjectCustomizations()
{}

void FDMXEditorModule::RegisterAssetTypeAction(IAssetTools& InOutAssetTools, TSharedRef<IAssetTypeActions> Action)
{
	InOutAssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

FDMXEditorModule& FDMXEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXEditorModule>("DMXEditor");
}

TSharedRef<FDMXEditor> FDMXEditorModule::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXLibrary * DMXLibrary)
{
	TSharedRef<FDMXEditor> DMXEditor(new FDMXEditor());
	DMXEditor->InitEditor(Mode, InitToolkitHost, DMXLibrary);

	return DMXEditor;
}

void FDMXEditorModule::AddToolbarExtension(FToolBarBuilder& InOutBuilder)
{
	auto OnGetToolbarButtonBrushLambda = [this]() -> const FSlateIcon
	{
		bool bSendEnabled = UDMXProtocolBlueprintLibrary::IsSendDMXEnabled();
		bool bReceiveEnabled = UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled();

		if (bSendEnabled && bReceiveEnabled)
		{
			return FSlateIcon(FDMXEditorStyle::GetStyleSetName(), "DMXEditor.LevelEditor.MenuIcon_snd-rcv");
		}
		else if (bSendEnabled)
		{
			return FSlateIcon(FDMXEditorStyle::GetStyleSetName(), "DMXEditor.LevelEditor.MenuIcon_snd");
		}
		else if (bReceiveEnabled)
		{
			return FSlateIcon(FDMXEditorStyle::GetStyleSetName(), "DMXEditor.LevelEditor.MenuIcon_rcv");
		}

		return FSlateIcon(FDMXEditorStyle::GetStyleSetName(), "DMXEditor.LevelEditor.MenuIcon_none");
	};

	InOutBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FDMXEditorModule::GenerateMonitorsMenu, SharedDMXEditorCommands),
		LOCTEXT("InputInfo_Label", "DMX"),
		LOCTEXT("InputInfo_ToolTip", "DMX Tools"),
		TAttribute<FSlateIcon>::Create(OnGetToolbarButtonBrushLambda)
	);
}

TSharedRef<SWidget> FDMXEditorModule::GenerateMonitorsMenu(TSharedPtr<FUICommandList> InCommands)
{
	FMenuBuilder MenuBuilder(true, InCommands);
	FUIAction OpenChannelsMonitor(FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnOpenChannelsMonitor));
	FUIAction OpenActivityMonitor(FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnOpenActivityMonitor));
	FUIAction OpenOutputConsole(FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnOpenOutputConsole));
	FUIAction ToggleReceiveDMX(FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnToggleReceiveDMX));
	FUIAction ToggleSendDMX(FExecuteAction::CreateRaw(this, &FDMXEditorModule::OnToggleSendDMX));

	MenuBuilder.BeginSection("CustomMenu", TAttribute<FText>(FText::FromString("DMX")));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LevelEditorMenu_OpenChannelsMonitor", "Open DMX Channel Monitor"), 
			LOCTEXT("LevelEditorMenu_ChannelsMonitorToolTip", "Monitor for all DMX Channels in a Universe"),
			FSlateIcon(), OpenChannelsMonitor);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LevelEditorMenu_OpenActivityMonitor", "Open DMX Activity Monitor"), 
			LOCTEXT("LevelEditorMenu_ActivityMonitorToolTip", "Monitor for all DMX activity in a range of Universes"),
			FSlateIcon(), OpenActivityMonitor);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LevelEditorMenu_OpenOutputChannels", "Open DMX Output Console"), 
			LOCTEXT("LevelEditorMenu_OutputConsoleTooltip", "Console to generate and output DMX Signals"),
			FSlateIcon(), OpenOutputConsole);
		
		MenuBuilder.AddMenuEntry(
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FDMXEditorModule::GetToggleReceiveDMXText)),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FDMXEditorModule::GetToggleReceiveDMXTooltip)),
			FSlateIcon(), ToggleReceiveDMX);

		MenuBuilder.AddMenuEntry(
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FDMXEditorModule::GetToggleSendDMXText)),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FDMXEditorModule::GetToggleSendDMXTooltip)),
			FSlateIcon(), ToggleSendDMX);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnActivityMonitorTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	UniverseMonitorTab = SNew(SDMXActivityMonitor);

	return SNew(SDockTab)
		.Label(LOCTEXT("ActivityMonitorTitle", "DMX Activity Monitor"))
		.TabRole(ETabRole::NomadTab)
		[
			UniverseMonitorTab.ToSharedRef()
		];
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnChannelsMonitorTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ChannelsMonitorTitle", "DMX Channel Monitor"))
		.TabRole(ETabRole::NomadTab)
		[
			SAssignNew(ChannelsMonitorTab, SDMXChannelsMonitor)
		];
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnOutputConsoleTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	OutputConsoleTab = SNew(SDMXOutputConsole);

	return SNew(SDockTab)
		.OnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(OutputConsoleTab.ToSharedRef(), &SDMXOutputConsole::HandleCloseParentTab))
		.Label(LOCTEXT("OutputConsoleTitle", "DMX Output Console"))
		.TabRole(ETabRole::NomadTab)
		[
			OutputConsoleTab.ToSharedRef()
		];
}

void FDMXEditorModule::OnOpenChannelsMonitor()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::ChannelsMonitorTabName);
}

void FDMXEditorModule::OnOpenActivityMonitor()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::ActivityMonitorTabName);
}

void FDMXEditorModule::OnOpenOutputConsole()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::OutputConsoleTabName);

	check(OutputConsoleTab.IsValid());
	OutputConsoleTab->RestoreConsole();	
}

void FDMXEditorModule::OnToggleSendDMX()
{
	bool bAffectEditor = true;

	if (UDMXProtocolBlueprintLibrary::IsSendDMXEnabled())
	{
		UDMXProtocolBlueprintLibrary::SetSendDMXEnabled(false, bAffectEditor);
	}
	else
	{
		UDMXProtocolBlueprintLibrary::SetSendDMXEnabled(true, bAffectEditor);
	}
}

FText FDMXEditorModule::GetToggleSendDMXText() const
{
	if (UDMXProtocolBlueprintLibrary::IsSendDMXEnabled())
	{
		return LOCTEXT("MenuButtonText_PauseSendDMX", "Pause Send DMX");
	}
	else
	{
		return LOCTEXT("MenuButtonText_ResumeSendDMX", "Resume Send DMX");
	}
}

FText FDMXEditorModule::GetToggleSendDMXTooltip() const
{
	if (UDMXProtocolBlueprintLibrary::IsSendDMXEnabled())
	{
		return LOCTEXT("MenuButtonText_DisableOutboundDMXPackets", "Disables outbound DMX packets in editor.");
	}
	else
	{
		return LOCTEXT("MenuButtonText_EnableOutboundDMXPackets", "Enables outbound DMX packets in editor.");
	}
}

void FDMXEditorModule::OnToggleReceiveDMX()
{
	bool bAffectEditor = true;

	if (UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled())
	{
		UDMXProtocolBlueprintLibrary::SetReceiveDMXEnabled(false, bAffectEditor);
	}
	else
	{
		UDMXProtocolBlueprintLibrary::SetReceiveDMXEnabled(true, bAffectEditor);
	}
}

FText FDMXEditorModule::GetToggleReceiveDMXText() const
{
	if (UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled())
	{
		return LOCTEXT("MenuButtonText_PauseReceiveDMX", "Pause Receive DMX");
	}
	else
	{
		return LOCTEXT("MenuButtonText_ResumeReceiveDMX", "Resume Receive DMX");
	}
}

FText FDMXEditorModule::GetToggleReceiveDMXTooltip() const
{
	if (UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled())
	{
		return LOCTEXT("MenuButtonText_DisableInboundDMXPackets", "Disables inbound DMX packets in editor.");
	}
	else
	{
		return LOCTEXT("MenuButtonText_EnableInboundDMXPackets", "Enables inbound DMX packets editor.");
	}
}

IMPLEMENT_MODULE(FDMXEditorModule, DMXEditor)

#undef LOCTEXT_NAMESPACE
