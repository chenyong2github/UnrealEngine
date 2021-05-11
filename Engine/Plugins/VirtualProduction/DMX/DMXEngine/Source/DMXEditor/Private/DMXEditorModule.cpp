// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorModule.h"

#include "DMXAttribute.h"
#include "DMXEditor.h"
#include "DMXEditorStyle.h"
#include "DMXEditorTabNames.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolBlueprintLibrary.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Game/DMXComponent.h"
#include "Commands/DMXEditorCommands.h"
#include "AssetTools/AssetTypeActions_DMXEditorLibrary.h"
#include "Customizations/DMXEditorPropertyEditorCustomization.h"
#include "Customizations/DMXLibraryPortReferencesCustomization.h"
#include "Sequencer/DMXLibraryTrackEditor.h"
#include "Sequencer/TakeRecorderDMXLibrarySource.h"
#include "Sequencer/Customizations/TakeRecorderDMXLibrarySourceEditorCustomization.h"
#include "Widgets/Monitors/SDMXActivityMonitor.h"
#include "Widgets/Monitors/SDMXChannelsMonitor.h"
#include "Widgets/OutputConsole/SDMXOutputConsole.h"
#include "Widgets/PatchTool/SDMXPatchTool.h"

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
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"


#define LOCTEXT_NAMESPACE "DMXEditorModule"

const FName FDMXEditorModule::DMXEditorAppIdentifier(TEXT("DMXEditorApp"));

EAssetTypeCategories::Type FDMXEditorModule::DMXEditorAssetCategory;

void FDMXEditorModule::StartupModule()
{
	FDMXEditorStyle::Initialize();

	FDMXEditorCommands::Register();
	BindDMXEditorCommands();

	MenuExtensibilityManager = MakeShared<FExtensibilityManager>();
	ToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();

	RegisterAssetTypeCategories();
	RegisterAssetTypeActions();

	RegisterPropertyTypeCustomizations();
	RegisterSequencerTypes();
	RegisterNomadTabSpawners();
	ExtendLevelEditorToolbar();
	
	StartupPIEManager();
}

void FDMXEditorModule::ShutdownModule()
{
	FDMXEditorStyle::Shutdown();
	FDMXEditorCommands::Unregister();

	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();
	DMXLevelEditorMenuCommands.Reset();

	UnregisterAssetTypeActions();
	UnregisterCustomClassLayouts();
	UnregisterCustomPropertyTypeLayouts();
	UnregisterCustomSequencerTrackTypes();
}

FDMXEditorModule& FDMXEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXEditorModule>("DMXEditor");
}

TSharedRef<FDMXEditor> FDMXEditorModule::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXLibrary * DMXLibrary)
{
	TSharedRef<FDMXEditor> NewDMXEditor = MakeShared<FDMXEditor>();
	NewDMXEditor->InitEditor(Mode, InitToolkitHost, DMXLibrary);

	return NewDMXEditor;
}

void FDMXEditorModule::BindDMXEditorCommands()
{
	check(!DMXLevelEditorMenuCommands.IsValid());

	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedRef<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();

	CommandList->MapAction(
		FDMXEditorCommands::Get().OpenChannelsMonitor,
		FExecuteAction::CreateStatic(&FDMXEditorModule::OnOpenChannelsMonitor)
	);
	CommandList->MapAction(
		FDMXEditorCommands::Get().OpenActivityMonitor,
		FExecuteAction::CreateStatic(&FDMXEditorModule::OnOpenActivityMonitor)
	);
	CommandList->MapAction(
		FDMXEditorCommands::Get().OpenOutputConsole,
		FExecuteAction::CreateStatic(&FDMXEditorModule::OnOpenOutputConsole)
	);
	CommandList->MapAction(
		FDMXEditorCommands::Get().OpenPatchTool,
		FExecuteAction::CreateStatic(&FDMXEditorModule::OnOpenPatchTool)
	);
	CommandList->MapAction(
		FDMXEditorCommands::Get().ToggleReceiveDMX,
		FExecuteAction::CreateStatic(&FDMXEditorModule::OnToggleReceiveDMX),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FDMXEditorModule::IsReceiveDMXEnabled)
	);
	CommandList->MapAction(
		FDMXEditorCommands::Get().ToggleSendDMX,
		FExecuteAction::CreateStatic(&FDMXEditorModule::OnToggleSendDMX),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FDMXEditorModule::IsSendDMXEnabled)
	);
}

void FDMXEditorModule::ExtendLevelEditorToolbar()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FUICommandList> CommandBindings = LevelEditorModule.GetGlobalLevelEditorActions();

	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		"Settings", 
		EExtensionHook::After,
		CommandBindings,
		FToolBarExtensionDelegate::CreateRaw(this, &FDMXEditorModule::GenerateToolbarExtension)
	);

	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void FDMXEditorModule::GenerateToolbarExtension(FToolBarBuilder& InOutBuilder)
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
		FOnGetContent::CreateRaw(this, &FDMXEditorModule::GenerateDMXLevelEditorToolbarMenu),
		LOCTEXT("InputInfo_Label", "DMX"),
		LOCTEXT("InputInfo_ToolTip", "DMX Tools"),
		TAttribute<FSlateIcon>::Create(OnGetToolbarButtonBrushLambda)
	);
}

TSharedRef<SWidget> FDMXEditorModule::GenerateDMXLevelEditorToolbarMenu()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FUICommandList> CommandBindings = LevelEditorModule.GetGlobalLevelEditorActions();

	FMenuBuilder MenuBuilder(true, CommandBindings);

	MenuBuilder.BeginSection("CustomMenu", TAttribute<FText>(FText::FromString("DMX")));

	MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().OpenChannelsMonitor);
	MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().OpenActivityMonitor);
	MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().OpenOutputConsole);
	MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().OpenPatchTool);
	MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().ToggleReceiveDMX);
	MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().ToggleSendDMX);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FDMXEditorModule::RegisterAssetTypeCategories()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	DMXEditorAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("DMX")), LOCTEXT("DmxCategory", "DMX"));
}

void FDMXEditorModule::RegisterAssetTypeActions()
{
	// Register the DMX Library asset type
	RegisterAssetTypeAction(MakeShared<FAssetTypeActions_DMXEditorLibrary>());
}

void FDMXEditorModule::RegisterPropertyTypeCustomizations()
{
	// Customizations for the name lists of our custom types, like Fixture Categories
	RegisterCustomPropertyTypeLayout(FDMXProtocolName::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXProtocolName>>));
	RegisterCustomPropertyTypeLayout(FDMXFixtureCategory::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXFixtureCategory>>));
	RegisterCustomPropertyTypeLayout(FDMXAttributeName::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FNameListCustomization<FDMXAttributeName>>));
	RegisterCustomPropertyTypeLayout("EDMXPixelMappingDistribution", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXPixelMappingDistributionCustomization>));

	// Customizations for the Entity Reference types
	RegisterCustomPropertyTypeLayout(FDMXEntityFixtureTypeRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));
	RegisterCustomPropertyTypeLayout(FDMXEntityFixturePatchRef::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXEntityReferenceCustomization>));

	// Customization for the DMXLibraryPortReferences struct type
	RegisterCustomPropertyTypeLayout(FDMXLibraryPortReferences::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXLibraryPortReferencesCustomization>));

	// DMXLibrary TakeRecorder AddAllPatchesButton customization
	RegisterCustomPropertyTypeLayout(FAddAllPatchesButton::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXLibraryRecorderAddAllPatchesButtonCustomization>));
}

void FDMXEditorModule::RegisterSequencerTypes()
{
	// Register the DMX Library Sequencer track type
	RegisterCustomSequencerTrackType(FOnCreateTrackEditor::CreateStatic(&FDMXLibraryTrackEditor::CreateTrackEditor));
}

void FDMXEditorModule::RegisterNomadTabSpawners()
{
	RegisterNomadTabSpawner(FDMXEditorTabNames::ChannelsMonitor,
		FOnSpawnTab::CreateStatic(&FDMXEditorModule::OnSpawnChannelsMonitorTab))
		.SetDisplayName(LOCTEXT("ChannelsMonitorTabTitle", "DMX Channel Monitor"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	RegisterNomadTabSpawner(FDMXEditorTabNames::ActivityMonitor,
		FOnSpawnTab::CreateStatic(&FDMXEditorModule::OnSpawnActivityMonitorTab))
		.SetDisplayName(LOCTEXT("ActivityMonitorTabTitle", "DMX Activity Monitor"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	RegisterNomadTabSpawner(FDMXEditorTabNames::OutputConsole,
		FOnSpawnTab::CreateStatic(&FDMXEditorModule::OnSpawnOutputConsoleTab))
		.SetDisplayName(LOCTEXT("OutputConsoleTabTitle", "DMX Output Console"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	RegisterNomadTabSpawner(FDMXEditorTabNames::PatchTool,
		FOnSpawnTab::CreateStatic(&FDMXEditorModule::OnSpawnPatchToolTab))
		.SetDisplayName(LOCTEXT("PatchToolTabTitle", "DMX Patch Tool"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FDMXEditorModule::StartupPIEManager()
{
	PIEManager = MakeUnique<FDMXPIEManager>();
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnActivityMonitorTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ActivityMonitorTitle", "DMX Activity Monitor"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDMXActivityMonitor)
		];
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnChannelsMonitorTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ChannelsMonitorTitle", "DMX Channel Monitor"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDMXChannelsMonitor)
		];
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnOutputConsoleTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("OutputConsoleTitle", "DMX Output Console"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDMXOutputConsole)
		];
}

TSharedRef<SDockTab> FDMXEditorModule::OnSpawnPatchToolTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PatchToolTitle", "DMX Patch Tool"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDMXPatchTool)
		];
}

void FDMXEditorModule::OnOpenChannelsMonitor()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::ChannelsMonitor);
}

void FDMXEditorModule::OnOpenActivityMonitor()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::ActivityMonitor);
}

void FDMXEditorModule::OnOpenOutputConsole()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::OutputConsole);
}

void FDMXEditorModule::OnOpenPatchTool()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDMXEditorTabNames::PatchTool);
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

bool FDMXEditorModule::IsSendDMXEnabled()
{
	return UDMXProtocolBlueprintLibrary::IsSendDMXEnabled();
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

bool FDMXEditorModule::IsReceiveDMXEnabled()
{
	return UDMXProtocolBlueprintLibrary::IsReceiveDMXEnabled();
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

void FDMXEditorModule::RegisterAssetTypeAction(TSharedRef<IAssetTypeActions> Action)
{
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(Action);
	RegisteredAssetTypeActions.Add(Action);
}

void FDMXEditorModule::UnregisterAssetTypeActions()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (TSharedPtr<IAssetTypeActions>& AssetIt : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(AssetIt.ToSharedRef());
		}
	}

	RegisteredAssetTypeActions.Reset();
}

void FDMXEditorModule::RegisterCustomClassLayout(FName ClassName, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	check(ClassName != NAME_None);

	RegisteredClassNames.Add(ClassName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomClassLayout(ClassName, DetailLayoutDelegate);

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FDMXEditorModule::UnregisterCustomClassLayouts()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		for (const FName& ClassName : RegisteredClassNames)
		{
			PropertyModule.UnregisterCustomClassLayout(ClassName);
		}
	}

	RegisteredClassNames.Reset();
}

void FDMXEditorModule::RegisterCustomPropertyTypeLayout(FName PropertyTypeName, FOnGetPropertyTypeCustomizationInstance PropertyTypeLayoutDelegate)
{
	check(PropertyTypeName != NAME_None);

	RegisteredPropertyTypes.Add(PropertyTypeName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomPropertyTypeLayout(PropertyTypeName, PropertyTypeLayoutDelegate);

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FDMXEditorModule::UnregisterCustomPropertyTypeLayouts()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		for (const FName& PropertyTypeName : RegisteredPropertyTypes)
		{
			PropertyModule.UnregisterCustomPropertyTypeLayout(PropertyTypeName);
		}
	}

	RegisteredPropertyTypes.Reset();
}

void  FDMXEditorModule::RegisterCustomSequencerTrackType(const FOnCreateTrackEditor& CreateTrackEditorDelegate)
{
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	RegisteredSequencerTrackHandles.Add(SequencerModule.RegisterTrackEditor(CreateTrackEditorDelegate));
}

void FDMXEditorModule::UnregisterCustomSequencerTrackTypes()
{
	if (FModuleManager::Get().IsModuleLoaded("Sequencer"))
	{
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");

		for (const FDelegateHandle& TrackCreateHandle : RegisteredSequencerTrackHandles)
		{
			SequencerModule.UnRegisterTrackEditor(TrackCreateHandle);
		}
	}

	RegisteredSequencerTrackHandles.Reset();
}

FTabSpawnerEntry& FDMXEditorModule::RegisterNomadTabSpawner(const FName TabId, const FOnSpawnTab& OnSpawnTab, const FCanSpawnTab& CanSpawnTab)
{
	RegisteredNomadTabNames.Add(TabId);
	return FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, OnSpawnTab, CanSpawnTab);
}

void FDMXEditorModule::UnregisterNomadTabSpawners()
{
	for (const FName& NomadTabName : RegisteredNomadTabNames)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NomadTabName);
	}
}

IMPLEMENT_MODULE(FDMXEditorModule, DMXEditor)

#undef LOCTEXT_NAMESPACE
