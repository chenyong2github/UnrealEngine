// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusEditorModule.h"
#include "OculusToolStyle.h"
#include "OculusToolCommands.h"
#include "OculusToolWidget.h"
#include "OculusPlatformToolWidget.h"
#include "OculusAssetDirectory.h"
#include "OculusHMDRuntimeSettings.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISettingsModule.h"
#include "OculusEditorSettings.h"

#define LOCTEXT_NAMESPACE "OculusEditor"

const FName FOculusEditorModule::OculusPerfTabName = FName("OculusPerfCheck");
const FName FOculusEditorModule::OculusPlatToolTabName = FName("OculusPlatormTool");

void FOculusEditorModule::PostLoadCallback()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
}

void FOculusEditorModule::StartupModule()
{
	void* ModuleCheck = FPlatformProcess::GetDllHandle(TEXT("OVRPlugin.dll"));
	if (!ModuleCheck)
	{
		return;
	}

	FPlatformProcess::FreeDllHandle(ModuleCheck);
	ModuleCheck = nullptr;

	bModuleValid = true;
	RegisterSettings();
	FOculusAssetDirectory::LoadForCook();

	if (!IsRunningCommandlet())
	{
		FOculusToolStyle::Initialize();
		FOculusToolStyle::ReloadTextures();

		FOculusToolCommands::Register();

		PluginCommands = MakeShareable(new FUICommandList);

		PluginCommands->MapAction(
			FOculusToolCommands::Get().OpenPluginWindow,
			FExecuteAction::CreateRaw(this, &FOculusEditorModule::PluginButtonClicked),
			FCanExecuteAction());

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		// Adds an option to launch the tool to Window->Developer Tools.
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("Miscellaneous", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FOculusEditorModule::AddMenuExtension));
		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);

		/*
		// If you want to make the tool even easier to launch, and add a toolbar button.
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Launch", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FOculusEditorModule::AddToolbarExtension));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
		*/

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(OculusPerfTabName, FOnSpawnTab::CreateRaw(this, &FOculusEditorModule::OnSpawnPluginTab))
			.SetDisplayName(LOCTEXT("FOculusEditorTabTitle", "Oculus Performance Check"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(OculusPlatToolTabName, FOnSpawnTab::CreateRaw(this, &FOculusEditorModule::OnSpawnPlatToolTab))
			.SetDisplayName(LOCTEXT("FOculusPlatfToolTabTitle", "Oculus Platform Tool"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FOculusEditorModule::OnEngineLoopInitComplete);
	}
}

void FOculusEditorModule::ShutdownModule()
{
	if (!bModuleValid)
	{
		return;
	}

	if (!IsRunningCommandlet())
	{
		FOculusToolStyle::Shutdown();
		FOculusToolCommands::Unregister();
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OculusPerfTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OculusPlatToolTabName);
		FOculusBuildAnalytics::Shutdown();
	}

	FOculusAssetDirectory::ReleaseAll();
	if (UObjectInitialized())
	{
		UnregisterSettings();
	}
}

TSharedRef<SDockTab> FOculusEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto myTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SOculusToolWidget)
		];


	return myTab;
}

TSharedRef<SDockTab> FOculusEditorModule::OnSpawnPlatToolTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto myTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SOculusPlatformToolWidget)
		];

	return myTab;
}

void FOculusEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "OculusVR",
			LOCTEXT("RuntimeSettingsName", "OculusVR"),
			LOCTEXT("RuntimeSettingsDescription", "Configure the OculusVR plugin"),
			GetMutableDefault<UOculusHMDRuntimeSettings>()
		);

		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(UOculusHMDRuntimeSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FOculusHMDSettingsDetailsCustomization::MakeInstance));
	}
}

void FOculusEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "OculusVR");
	}
}

FReply FOculusEditorModule::PluginClickFn(bool text)
{
	PluginButtonClicked();
	return FReply::Handled();
}

void FOculusEditorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->InvokeTab(OculusPerfTabName);
}

void FOculusEditorModule::AddMenuExtension(FMenuBuilder& Builder)
{
	bool v = false;
	GConfig->GetBool(TEXT("/Script/OculusEditor.OculusEditorSettings"), TEXT("bAddMenuOption"), v, GEditorIni);
	if (v)
	{
		Builder.AddMenuEntry(FOculusToolCommands::Get().OpenPluginWindow);
	}
}

void FOculusEditorModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FOculusToolCommands::Get().OpenPluginWindow);
}

void FOculusEditorModule::OnEngineLoopInitComplete()
{
	BuildAnalytics = FOculusBuildAnalytics::GetInstance();
}

TSharedRef<IDetailCustomization> FOculusHMDSettingsDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FOculusHMDSettingsDetailsCustomization);
}

FReply FOculusHMDSettingsDetailsCustomization::PluginClickPerfFn(bool text)
{
	FGlobalTabmanager::Get()->InvokeTab(FOculusEditorModule::OculusPerfTabName);
	return FReply::Handled();
}

FReply FOculusHMDSettingsDetailsCustomization::PluginClickPlatFn(bool text)
{
	FGlobalTabmanager::Get()->InvokeTab(FOculusEditorModule::OculusPlatToolTabName);
	return FReply::Handled();
}

void FOculusHMDSettingsDetailsCustomization::OnEnableBuildTelemetry(ECheckBoxState NewState)
{
	FOculusBuildAnalytics::GetInstance()->OnTelemetryToggled(NewState == ECheckBoxState::Checked);
	GConfig->SetBool(TEXT("/Script/OculusEditor.OculusEditorSettings"), TEXT("bEnableOculusBuildTelemetry"), NewState == ECheckBoxState::Checked, GEditorIni);
	GConfig->Flush(0);
}

ECheckBoxState FOculusHMDSettingsDetailsCustomization::IsBuildTelemetryEnabled() const
{
	bool v;
	GConfig->GetBool(TEXT("/Script/OculusEditor.OculusEditorSettings"), TEXT("bEnableOculusBuildTelemetry"), v, GEditorIni);
	return v ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FOculusHMDSettingsDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	// Labeled "General Oculus" instead of "General" to enable searchability. The button "Launch Oculus Utilities Window" doesn't show up if you search for "Oculus"
	IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General Oculus", FText::GetEmpty(), ECategoryPriority::Important);
	CategoryBuilder.AddCustomRow(LOCTEXT("General Oculus", "General"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("LaunchTool", "Launch Oculus Performance Window"))
		.OnClicked(this, &FOculusHMDSettingsDetailsCustomization::PluginClickPerfFn, true)
		]
	+ SHorizontalBox::Slot().FillWidth(8)
		]
	+ SVerticalBox::Slot().AutoHeight().Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("LaunchPlatTool", "Launch Oculus Platform Window"))
		.OnClicked(this, &FOculusHMDSettingsDetailsCustomization::PluginClickPlatFn, true)
		]
	+ SHorizontalBox::Slot().FillWidth(8)
		]
	+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EnableBuildTelemetry", "Enable Oculus Build Telemetry"))
			]
		]
	+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(10.f)
		]
	+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &FOculusHMDSettingsDetailsCustomization::OnEnableBuildTelemetry)
		.IsChecked(this, &FOculusHMDSettingsDetailsCustomization::IsBuildTelemetryEnabled)
		]
	+ SHorizontalBox::Slot().FillWidth(8)
		]
		];
}

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FOculusEditorModule, OculusEditor);

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE