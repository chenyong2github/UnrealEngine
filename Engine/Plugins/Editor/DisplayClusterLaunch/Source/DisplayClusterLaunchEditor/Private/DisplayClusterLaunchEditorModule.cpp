// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLaunchEditorModule.h"

#include "DisplayClusterLaunchEditorLog.h"
#include "DisplayClusterLaunchEditorProjectSettings.h"
#include "DisplayClusterLaunchEditorStyle.h"

#include "ConcertSettings.h"
#include "IMultiUserClientModule.h"

#include "DisplayClusterRootActor.h"
#include "IDisplayClusterConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Shared/UdpMessagingSettings.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/GameEngine.h"
#include "Framework/Commands/Commands.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "Misc/ConfigCacheIni.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterLaunchEditorModule"

FString EnumToString(const TCHAR* EnumName, const int32 EnumValue)
{
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, EnumName, true);
	
	if (!EnumPtr)
	{
		return LOCTEXT("EnumNotFound", "Enum not found").ToString();
	}

	return EnumPtr->GetNameStringByIndex(EnumValue);
}

static UWorld* GetCurrentWorld()
{
	UWorld* CurrentWorld = nullptr;
	if (GIsEditor)
	{
		CurrentWorld = GEditor->GetEditorWorldContext().World();
	}
	else if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		CurrentWorld = GameEngine->GetGameWorld();
	}
	return CurrentWorld;
}

class FDisplayClusterLaunchUiCommands : public TCommands<FDisplayClusterLaunchUiCommands>
{
public:
	FDisplayClusterLaunchUiCommands()
		: TCommands<FDisplayClusterLaunchUiCommands>(
			"DisplayClusterLaunch",
			LOCTEXT("DisplayClusterLaunchCommands", "DisplayClusterLaunch UI Commands"),
			NAME_None,
			FDisplayClusterLaunchEditorStyle::Get().GetStyleSetName()
		)
	{}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(LaunchDisplayCluster, "Launch nDisplay", "Launch nDisplay", EUserInterfaceActionType::Button, FInputChord());
	}

	TSharedPtr<FUICommandInfo> LaunchDisplayCluster;
};

FDisplayClusterLaunchEditorModule& FDisplayClusterLaunchEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FDisplayClusterLaunchEditorModule>("DisplayClusterLaunchEditor");
}

void FDisplayClusterLaunchEditorModule::StartupModule()
{
	FDisplayClusterLaunchEditorStyle::Initialize();

	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FDisplayClusterLaunchEditorModule::OnFEngineLoopInitComplete);
}

void FDisplayClusterLaunchEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	FDisplayClusterLaunchEditorStyle::Shutdown();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "nDisplay Launch");
	}
}

void FDisplayClusterLaunchEditorModule::OpenProjectSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings")
		.ShowViewer("Project", "Plugins", "nDisplay Launch");

}

void GetProjectSettingsArguments(const UDisplayClusterLaunchEditorProjectSettings* ProjectSettings, FString& ConcatenatedCommandLineArguments, FString& ConcatenatedConsoleCommands, FString& ConcatenatedDPCvars, FString& ConcatenatedLogCommands)
{
	{
		for (const FString& CommandLineArgument : ProjectSettings->CommandLineArguments)
		{
			if (CommandLineArgument.IsEmpty())
			{
				continue;
			}
			ConcatenatedCommandLineArguments += FString::Printf(TEXT("-%s "), *CommandLineArgument);
		}
		// Remove whitespace
		ConcatenatedCommandLineArguments.TrimStartAndEndInline();
	}

	if (ProjectSettings->ConsoleCommands.Num() > 0)
	{
		ConcatenatedConsoleCommands += FString::Join(ProjectSettings->ConsoleCommands, TEXT(","));
	}

	if (ProjectSettings->DPCvars.Num() > 0)
	{
		ConcatenatedDPCvars += FString::Join(ProjectSettings->DPCvars, TEXT(","));
	}

	{
		for (const FDisplayClusterLaunchLoggingConstruct& LoggingConstruct : ProjectSettings->Logging)
		{
			if (LoggingConstruct.Category.IsNone())
			{
				continue;
			}
			ConcatenatedLogCommands += FString::Printf(TEXT("%s %s, "),
			                                           *LoggingConstruct.Category.ToString(),
			                                           *EnumToString(TEXT("EDisplayClusterLaunchLogVerbosity"), (int32)LoggingConstruct.VerbosityLevel.GetValue()));
		}
		// Remove whitespace
		ConcatenatedLogCommands.TrimStartAndEndInline();
		// Remove last comma
		ConcatenatedLogCommands = ConcatenatedLogCommands.LeftChop(1);
	}
}

bool AddUdpMessagingArguments(FString& ConcatenatedArguments)
{
	// Get from reflection because UUdpMessagingSettings does not export its API
	FConfigFile* EngineConfig = GConfig ? GConfig->FindConfigFileWithBaseName(FName(TEXT("Engine"))) : nullptr;
	if (EngineConfig)
	{
		TArray<FString> Settings;
		FString Setting;

		// Unicast endpoint setting
		EngineConfig->GetString(TEXT("/Script/UdpMessaging.UdpMessagingSettings"), TEXT("UnicastEndpoint"), Setting);

		// if the unicast endpoint port is bound, concatenate it
		if (Setting != "0.0.0.0:0" && !Setting.IsEmpty())
		{
			ConcatenatedArguments += TEXT(" -UDPMESSAGING_TRANSPORT_UNICAST=") + Setting;
		}

		// Multicast endpoint setting
		EngineConfig->GetString(TEXT("/Script/UdpMessaging.UdpMessagingSettings"), TEXT("MulticastEndpoint"), Setting);
		ConcatenatedArguments += TEXT(" -UDPMESSAGING_TRANSPORT_MULTICAST=") + Setting;

		// Static endpoints setting
		Settings.Empty(1);
		EngineConfig->GetArray(TEXT("/Script/UdpMessaging.UdpMessagingSettings"), TEXT("StaticEndpoints"), Settings);
		if (Settings.Num() > 0)
		{
			ConcatenatedArguments += TEXT(" -UDPMESSAGING_TRANSPORT_STATIC=");
			ConcatenatedArguments += Settings[0];
			for (int32 i = 1; i < Settings.Num(); ++i)
			{
				ConcatenatedArguments += ',';
				ConcatenatedArguments += Settings[i];
			}
		}
		return true;
	}
	return false;
}

FString AppendRandomNumbersToString(const FString InString, uint8 NumberToAppend = 6)
{
	FString RandomizedString = "_";

	for (uint8 RandomIteration = 0; RandomIteration < NumberToAppend; RandomIteration++)
	{
		RandomizedString += FString::FromInt(FMath::RandRange(0, 9));
	}
	return InString + RandomizedString;
}

FString GetConcertServerName()
{
	const UDisplayClusterLaunchEditorProjectSettings* Settings = GetDefault<UDisplayClusterLaunchEditorProjectSettings>();
	return Settings->bAutoGenerateServerName ? AppendRandomNumbersToString("nDisplayLaunchServer") : Settings->ExplicitServerName;
}

FString GetConcertSessionName()
{
	const UDisplayClusterLaunchEditorProjectSettings* Settings = GetDefault<UDisplayClusterLaunchEditorProjectSettings>();
	return Settings->bAutoGenerateSessionName ? AppendRandomNumbersToString("nDisplayLaunchSession") : Settings->ExplicitSessionName;
}

FString GetConcertArguments()
{
	const UConcertClientConfig* ConcertClientConfig = GetDefault<UConcertClientConfig>();
	ensureAlwaysMsgf (ConcertClientConfig, TEXT("%hs: Unable to launch nDisplay because there is no UConcertClientConfig object."));

	FString ReturnValue =
		FString::Printf(TEXT("-CONCERTISHEADLESS -CONCERTRETRYAUTOCONNECTONERROR -CONCERTAUTOCONNECT -CONCERTSERVER=\"%s\" -CONCERTSESSION=\"%s\""),
			 *GetConcertServerName(), *GetConcertSessionName());

	return ReturnValue;
}

void LaunchConcertServerIfNotRunning()
{
	IMultiUserClientModule& MultiUserClientModule = IMultiUserClientModule::Get();
	if (!MultiUserClientModule.IsConcertServerRunning())
	{
		MultiUserClientModule.LaunchConcertServer();
	}
}

void FDisplayClusterLaunchEditorModule::LaunchDisplayClusterProcess()
{
	if (!SelectedDisplayClusterConfigActor.IsValid())
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because there is no valid selected nDisplay configuration."), __FUNCTION__);
		return;
	}

	if (!DoesCurrentWorldHaveDisplayClusterConfig())
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because there are no valid selected nDisplay configurations in the world."), __FUNCTION__);
		return;
	}

	if (SelectedDisplayClusterConfigActorNodes.Num() == 0)
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because there are no selected nDisplay nodes."), __FUNCTION__);
		return;
	}

	FString ConfigActorPath;
	if (const ADisplayClusterRootActor* ConfigActor = Cast<ADisplayClusterRootActor>(SelectedDisplayClusterConfigActor.ResolveObject()))
	{
		const FString FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), "Temp.ndisplay");
		UDisplayClusterConfigurationData* ConfigDataCopy = DuplicateObject(ConfigActor->GetConfigData(), GetTransientPackage());

		if (!ConfigDataCopy->Scene)
		{
			ConfigDataCopy->Scene = NewObject<UDisplayClusterConfigurationScene>(ConfigDataCopy, NAME_None,
			RF_ArchetypeObject | RF_Public );
		}
		
		if (!ensureAlways(IDisplayClusterConfiguration::Get().SaveConfig(ConfigDataCopy, FilePath)))
		{
			UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because the selected nDisplay Configuration could not be saved to a .ndisplay file. See the log for more information."), __FUNCTION__);
			return;
		}
		
		ConfigActorPath = FString::Printf(TEXT("-dc_cfg=\"%s\""), *FilePath);
	}
	else
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because the selected nDisplay Config Actor could not be resolved or does not exist in the current level."), __FUNCTION__);
		return;
	}
	
	const UDisplayClusterLaunchEditorProjectSettings* ProjectSettings = GetDefault<UDisplayClusterLaunchEditorProjectSettings>();
	if (!ensureAlwaysMsgf (ProjectSettings, TEXT("%hs: Unable to launch nDisplay because there is no UDisplayClusterLaunchEditorProjectSettings object.")))
	{
		return;
	}
	
	const FString EditorBinary = FPlatformProcess::ExecutablePath();
	
	const FString Project = FPaths::SetExtension(FPaths::Combine(FPaths::ProjectDir(), FApp::GetProjectName()),".uproject");
	const FString Map = GetCurrentWorld()->GetCurrentLevel()->GetPackage()->GetFName().ToString();

	// Create Multi-user params
	FString ConcertArguments;
	if (GetConnectToMultiUser())
	{
		ConcertArguments = GetConcertArguments();
		LaunchConcertServerIfNotRunning();
	}

	for (const FString& Node : SelectedDisplayClusterConfigActorNodes)
	{
		FString ConcatenatedCommandLineArguments;
		FString ConcatenatedConsoleCommands;
		FString ConcatenatedDPCvars;
		FString ConcatenatedLogCommands;
	
		GetProjectSettingsArguments(ProjectSettings, ConcatenatedCommandLineArguments, ConcatenatedConsoleCommands, ConcatenatedDPCvars,
					 ConcatenatedLogCommands);

		AddUdpMessagingArguments(ConcatenatedCommandLineArguments);

		// Add nDisplay node information
		ConcatenatedCommandLineArguments += " " + ConfigActorPath;
		ConcatenatedCommandLineArguments += FString::Printf(TEXT(" -dc_node=\"%s\""), *Node);

		// Add Multi-User params
		if (!ConcertArguments.IsEmpty())
		{
			ConcatenatedCommandLineArguments += " " + ConcertArguments;
		}

		// Log file
		const FString LogFileName = (ProjectSettings->LogFileName.IsEmpty() ? Node : ProjectSettings->LogFileName) + ".log";

		const FString Params =
			FString::Printf(
				TEXT("\"%s\" -game \"%s\" Log=%s %s -ExecCmds=\"%s\" -DPCVars=\"%s\" -LogCmds=\"%s\""),
				*Project, *Map, *LogFileName, *ConcatenatedCommandLineArguments,
				*ConcatenatedConsoleCommands, *ConcatenatedDPCvars, *ConcatenatedLogCommands
	
		);

		UE_LOG(LogDisplayClusterLaunchEditor, Log, TEXT("Full Command: %s %s"), *EditorBinary, *Params);

		void* WritePipe = nullptr;

		FProcHandle Process = FPlatformProcess::CreateProc(
			*EditorBinary, *Params,
			ProjectSettings->bCloseEditorOnLaunch,
			false, false, NULL,
			0, NULL, WritePipe
		);
	}
}

void FDisplayClusterLaunchEditorModule::OnFEngineLoopInitComplete()
{
	Actions = MakeShared<FUICommandList>();
	FDisplayClusterLaunchUiCommands::Register();

	Actions->MapAction(FDisplayClusterLaunchUiCommands::Get().LaunchDisplayCluster,
		FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::LaunchDisplayClusterProcess));
	
	RegisterProjectSettings();
	RegisterToolbarItem();
}

void FDisplayClusterLaunchEditorModule::RegisterToolbarItem()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	
	RemoveToolbarItem();

	FToolMenuSection& Section = Menu->AddSection("DisplayClusterLaunch");

	FToolMenuEntry DisplayClusterLaunchButton = FToolMenuEntry::InitToolBarButton(FDisplayClusterLaunchUiCommands::Get().LaunchDisplayCluster);
	DisplayClusterLaunchButton.SetCommandList(Actions);

	const FToolMenuEntry DisplayClusterLaunchComboButton = FToolMenuEntry::InitComboButton(
		"DisplayClusterLaunchMenu",
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FDisplayClusterLaunchEditorModule::CreateToolbarMenuEntries),
		LOCTEXT("DisplayClusterLaunchActions", "Launch nDisplay Actions"),
		LOCTEXT("DisplayClusterLaunchActionsTooltip", "Actions related to nDisplay Launch"),
		FSlateIcon(),
		true //bInSimpleComboBox
	);

	Section.AddEntry(DisplayClusterLaunchButton);
	Section.AddEntry(DisplayClusterLaunchComboButton);
}

void FDisplayClusterLaunchEditorModule::RemoveToolbarItem()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	if (Menu->FindSection("DisplayClusterLaunch"))
	{
		Menu->RemoveSection("DisplayClusterLaunch");
	}
}

void FDisplayClusterLaunchEditorModule::RegisterProjectSettings() const
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		// User Project Settings
		const TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr = SettingsModule.RegisterSettings(
			"Project", "Plugins", "nDisplay Launch",
			LOCTEXT("DisplayClusterLaunchSettingsCategoryDisplayName", "nDisplay Launch"),
			LOCTEXT("DisplayClusterLaunchSettingsDescription", "Configure the nDisplay Launch user settings"),
			GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>());
	}
}

TArray<TWeakObjectPtr<ADisplayClusterRootActor>> FDisplayClusterLaunchEditorModule::GetAllDisplayClusterConfigsInWorld()
{
	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> CachedDisplayClusterActors;
	for (TActorIterator<ADisplayClusterRootActor> Itr(GetCurrentWorld()); Itr; ++Itr)
	{
		if (ADisplayClusterRootActor* RootActor = *Itr)
		{
			CachedDisplayClusterActors.Add(RootActor);
		}
	}

	bAreConfigsFoundInWorld = CachedDisplayClusterActors.Num() > 0;

	CachedDisplayClusterActors.Sort([](const TWeakObjectPtr<ADisplayClusterRootActor>& A, const TWeakObjectPtr<ADisplayClusterRootActor>& B)
	{
		return A->GetActorLabel() < B->GetActorLabel();
	});

	return CachedDisplayClusterActors;
}

bool FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig()
{
	return bAreConfigsFoundInWorld;
}

void FDisplayClusterLaunchEditorModule::SetSelectedDisplayClusterConfigActor(ADisplayClusterRootActor* SelectedActor)
{
	if (SelectedActor)
	{
		const FSoftObjectPath AsSoftObjectPath(SelectedActor);

		if (AsSoftObjectPath != SelectedDisplayClusterConfigActor)
		{
			SelectedDisplayClusterConfigActor = AsSoftObjectPath;

			SelectedDisplayClusterConfigActorNodes.Empty();
		}
	}
}

void FDisplayClusterLaunchEditorModule::ToggleDisplayClusterConfigActorNodeSelected(FString InNodeName)
{
	if (IsDisplayClusterConfigActorNodeSelected(InNodeName))
	{
		SelectedDisplayClusterConfigActorNodes.Remove(InNodeName);
	}
	else
	{
		SelectedDisplayClusterConfigActorNodes.Add(InNodeName);
	}
}

bool FDisplayClusterLaunchEditorModule::IsDisplayClusterConfigActorNodeSelected(FString InNodeName)
{
	return SelectedDisplayClusterConfigActorNodes.Contains(InNodeName);
}

void FDisplayClusterLaunchEditorModule::SetSelectedConsoleVariablesAsset(const FAssetData InConsoleVariablesAsset)
{
	if (SelectedConsoleVariablesAssetName == InConsoleVariablesAsset.AssetName)
	{
		SelectedConsoleVariablesAssetName = NAME_None;
	}
	else
	{
		SelectedConsoleVariablesAssetName = InConsoleVariablesAsset.AssetName;
	}
}

TSharedRef<SWidget>  FDisplayClusterLaunchEditorModule::CreateToolbarMenuEntries()
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	
	FMenuBuilder MenuBuilder(true, Actions);

	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> DisplayClusterConfigs = GetAllDisplayClusterConfigsInWorld();

	MenuBuilder.BeginSection("DisplayClusterLaunch", LOCTEXT("DisplayClusterLauncher", "Launch nDisplay"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DisplayClusterLaunchLastNode", "Launch Last Node"),
			LOCTEXT("DisplayClusterLaunchLastNodeTooltip", "Launch the last node."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::LaunchDisplayClusterProcess),
				FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig)
			),
			NAME_None
		);
	}
	MenuBuilder.EndSection();

	AddDisplayClusterLaunchConfigurations(AssetRegistry, MenuBuilder, DisplayClusterConfigs);

	AddDisplayClusterLaunchNodes(AssetRegistry, MenuBuilder);

	AddConsoleVariablesEditorAssetsToToolbarMenu(AssetRegistry, MenuBuilder);

	AddOptionsToToolbarMenu(MenuBuilder);

	return MenuBuilder.MakeWidget();
}

void FDisplayClusterLaunchEditorModule::AddDisplayClusterLaunchConfigurations(IAssetRegistry* AssetRegistry,
	FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<ADisplayClusterRootActor>>& DisplayClusterConfigs)
{
	MenuBuilder.BeginSection("DisplayClusterLaunchConfigurations", LOCTEXT("DisplayClusterLaunchConfigurations", "Configuration"));
	{
		if (DisplayClusterConfigs.Num())
		{
			// If one is not set, select the first one found
			bool bIsConfigActorValid = false;
			if (ADisplayClusterRootActor* SelectedActor = Cast<ADisplayClusterRootActor>(SelectedDisplayClusterConfigActor.ResolveObject()))
			{
				bIsConfigActorValid = DisplayClusterConfigs.ContainsByPredicate([SelectedActor](TWeakObjectPtr<ADisplayClusterRootActor>& Comparator)
				{
					return Comparator.IsValid() && SelectedActor == Comparator.Get();
				});
			}

			if (!bIsConfigActorValid)
			{
				SetSelectedDisplayClusterConfigActor(DisplayClusterConfigs[0].Get());
			}
			
			for (const TWeakObjectPtr<ADisplayClusterRootActor>& Node : DisplayClusterConfigs)
			{
				if (!Node.IsValid())
				{
					continue;
				}
				
				const FText NodeName = FText::FromString(Node->GetActorLabel());
				const FText DisplayClusterConfigTooltip =
					FText::Format(LOCTEXT("SelectDisplayClusterConfigFormat","Select configuration '{0}'"), NodeName);
			
				MenuBuilder.AddMenuEntry(
					NodeName,
					DisplayClusterConfigTooltip,
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::SetSelectedDisplayClusterConfigActor, Node.Get()),
						FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig),
						FIsActionChecked::CreateLambda([this, Node]() { return SelectedDisplayClusterConfigActor == FSoftObjectPath(Node.Get()); })
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
		else
		{
			SelectedDisplayClusterConfigActor.Reset();
			
			MenuBuilder.AddWidget(SNullWidget::NullWidget,
				LOCTEXT("NoDisplayClusterConfigAssetInLevelText", "Add an nDisplay configuration asset to\nthe current level to launch nDisplay."));
		}
	}
	MenuBuilder.EndSection();
}

void FDisplayClusterLaunchEditorModule::AddDisplayClusterLaunchNodes(IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder)
{
	if (ADisplayClusterRootActor* SelectedActor = Cast<ADisplayClusterRootActor>(SelectedDisplayClusterConfigActor.ResolveObject()))
	{
		TArray<FString> NodeNames;
		SelectedActor->GetConfigData()->Cluster->Nodes.GenerateKeyArray(NodeNames);

		if (NodeNames.Num() > 0)
		{
			MenuBuilder.BeginSection("DisplayClusterLaunchNodes", LOCTEXT("DisplayClusterLaunchNodes", "Nodes"));
			{
				const TAttribute<FText> SelectedDisplayClusterNodeNames =
					TAttribute<FText>::Create(
						[this]()
						{
							if (SelectedDisplayClusterConfigActorNodes.Num() > 0)
							{
								return FText::FromString(FString::Join(SelectedDisplayClusterConfigActorNodes, TEXT(", ")));
							}
							return LOCTEXT("NoDisplayClusterLaunchNodesSelected", "Please select nDisplay nodes to launch.");
						}
					);
				
				MenuBuilder.AddSubMenu(
					SelectedDisplayClusterNodeNames,
					LOCTEXT("SelectDisplayClusterNodes","Select nDisplay Nodes"),
					FNewMenuDelegate::CreateLambda([this, NodeNames](FMenuBuilder& NewMenuBuilder)
					{
						for (const FString& NodeName : NodeNames)
						{
							const FText DisplayClusterNodeName = FText::FromString(NodeName);
							const FText DisplayClusterNodeTooltip =
								FText::Format(LOCTEXT("SelectDisplayClusterNodeFormat","Select node '{0}'"), DisplayClusterNodeName);
							
							NewMenuBuilder.AddMenuEntry(
								DisplayClusterNodeName,
								DisplayClusterNodeTooltip,
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateRaw(
										this, &FDisplayClusterLaunchEditorModule::ToggleDisplayClusterConfigActorNodeSelected, NodeName),
									FCanExecuteAction(),
									FIsActionChecked::CreateRaw(
										this, &FDisplayClusterLaunchEditorModule::IsDisplayClusterConfigActorNodeSelected, NodeName)
								),
								NAME_None,
								EUserInterfaceActionType::Check
							);
						}
					}),
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig)),
					NAME_None,
					EUserInterfaceActionType::None
				);
			}
			MenuBuilder.EndSection();
		}
	}
}

void FDisplayClusterLaunchEditorModule::AddConsoleVariablesEditorAssetsToToolbarMenu(
	IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder)
{
	TArray<FAssetData> FoundConsoleVariablesAssets;
	AssetRegistry->GetAssetsByClass("ConsoleVariablesAsset", FoundConsoleVariablesAssets, true);

	if (FoundConsoleVariablesAssets.Num())
	{
		MenuBuilder.BeginSection("DisplayClusterLaunchCvars", LOCTEXT("DisplayClusterLaunchCvars", "Console Variables"));
		{
			const FText ConsoleVariablesAssetTooltip = LOCTEXT("SelectConsoleVariablesAssetFormat","Select Console Variables Asset");
			
			MenuBuilder.AddSubMenu(
				TAttribute<FText>::Create([this](){ return FText::FromName(SelectedConsoleVariablesAssetName); }),
				ConsoleVariablesAssetTooltip,
				FNewMenuDelegate::CreateLambda([this, FoundConsoleVariablesAssets](FMenuBuilder& NewMenuBuilder)
				{
					for (const FAssetData& Asset : FoundConsoleVariablesAssets)
					{
						const FText ConsoleVariablesAssetName = FText::FromName(Asset.AssetName);
						const FText ConsoleVariablesAssetTooltip =
							FText::Format(LOCTEXT("SelectConsoleVariablesAssetFormat","Select Console Variables Asset '{0}'"), ConsoleVariablesAssetName);
						NewMenuBuilder.AddMenuEntry(
							ConsoleVariablesAssetName,
							ConsoleVariablesAssetTooltip,
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::SetSelectedConsoleVariablesAsset, Asset),
								FCanExecuteAction(),
								FGetActionCheckState::CreateLambda([this, Asset](){ return SelectedConsoleVariablesAssetName == Asset.AssetName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							),
							NAME_None,
							EUserInterfaceActionType::RadioButton
						);
					}
				}),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig)),
				NAME_None,
				EUserInterfaceActionType::None
			);
		}
		MenuBuilder.EndSection();
	}
}

void FDisplayClusterLaunchEditorModule::AddOptionsToToolbarMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("DisplayClusterLaunchOptions", LOCTEXT("DisplayClusterLaunchOptions", "Options"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ConnectToMultiUserLabel", "Connect to Multi-User"),
			LOCTEXT("ConnectToMultiUserTooltip", "Connect to Multi-User"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UDisplayClusterLaunchEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>();
					Settings->bConnectToMultiUser = !GetConnectToMultiUser();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateRaw(this, &FDisplayClusterLaunchEditorModule::GetConnectToMultiUser)
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("EnableUnrealInsightsLabel", "Enable Unreal Insights"),
			LOCTEXT("EnableUnrealInsightsTooltip", "Enable Unreal Insights"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UDisplayClusterLaunchEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>();
					Settings->bEnableUnrealInsights = !Settings->bEnableUnrealInsights;
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return GetDefault<UDisplayClusterLaunchEditorProjectSettings>()->bEnableUnrealInsights; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CloseEditorOnLaunchLabel", "Close Editor on Launch"),
			LOCTEXT("CloseEditorOnLaunchTooltip", "Close Editor on Launch"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UDisplayClusterLaunchEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>();
					Settings->bCloseEditorOnLaunch = !Settings->bCloseEditorOnLaunch;
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return GetDefault<UDisplayClusterLaunchEditorProjectSettings>()->bCloseEditorOnLaunch; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("AdvancedSettingsLabel", "Advanced Settings..."),
			LOCTEXT("AdvancedSettingsTooltip", "Open the nDisplay Launch Project Settings"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FDisplayClusterLaunchEditorModule::OpenProjectSettings)
			),
			NAME_None
		);
	}
	MenuBuilder.EndSection();
}

bool FDisplayClusterLaunchEditorModule::GetConnectToMultiUser() const
{
	return GetDefault<UDisplayClusterLaunchEditorProjectSettings>()->bConnectToMultiUser;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDisplayClusterLaunchEditorModule, DisplayClusterLaunchEditor)
