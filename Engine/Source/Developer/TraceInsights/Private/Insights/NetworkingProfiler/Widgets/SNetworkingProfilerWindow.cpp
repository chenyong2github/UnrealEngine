// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNetworkingProfilerWindow.h"

#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_EDITOR
	#include "EngineAnalytics.h"
	#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
	#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#endif // WITH_EDITOR

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/NetworkingProfiler/Widgets/SNetStatsView.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerToolbar.h"
#include "Insights/NetworkingProfiler/Widgets/SPacketContentView.h"
#include "Insights/NetworkingProfiler/Widgets/SPacketView.h"
#include "Insights/TraceInsightsModule.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SInsightsSettings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SNetworkingProfilerWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FNetworkingProfilerTabs::PacketViewID(TEXT("PacketView"));
const FName FNetworkingProfilerTabs::PacketContentViewID(TEXT("PacketContent"));
const FName FNetworkingProfilerTabs::NetStatsViewID(TEXT("NetStats"));

////////////////////////////////////////////////////////////////////////////////////////////////////

SNetworkingProfilerWindow::SNetworkingProfilerWindow()
	: SelectedPacketStartIndex(0)
	, SelectedPacketEndIndex(0)
	, SelectionStartPosition(0)
	, SelectionEndPosition(0)
	, SelectedEventTypeIndex(InvalidEventTypeIndex)
	, DurationActive(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SNetworkingProfilerWindow::~SNetworkingProfilerWindow()
{
	if (NetStatsView)
	{
		HideTab(FNetworkingProfilerTabs::NetStatsViewID);
		check(NetStatsView == nullptr);
	}

	if (PacketContentView)
	{
		HideTab(FNetworkingProfilerTabs::PacketContentViewID);
		check(PacketContentView == nullptr);
	}

	if (PacketView)
	{
		HideTab(FNetworkingProfilerTabs::PacketViewID);
		check(PacketView == nullptr);
	}

#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Insights.Usage.NetworkingProfiler"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::Reset()
{
	if (PacketView)
	{
		PacketView->Reset();
	}

	if (PacketContentView)
	{
		PacketContentView->Reset();
	}

	if (NetStatsView)
	{
		NetStatsView->Reset();
	}

	AvailableGameInstances.Reset();
	SelectedGameInstance.Reset();
	AvailableConnections.Reset();
	SelectedConnection.Reset();
	AvailableConnectionModes.Reset();
	SelectedConnectionMode.Reset();

	SelectedPacketStartIndex = 0;
	SelectedPacketEndIndex = 0;
	SelectionStartPosition = 0;
	SelectionEndPosition = 0;

	SelectedEventTypeIndex = InvalidEventTypeIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SNetworkingProfilerWindow::SpawnTab_PacketView(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(PacketView, SPacketView, SharedThis(this))
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SNetworkingProfilerWindow::OnPacketViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::OnPacketViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	PacketView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SNetworkingProfilerWindow::SpawnTab_PacketContentView(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(PacketContentView, SPacketContentView, SharedThis(this))
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SNetworkingProfilerWindow::OnPacketContentViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::OnPacketContentViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	PacketContentView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SNetworkingProfilerWindow::SpawnTab_NetStatsView(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(NetStatsView, SNetStatsView, SharedThis(this))
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SNetworkingProfilerWindow::OnNetStatsViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::OnNetStatsViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	NetStatsView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SNetworkingProfilerWindow::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	CommandList = MakeShared<FUICommandList>();

	// Create & initialize tab manager.
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);
	const auto& PersistLayout = [](const TSharedRef<FTabManager::FLayout>& LayoutToSave)
	{
		FLayoutSaveRestore::SaveToConfig(FTraceInsightsModule::GetUnrealInsightsLayoutIni(), LayoutToSave);
	};
	TabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateLambda(PersistLayout));

	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("NetworkingProfilerMenuGroupName", "Networking Insights"));

	TabManager->RegisterTabSpawner(FNetworkingProfilerTabs::PacketViewID, FOnSpawnTab::CreateRaw(this, &SNetworkingProfilerWindow::SpawnTab_PacketView))
		.SetDisplayName(LOCTEXT("PacketViewTabTitle", "Packet View"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.PacketView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FNetworkingProfilerTabs::PacketContentViewID, FOnSpawnTab::CreateRaw(this, &SNetworkingProfilerWindow::SpawnTab_PacketContentView))
		.SetDisplayName(LOCTEXT("PacketContentViewTabTitle", "Packet Content"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.PacketContentView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FNetworkingProfilerTabs::NetStatsViewID, FOnSpawnTab::CreateRaw(this, &SNetworkingProfilerWindow::SpawnTab_NetStatsView))
		.SetDisplayName(LOCTEXT("NetStatsViewTabTitle", "Net Stats"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.NetStatsView"))
		.SetGroup(AppMenuGroup);

	TSharedPtr<FNetworkingProfilerManager> NetworkingProfilerManager = FNetworkingProfilerManager::Get();
	ensure(NetworkingProfilerManager.IsValid());

	// Create tab layout.
	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("InsightsNetworkingProfilerLayout_v1.2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.65f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.35f)
					->SetHideTabWell(true)
					->AddTab(FNetworkingProfilerTabs::PacketViewID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.65f)
					->SetHideTabWell(true)
					->AddTab(FNetworkingProfilerTabs::PacketContentViewID, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.35f)
				->AddTab(FNetworkingProfilerTabs::NetStatsViewID, ETabState::OpenedTab)
				->SetForegroundTab(FNetworkingProfilerTabs::NetStatsViewID)
			)
		);

	Layout = FLayoutSaveRestore::LoadFromConfig(FTraceInsightsModule::GetUnrealInsightsLayoutIni(), Layout);

	// Create & initialize main menu.
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("MenuLabel", "Menu"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateStatic(&SNetworkingProfilerWindow::FillMenu, TabManager),
		FName(TEXT("Menu"))
	);

#if !WITH_EDITOR
	TSharedRef<SWidget> MenuWidget = MenuBarBuilder.MakeWidget();
	MenuWidget->SetClipping(EWidgetClipping::ClipToBoundsWithoutIntersecting);
#endif

	ChildSlot
	[
		SNew(SOverlay)

#if !WITH_EDITOR
		// Menu
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(34.0f, -60.0f, 0.0f, 0.0f)
		[
			MenuWidget
		]
#endif

		// Version
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(0.0f, -16.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Clipping(EWidgetClipping::ClipToBoundsWithoutIntersecting)
			.Text(LOCTEXT("UnrealInsightsVersion", UNREAL_INSIGHTS_VERSION_STRING_EX))
			.ColorAndOpacity(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
		]

		// Overlay slot for the main window area
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
			[
				SNew(SNetworkingProfilerToolbar, SharedThis(this))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				TabManager->RestoreFrom(Layout, ConstructUnderWindow).ToSharedRef()
			]
		]

		// Session hint overlay
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.Visibility(this, &SNetworkingProfilerWindow::IsSessionOverlayVisible)
			.BorderImage(FAppStyle::Get().GetBrush("PopupText.Background"))
			.Padding(8.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SelectTraceOverlayText", "Please select a trace."))
			]
		]
	];

#if !WITH_EDITOR
	// Tell tab-manager about the global menu bar.
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuWidget);
#endif

	BindCommands();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::BindCommands()
{
	Map_TogglePacketViewVisibility();
	Map_TogglePacketContentViewVisibility();
	Map_ToggleNetStatsViewVisibility();

	CommandList->MapAction(FInsightsCommands::Get().ToggleDebugInfo, FInsightsManager::GetActionManager().ToggleDebugInfo_Custom());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Toggle Commands
////////////////////////////////////////////////////////////////////////////////////////////////////

#define IMPLEMENT_TOGGLE_COMMAND(CmdName, IsEnabled, SetEnabled) \
	\
	void SNetworkingProfilerWindow::Map_##CmdName()\
	{\
		CommandList->MapAction(FNetworkingProfilerManager::GetCommands().CmdName, CmdName##_Custom());\
	}\
	\
	const FUIAction SNetworkingProfilerWindow::CmdName##_Custom() \
	{\
		FUIAction UIAction;\
		UIAction.ExecuteAction = FExecuteAction::CreateRaw(this, &SNetworkingProfilerWindow::CmdName##_Execute);\
		UIAction.CanExecuteAction = FCanExecuteAction::CreateRaw(this, &SNetworkingProfilerWindow::CmdName##_CanExecute);\
		UIAction.GetActionCheckState = FGetActionCheckState::CreateRaw(this, &SNetworkingProfilerWindow::CmdName##_GetCheckState);\
		return UIAction;\
	}\
	\
	void SNetworkingProfilerWindow::CmdName##_Execute()\
	{\
		SetEnabled(!IsEnabled());\
	}\
	\
	bool SNetworkingProfilerWindow::CmdName##_CanExecute() const\
	{\
		return FInsightsManager::Get()->GetSession().IsValid();\
	}\
	\
	ECheckBoxState SNetworkingProfilerWindow::CmdName##_GetCheckState() const\
	{\
		return IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;\
	}

IMPLEMENT_TOGGLE_COMMAND(TogglePacketViewVisibility, IsPacketViewVisible, ShowOrHidePacketView)
IMPLEMENT_TOGGLE_COMMAND(TogglePacketContentViewVisibility, IsPacketContentViewVisible, ShowOrHidePacketContentView)
IMPLEMENT_TOGGLE_COMMAND(ToggleNetStatsViewVisibility, IsNetStatsViewVisible, ShowOrHideNetStatsView)

#undef IMPLEMENT_TOGGLE_COMMAND

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager)
{
	if (!TabManager.IsValid())
	{
		return;
	}

	FInsightsManager::Get()->GetInsightsMenuBuilder()->PopulateMenu(MenuBuilder);

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::ShowTab(const FName& TabID)
{
	if (TabManager->HasTabSpawner(TabID))
	{
		TabManager->TryInvokeTab(TabID);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::HideTab(const FName& TabID)
{
	TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(TabID);
	if (Tab.IsValid())
	{
		Tab->RequestCloseTab();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SNetworkingProfilerWindow::IsSessionOverlayVisible() const
{
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Hidden;
	}
	else
	{
		return EVisibility::Visible;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetworkingProfilerWindow::IsProfilerEnabled() const
{
	return FInsightsManager::Get()->GetSession().IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EActiveTimerReturnType SNetworkingProfilerWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The profiler window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();

	uint32 GameInstanceCount = 0;
	if (Session.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::INetProfilerProvider* NetProfilerProvider = TraceServices::ReadNetProfilerProvider(*Session.Get());
		if (NetProfilerProvider)
		{
			GameInstanceCount = NetProfilerProvider->GetGameInstanceCount();
		}
	}

	if (GameInstanceCount != AvailableGameInstances.Num())
	{
		UpdateAvailableGameInstances();
	}

	uint32 ConnectionCount = 0;
	if (Session.IsValid() && SelectedGameInstance.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::INetProfilerProvider* NetProfilerProvider = TraceServices::ReadNetProfilerProvider(*Session.Get());
		if (NetProfilerProvider)
		{
			ConnectionCount = NetProfilerProvider->GetConnectionCount(SelectedGameInstance->GetIndex());
		}
	}

	if (ConnectionCount != AvailableConnections.Num())
	{
		UpdateAvailableConnections();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::UpdateAvailableGameInstances()
{
	uint32 CurrentSelectedGameInstanceIndex = 0;
	bool bIsInstanceSelected = false;
	if (GameInstanceComboBox.IsValid())
	{
		TSharedPtr<FGameInstanceItem> CurrentSelectedGameInstance = GameInstanceComboBox->GetSelectedItem();
		if (CurrentSelectedGameInstance.IsValid())
		{
			CurrentSelectedGameInstanceIndex = CurrentSelectedGameInstance->GetIndex();
			bIsInstanceSelected = true;
		}
	}

	AvailableGameInstances.Reset();

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::INetProfilerProvider* NetProfilerProvider = TraceServices::ReadNetProfilerProvider(*Session.Get());
		if (NetProfilerProvider)
		{
			NetProfilerProvider->ReadGameInstances([this](const TraceServices::FNetProfilerGameInstance& GameInstance)
			{
				AvailableGameInstances.Add(MakeShared<FGameInstanceItem>(GameInstance));
			});
		}
	}

	if (GameInstanceComboBox.IsValid())
	{
		const uint32 AvailableGameInstanceCount = AvailableGameInstances.Num();
		if (bIsInstanceSelected && AvailableGameInstanceCount > CurrentSelectedGameInstanceIndex)
		{
			GameInstanceComboBox->SetSelectedItem(AvailableGameInstances[CurrentSelectedGameInstanceIndex]);
		}
		else
		{
			GameInstanceComboBox->SetSelectedItem(AvailableGameInstances.Num() > 0 ? AvailableGameInstances[0] : nullptr);
		}
	}

	UpdateAvailableConnections();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::UpdateAvailableConnections()
{
	uint32 CurrentSelectedConnectionIndex = 0;
	bool bIsConnectionSelected = false;
	if (ConnectionComboBox.IsValid())
	{
		TSharedPtr<FConnectionItem> CurrentSelectedConnection = ConnectionComboBox->GetSelectedItem();
		if (CurrentSelectedConnection.IsValid())
		{
			CurrentSelectedConnectionIndex = CurrentSelectedConnection->GetIndex();
			bIsConnectionSelected = true;
		}
	}

	AvailableConnections.Reset();

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && SelectedGameInstance.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::INetProfilerProvider* NetProfilerProvider = TraceServices::ReadNetProfilerProvider(*Session.Get());
		if (NetProfilerProvider)
		{
			NetProfilerProvider->ReadConnections(SelectedGameInstance->GetIndex(), [this](const TraceServices::FNetProfilerConnection& Connection)
			{
				AvailableConnections.Add(MakeShared<FConnectionItem>(Connection));
			});
		}
	}

	if (ConnectionComboBox.IsValid())
	{
		const uint32 AvailableConnectionCount = AvailableConnections.Num();
		if (bIsConnectionSelected && AvailableConnectionCount > CurrentSelectedConnectionIndex)
		{
			ConnectionComboBox->SetSelectedItem(AvailableConnections[CurrentSelectedConnectionIndex]);
		}
		else
		{
			ConnectionComboBox->SetSelectedItem(AvailableConnections.Num() > 0 ? AvailableConnections[0] : nullptr);
		}
	}

	UpdateAvailableConnectionModes();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::UpdateAvailableConnectionModes()
{
	bool bIsConnectionModeSelected = false;
	TraceServices::ENetProfilerConnectionMode CurrentSelectedConnectionModeChoice = TraceServices::ENetProfilerConnectionMode::Incoming;
	if (ConnectionModeComboBox.IsValid())
	{
		TSharedPtr<FConnectionModeItem> CurrentSelectedConnectionMode = ConnectionModeComboBox->GetSelectedItem();
		if (CurrentSelectedConnectionMode.IsValid())
		{
			CurrentSelectedConnectionModeChoice = CurrentSelectedConnectionMode->Mode;
			bIsConnectionModeSelected = true;
		}
	}

	AvailableConnectionModes.Reset();

	if (SelectedConnection.IsValid())
	{
		if (SelectedConnection->Connection.bHasIncomingData)
		{
			AvailableConnectionModes.Add(MakeShared<FConnectionModeItem>(TraceServices::ENetProfilerConnectionMode::Incoming));
		}
		if (SelectedConnection->Connection.bHasOutgoingData)
		{
			AvailableConnectionModes.Add(MakeShared<FConnectionModeItem>(TraceServices::ENetProfilerConnectionMode::Outgoing));
		}
	}

	if (ConnectionModeComboBox.IsValid())
	{
		const uint32 AvailableConnectionModeCount = AvailableConnectionModes.Num();
		if (bIsConnectionModeSelected && AvailableConnectionModeCount == 2)
		{
			if (CurrentSelectedConnectionModeChoice == TraceServices::ENetProfilerConnectionMode::Outgoing)
			{
				ConnectionModeComboBox->SetSelectedItem(AvailableConnectionModes[1]);
			}
			else
			{
				ConnectionModeComboBox->SetSelectedItem(AvailableConnectionModes[0]);
			}
		}
		else
		{
			ConnectionModeComboBox->SetSelectedItem(AvailableConnectionModes.Num() > 0 ? AvailableConnectionModes[0] : nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SNetworkingProfilerWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SNetworkingProfilerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return GetCommandList()->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SNetworkingProfilerWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (FInsightsManager::Get()->OnDragOver(DragDropEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SNetworkingProfilerWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (FInsightsManager::Get()->OnDrop(DragDropEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::FGameInstanceItem::GetText() const
{
	if (IsInstanceNameSet())
	{
		return FText::Format(LOCTEXT("GameInstanceItemFmt0", "{0} Game Instance {1} [{2}]"), FText::FromString(GetInstanceName()), FText::AsNumber(GetIndex()),
			IsServer() ? FText::FromString("Server") : FText::FromString("Client"));
	}
	else
	{
		return FText::Format(LOCTEXT("GameInstanceItemFmt1", "Game Instance {0}"), FText::AsNumber(GetIndex()));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::FGameInstanceItem::GetTooltipText() const
{
	return GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::FConnectionItem::GetText() const
{
	if (IsConnectionNameSet())
	{
		if (IsConnectionAddressSet())
		{
			return FText::Format(LOCTEXT("ConnectionItemFmt3", "Connection {0} ({1}) to {2}"), FText::AsNumber(GetIndex()), FText::FromString(GetConnectionName()),
				FText::FromString(GetConnectionAddress()));
		}
		else
		{
			return FText::Format(LOCTEXT("ConnectionItemFmt4", "Connection {0} ({1})"), FText::AsNumber(GetIndex()), FText::FromString(GetConnectionName()));
		}
	}
	else
	{
		if (IsConnectionAddressSet())
		{
			return FText::Format(LOCTEXT("ConnectionItemFmt1", "Connection {0} to {1}"), FText::AsNumber(GetIndex()),
				FText::FromString(GetConnectionAddress()));
		}
		else
		{
			return FText::Format(LOCTEXT("ConnectionItemFmt2", "Connection {0}"), FText::AsNumber(GetIndex()));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::FConnectionItem::GetTooltipText() const
{
	return GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::FConnectionModeItem::GetText() const
{
	switch (Mode)
	{
	case TraceServices::ENetProfilerConnectionMode::Outgoing:
		return LOCTEXT("ConnectionMode_Outgoing", "Outgoing");

	case TraceServices::ENetProfilerConnectionMode::Incoming:
		return LOCTEXT("ConnectionMode_Incoming", "Incoming");

	default:
		return LOCTEXT("ConnectionMode_Unknown", "Unknown");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::FConnectionModeItem::GetTooltipText() const
{
	return GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SNetworkingProfilerWindow::CreateGameInstanceComboBox()
{
	GameInstanceComboBox = SNew(SComboBox<TSharedPtr<FGameInstanceItem>>)
		.ToolTipText(this, &SNetworkingProfilerWindow::GameInstance_GetSelectedTooltipText)
		.OptionsSource(&AvailableGameInstances)
		.OnSelectionChanged(this, &SNetworkingProfilerWindow::GameInstance_OnSelectionChanged)
		.OnGenerateWidget(this, &SNetworkingProfilerWindow::GameInstance_OnGenerateWidget)
		[
			SNew(STextBlock)
			.Text(this, &SNetworkingProfilerWindow::GameInstance_GetSelectedText)
		];
	return SNew(SBox)
		.Padding(FMargin(4.0f, 0.0f, 2.0f, 0.0f))
		[
			GameInstanceComboBox.ToSharedRef()
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SNetworkingProfilerWindow::GameInstance_OnGenerateWidget(TSharedPtr<FGameInstanceItem> InGameInstance) const
{
	return SNew(STextBlock)
		.Text(InGameInstance->GetText())
		.ToolTipText(InGameInstance->GetTooltipText());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::GameInstance_OnSelectionChanged(TSharedPtr<FGameInstanceItem> NewGameInstance, ESelectInfo::Type SelectInfo)
{
	const bool bSameValue = (!SelectedGameInstance.IsValid() && !NewGameInstance.IsValid()) ||
							(SelectedGameInstance.IsValid() && NewGameInstance.IsValid() &&
								SelectedGameInstance->GetIndex() == NewGameInstance->GetIndex());

	SelectedGameInstance = NewGameInstance;

	if (!bSameValue)
	{
		UpdateAvailableConnections();
		if (PacketView.IsValid())
		{
			PacketView->SetConnection(GetSelectedGameInstanceIndex(), GetSelectedConnectionIndex(), GetSelectedConnectionMode());
		}
		UpdateAggregatedNetStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::GameInstance_GetSelectedText() const
{
	return SelectedGameInstance.IsValid() ? SelectedGameInstance->GetText() : LOCTEXT("NoGameInstanceText", "Game Instance N/A");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::GameInstance_GetSelectedTooltipText() const
{
	return SelectedGameInstance.IsValid() ? SelectedGameInstance->GetTooltipText() : FText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SNetworkingProfilerWindow::CreateConnectionComboBox()
{
	ConnectionComboBox = SNew(SComboBox<TSharedPtr<FConnectionItem>>)
		.ToolTipText(this, &SNetworkingProfilerWindow::Connection_GetSelectedTooltipText)
		.OptionsSource(&AvailableConnections)
		.OnSelectionChanged(this, &SNetworkingProfilerWindow::Connection_OnSelectionChanged)
		.OnGenerateWidget(this, &SNetworkingProfilerWindow::Connection_OnGenerateWidget)
		[
			SNew(STextBlock)
			.Text(this, &SNetworkingProfilerWindow::Connection_GetSelectedText)
		];
	return SNew(SBox)
		.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
		[
			ConnectionComboBox.ToSharedRef()
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SNetworkingProfilerWindow::Connection_OnGenerateWidget(TSharedPtr<FConnectionItem> InConnection) const
{
	return SNew(STextBlock)
		.Text(InConnection->GetText())
		.ToolTipText(InConnection->GetTooltipText());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::Connection_OnSelectionChanged(TSharedPtr<FConnectionItem> NewConnection, ESelectInfo::Type SelectInfo)
{
	const bool bSameValue = (!SelectedConnection.IsValid() && !NewConnection.IsValid()) ||
							(SelectedConnection.IsValid() && NewConnection.IsValid() &&
								SelectedConnection->GetIndex() == NewConnection->GetIndex());

	SelectedConnection = NewConnection;

	if (!bSameValue)
	{
		UpdateAvailableConnectionModes();
		if (PacketView.IsValid())
		{
			PacketView->SetConnection(GetSelectedGameInstanceIndex(), GetSelectedConnectionIndex(), GetSelectedConnectionMode());
		}
		UpdateAggregatedNetStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::Connection_GetSelectedText() const
{
	return SelectedConnection.IsValid() ? SelectedConnection->GetText() : LOCTEXT("NoConnectionText", "Connection N/A");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::Connection_GetSelectedTooltipText() const
{
	return SelectedConnection.IsValid() ? SelectedConnection->GetTooltipText() : FText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SNetworkingProfilerWindow::CreateConnectionModeComboBox()
{
	ConnectionModeComboBox = SNew(SComboBox<TSharedPtr<FConnectionModeItem>>)
		.ToolTipText(this, &SNetworkingProfilerWindow::ConnectionMode_GetSelectedTooltipText)
		.OptionsSource(&AvailableConnectionModes)
		.OnSelectionChanged(this, &SNetworkingProfilerWindow::ConnectionMode_OnSelectionChanged)
		.OnGenerateWidget(this, &SNetworkingProfilerWindow::ConnectionMode_OnGenerateWidget)
		[
			SNew(STextBlock)
			.Text(this, &SNetworkingProfilerWindow::ConnectionMode_GetSelectedText)
		];

	return SNew(SBox)
		.Padding(FMargin(2.0f, 0.0f, 4.0f, 0.0f))
		[
			ConnectionModeComboBox.ToSharedRef()
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SNetworkingProfilerWindow::ConnectionMode_OnGenerateWidget(TSharedPtr<FConnectionModeItem> InConnectionMode) const
{
	return SNew(STextBlock)
		.Text(InConnectionMode->GetText())
		.ToolTipText(InConnectionMode->GetTooltipText());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::ConnectionMode_OnSelectionChanged(TSharedPtr<FConnectionModeItem> NewConnectionMode, ESelectInfo::Type SelectInfo)
{
	const bool bSameValue = (!SelectedConnectionMode.IsValid() && !NewConnectionMode.IsValid()) ||
							(SelectedConnectionMode.IsValid() && NewConnectionMode.IsValid() &&
								SelectedConnectionMode->Mode == NewConnectionMode->Mode);

	SelectedConnectionMode = NewConnectionMode;

	if (!bSameValue)
	{
		if (PacketView.IsValid())
		{
			PacketView->SetConnection(GetSelectedGameInstanceIndex(), GetSelectedConnectionIndex(), GetSelectedConnectionMode());
		}
		UpdateAggregatedNetStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::ConnectionMode_GetSelectedText() const
{
	return SelectedConnectionMode.IsValid() ? SelectedConnectionMode->GetText() : LOCTEXT("NoConnectionModeText", "N/A");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetworkingProfilerWindow::ConnectionMode_GetSelectedTooltipText() const
{
	return SelectedConnectionMode.IsValid() ? SelectedConnectionMode->GetTooltipText() : FText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::SetSelectedPacket(uint32 StartIndex, uint32 EndIndex, uint32 SinglePacketBitSize)
{
	if (StartIndex != SelectedPacketStartIndex || EndIndex != SelectedPacketEndIndex)
	{
		SelectedPacketStartIndex = StartIndex;
		SelectedPacketEndIndex = EndIndex;

		UpdateAggregatedNetStats();

		if (PacketContentView.IsValid())
		{
			if (SelectedPacketEndIndex == SelectedPacketStartIndex + 1 && // only one packet selected
				SelectedGameInstance.IsValid() &&
				SelectedConnection.IsValid() &&
				SelectedConnectionMode.IsValid())
			{
				PacketContentView->SetPacket(SelectedGameInstance->GetIndex(),
											 SelectedConnection->GetIndex(),
											 SelectedConnectionMode->Mode,
											 SelectedPacketStartIndex,
											 SinglePacketBitSize);
			}
			else
			{
				PacketContentView->ResetPacket();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::SetSelectedBitRange(uint32 StartPos, uint32 EndPos)
{
	if (StartPos != SelectionStartPosition || EndPos != SelectionEndPosition)
	{
		SelectionStartPosition = StartPos;
		SelectionEndPosition = EndPos;
		UpdateAggregatedNetStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::SetSelectedEventTypeIndex(const uint32 InEventTypeIndex)
{
	if (InEventTypeIndex != SelectedEventTypeIndex)
	{
		SelectedEventTypeIndex = InEventTypeIndex;

		if (SelectedEventTypeIndex != InvalidEventTypeIndex)
		{
			if (NetStatsView)
			{
				const uint64 NodeId = static_cast<uint64>(SelectedEventTypeIndex);
				NetStatsView->SelectNetEventNode(NodeId);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerWindow::UpdateAggregatedNetStats()
{
	if (NetStatsView)
	{
		if (SelectedGameInstance.IsValid() &&
			SelectedConnection.IsValid() &&
			SelectedConnectionMode.IsValid() &&
			(SelectedPacketStartIndex < SelectedPacketEndIndex) &&
			(SelectedPacketStartIndex + 1 != SelectedPacketEndIndex || SelectionStartPosition < SelectionEndPosition))
		{
			NetStatsView->UpdateStats(SelectedGameInstance->GetIndex(),
									  SelectedConnection->GetIndex(),
									  SelectedConnectionMode->Mode,
									  SelectedPacketStartIndex,
									  SelectedPacketEndIndex,
									  SelectionStartPosition,
									  SelectionEndPosition);
		}
		else
		{
			NetStatsView->ResetStats();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
