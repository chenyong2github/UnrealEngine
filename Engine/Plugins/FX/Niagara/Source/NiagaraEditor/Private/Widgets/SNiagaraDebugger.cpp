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

#define LOCTEXT_NAMESPACE "SNiagaraDebugger"

namespace NiagaraDebuggerLocal
{
	static const FName NiagaraDebuggerTabName(TEXT("NiagaraDebugger"));

	DECLARE_DELEGATE_TwoParams(FOnExecConsoleCommand, const TCHAR*, bool);

	static FSlateIcon GetNoDeviceIcon()
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), "DeviceDetails.TabIcon");
	}

	static FText GetNoDevicePlatformText()
	{
		return LOCTEXT("LocalDevice", "This Device");
	}

	static FText GetNoDeviceText()
	{
		return LOCTEXT("LocalDevice", "<This Device : This Application>");
	}

	static bool IsSupportedPlatform(ITargetPlatform* Platform)
	{
		check(Platform);
		return Platform->SupportsFeature(ETargetPlatformFeatures::DeviceOutputLog);
	}

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

#if WITH_EDITOR
void UNiagaraDebugHUDSettings::PostEditChangeProperty()
{
	OnChangedDelegate.Broadcast();
	SaveConfig();
}

void UNiagaraDebugHUDSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	PostEditChangeProperty();
}
#endif

SNiagaraDebugger::SNiagaraDebugger()
{
}

SNiagaraDebugger::~SNiagaraDebugger()
{
	DestroyDeviceList();
}

void SNiagaraDebugger::Construct(const FArguments& InArgs)
{
	using namespace NiagaraDebuggerLocal;

	InitDeviceList();

	TabManager = InArgs._TabManager;

	NiagaraDebugHudTab::RegisterTabSpawner(TabManager);
	NiagaraPerformanceTab::RegisterTabSpawner(TabManager, FOnExecConsoleCommand::CreateSP(this, &SNiagaraDebugger::ExecConsoleCommand));

	GetMutableDefault<UNiagaraDebugHUDSettings>()->OnChangedDelegate.AddSP(this, &SNiagaraDebugger::ExecHUDConsoleCommand);

	TSharedPtr<FTabManager::FLayout> DebuggerLayout = FTabManager::NewLayout("NiagaraDebugger_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(.4f)
				->SetHideTabWell(true)
				->AddTab(NiagaraDebugHudTab::TabName, ETabState::OpenedTab)
				->AddTab(NiagaraPerformanceTab::TabName, ETabState::OpenedTab)
				->SetForegroundTab(NiagaraDebugHudTab::TabName)
			)
		);

	DebuggerLayout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, DebuggerLayout.ToSharedRef());

	TSharedRef<SWidget> TabContents = TabManager->RestoreFrom(DebuggerLayout.ToSharedRef(), TSharedPtr<SWindow>()).ToSharedRef();

	ChildSlot
	[
		SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeToolbar()
			]
			// Tab Contents
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				TabContents
			]
		]
	];
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

void SNiagaraDebugger::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if ( bWasDeviceConnected == false )
	{
		if ( SelectedTargetDevice != nullptr )
		{
			if ( ITargetDevicePtr DevicePtr = SelectedTargetDevice->DeviceWeakPtr.Pin() )
			{
				if ( DevicePtr->IsConnected() )
				{
					bWasDeviceConnected = true;
					ExecHUDConsoleCommand();
				}
			}
		}
	}
}

void SNiagaraDebugger::AddReferencedObjects(FReferenceCollector& Collector)
{
	//if (DebugHUDSettings != nullptr)
	//{
	//	Collector.AddReferencedObject(DebugHUDSettings);
	//}
}

void SNiagaraDebugger::ExecConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld)
{
	if (SelectedTargetDevice)
	{
		ITargetDevicePtr DevicePtr = SelectedTargetDevice->DeviceWeakPtr.Pin();
		if (DevicePtr && DevicePtr->IsConnected())
		{
			DevicePtr->ExecuteConsoleCommand(Cmd);
		}
	}
	else
	{
		if (bRequiresWorld)
		{
			for (TObjectIterator<UWorld> WorldIt; WorldIt; ++WorldIt)
			{
				UWorld* World = *WorldIt;
				if ( (World != nullptr) &&
					 (World->PersistentLevel != nullptr) &&
					 (World->PersistentLevel->OwningWorld == World) &&
					 ((World->GetNetMode() == ENetMode::NM_Client) || (World->GetNetMode() == ENetMode::NM_Standalone)) )
				{
					GEngine->Exec(*WorldIt, Cmd);
				}
			}
		}
		else
		{
			GEngine->Exec(nullptr, Cmd);
		}
	}
}

void SNiagaraDebugger::ExecHUDConsoleCommand()
{
	const auto BuildVariableString =
		[](const TArray<FNiagaraDebugHUDVariable>& Variables) -> FString
		{
			FString Output;
			for (const FNiagaraDebugHUDVariable& Variable : Variables)
			{
				if (Variable.bEnabled && Variable.Name.Len() > 0)
				{
					if (Output.Len() > 0)
					{
						Output.Append(TEXT(","));
					}
					Output.Append(Variable.Name);
				}
			}
			return Output;
		};

	// Some platforms have limits on the size of the remote command, so split into a series of several commands to send
	if ( const UNiagaraDebugHUDSettings* Settings = GetDefault<UNiagaraDebugHUDSettings>() )
	{
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud HudVerbosity=%d"), Settings->HudVerbosity), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud GpuReadback=%d"), Settings->bEnableGpuReadback ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud DisplayLocation=%d,%d"), Settings->HUDLocation.X, Settings->HUDLocation.Y), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemVerbosity=%d"), (int32)Settings->SystemVerbosity), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemShowBounds=%d "), Settings->bSystemShowBounds ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemShowActiveOnlyInWorld=%d"), Settings->bSystemShowActiveOnlyInWorld ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemFilter=%s"), Settings->bSystemFilterEnabled ? *Settings->SystemFilter : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud EmitterFilter=%s"), Settings->bEmitterFilterEnabled ? *Settings->EmitterFilter : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ActorFilter=%s"), Settings->bActorFilterEnabled ? *Settings->ActorFilter : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ComponentFilter=%s"), Settings->bComponentFilterEnabled ? *Settings->ComponentFilter : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemVariables=%s"), Settings->bShowSystemVariables ? *BuildVariableString(Settings->SystemVariables) : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ParticleVariables=%s"), Settings->bShowParticleVariables ? *BuildVariableString(Settings->ParticlesVariables) : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud MaxParticlesToDisplay=%d"), Settings->MaxParticlesToDisplay), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ShowParticlesInWorld=%d"), Settings->bShowParticlesInWorld ? 1 : 0), false);

		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.PlaybackMode %d"), Settings->PlaybackMode), true);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.PlaybackRate %.3f"), Settings->bPlaybackRateEnabled ? Settings->PlaybackRate : 1.0f), true);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.GlobalLoopTime %.3f"), (Settings->PlaybackMode == ENiagaraDebugPlaybackMode::Loop) && Settings->bLoopTimeEnabled ? Settings->LoopTime : 0.0f), true);
	}
}

TSharedRef<SWidget> SNiagaraDebugger::MakeToolbar()
{
	using namespace NiagaraDebuggerLocal;

	FToolBarBuilder ToolbarBuilder(MakeShareable(new FUICommandList), FMultiBoxCustomization::None);
	UNiagaraDebugHUDSettings* Settings = GetMutableDefault<UNiagaraDebugHUDSettings>();
	ToolbarBuilder.BeginSection("Main");

	// Device selection
	{
		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SNiagaraDebugger::MakeDeviceComboButtonMenu),
			CreateTAttribute<FText>([Owner = this]() { return Owner->SelectedTargetDevice.IsValid() ? Owner->SelectedTargetDevice->PlatformName : GetNoDevicePlatformText(); }),
			LOCTEXT("Device_Tooltip", "The device we are currently connected to."),
			CreateTAttribute<FSlateIcon>([Owner=this]() { return Owner->SelectedTargetDevice.IsValid() ? FSlateIcon(FEditorStyle::GetStyleSetName(), Owner->SelectedTargetDevice->DeviceIconStyle) : GetNoDeviceIcon(); })
		);
	}
	// Refresh button
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateLambda([Owner=this]() { Owner->ExecHUDConsoleCommand(); })),
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
					FExecuteAction::CreateLambda([=]() {Settings->PlaybackMode = ENiagaraDebugPlaybackMode::Play; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->PlaybackMode == ENiagaraDebugPlaybackMode::Play; })
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
					FExecuteAction::CreateLambda([=]() {Settings->PlaybackMode = ENiagaraDebugPlaybackMode::Paused; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->PlaybackMode == ENiagaraDebugPlaybackMode::Paused; })
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
					FExecuteAction::CreateLambda([=]() {Settings->PlaybackMode = ENiagaraDebugPlaybackMode::Loop; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->PlaybackMode == ENiagaraDebugPlaybackMode::Loop; })
				),
				NAME_None,
				CreateTAttribute<FText>([=]() { return Settings->bLoopTimeEnabled ? FText::Format(LOCTEXT("PlaybackLoopFormat", "Loop Every\n{0} Seconds"), FText::AsNumber(Settings->LoopTime)) : LOCTEXT("Loop", "Loop"); }),
				LOCTEXT("LoopTooltip", "Loop all simulations, i.e. one shot effects will loop"),
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Debugger.LoopIcon"),
				EUserInterfaceActionType::ToggleButton
			);
		}
		// Step Button
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateLambda([=]() {Settings->PlaybackMode = ENiagaraDebugPlaybackMode::Step; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->PlaybackMode == ENiagaraDebugPlaybackMode::Step; })
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
					FExecuteAction::CreateLambda([=]() {Settings->bPlaybackRateEnabled = !Settings->bPlaybackRateEnabled; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([=]() { return Settings->bPlaybackRateEnabled; })
				),
				NAME_None,
				CreateTAttribute<FText>([=]() { return  FText::Format(LOCTEXT("PlaybackSpeedFormat", "Speed\n{0} x"), FText::AsNumber(Settings->PlaybackRate)); }),
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
					FExecuteAction::CreateLambda([Settings, Rate=Speed.Get<0>()]() { Settings->PlaybackRate = Rate; Settings->PostEditChangeProperty(); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([Settings, Rate=Speed.Get<0>()]() { return FMath::IsNearlyEqual(Settings->PlaybackRate, Rate); })
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
				.Value(CreateTAttribute<TOptional<float>>([=]() { return TOptional<float>(Settings->PlaybackRate); }))
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(TOptional<float>())
				.MinSliderValue(0.0f)
				.MaxSliderValue(1.0f)
				.OnValueChanged(SNumericEntryBox<float>::FOnValueChanged::CreateLambda([=](float InNewValue) { Settings->PlaybackRate = InNewValue; Settings->PostEditChangeProperty(); }))
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
				FExecuteAction::CreateLambda([Settings]() { Settings->bLoopTimeEnabled = !Settings->bLoopTimeEnabled; Settings->PostEditChangeProperty(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Settings]() { return Settings->bLoopTimeEnabled; })
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
				.Value(CreateTAttribute<TOptional<float>>([=]() { return TOptional<float>(Settings->LoopTime); }))
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(TOptional<float>())
				.MinSliderValue(0.0f)
				.MaxSliderValue(5.0f)
				.OnValueChanged(SNumericEntryBox<float>::FOnValueChanged::CreateLambda([=](float InNewValue) { Settings->LoopTime = InNewValue; Settings->PostEditChangeProperty(); }))
			],
			FText()
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SNiagaraDebugger::MakeDeviceComboButtonMenu()
{
	using namespace NiagaraDebuggerLocal;

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	// Entry for local application
	MenuBuilder.AddMenuEntry(
		NiagaraDebuggerLocal::GetNoDeviceText(),
		FText(),
		NiagaraDebuggerLocal::GetNoDeviceIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SNiagaraDebugger::SelectDevice, FTargetDeviceEntryPtr()),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([Owner = this]() { return Owner->SelectedTargetDevice == FTargetDeviceEntryPtr(); })
		),
		NAME_None,
		EUserInterfaceActionType::RadioButton
	);

	// Create entry per device
	for (const auto& DeviceEntry : TargetDeviceList)
	{
		MenuBuilder.AddMenuEntry(
			CreateTAttribute<FText>([Owner=this, DeviceEntry]() { return Owner->GetTargetDeviceText(DeviceEntry); }),
			FText(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), DeviceEntry->DeviceIconStyle),
			FUIAction(
				FExecuteAction::CreateSP(this, &SNiagaraDebugger::SelectDevice, DeviceEntry),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Owner = this, DeviceEntry]() { return Owner->SelectedTargetDevice == DeviceEntry; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	return MenuBuilder.MakeWidget();
}

void SNiagaraDebugger::InitDeviceList()
{
	if (ITargetPlatformManagerModule* TPM = FModuleManager::GetModulePtr<ITargetPlatformManagerModule>("TargetPlatform"))
	{
		for (ITargetPlatform* TargetPlatform : TPM->GetTargetPlatforms())
		{
			if (NiagaraDebuggerLocal::IsSupportedPlatform(TargetPlatform))
			{
				TargetPlatform->OnDeviceDiscovered().AddRaw(this, &SNiagaraDebugger::AddTargetDevice);
				TargetPlatform->OnDeviceLost().AddRaw(this, &SNiagaraDebugger::RemoveTargetDevice);

				TArray<ITargetDevicePtr> TargetDevices;
				TargetPlatform->GetAllDevices(TargetDevices);
				
				for (const ITargetDevicePtr& TargetDevice : TargetDevices)
				{
					if (TargetDevice.IsValid())
					{
						AddTargetDevice(TargetDevice.ToSharedRef());
					}
				}
			}
		}
	}
}

void SNiagaraDebugger::DestroyDeviceList()
{
	if ( ITargetPlatformManagerModule* TPM = FModuleManager::GetModulePtr<ITargetPlatformManagerModule>("TargetPlatform") )
	{
		for (ITargetPlatform* TargetPlatform : TPM->GetTargetPlatforms())
		{
			TargetPlatform->OnDeviceDiscovered().RemoveAll(this);
			TargetPlatform->OnDeviceLost().RemoveAll(this);
		}
	}
}

void SNiagaraDebugger::AddTargetDevice(ITargetDeviceRef TargetDevice)
{
	// Check it doesn't already exist
	for ( const auto& DevicePtr : TargetDeviceList )
	{
		if ( DevicePtr->DeviceId.GetDeviceName() == TargetDevice->GetId().GetDeviceName() )
		{
			return;
		}
	}

	// Add device
	auto PlatformInfo = TargetDevice->GetTargetPlatform().GetPlatformInfo();
	FTargetDeviceEntryPtr NewDevice = MakeShareable(new FTargetDeviceEntry);
	NewDevice->DeviceId = TargetDevice->GetId();
	NewDevice->DeviceWeakPtr = TargetDevice;
	NewDevice->PlatformName = PlatformInfo.DisplayName;
	NewDevice->DeviceIconStyle = PlatformInfo.GetIconStyleName(PlatformInfo::EPlatformIconSize::Normal);

	TargetDeviceList.Add(NewDevice);
}

void SNiagaraDebugger::RemoveTargetDevice(ITargetDeviceRef TargetDevice)
{
	if (SelectedTargetDevice && (SelectedTargetDevice->DeviceId.GetDeviceName() == TargetDevice->GetId().GetDeviceName()))
	{
		SelectDevice(nullptr);
	}

	TargetDeviceList.RemoveAllSwap(
		[&](const FTargetDeviceEntryPtr& DevicePtr)
		{
			return DevicePtr->DeviceId.GetDeviceName() == TargetDevice->GetId().GetDeviceName();
		}
	);
}

void SNiagaraDebugger::SelectDevice(FTargetDeviceEntryPtr DeviceEntry)
{
	SelectedTargetDevice = DeviceEntry;
	bWasDeviceConnected = true;
	if ( SelectedTargetDevice != nullptr )
	{
		if ( ITargetDevicePtr DevicePtr = SelectedTargetDevice->DeviceWeakPtr.Pin() )
		{
			bWasDeviceConnected = DevicePtr->IsConnected();
		}
	}

	ExecHUDConsoleCommand();
}

FText SNiagaraDebugger::GetTargetDeviceText(FTargetDeviceEntryPtr DeviceEntry) const
{
	if (DeviceEntry.IsValid())
	{
		ITargetDevicePtr PinnedPtr = DeviceEntry->DeviceWeakPtr.Pin();
		if (PinnedPtr.IsValid() && PinnedPtr->IsConnected())
		{
			FString DeviceName = PinnedPtr->GetName();
			return FText::FromString(DeviceName);
		}
		else
		{
			FString DeviceName = DeviceEntry->DeviceId.GetDeviceName();
			return FText::Format(LOCTEXT("TargetDeviceOffline", "{0} (Offline)"), FText::FromString(DeviceName));
		}
	}
	else
	{
		return NiagaraDebuggerLocal::GetNoDeviceText();
	}
}

#undef LOCTEXT_NAMESPACE
