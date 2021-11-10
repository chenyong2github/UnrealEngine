// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorModule.h"

#include "AssetTypeActions/AssetTypeActions_ConsoleVariables.h"
#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorCommands.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorStyle.h"
#include "ConsoleVariablesEditorProjectSettings.h"
#include "ConsoleVariablesEditorCommandInfo.h"
#include "Toolkits/ConsoleVariablesEditorToolkit.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "Algo/Find.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "LevelEditor.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FConsoleVariablesEditorModule"

FConsoleVariablesEditorModule& FConsoleVariablesEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FConsoleVariablesEditorModule>("ConsoleVariablesEditor");
}

void FConsoleVariablesEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_ConsoleVariables>());

	FConsoleVariablesEditorStyle::Initialize();
	FConsoleVariablesEditorCommands::Register();

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FConsoleVariablesEditorModule::PostEngineInit);
	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FConsoleVariablesEditorModule::OnFEngineLoopInitComplete);
}

void FConsoleVariablesEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	FConsoleVariablesEditorStyle::Shutdown();
	
	FConsoleVariablesEditorCommands::Unregister();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "Console Variables Editor");
	}

	IConsoleManager::Get().UnregisterConsoleVariableSink_Handle(VariableChangedSinkHandle);
	VariableChangedSinkDelegate.Unbind();

	ConsoleVariablesEditorToolkit.Reset();

	ProjectSettingsSectionPtr.Reset();
	ProjectSettingsObjectPtr.Reset();

	ConsoleVariablesMasterReference.Empty();
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesDialogWithAssetSelected(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FAssetData& InAssetData)
{
	if (InAssetData.IsValid())
	{
		OpenConsoleVariablesEditor(Mode, InitToolkitHost);
	}

	if (ConsoleVariablesEditorToolkit.IsValid())
	{
		TWeakPtr<FConsoleVariablesEditorMainPanel> MainPanel = ConsoleVariablesEditorToolkit.Pin()->GetMainPanel();
		if (MainPanel.IsValid())
		{
			MainPanel.Pin()->ImportPreset(InAssetData);
		}
	}
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Console Variables Editor");
}

void FConsoleVariablesEditorModule::QueryAndBeginTrackingConsoleVariables()
{
	const int32 VariableCount = ConsoleVariablesMasterReference.Num();
	
	ConsoleVariablesMasterReference.Empty(VariableCount);
	
	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
		[this] (const TCHAR* Key, IConsoleObject* ConsoleObject)
		{
			if (IConsoleVariable* AsVariable = ConsoleObject->AsVariable())
			{
				ConsoleVariablesMasterReference.Add(
					MakeShared<FConsoleVariablesEditorCommandInfo>(Key, AsVariable, AsVariable->GetString()));
			}
		}),
		TEXT(""));
}

TWeakPtr<FConsoleVariablesEditorCommandInfo> FConsoleVariablesEditorModule::FindCommandInfoByName(const FString& NameToSearch, ESearchCase::Type InSearchCase)
{
	TSharedPtr<FConsoleVariablesEditorCommandInfo>* Match = Algo::FindByPredicate(
		ConsoleVariablesMasterReference,
		[&NameToSearch, InSearchCase](const TSharedPtr<FConsoleVariablesEditorCommandInfo> Comparator)
		{
			return Comparator->Command.Equals(NameToSearch, InSearchCase);
		});

	return Match ? *Match : nullptr;
}

void FConsoleVariablesEditorModule::PostEngineInit()
{
	RegisterMenuItem();
	RegisterProjectSettings();
}

void FConsoleVariablesEditorModule::OnFEngineLoopInitComplete()
{
	QueryAndBeginTrackingConsoleVariables();
	AllocateTransientPreset();

	VariableChangedSinkDelegate = FConsoleCommandDelegate::CreateRaw(this, &FConsoleVariablesEditorModule::OnConsoleVariableChange);
	VariableChangedSinkHandle = IConsoleManager::Get().RegisterConsoleVariableSink_Handle(VariableChangedSinkDelegate);
}

void FConsoleVariablesEditorModule::RegisterMenuItem()
{
	if (FSlateApplication::IsInitialized())
	{
		if (IsRunningGame())
		{
			return;
		}
		
		TSharedRef<FUICommandList> MenuItemCommandList = MakeShareable(new FUICommandList);

		MenuItemCommandList->MapAction(
			FConsoleVariablesEditorCommands::Get().OpenConsoleVariablesEditorMenuItem,
			FExecuteAction::CreateLambda([this] ()
			{
				OpenConsoleVariablesEditor(EToolkitMode::WorldCentric, FModuleManager::LoadModuleChecked< FLevelEditorModule >("LevelEditor").GetFirstLevelEditor());
			})
		);
		
		TSharedPtr<FExtender> NewMenuExtender = MakeShareable(new FExtender);
		NewMenuExtender->AddMenuExtension("ExperimentalTabSpawners", 
		                                  EExtensionHook::After, 
		                                  MenuItemCommandList, 
		                                  FMenuExtensionDelegate::CreateLambda([this] (FMenuBuilder& MenuBuilder)
		                                  {
			                                  MenuBuilder.AddMenuEntry(FConsoleVariablesEditorCommands::Get().OpenConsoleVariablesEditorMenuItem);
		                                  }));
	
		// Get the Level Editor so we can insert our item into the Level Editor menu subsection
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewMenuExtender);
	}
}

bool FConsoleVariablesEditorModule::RegisterProjectSettings()
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		// User Project Settings
		ProjectSettingsSectionPtr = SettingsModule.RegisterSettings("Project", "Plugins", "Console Variables Editor",
			NSLOCTEXT("ConsoleVariables", "ConsoleVariablesSettingsCategoryDisplayName", "Console Variables Editor"),
			NSLOCTEXT("ConsoleVariables", "ConsoleVariablesSettingsDescription", "Configure the Console Variables Editor user settings"),
			GetMutableDefault<UConsoleVariablesEditorProjectSettings>());

		if (ProjectSettingsSectionPtr.IsValid() && ProjectSettingsSectionPtr->GetSettingsObject().IsValid())
		{
			ProjectSettingsObjectPtr = Cast<UConsoleVariablesEditorProjectSettings>(ProjectSettingsSectionPtr->GetSettingsObject());

			ProjectSettingsSectionPtr->OnModified().BindRaw(this, &FConsoleVariablesEditorModule::HandleModifiedProjectSettings);
		}
	}

	return ProjectSettingsObjectPtr.IsValid();
}

bool FConsoleVariablesEditorModule::HandleModifiedProjectSettings()
{	
	return true;
}

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorModule::AllocateTransientPreset()
{
	static const TCHAR* PackageName = TEXT("/Temp/ConsoleVariablesEditor/PendingConsoleVariablesCollections");

	static FName DesiredName = "PendingConsoleVariablesCollection";

	UPackage* NewPackage = CreatePackage(PackageName);
	NewPackage->SetFlags(RF_Transient);
	NewPackage->AddToRoot();

	EditingAsset = NewObject<UConsoleVariablesAsset>(NewPackage, DesiredName, RF_Transient | RF_Transactional | RF_Standalone);

	return EditingAsset;
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost)
{
	if (ConsoleVariablesEditorToolkit.IsValid())
	{
		ConsoleVariablesEditorToolkit.Pin()->CloseWindow();
	}
	
	ConsoleVariablesEditorToolkit = FConsoleVariablesEditorToolkit::CreateConsoleVariablesEditor(Mode, InitToolkitHost);
}

void FConsoleVariablesEditorModule::OnConsoleVariableChange()
{
	check(EditingAsset);

	const int32 StartingTrackedCommandsCount = EditingAsset->GetSavedCommandsAndValues().Num();

	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
	[this] (const TCHAR* Key, IConsoleObject* ConsoleObject)
	{
		if (IConsoleVariable* AsVariable = ConsoleObject->AsVariable())
		{
			TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo = FindCommandInfoByName(Key);
			
			if (CommandInfo.IsValid())
			{
				CommandInfo.Pin()->ConsoleVariablePtr = AsVariable;
						
				if (ProjectSettingsObjectPtr->bAddAllChangedConsoleVariablesToCurrentPreset)
				{
					FString OutValue;
					if (!EditingAsset->FindSavedValueByCommandString(Key, OutValue) &&
						CommandInfo.Pin()->IsCurrentValueDifferentFromInputValue(CommandInfo.Pin()->StartupValueAsString))
					{
						EditingAsset->AddOrSetConsoleVariableSavedValue(Key, AsVariable->GetString());
					}
				}
			}
			else
			{
				ConsoleVariablesMasterReference.Add(
					MakeShared<FConsoleVariablesEditorCommandInfo>((wchar_t*)Key, AsVariable, AsVariable->GetString()));
			}
		}
	}),
	TEXT(""));

	if (StartingTrackedCommandsCount < EditingAsset->GetSavedCommandsAndValues().Num())
	{
		if (ConsoleVariablesEditorToolkit.IsValid() && ConsoleVariablesEditorToolkit.Pin()->GetMainPanel().IsValid())
		{
			ConsoleVariablesEditorToolkit.Pin()->GetMainPanel().Pin()->RefreshList(EditingAsset.Get());
		}
	}
}

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorModule::GetEditingAsset() const
{
	return EditingAsset.Get();
}

void FConsoleVariablesEditorModule::SetEditingAsset(const TObjectPtr<UConsoleVariablesAsset> InEditingAsset)
{
	EditingAsset = InEditingAsset;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FConsoleVariablesEditorModule, ConsoleVariablesEditor)
