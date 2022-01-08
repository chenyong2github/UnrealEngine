// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorModule.h"

#include "AssetTypeActions/AssetTypeActions_ConsoleVariables.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorProjectSettings.h"
#include "ConsoleVariablesEditorStyle.h"
#include "MultiUser/ConsoleVariableSyncData.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "Algo/AnyOf.h"
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

	ConsoleObjectsMasterReference.Empty();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "Console Variables Editor");
	}
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesDialogWithAssetSelected(const FAssetData& InAssetData) const
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
	const int32 VariableCount = ConsoleObjectsMasterReference.Num();
	
	ConsoleObjectsMasterReference.Empty(VariableCount);
	
	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
		[this] (const TCHAR* Key, IConsoleObject* ConsoleObject)
		{
			if (!ConsoleObject || ConsoleObject->TestFlags(ECVF_Unregistered))
			{
				return;
			}

			const TSharedRef<FConsoleVariablesEditorCommandInfo> Info =
				MakeShared<FConsoleVariablesEditorCommandInfo>(Key);
			
			Info->StartupSource = Info->GetSource();
			Info->OnDetectConsoleObjectUnregisteredHandle = Info->OnDetectConsoleObjectUnregistered.AddRaw(
				this, &FConsoleVariablesEditorModule::OnDetectConsoleObjectUnregistered);

			if (IConsoleVariable* AsVariable = ConsoleObject->AsVariable())
			{
				Info->OnVariableChangedCallbackHandle = AsVariable->OnChangedDelegate().AddRaw(
					this, &FConsoleVariablesEditorModule::OnConsoleVariableChanged);
			}
			
			AddConsoleObjectCommandInfoToMasterReference(Info);
		}),
		TEXT(""));
}

TWeakPtr<FConsoleVariablesEditorCommandInfo> FConsoleVariablesEditorModule::FindCommandInfoByName(const FString& NameToSearch, ESearchCase::Type InSearchCase)
{
	TSharedPtr<FConsoleVariablesEditorCommandInfo>* Match = Algo::FindByPredicate(
		ConsoleObjectsMasterReference,
		[&NameToSearch, InSearchCase](const TSharedPtr<FConsoleVariablesEditorCommandInfo> Comparator)
		{
			return Comparator->Command.Equals(NameToSearch, InSearchCase);
		});

	return Match ? *Match : nullptr;
}

TArray<TWeakPtr<FConsoleVariablesEditorCommandInfo>> FConsoleVariablesEditorModule::FindCommandInfosMatchingTokens(
	const TArray<FString>& InTokens, ESearchCase::Type InSearchCase)
{
	TArray<TWeakPtr<FConsoleVariablesEditorCommandInfo>> ReturnValue;
	
	for (const TSharedPtr<FConsoleVariablesEditorCommandInfo>& CommandInfo : ConsoleObjectsMasterReference)
	{
		if (Algo::AnyOf(InTokens,
			[&CommandInfo, InSearchCase](const FString& Token)
			{
				return CommandInfo->Command.Contains(Token, InSearchCase);
			}))
		{
			ReturnValue.Add(CommandInfo);
		}
	}

	return ReturnValue;
}

TWeakPtr<FConsoleVariablesEditorCommandInfo> FConsoleVariablesEditorModule::FindCommandInfoByConsoleObjectReference(
	IConsoleObject* InConsoleObjectReference)
{
	TSharedPtr<FConsoleVariablesEditorCommandInfo>* Match = Algo::FindByPredicate(
	ConsoleObjectsMasterReference,
	[InConsoleObjectReference](const TSharedPtr<FConsoleVariablesEditorCommandInfo> Comparator)
	{
		return Comparator->GetConsoleObjectPtr() == InConsoleObjectReference;
	});

	return Match ? *Match : nullptr;
}

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorModule::GetPresetAsset() const
{
	return EditingPresetAsset;
}

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorModule::GetGlobalSearchAsset() const
{
	return EditingGlobalSearchAsset;
}

bool FConsoleVariablesEditorModule::PopulateGlobalSearchAssetWithVariablesMatchingTokens(const TArray<FString>& InTokens)
{
	// Remove existing commands
	TArray<FConsoleVariablesEditorAssetSaveData> NullList;
	EditingGlobalSearchAsset->ReplaceSavedCommands(NullList);
	
	for (const TWeakPtr<FConsoleVariablesEditorCommandInfo>& CommandInfo : FindCommandInfosMatchingTokens(InTokens))
	{
		EditingGlobalSearchAsset->AddOrSetConsoleObjectSavedData(
			{
				CommandInfo.Pin()->Command,
				CommandInfo.Pin()->GetConsoleVariablePtr() ?
					CommandInfo.Pin()->GetConsoleVariablePtr()->GetString() : "",
				ECheckBoxState::Checked
			}
		);
	}

	return EditingGlobalSearchAsset->GetSavedCommandsCount() > 0;
}

void FConsoleVariablesEditorModule::SendMultiUserConsoleVariableChange(const FString& InVariableName, const FString& InValueAsString) const
{
	MainPanel->GetMultiUserManager().SendConsoleVariableChange(InVariableName, InValueAsString);
}

void FConsoleVariablesEditorModule::OnRemoteCvarChanged(const FString InName, const FString InValue)
{
	UE_LOG(LogConsoleVariablesEditor, Display, TEXT("Remote set console variable %s = %s"), *InName, *InValue);

	if (GetDefault<UConcertCVarSynchronization>()->bSyncCVarTransactions)
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

void FConsoleVariablesEditorModule::OnFEngineLoopInitComplete()
{
	RegisterMenuItem();
	RegisterProjectSettings();
	QueryAndBeginTrackingConsoleVariables();
	CreateEditingPresets();
	
	MainPanel = MakeShared<FConsoleVariablesEditorMainPanel>();
}

void FConsoleVariablesEditorModule::RegisterMenuItem()
{
	FTabSpawnerEntry& BrowserSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ConsoleVariablesToolkitPanelTabId,
		FOnSpawnTab::CreateRaw(this, & FConsoleVariablesEditorModule::SpawnMainPanelTab))
			.SetIcon(FSlateIcon(FConsoleVariablesEditorStyle::Get().GetStyleSetName(), "ConsoleVariables.ToolbarButton", "ConsoleVariables.ToolbarButton.Small"))
			.SetDisplayName(LOCTEXT("OpenConsoleVariablesEditorMenuItem", "Console Variables"))
			.SetTooltipText(LOCTEXT("OpenConsoleVariablesEditorTooltip", "Open the Console Variables Editor"))
			.SetMenuType(ETabSpawnerMenuType::Enabled);

	BrowserSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FConsoleVariablesEditorModule::RegisterProjectSettings() const
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
	check(EditingPresetAsset);

	if (const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo =
		FindCommandInfoByConsoleObjectReference(ChangedVariable); CommandInfo.IsValid())
	{
		const TSharedPtr<FConsoleVariablesEditorCommandInfo>& PinnedCommand = CommandInfo.Pin();
		const FString& Key = PinnedCommand->Command;
		
		FConsoleVariablesEditorAssetSaveData FoundData;
		const bool bIsVariableCurrentlyTracked = EditingPresetAsset->FindSavedDataByCommandString(Key, FoundData);

		const UConsoleVariablesEditorProjectSettings* Settings = GetDefault<UConsoleVariablesEditorProjectSettings>();
		check(Settings);
		
		if (!bIsVariableCurrentlyTracked)
		{
			// If not yet tracked and we want to track variable changes from outside the dialogue,
			// Check if the changed value differs from the startup value before tracking it
			if (Settings->bAddAllChangedConsoleVariablesToCurrentPreset &&
				!Settings->ChangedConsoleVariableSkipList.Contains(Key) && 
				PinnedCommand->IsCurrentValueDifferentFromInputValue(PinnedCommand->StartupValueAsString))
			{
				if (MainPanel.IsValid())
				{
					MainPanel->AddConsoleObjectToPreset(
						Key,
						// If we're not in preset mode then pass empty value
						// This forces the row to get the current value at the time it's generated
						MainPanel->GetEditorListMode() ==
						FConsoleVariablesEditorList::EConsoleVariablesEditorListMode::Preset ?
						ChangedVariable->GetString() : "",
						true
					);
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

void FConsoleVariablesEditorModule::OnDetectConsoleObjectUnregistered(FString CommandName)
{
	check(EditingPresetAsset);

	EditingPresetAsset->RemoveConsoleVariable(CommandName);

	if (MainPanel.IsValid())
	{
		MainPanel->RefreshList();
	}

	if (const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo =
		FindCommandInfoByName(CommandName); CommandInfo.IsValid())
	{
		ConsoleObjectsMasterReference.Remove(CommandInfo.Pin());
	}
}

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorModule::AllocateTransientPreset(const FName DesiredName) const
{
	const FString PackageName = "/Temp/ConsoleVariablesEditor/PendingConsoleVariablesPresets";

	UPackage* NewPackage = CreatePackage(*PackageName);
	NewPackage->SetFlags(RF_Transient);
	NewPackage->AddToRoot();

	return NewObject<UConsoleVariablesAsset>(
		NewPackage, DesiredName, RF_Transient | RF_Transactional | RF_Standalone);
}

void FConsoleVariablesEditorModule::CreateEditingPresets()
{
	EditingPresetAsset = AllocateTransientPreset("ConsoleVariablesPreset_PendingPreset");
	
	EditingGlobalSearchAsset = AllocateTransientPreset("ConsoleVariablesPreset_GlobalSearch");
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
