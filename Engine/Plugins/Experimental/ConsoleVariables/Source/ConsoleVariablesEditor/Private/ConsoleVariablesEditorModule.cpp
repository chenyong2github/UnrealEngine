// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorModule.h"

#include "AssetTypeActions/AssetTypeActions_ConsoleVariables.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorProjectSettings.h"
#include "ConsoleVariablesEditorStyle.h"
#include "MultiUser/ConsoleVariableSyncData.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "Algo/Find.h"
#include "Framework/Docking/TabManager.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FConsoleVariablesEditorModule"

const FName FConsoleVariablesEditorModule::ConsoleVariablesToolkitPanelTabId(TEXT("ConsoleVariablesToolkitPanel"));

FConsoleVariablesEditorModule& FConsoleVariablesEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FConsoleVariablesEditorModule>("ConsoleVariablesEditor");
}

void FConsoleVariablesEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_ConsoleVariables>());

	FConsoleVariablesEditorStyle::Initialize();

	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FConsoleVariablesEditorModule::OnFEngineLoopInitComplete);
}

void FConsoleVariablesEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	FConsoleVariablesEditorStyle::Shutdown();
	
	MainPanel.Reset();

	ConsoleVariablesMasterReference.Empty();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "Console Variables Editor");
	}

	// Remove all OnChanged delegates
	for (TSharedPtr<FConsoleVariablesEditorCommandInfo> CommandInfo : ConsoleVariablesMasterReference)
	{
		if (CommandInfo.IsValid() && CommandInfo->ConsoleVariablePtr)
		{
			CommandInfo->ConsoleVariablePtr->OnChangedDelegate().Remove(CommandInfo->OnVariableChangedCallbackHandle);
		}
	}
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesDialogWithAssetSelected(const FAssetData& InAssetData)
{
	if (InAssetData.IsValid())
	{
		OpenConsoleVariablesEditor();
	}

	if (MainPanel.IsValid())
	{
		MainPanel->ImportPreset(InAssetData);
	}
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
				const FDelegateHandle Handle =
					AsVariable->OnChangedDelegate().AddRaw(this, &FConsoleVariablesEditorModule::OnConsoleVariableChanged);
				const TSharedRef<FConsoleVariablesEditorCommandInfo> Info =
					MakeShared<FConsoleVariablesEditorCommandInfo>(Key, AsVariable, Handle);
				Info->StartupSource = Info->GetSource();
				ConsoleVariablesMasterReference.Add(Info);
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

TWeakPtr<FConsoleVariablesEditorCommandInfo> FConsoleVariablesEditorModule::FindCommandInfoByConsoleVariableReference(IConsoleVariable* InVariableReference)
{
	TSharedPtr<FConsoleVariablesEditorCommandInfo>* Match = Algo::FindByPredicate(
	ConsoleVariablesMasterReference,
	[InVariableReference](const TSharedPtr<FConsoleVariablesEditorCommandInfo> Comparator)
	{
		return Comparator->ConsoleVariablePtr == InVariableReference;
	});

	return Match ? *Match : nullptr;
}

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorModule::GetEditingAsset() const
{
	return EditingAsset.Get();
}

void FConsoleVariablesEditorModule::SetEditingAsset(const TObjectPtr<UConsoleVariablesAsset> InEditingAsset)
{
	EditingAsset = InEditingAsset;
}

void FConsoleVariablesEditorModule::SendMultiUserConsoleVariableChange(const FString& InVariableName, const FString& InValueAsString)
{
	MainPanel->GetMultiUserManager().SendConsoleVariableChange(InVariableName, InValueAsString);
}

void FConsoleVariablesEditorModule::OnFEngineLoopInitComplete()
{
	RegisterMenuItem();
	RegisterProjectSettings();
	QueryAndBeginTrackingConsoleVariables();
	AllocateTransientPreset();
	
	MainPanel = MakeShared<FConsoleVariablesEditorMainPanel>();
}

void FConsoleVariablesEditorModule::RegisterMenuItem()
{
	FTabSpawnerEntry& BrowserSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ConsoleVariablesToolkitPanelTabId,
		FOnSpawnTab::CreateRaw(this, & FConsoleVariablesEditorModule::SpawnMainPanelTab))
			.SetIcon(FSlateIcon(FConsoleVariablesEditorStyle::Get().GetStyleSetName(), "ConsoleVariables.ToolbarButton", "ConsoleVariables.ToolbarButton.Small"))
			.SetDisplayName(LOCTEXT("OpenConsoleVariablesEditorMenuItem", "Console Variables Editor"))
			.SetTooltipText(LOCTEXT("OpenConsoleVariablesEditorTooltip", "Open the Console Variables Editor"))
			.SetMenuType(ETabSpawnerMenuType::Enabled);

	BrowserSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FConsoleVariablesEditorModule::RegisterProjectSettings()
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		// User Project Settings
		const TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr = SettingsModule.RegisterSettings(
			"Project", "Plugins", "Console Variables Editor",
			NSLOCTEXT("ConsoleVariables", "ConsoleVariablesSettingsCategoryDisplayName", "Console Variables Editor"),
			NSLOCTEXT("ConsoleVariables", "ConsoleVariablesSettingsDescription",
			          "Configure the Console Variables Editor user settings"),
			GetMutableDefault<UConsoleVariablesEditorProjectSettings>());
	}
}

void FConsoleVariablesEditorModule::OnConsoleVariableChanged(IConsoleVariable* ChangedVariable)
{
	check(EditingAsset);

	if (const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo =
		FindCommandInfoByConsoleVariableReference(ChangedVariable); CommandInfo.IsValid())
	{
		FString OutValue;
		const TSharedPtr<FConsoleVariablesEditorCommandInfo>& PinnedCommand = CommandInfo.Pin();
		const FString& Key = PinnedCommand->Command;
		
		const bool bIsVariableCurrentlyTracked = EditingAsset->FindSavedValueByCommandString(Key, OutValue);
		
		if (!bIsVariableCurrentlyTracked)
		{
			// If not yet tracked and we want to track variable changes from outside the dialogue,
			// Check if the changed value differs from the startup value before tracking it
			if (GetMutableDefault<UConsoleVariablesEditorProjectSettings>()->bAddAllChangedConsoleVariablesToCurrentPreset
				&& PinnedCommand->IsCurrentValueDifferentFromInputValue(PinnedCommand->StartupValueAsString))
			{
				EditingAsset->AddOrSetConsoleVariableSavedValue(Key, ChangedVariable->GetString());
				if (MainPanel.IsValid())
				{
					MainPanel->RebuildList();
				}

				SendMultiUserConsoleVariableChange(Key, ChangedVariable->GetString());
			}
		}
		else // If it's already being tracked, refreshed the list to update show filters and other possibly stale elements
		{
			if (MainPanel.IsValid())
			{
				MainPanel->RefreshList();
			}
			
			SendMultiUserConsoleVariableChange(Key, ChangedVariable->GetString());
		}
	}
}

void FConsoleVariablesEditorModule::OnRemoteCvarChanged(const FString InName, const FString InValue)
{
	UE_LOG(LogConsoleVariablesEditor, Display, TEXT("Remote set console variable %s = %s"), *InName, *InValue);

	if (GetMutableDefault<UConcertCVarSynchronization>()->bSyncCVarTransactions)
	{
		if (const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo =
			FindCommandInfoByName(InName); CommandInfo.IsValid())
		{
			if (CommandInfo.Pin()->IsCurrentValueDifferentFromInputValue(InValue))
			{
				GEngine->Exec(FConsoleVariablesEditorCommandInfo::GetCurrentWorld(),
					*FString::Printf(TEXT("%s %s"), *InName, *InValue));
			}
		}
	}
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

TSharedRef<SDockTab> FConsoleVariablesEditorModule::SpawnMainPanelTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	DockTab->SetContent(MainPanel->GetOrCreateWidget());
	MainPanel->RebuildList();
			
	return DockTab;
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesEditor()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ConsoleVariablesToolkitPanelTabId);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FConsoleVariablesEditorModule, ConsoleVariablesEditor)
