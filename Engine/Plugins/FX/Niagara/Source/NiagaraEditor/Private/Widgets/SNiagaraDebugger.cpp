// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraDebugger.h"
#include "NiagaraEditorStyle.h"

#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructure.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "UObject/UObjectIterator.h"
#include "EditorWidgetsModule.h"
#include "PropertyEditorModule.h"
#include "PlatformInfo.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "ISessionServicesModule.h"
#include "Widgets/Browser/SSessionBrowser.h"

#define LOCTEXT_NAMESPACE "SNiagaraDebugger"

DEFINE_LOG_CATEGORY(LogNiagaraDebugger);

namespace NiagaraDebuggerLocal
{
	static const FName NiagaraDebuggerTabName(TEXT("NiagaraDebugger"));

	DECLARE_DELEGATE_TwoParams(FOnExecConsoleCommand, const TCHAR*, bool);

	template<typename T>
	static TAttribute<T> CreateTAttribute(TFunction<T()> InFunction)
	{
		return TAttribute<T>::Create(TAttribute<T>::FGetter::CreateLambda(InFunction));
	}
}

namespace NiagaraDebugHudTab
{
	static const FName TabName = FName(TEXT("DebugHudTab"));

	static void RegisterTabSpawner(const TSharedPtr<FTabManager>& TabManager)
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsArgs;
		DetailsArgs.bHideSelectionTip = true;
		TSharedPtr<IDetailsView> DebuggerSettingsDetails = PropertyModule.CreateDetailView(DetailsArgs);

		UNiagaraDebugHUDSettings* DebugHUDSettings = GetMutableDefault<UNiagaraDebugHUDSettings>();
		DebuggerSettingsDetails->SetObject(DebugHUDSettings);

		TabManager->RegisterTabSpawner(
			TabName,
			FOnSpawnTab::CreateLambda(
				[=](const FSpawnTabArgs&)
				{
					return SNew(SDockTab)
						.TabRole(ETabRole::PanelTab)
						.Label(LOCTEXT("DebugHudTitle", "Debug Hud"))
						[
							DebuggerSettingsDetails.ToSharedRef()
						];
				}
			)
		)
		.SetDisplayName(LOCTEXT("DebugHudTabTitle", "Debug Hud"))
		.SetTooltipText(LOCTEXT("DebugHudTooltipText", "Open the Debug Hud tab."));
	}
}

namespace NiagaraPerformanceTab
{
	static const FName TabName = FName(TEXT("PerformanceTab"));

	class SPerformanceWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPerformanceWidget) {}
			SLATE_ARGUMENT(NiagaraDebuggerLocal::FOnExecConsoleCommand, ExecConsoleCommand)
		SLATE_END_ARGS();

		void Construct(const FArguments& InArgs)
		{
			using namespace NiagaraDebuggerLocal;

			ExecConsoleCommand = InArgs._ExecConsoleCommand;

			ChildSlot
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
					.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
					.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked(FOnClicked::CreateLambda([&]() { ExecConsoleCommand.ExecuteIfBound(TEXT("stat particleperf"), true); return FReply::Handled(); }))
						.Text(LOCTEXT("ToggleParticlePerf", "Toggle ParticlePerf"))
						.ToolTipText(LOCTEXT("ToggleParticlePerfTooltip", "Toggles particle performance stat display on & off"))
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked(FOnClicked::CreateLambda([&]() { ExecConsoleCommand.ExecuteIfBound(*FString::Printf(TEXT("fx.ParticlePerfStats.RunTest %d"), PerfTestNumFrames), true); return FReply::Handled(); } ))
						.ToolTipText(LOCTEXT("RunPerfTestTooltip", "Runs performance tests for the number of frames and dumps results to the log / csv."))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RunPerfTest", "Run Performance Test"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SNumericEntryBox<int>)
								.Value(CreateTAttribute<TOptional<int>>([&]() { return TOptional<int>(PerfTestNumFrames); }))
								.AllowSpin(true)
								.MinValue(1)
								.MaxValue(TOptional<int>())
								.MinSliderValue(1)
								.MaxSliderValue(60*10)
								.OnValueChanged(SNumericEntryBox<int>::FOnValueChanged::CreateLambda([&](int InNewValue) { PerfTestNumFrames = InNewValue; }))
							]
						]
					]
					+ SUniformGridPanel::Slot(0, 1)
					[
						SNew(SButton)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked(FOnClicked::CreateLambda([&]() { ExecConsoleCommand.ExecuteIfBound(TEXT("stat NiagaraBaselines"), true); return FReply::Handled(); }))
						.Text(LOCTEXT("ToggleBaseline", "Toggle Baseline"))
						.ToolTipText(LOCTEXT("ToggleBaselineTooltip", "Toggles baseline performance display on & off."))
					]
					+ SUniformGridPanel::Slot(0, 2)
					[
						SNew(SButton)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked(FOnClicked::CreateLambda(
							[&]()
							{
								ExecConsoleCommand.ExecuteIfBound(TEXT("fx.ParallelSystemInstanceTick 1"), true);
								ExecConsoleCommand.ExecuteIfBound(TEXT("fx.ParallelSystemSimTick 1"), true);
								return FReply::Handled();
							}
						))
						.Text(LOCTEXT("EnableAsyncSim", "Enable Async Simulation"))
						.ToolTipText(LOCTEXT("EnableAsyncSimTooltip", "Overrides existing settings to enable async simulations."))
					]
					+ SUniformGridPanel::Slot(1, 2)
					[
						SNew(SButton)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked(FOnClicked::CreateLambda(
							[&]()
							{
								ExecConsoleCommand.ExecuteIfBound(TEXT("fx.ParallelSystemInstanceTick 0"), true);
								ExecConsoleCommand.ExecuteIfBound(TEXT("fx.ParallelSystemSimTick 0"), true);
								return FReply::Handled();
							}
						))
						.Text(LOCTEXT("DisableAsyncSim", "Disable Async Simulation"))
						.ToolTipText(LOCTEXT("DisableAsyncSimTooltip", "Overrides existing settings to disable async simulations."))
					]
				]
			];
		}
	
	private:
		NiagaraDebuggerLocal::FOnExecConsoleCommand ExecConsoleCommand;
		int	PerfTestNumFrames = 60;
	};

	static void RegisterTabSpawner(const TSharedPtr<FTabManager>& TabManager, NiagaraDebuggerLocal::FOnExecConsoleCommand ExecConsoleCommand)
	{
		TabManager->RegisterTabSpawner(
			TabName,
			FOnSpawnTab::CreateLambda(
				[=](const FSpawnTabArgs&)
				{
					return SNew(SDockTab)
						.TabRole(ETabRole::PanelTab)
						.Label(LOCTEXT("PerformanceTitle", "Performance"))
						[
							SNew(SPerformanceWidget)
							.ExecConsoleCommand(ExecConsoleCommand)
						];
				}
			)
		)
		.SetDisplayName(LOCTEXT("PerformanceTabTitle", "Performance"))
		.SetTooltipText(LOCTEXT("PerformanceTooltipText", "Open the Performance tab."));
	}
}

namespace NiagaraOutlinerTab
{
	static const FName TabName = FName(TEXT("OutlinerTab"));

	static void RegisterTabSpawner(const TSharedPtr<FTabManager>& TabManager)
	{
		TabManager->RegisterTabSpawner(
			TabName,
			FOnSpawnTab::CreateLambda(
				[=](const FSpawnTabArgs&)
				{
					return SNew(SDockTab)
						.TabRole(ETabRole::PanelTab)
						.Label(LOCTEXT("OutlinerTitle", "FX Outliner"))
						[
							//TODO: Pull scene data as a struct from the client and view here in a details view (with customization).
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
						];
				}
			)
		)
			.SetDisplayName(LOCTEXT("OutlinerTabTitle", "FX Outliner"))
					.SetTooltipText(LOCTEXT("OutlinerTooltipText", "Open the FX Outliner tab."));
	}
}

namespace NiagaraSessionBrowserTab
{
	static const FName TabName = FName(TEXT("Session Browser"));

	static void RegisterTabSpawner(const TSharedPtr<FTabManager>& TabManager, TSharedPtr<ISessionManager>& SessionManager)
	{
		TabManager->RegisterTabSpawner(
			TabName,
			FOnSpawnTab::CreateLambda(
				[=](const FSpawnTabArgs&)
				{
					return SNew(SDockTab)
						.TabRole(ETabRole::PanelTab)
						.Label(LOCTEXT("SessionBrowser", "Session Browser"))
						[
							SNew(SSessionBrowser, SessionManager.ToSharedRef())
						];
				}
			)
		)
			.SetDisplayName(LOCTEXT("SessionBrowserTabTitle", "Session Browser"))
					.SetTooltipText(LOCTEXT("SessionBrowserTooltipText", "Open the Session Browser tab."));
	}
}

SNiagaraDebugger::SNiagaraDebugger()
{
}

SNiagaraDebugger::~SNiagaraDebugger()
{
}

void SNiagaraDebugger::Construct(const FArguments& InArgs)
{
	using namespace NiagaraDebuggerLocal;

	//Init message handling.

	MessageEndpoint = FMessageEndpoint::Builder("SNiagaraDebugger")
		.Handling<FNiagaraDebuggerAcceptConnection>(this, &SNiagaraDebugger::HandleConnectionAcceptedMessage)
		.Handling<FNiagaraDebuggerConnectionClosed>(this, &SNiagaraDebugger::HandleConnectionClosedMessage);

	ISessionServicesModule& SessionServicesModule = FModuleManager::LoadModuleChecked<ISessionServicesModule>("SessionServices");
	SessionManager = SessionServicesModule.GetSessionManager();

	SessionManager->OnSelectedSessionChanged().AddSP(this, &SNiagaraDebugger::SessionManager_OnSessionSelectionChanged);
	SessionManager->OnInstanceSelectionChanged().AddSP(this, &SNiagaraDebugger::SessionManager_OnInstanceSelectionChanged);

	//////////////////////////////////////////////////////////////////////////

	TabManager = InArgs._TabManager;

	NiagaraDebugHudTab::RegisterTabSpawner(TabManager);
	NiagaraPerformanceTab::RegisterTabSpawner(TabManager, FOnExecConsoleCommand::CreateSP(this, &SNiagaraDebugger::ExecConsoleCommand));
	NiagaraOutlinerTab::RegisterTabSpawner(TabManager);
	NiagaraSessionBrowserTab::RegisterTabSpawner(TabManager, SessionManager);

	GetMutableDefault<UNiagaraDebugHUDSettings>()->OnChangedDelegate.AddSP(this, &SNiagaraDebugger::UpdateDebugHUDSettings);

	TSharedPtr<FTabManager::FLayout> DebuggerLayout = FTabManager::NewLayout("NiagaraDebugger_Layout_v1.1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.3f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(.8f)
					->SetHideTabWell(true)
					->AddTab(NiagaraDebugHudTab::TabName, ETabState::OpenedTab)
					->AddTab(NiagaraPerformanceTab::TabName, ETabState::OpenedTab)
					->SetForegroundTab(NiagaraDebugHudTab::TabName)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.2f)
					->AddTab(NiagaraSessionBrowserTab::TabName, ETabState::OpenedTab)
					->SetForegroundTab(NiagaraSessionBrowserTab::TabName)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(.7f)
				->AddTab(NiagaraOutlinerTab::TabName, ETabState::OpenedTab)
				->SetForegroundTab(NiagaraOutlinerTab::TabName)
			)
		);

	DebuggerLayout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, DebuggerLayout.ToSharedRef());

	TSharedRef<SWidget> TabContents = TabManager->RestoreFrom(DebuggerLayout.ToSharedRef(), TSharedPtr<SWindow>()).ToSharedRef();

	// create & initialize main menu
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());

	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("WindowMenuLabel", "Window"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SNiagaraDebugger::FillWindowMenu),
		"Window"
	);

	// Tell tab-manager about the multi-box for platforms with a global menu bar
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox());

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MenuBarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeToolbar()
		]
		+ SVerticalBox::Slot()
		.Padding(2.0)
		[
			TabContents
		]
	];
}

void SNiagaraDebugger::FillWindowMenu(FMenuBuilder& MenuBuilder)
{
	if (!TabManager.IsValid())
	{
		return;
	}

#if !WITH_EDITOR
	FGlobalTabmanager::Get()->PopulateTabSpawnerMenu(MenuBuilder, WorkspaceMenu::GetMenuStructure().GetStructureRoot());
#endif //!WITH_EDITOR

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}

void SNiagaraDebugger::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		NiagaraDebuggerLocal::NiagaraDebuggerTabName,
		FOnSpawnTab::CreateStatic(&SNiagaraDebugger::SpawnNiagaraDebugger)
	)
	.SetDisplayName(NSLOCTEXT("UnrealEditor", "NiagaraDebuggerTab", "Niagara Debugger"))
	.SetTooltipText(NSLOCTEXT("UnrealEditor", "NiagaraDebuggerTooltipText", "Open the Niagara Debugger Tab."))
	.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
	.SetIcon(FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.TabIcon"));
}

void SNiagaraDebugger::UnregisterTabSpawner()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NiagaraDebuggerLocal::NiagaraDebuggerTabName);
	}
}

TSharedRef<SDockTab> SNiagaraDebugger::SpawnNiagaraDebugger(const FSpawnTabArgs& Args)
{
	auto NomadTab = SNew(SDockTab)
		.Icon(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Debugger.TabIcon"))
		.TabRole(ETabRole::NomadTab)
		.Label(NSLOCTEXT("NiagaraDebugger", "NiagaraDebuggerTabTitle", "Niagara Debugger"));

	auto TabManager = FGlobalTabmanager::Get()->NewTabManager(NomadTab);
	TabManager->SetOnPersistLayout(
		FTabManager::FOnPersistLayout::CreateStatic(
			[](const TSharedRef<FTabManager::FLayout>& InLayout)
			{
				if (InLayout->GetPrimaryArea().Pin().IsValid())
				{
					FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, InLayout);
				}
			}
		)
	);

	NomadTab->SetOnTabClosed(
		SDockTab::FOnTabClosedCallback::CreateStatic(
			[](TSharedRef<SDockTab> Self, TWeakPtr<FTabManager> TabManager)
			{
				TSharedPtr<FTabManager> OwningTabManager = TabManager.Pin();
				if (OwningTabManager.IsValid())
				{
					FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, OwningTabManager->PersistLayout());
					OwningTabManager->CloseAllAreas();
				}
			}
			, TWeakPtr<FTabManager>(TabManager)
		)
	);

	auto MainWidget = SNew(SNiagaraDebugger)
		.TabManager(TabManager);

	NomadTab->SetContent(MainWidget);
	return NomadTab;
}

void SNiagaraDebugger::ExecConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld)
{
	auto SendExecCommand = [&](SNiagaraDebugger::FClientInfo& Client)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Sending console command %s. | Session: %s | Instance: %s |"), Cmd, *Client.SessionId.ToString(), *Client.InstanceId.ToString());
		MessageEndpoint->Send(new FNiagaraDebuggerExecuteConsoleCommand(Cmd, bRequiresWorld), Client.Address);
	};

	ForAllConnectedClients(SendExecCommand);
}

void SNiagaraDebugger::UpdateDebugHUDSettings()
{
	//Send the current state as a message to all connected clients.
	if (const UNiagaraDebugHUDSettings* Settings = GetDefault<UNiagaraDebugHUDSettings>())
	{
		auto SendSettingsUpdate = [&](SNiagaraDebugger::FClientInfo& Client)
		{
			//Create the message and copy the current state of the settings into it.
			FNiagaraDebugHUDSettingsData* Message = new FNiagaraDebugHUDSettingsData();
			FNiagaraDebugHUDSettingsData::StaticStruct()->CopyScriptStruct(Message, &Settings->Data);

			UE_LOG(LogNiagaraDebugger, Log, TEXT("Sending updated debug HUD settings. | Session: %s | Instance: %s |"), *Client.SessionId.ToString(), *Client.InstanceId.ToString());
			MessageEndpoint->Send(Message, Client.Address);
		};

		ForAllConnectedClients(SendSettingsUpdate);
	}
}

TSharedRef<SWidget> SNiagaraDebugger::MakeToolbar()
{
	using namespace NiagaraDebuggerLocal;

	FToolBarBuilder ToolbarBuilder(MakeShareable(new FUICommandList), FMultiBoxCustomization::None);
	UNiagaraDebugHUDSettings* Settings = GetMutableDefault<UNiagaraDebugHUDSettings>();
	ToolbarBuilder.BeginSection("Main");

	// Refresh button
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateLambda([Owner=this]() { Owner->UpdateDebugHUDSettings(); })),
			NAME_None,
			LOCTEXT("Refresh", "Refresh"),
			LOCTEXT("RefreshTooltip", "Refesh the settings on the target device.  Used if we get out of sync."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Icons.Refresh")
		);
	}

	ToolbarBuilder.AddSeparator();

	// Playback controls
	{
		// Play Button
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateLambda([=]() {Settings->Data.PlaybackMode = ENiagaraDebugPlaybackMode::Play; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->Data.PlaybackMode == ENiagaraDebugPlaybackMode::Play; })
				),
				NAME_None,
				LOCTEXT("Play", "Play"),
				LOCTEXT("PlayTooltip", "Simulations will play as normal"),
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.PlayIcon"),
				EUserInterfaceActionType::ToggleButton
			);
		}
		// Pause Button
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateLambda([=]() {Settings->Data.PlaybackMode = ENiagaraDebugPlaybackMode::Paused; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->Data.PlaybackMode == ENiagaraDebugPlaybackMode::Paused; })
				),
				NAME_None,
				LOCTEXT("Pause", "Pause"),
				LOCTEXT("PauseTooltip", "Pause all simulations"),
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.PauseIcon"),
				EUserInterfaceActionType::ToggleButton
			);
		}
		// Loop Button
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateLambda([=]() {Settings->Data.PlaybackMode = ENiagaraDebugPlaybackMode::Loop; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->Data.PlaybackMode == ENiagaraDebugPlaybackMode::Loop; })
				),
				NAME_None,
				CreateTAttribute<FText>([=]() { return Settings->Data.bLoopTimeEnabled ? FText::Format(LOCTEXT("PlaybackLoopFormat", "Loop Every\n{0} Seconds"), FText::AsNumber(Settings->Data.LoopTime)) : LOCTEXT("Loop", "Loop"); }),
				LOCTEXT("LoopTooltip", "Loop all simulations, i.e. one shot effects will loop"),
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.LoopIcon"),
				EUserInterfaceActionType::ToggleButton
			);
		}
		// Step Button
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateLambda([=]() {Settings->Data.PlaybackMode = ENiagaraDebugPlaybackMode::Step; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->Data.PlaybackMode == ENiagaraDebugPlaybackMode::Step; })
				),
				NAME_None,
				LOCTEXT("Step", "Step"),
				LOCTEXT("StepTooltip", "Step all simulations a single frame then pause them"),
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.StepIcon"),
				EUserInterfaceActionType::ToggleButton
			);
		}
		// Speed Button
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateLambda([=]() {Settings->Data.bPlaybackRateEnabled = !Settings->Data.bPlaybackRateEnabled; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->Data.bPlaybackRateEnabled; })
				),
				NAME_None,
				CreateTAttribute<FText>([=]() { return  FText::Format(LOCTEXT("PlaybackSpeedFormat", "Speed\n{0} x"), FText::AsNumber(Settings->Data.PlaybackRate)); }),
				LOCTEXT("SlowTooltip", "When enabled adjusts the playback speed for simulations."),
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.SpeedIcon"),
				EUserInterfaceActionType::ToggleButton
			);
		}
		// Additional options
		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SNiagaraDebugger::MakePlaybackOptionsMenu),
			FText(),
			LOCTEXT("PlaybackOptionsTooltip", "Additional options to control playback."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.ToggleMaterialStats"),
			true
		);
	}

	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SNiagaraDebugger::MakePlaybackOptionsMenu()
{
	using namespace NiagaraDebuggerLocal;

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("PlaybackSpeed", "Playback Speed"));
	{
		static const TTuple<float, FText, FText> PlaybackSpeeds[] =
		{
			MakeTuple(1.0000f,	LOCTEXT("PlaybackSpeed_Normal", "Normal Speed"),		LOCTEXT("NormalSpeedTooltip", "Set playback speed to normal")),
			MakeTuple(0.5000f,	LOCTEXT("PlaybackSpeed_Half", "Half Speed"),			LOCTEXT("NormalSpeedTooltip", "Set playback speed to half the normal speed")),
			MakeTuple(0.2500f,	LOCTEXT("PlaybackSpeed_Quarter", "Quarter Speed "),		LOCTEXT("NormalSpeedTooltip", "Set playback speed to quarter the normal speed")),
			MakeTuple(0.1250f,	LOCTEXT("PlaybackSpeed_Eighth", "Eighth Speed "),		LOCTEXT("NormalSpeedTooltip", "Set playback speed to eighth the normal speed")),
			MakeTuple(0.0625f,	LOCTEXT("PlaybackSpeed_Sixteenth", "Sixteenth Speed "),	LOCTEXT("NormalSpeedTooltip", "Set playback speed to sixteenth the normal speed")),
		};
		UNiagaraDebugHUDSettings* Settings = GetMutableDefault<UNiagaraDebugHUDSettings>();

		for ( const auto& Speed : PlaybackSpeeds )
		{
			MenuBuilder.AddMenuEntry(
				Speed.Get<1>(),
				Speed.Get<2>(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([Settings, Rate=Speed.Get<0>()]() { Settings->Data.PlaybackRate = Rate; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([Settings, Rate=Speed.Get<0>()]() { return FMath::IsNearlyEqual(Settings->Data.PlaybackRate, Rate); })
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CustomSpeed", "Custom Speed"))
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SNumericEntryBox<float>)
				.Value(CreateTAttribute<TOptional<float>>([=]() { return TOptional<float>(Settings->Data.PlaybackRate); }))
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(TOptional<float>())
				.MinSliderValue(0.0f)
				.MaxSliderValue(1.0f)
				.OnValueChanged(SNumericEntryBox<float>::FOnValueChanged::CreateLambda([=](float InNewValue) { Settings->Data.PlaybackRate = InNewValue; Settings->PostEditChangeProperty(); }))
			],
			FText()
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LoopTime", "Loop Time"));
	{
		UNiagaraDebugHUDSettings* Settings = GetMutableDefault<UNiagaraDebugHUDSettings>();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LoopTimeEnabled", "Enabled"),
			LOCTEXT("LoopTimeEnabledTooltip", "When enabled and in loop mode systems will loop on this time rather than when they finish"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Settings]() { Settings->Data.bLoopTimeEnabled = !Settings->Data.bLoopTimeEnabled; Settings->PostEditChangeProperty(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Settings]() { return Settings->Data.bLoopTimeEnabled; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LoopTime", "Loop Time"))
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SNumericEntryBox<float>)
				.Value(CreateTAttribute<TOptional<float>>([=]() { return TOptional<float>(Settings->Data.LoopTime); }))
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(TOptional<float>())
				.MinSliderValue(0.0f)
				.MaxSliderValue(5.0f)
				.OnValueChanged(SNumericEntryBox<float>::FOnValueChanged::CreateLambda([=](float InNewValue) { Settings->Data.LoopTime = InNewValue; Settings->PostEditChangeProperty(); }))
			],
			FText()
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

//////////////////////////////////////////////////////////////////////////
// New Session and Messaging code.

void SNiagaraDebugger::SessionManager_OnSessionSelectionChanged(const TSharedPtr<ISessionInfo>& Session)
{
	//Drop all existing and pending connections when the session selection changes.
	TArray<FClientInfo> ToClose;
	if (Session.IsValid())
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Session selection changed. Dropping connections not from this session. | Session: %s (%s)"), *Session->GetSessionId().ToString(), *Session->GetSessionName());
		for (FClientInfo& Client : ConnectedClients)
		{
			if (Client.SessionId != Session->GetSessionId())
			{
				ToClose.Add(Client);
			}
		}
		for (FClientInfo& Client : PendingClients)
		{
			if (Client.SessionId != Session->GetSessionId())
			{
				ToClose.Add(Client);
			}
		}
	}
	else
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Session selection changed. Dropping all connections."));
		ToClose.Append(ConnectedClients);
		ToClose.Append(PendingClients);
	}

	for (FClientInfo& Client : ToClose)
	{
		CloseConnection(Client.SessionId, Client.InstanceId);
	}
}

void SNiagaraDebugger::SessionManager_OnInstanceSelectionChanged(const TSharedPtr<ISessionInstanceInfo>& Instance, bool Selected)
{
	if (MessageEndpoint.IsValid())
	{
		if (Selected)
		{
			const FGuid& SessionId = Instance->GetOwnerSession()->GetSessionId();
			const FGuid& InstanceId = Instance->GetInstanceId();

			int32 FoundActive = FindActiveConnection(SessionId, InstanceId);
			if (FoundActive != INDEX_NONE)
			{
				UE_LOG(LogNiagaraDebugger, Log, TEXT("Session Instance selection callback for existing active connection. Ignored. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *Instance->GetInstanceName());
				return;
			}

			int32 FoundPending = FindPendingConnection(SessionId, InstanceId);
			if (FoundPending != INDEX_NONE)
			{
				UE_LOG(LogNiagaraDebugger, Log, TEXT("Session Instance selection callback for existing pending connection. Ignored. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *Instance->GetInstanceName());
				return;
			}

			FNiagaraDebuggerRequestConnection* ConnectionRequestMessage = new FNiagaraDebuggerRequestConnection(SessionId, InstanceId);
			UE_LOG(LogNiagaraDebugger, Log, TEXT("Establishing connection. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *Instance->GetInstanceName());
			MessageEndpoint->Publish(ConnectionRequestMessage);

			FClientInfo& NewPendingConnection = PendingClients.AddDefaulted_GetRef();
			NewPendingConnection.SessionId = SessionId;
			NewPendingConnection.InstanceId = InstanceId;
			NewPendingConnection.StartTime = FPlatformTime::Seconds();
		}
		else
		{
			CloseConnection(Instance->GetOwnerSession()->GetSessionId(), Instance->GetInstanceId());
		}
	}
}

int32 SNiagaraDebugger::FindPendingConnection(FGuid SessionId, FGuid InstanceId)const
{
	return PendingClients.IndexOfByPredicate([&](const FClientInfo& Pending) { return Pending.SessionId == SessionId && Pending.SessionId == SessionId; });
}

int32 SNiagaraDebugger::FindActiveConnection(FGuid SessionId, FGuid InstanceId)const
{
	return ConnectedClients.IndexOfByPredicate([&](const FClientInfo& Active) { return Active.SessionId == SessionId && Active.SessionId == SessionId; });
}

void SNiagaraDebugger::CloseConnection(FGuid SessionId, FGuid InstanceId)
{
	int32 FoundPending = FindPendingConnection(SessionId, InstanceId);
	int32 FoundActive = FindActiveConnection(SessionId, InstanceId);

	checkf(FoundActive == INDEX_NONE || FoundPending == INDEX_NONE, TEXT("Same client info is on both the pending and active connections lists."));

	if (FoundPending != INDEX_NONE)
	{
		FClientInfo Pending = PendingClients[FoundPending];
		PendingClients.RemoveAtSwap(FoundPending);
		MessageEndpoint->Publish(new FNiagaraDebuggerConnectionClosed(Pending.SessionId, Pending.InstanceId));
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Closing pending connection. | Session: %s | Instance: %s |"), *SessionId.ToString(), *InstanceId.ToString());

		OnConnectionClosedDelegate.Broadcast(Pending);
	}
	if (FoundActive != INDEX_NONE)
	{
		FClientInfo Active = ConnectedClients[FoundActive];
		ConnectedClients.RemoveAtSwap(FoundActive);
		MessageEndpoint->Send(new FNiagaraDebuggerConnectionClosed(Active.SessionId, Active.InstanceId), Active.Address);
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Closing active connection. | Session: %s | Instance: %s |"), *SessionId.ToString(), *InstanceId.ToString());

		OnConnectionClosedDelegate.Broadcast(Active);
	}
}

void SNiagaraDebugger::HandleConnectionAcceptedMessage(const FNiagaraDebuggerAcceptConnection& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	int32 FoundActive = FindActiveConnection(Message.SessionId, Message.InstanceId);
	if (FoundActive != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Recieved connection accepted message from an already connected client. Ignored. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		return;
	}

	int32 FoundPending = FindPendingConnection(Message.SessionId, Message.InstanceId);
	if (FoundPending != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Connection accepted. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		PendingClients.RemoveAtSwap(FoundPending);

		FClientInfo& NewConnection = ConnectedClients.AddDefaulted_GetRef();
		NewConnection.Address = Context->GetSender();
		NewConnection.SessionId = Message.SessionId;
		NewConnection.InstanceId = Message.InstanceId;
		NewConnection.StartTime = FPlatformTime::Seconds();

		UpdateDebugHUDSettings();

		OnConnectionMadeDelegate.Broadcast(NewConnection);
	}
	else
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Recieved connection accepted message from a client that is not in our pending list. Ignored. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
	}
}

void SNiagaraDebugger::HandleConnectionClosedMessage(const FNiagaraDebuggerConnectionClosed& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	int32 FoundPending = FindPendingConnection(Message.SessionId, Message.InstanceId);
	int32 FoundActive = FindActiveConnection(Message.SessionId, Message.InstanceId);

	checkf(FoundActive == INDEX_NONE || FoundPending == INDEX_NONE, TEXT("Same client info is on both the pending and active connections lists."));

	if (FoundPending != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Pending connection closed by the client. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		OnConnectionClosedDelegate.Broadcast(PendingClients[FoundPending]);
		PendingClients.RemoveAtSwap(FoundPending);
	}
	if (FoundActive != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Active connection closed by the client. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		OnConnectionClosedDelegate.Broadcast(ConnectedClients[FoundActive]);
		ConnectedClients.RemoveAtSwap(FoundActive);
	}
}

//////////////////////////////////////////////////////////////////////////
#undef LOCTEXT_NAMESPACE
