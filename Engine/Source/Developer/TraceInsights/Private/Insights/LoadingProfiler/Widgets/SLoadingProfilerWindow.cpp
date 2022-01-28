// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLoadingProfilerWindow.h"

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
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerToolbar.h"
#include "Insights/Table/Widgets/SUntypedTableTreeView.h"
#include "Insights/TraceInsightsModule.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SInsightsSettings.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SLoadingProfilerWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FLoadingProfilerTabs::TimingViewID(TEXT("TimingView"));
const FName FLoadingProfilerTabs::EventAggregationTreeViewID(TEXT("EventAggregation"));
const FName FLoadingProfilerTabs::ObjectTypeAggregationTreeViewID(TEXT("ObjectTypeAggregation"));
const FName FLoadingProfilerTabs::PackageDetailsTreeViewID(TEXT("PackageDetails"));
const FName FLoadingProfilerTabs::ExportDetailsTreeViewID(TEXT("ExportDetails"));
const FName FLoadingProfilerTabs::RequestsTreeViewID(TEXT("Requests"));

////////////////////////////////////////////////////////////////////////////////////////////////////

SLoadingProfilerWindow::SLoadingProfilerWindow()
	: DurationActive(0.0f)
	, SelectionStartTime(0.0f)
	, SelectionEndTime(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SLoadingProfilerWindow::~SLoadingProfilerWindow()
{
	if (RequestsTreeView)
	{
		HideTab(FLoadingProfilerTabs::RequestsTreeViewID);
		check(RequestsTreeView == nullptr);
	}

	if (ExportDetailsTreeView)
	{
		HideTab(FLoadingProfilerTabs::ExportDetailsTreeViewID);
		check(ExportDetailsTreeView == nullptr);
	}

	if (PackageDetailsTreeView)
	{
		HideTab(FLoadingProfilerTabs::PackageDetailsTreeViewID);
		check(PackageDetailsTreeView == nullptr);
	}

	if (ObjectTypeAggregationTreeView)
	{
		HideTab(FLoadingProfilerTabs::ObjectTypeAggregationTreeViewID);
		check(ObjectTypeAggregationTreeView == nullptr);
	}

	if (EventAggregationTreeView)
	{
		HideTab(FLoadingProfilerTabs::EventAggregationTreeViewID);
		check(EventAggregationTreeView == nullptr);
	}

	if (TimingView)
	{
		HideTab(FLoadingProfilerTabs::TimingViewID);
		check(TimingView == nullptr);
	}

#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Insights.Usage.LoadingProfiler"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::Reset()
{
	if (TimingView)
	{
		TimingView->Reset();
	}

	if (EventAggregationTreeView)
	{
		EventAggregationTreeView->Reset();
	}

	if (ObjectTypeAggregationTreeView)
	{
		ObjectTypeAggregationTreeView->Reset();
	}

	if (PackageDetailsTreeView)
	{
		PackageDetailsTreeView->Reset();
	}

	if (ExportDetailsTreeView)
	{
		ExportDetailsTreeView->Reset();
	}

	if (RequestsTreeView)
	{
		RequestsTreeView->Reset();
	}

	UpdateTableTreeViews();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::UpdateTableTreeViews()
{
	UpdateEventAggregationTreeView();
	UpdateObjectTypeAggregationTreeView();
	UpdatePackageDetailsTreeView();
	UpdateExportDetailsTreeView();
	UpdateRequestsTreeView();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::UpdateEventAggregationTreeView()
{
	if (EventAggregationTreeView)
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && TraceServices::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const TraceServices::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *TraceServices::ReadLoadTimeProfilerProvider(*Session.Get());

			TraceServices::ITable<TraceServices::FLoadTimeProfilerAggregatedStats>* EventAggregationTable = LoadTimeProfilerProvider.CreateEventAggregation(SelectionStartTime, SelectionEndTime);
			EventAggregationTreeView->UpdateSourceTable(MakeShareable(EventAggregationTable));
		}
		else
		{
			EventAggregationTreeView->UpdateSourceTable(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::UpdateObjectTypeAggregationTreeView()
{
	if (ObjectTypeAggregationTreeView)
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && TraceServices::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const TraceServices::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *TraceServices::ReadLoadTimeProfilerProvider(*Session.Get());

			TraceServices::ITable<TraceServices::FLoadTimeProfilerAggregatedStats>* ObjectTypeAggregationTable = LoadTimeProfilerProvider.CreateObjectTypeAggregation(SelectionStartTime, SelectionEndTime);
			ObjectTypeAggregationTreeView->UpdateSourceTable(MakeShareable(ObjectTypeAggregationTable));
		}
		else
		{
			ObjectTypeAggregationTreeView->UpdateSourceTable(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::UpdatePackageDetailsTreeView()
{
	if (PackageDetailsTreeView)
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && TraceServices::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const TraceServices::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *TraceServices::ReadLoadTimeProfilerProvider(*Session.Get());

			TraceServices::ITable<TraceServices::FPackagesTableRow>* PackageDetailsTable = LoadTimeProfilerProvider.CreatePackageDetailsTable(SelectionStartTime, SelectionEndTime);
			PackageDetailsTreeView->UpdateSourceTable(MakeShareable(PackageDetailsTable));
		}
		else
		{
			PackageDetailsTreeView->UpdateSourceTable(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::UpdateExportDetailsTreeView()
{
	if (ExportDetailsTreeView)
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && TraceServices::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const TraceServices::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *TraceServices::ReadLoadTimeProfilerProvider(*Session.Get());

			TraceServices::ITable<TraceServices::FExportsTableRow>* ExportDetailsTable = LoadTimeProfilerProvider.CreateExportDetailsTable(SelectionStartTime, SelectionEndTime);
			ExportDetailsTreeView->UpdateSourceTable(MakeShareable(ExportDetailsTable));
		}
		else
		{
			ExportDetailsTreeView->UpdateSourceTable(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

class FProxyUntypedTable : public  TraceServices::IUntypedTable
{
public:
	FProxyUntypedTable(const TraceServices::IUntypedTable* InTable) : TablePtr(InTable) {}
	virtual ~FProxyUntypedTable() = default;

	virtual const TraceServices::ITableLayout& GetLayout() const { return TablePtr->GetLayout(); }
	virtual uint64 GetRowCount() const { return TablePtr->GetRowCount(); }
	virtual TraceServices::IUntypedTableReader* CreateReader() const { return TablePtr->CreateReader(); }

private:
	const TraceServices::IUntypedTable* TablePtr;
};

void SLoadingProfilerWindow::UpdateRequestsTreeView()
{
	if (RequestsTreeView)
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && TraceServices::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const TraceServices::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *TraceServices::ReadLoadTimeProfilerProvider(*Session.Get());

			const TraceServices::ITable<TraceServices::FLoadRequest>& RequestsTable = LoadTimeProfilerProvider.GetRequestsTable();
			RequestsTreeView->UpdateSourceTable(MakeShared<FProxyUntypedTable>(&RequestsTable));
		}
		else
		{
			RequestsTreeView->UpdateSourceTable(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SLoadingProfilerWindow::SpawnTab_TimingView(const FSpawnTabArgs& Args)
{
	FLoadingProfilerManager::Get()->SetTimingViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(TimingView, STimingView)
		];

	TimingView->EnableAssetLoadingMode();
	TimingView->Reset(true);
	TimingView->OnSelectionChanged().AddSP(this, &SLoadingProfilerWindow::OnTimeSelectionChanged);
	TimingView->SelectTimeInterval(SelectionStartTime, SelectionEndTime - SelectionStartTime);

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SLoadingProfilerWindow::OnTimingViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnTimingViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FLoadingProfilerManager::Get()->SetTimingViewVisible(false);
	if (TimingView)
	{
		TimingView->OnSelectionChanged().RemoveAll(this);
		TimingView = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SLoadingProfilerWindow::SpawnTab_EventAggregationTreeView(const FSpawnTabArgs& Args)
{
	FLoadingProfilerManager::Get()->SetEventAggregationTreeViewVisible(true);

	TSharedRef<Insights::FUntypedTable> Table = MakeShared<Insights::FUntypedTable>();
	Table->SetDisplayName(LOCTEXT("EventAggregation_TableName", "Event Aggregation"));

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(EventAggregationTreeView, Insights::SUntypedTableTreeView, Table)
		];

	EventAggregationTreeView->SetLogListingName(FLoadingProfilerManager::Get()->GetLogListingName());
	UpdateEventAggregationTreeView();

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SLoadingProfilerWindow::OnEventAggregationTreeViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnEventAggregationTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FLoadingProfilerManager::Get()->SetEventAggregationTreeViewVisible(false);
	EventAggregationTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SLoadingProfilerWindow::SpawnTab_ObjectTypeAggregationTreeView(const FSpawnTabArgs& Args)
{
	FLoadingProfilerManager::Get()->SetObjectTypeAggregationTreeViewVisible(true);

	TSharedRef<Insights::FUntypedTable> Table = MakeShared<Insights::FUntypedTable>();
	Table->SetDisplayName(LOCTEXT("ObjectTypeAggregation_TableName", "Object Type Aggregation"));

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(ObjectTypeAggregationTreeView, Insights::SUntypedTableTreeView, Table)
		];

	ObjectTypeAggregationTreeView->SetLogListingName(FLoadingProfilerManager::Get()->GetLogListingName());
	UpdateObjectTypeAggregationTreeView();

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SLoadingProfilerWindow::OnObjectTypeAggregationTreeViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnObjectTypeAggregationTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FLoadingProfilerManager::Get()->SetObjectTypeAggregationTreeViewVisible(false);
	ObjectTypeAggregationTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SLoadingProfilerWindow::SpawnTab_PackageDetailsTreeView(const FSpawnTabArgs& Args)
{
	FLoadingProfilerManager::Get()->SetPackageDetailsTreeViewVisible(true);

	TSharedRef<Insights::FUntypedTable> Table = MakeShared<Insights::FUntypedTable>();
	Table->SetDisplayName(LOCTEXT("PackageDetails_TableName", "Package Details"));

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(PackageDetailsTreeView, Insights::SUntypedTableTreeView, Table)
		];

	PackageDetailsTreeView->SetLogListingName(FLoadingProfilerManager::Get()->GetLogListingName());
	UpdatePackageDetailsTreeView();

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SLoadingProfilerWindow::OnPackageDetailsTreeViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnPackageDetailsTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FLoadingProfilerManager::Get()->SetPackageDetailsTreeViewVisible(false);
	PackageDetailsTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SLoadingProfilerWindow::SpawnTab_ExportDetailsTreeView(const FSpawnTabArgs& Args)
{
	FLoadingProfilerManager::Get()->SetExportDetailsTreeViewVisible(true);

	TSharedRef<Insights::FUntypedTable> Table = MakeShared<Insights::FUntypedTable>();
	Table->SetDisplayName(LOCTEXT("ExportDetails_TableName", "Export Details"));

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(ExportDetailsTreeView, Insights::SUntypedTableTreeView, Table)
		];

	ExportDetailsTreeView->SetLogListingName(FLoadingProfilerManager::Get()->GetLogListingName());
	UpdateExportDetailsTreeView();

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SLoadingProfilerWindow::OnExportDetailsTreeViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnExportDetailsTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FLoadingProfilerManager::Get()->SetExportDetailsTreeViewVisible(false);
	ExportDetailsTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SDockTab> SLoadingProfilerWindow::SpawnTab_RequestsTreeView(const FSpawnTabArgs& Args)
{
	FLoadingProfilerManager::Get()->SetRequestsTreeViewVisible(true);

	TSharedRef<Insights::FUntypedTable> Table = MakeShared<Insights::FUntypedTable>();
	Table->SetDisplayName(LOCTEXT("Requests_TableName", "Requests"));

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(RequestsTreeView, Insights::SUntypedTableTreeView, Table)
		];

	RequestsTreeView->SetLogListingName(FLoadingProfilerManager::Get()->GetLogListingName());
	UpdateRequestsTreeView();

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &SLoadingProfilerWindow::OnRequestsTreeViewTabClosed));

	return DockTab;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnRequestsTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FLoadingProfilerManager::Get()->SetRequestsTreeViewVisible(false);
	RequestsTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SLoadingProfilerWindow::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	// Create & initialize tab manager.
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);
	const auto& PersistLayout = [](const TSharedRef<FTabManager::FLayout>& LayoutToSave)
	{
		FLayoutSaveRestore::SaveToConfig(FTraceInsightsModule::GetUnrealInsightsLayoutIni(), LayoutToSave);
	};
	TabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateLambda(PersistLayout));

	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("LoadingProfilerMenuGroupName", "Asset Loading Insights"));

	TabManager->RegisterTabSpawner(FLoadingProfilerTabs::TimingViewID, FOnSpawnTab::CreateRaw(this, &SLoadingProfilerWindow::SpawnTab_TimingView))
		.SetDisplayName(LOCTEXT("TimingViewTabTitle", "Timing View"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TimingView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FLoadingProfilerTabs::EventAggregationTreeViewID, FOnSpawnTab::CreateRaw(this, &SLoadingProfilerWindow::SpawnTab_EventAggregationTreeView))
		.SetDisplayName(LOCTEXT("EventAggregationTreeViewTabTitle", "Event Aggregation"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TableTreeView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FLoadingProfilerTabs::ObjectTypeAggregationTreeViewID, FOnSpawnTab::CreateRaw(this, &SLoadingProfilerWindow::SpawnTab_ObjectTypeAggregationTreeView))
		.SetDisplayName(LOCTEXT("ObjectTypeAggregationTreeViewTabTitle", "Object Type Aggregation"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TableTreeView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FLoadingProfilerTabs::PackageDetailsTreeViewID, FOnSpawnTab::CreateRaw(this, &SLoadingProfilerWindow::SpawnTab_PackageDetailsTreeView))
		.SetDisplayName(LOCTEXT("PackageDetailsTreeViewTabTitle", "Package Details"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TableTreeView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FLoadingProfilerTabs::ExportDetailsTreeViewID, FOnSpawnTab::CreateRaw(this, &SLoadingProfilerWindow::SpawnTab_ExportDetailsTreeView))
		.SetDisplayName(LOCTEXT("ExportDetailsTreeViewTabTitle", "Export Details"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TableTreeView"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FLoadingProfilerTabs::RequestsTreeViewID, FOnSpawnTab::CreateRaw(this, &SLoadingProfilerWindow::SpawnTab_RequestsTreeView))
		.SetDisplayName(LOCTEXT("RequestsTreeViewTabTitle", "Requests"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TableTreeView"))
		.SetGroup(AppMenuGroup);

	TSharedPtr<FLoadingProfilerManager> LoadingProfilerManager = FLoadingProfilerManager::Get();
	ensure(LoadingProfilerManager.IsValid());

	// Create tab layout.
	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("InsightsLoadingProfilerLayout_v1.2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.5f)
				->SetHideTabWell(true)
				->AddTab(FLoadingProfilerTabs::TimingViewID, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.35f)
				->AddTab(FLoadingProfilerTabs::EventAggregationTreeViewID, ETabState::OpenedTab)
				->AddTab(FLoadingProfilerTabs::ObjectTypeAggregationTreeViewID, ETabState::OpenedTab)
				->AddTab(FLoadingProfilerTabs::PackageDetailsTreeViewID, ETabState::OpenedTab)
				->AddTab(FLoadingProfilerTabs::ExportDetailsTreeViewID, ETabState::OpenedTab)
				->AddTab(FLoadingProfilerTabs::RequestsTreeViewID, ETabState::OpenedTab)
				->SetForegroundTab(FLoadingProfilerTabs::EventAggregationTreeViewID)
			)
		);

	Layout = FLayoutSaveRestore::LoadFromConfig(FTraceInsightsModule::GetUnrealInsightsLayoutIni(), Layout);

	// Create & initialize main menu.
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("MenuLabel", "Menu"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateStatic(&SLoadingProfilerWindow::FillMenu, TabManager),
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
				SNew(SLoadingProfilerToolbar)
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
			.Visibility(this, &SLoadingProfilerWindow::IsSessionOverlayVisible)
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
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager)
{
	if (!TabManager.IsValid())
	{
		return;
	}

	FInsightsManager::Get()->GetInsightsMenuBuilder()->PopulateMenu(MenuBuilder);

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::ShowTab(const FName& TabID)
{
	if (TabManager->HasTabSpawner(TabID))
	{
		TabManager->TryInvokeTab(TabID);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::HideTab(const FName& TabID)
{
	TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(TabID);
	if (Tab.IsValid())
	{
		Tab->RequestCloseTab();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SLoadingProfilerWindow::IsSessionOverlayVisible() const
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

bool SLoadingProfilerWindow::IsProfilerEnabled() const
{
	return FInsightsManager::Get()->GetSession().IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EActiveTimerReturnType SLoadingProfilerWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The profiler window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SLoadingProfilerWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SLoadingProfilerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FLoadingProfilerManager::Get()->GetCommandList()->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SLoadingProfilerWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (FInsightsManager::Get()->OnDragOver(DragDropEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SLoadingProfilerWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (FInsightsManager::Get()->OnDrop(DragDropEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLoadingProfilerWindow::OnTimeSelectionChanged(Insights::ETimeChangedFlags InFlags, double InStartTime, double InEndTime)
{
	if (InFlags != Insights::ETimeChangedFlags::Interactive)
	{
		if (InStartTime < InEndTime)
		{
			SelectionStartTime = InStartTime;
			SelectionEndTime = InEndTime;
			UpdateTableTreeViews();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
