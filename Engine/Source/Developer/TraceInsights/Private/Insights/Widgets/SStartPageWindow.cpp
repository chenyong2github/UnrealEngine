// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStartPageWindow.h"

#include "Containers/StringView.h"
#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManagerGeneric.h"
#include "Input/Events.h"
#include "Internationalization/Text.h"
#include "IPAddress.h"
#include "SlateOptMacros.h"
#include "SocketSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#include "Trace/ControlClient.h"
#include "Trace/StoreClient.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
//#include "WorkspaceMenuStructure.h"
//#include "WorkspaceMenuStructureModule.h"

#if WITH_EDITOR
	#include "EngineAnalytics.h"
	#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
	#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#endif // WITH_EDITOR

// Insights
#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsManager.h"
#include "Insights/Log.h"
#include "Insights/StoreService/StoreBrowser.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SInsightsSettings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SStartPageWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////
// STraceListRow
////////////////////////////////////////////////////////////////////////////////////////////////////

class STraceListRow : public SMultiColumnTableRow<TSharedPtr<FTraceViewModel>>
{
	SLATE_BEGIN_ARGS(STraceListRow) {}
	SLATE_END_ARGS()

public:
	/**
	 * Constructs the widget.
	 *
	 * @param InArgs The construction arguments.
	 * @param InTrace The trace displayed by this row.
	 * @param InOwnerTableView The table to which the row must be added.
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FTraceViewModel> InTrace, TSharedRef<SStartPageWindow> InParentWidget, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		WeakTrace = MoveTemp(InTrace);
		WeakParentWidget = InParentWidget;

		SMultiColumnTableRow<TSharedPtr<FTraceViewModel>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
	}

public:
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == FName(TEXT("Name")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceName)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("Uri")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceUri)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("Platform")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTracePlatform)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("AppName")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceAppName)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("BuildConfig")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceBuildConfiguration)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("BuildTarget")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceBuildTarget)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("Size")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceSize)
					.ColorAndOpacity(this, &STraceListRow::GetColorBySize)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else if (ColumnName == FName(TEXT("Status")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceStatus)
					.ToolTip(STraceListRow::GetTraceTooltip())
				];
		}
		else
		{
			return SNew(STextBlock).Text(LOCTEXT("UnknownColumn", "Unknown Column"));
		}
	}

	FText GetTraceIndexAndId() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			const FString TraceIdStr = FString::Printf(TEXT("%d (0x%08X)"), TracePin->TraceIndex, TracePin->TraceId);
			return FText::FromString(TraceIdStr);
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceName() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->Name;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceUri() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->Uri;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTracePlatform() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->Platform;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceAppName() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->AppName;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceCommandLine() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->CommandLine;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceBuildConfiguration() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			if (TracePin->ConfigurationType != EBuildConfiguration::Unknown)
			{
				return EBuildConfigurations::ToText(TracePin->ConfigurationType);
			}
		}
		return FText::GetEmpty();
	}

	FText GetTraceBuildTarget() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			if (TracePin->TargetType != EBuildTargetType::Unknown)
			{
				return FText::FromString(LexToString(TracePin->TargetType));
			}
		}
		return FText::GetEmpty();
	}

	FText GetTraceTimestamp() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return FText::AsDate(TracePin->Timestamp);
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceTimestampForTooltip() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return FText::AsDateTime(TracePin->Timestamp);
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSize() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			//FNumberFormattingOptions FormattingOptions;
			//FormattingOptions.MinimumFractionalDigits = 1;
			//FormattingOptions.MaximumFractionalDigits = 1;
			//return FText::AsMemory(TracePin->Size, &FormattingOptions);
			return FText::Format(LOCTEXT("SessionFileSizeFormatKiB", "{0} KiB"), TracePin->Size / 1024);
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSizeForTooltip() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			if (TracePin->Size > 1024)
			{
				return FText::Format(LOCTEXT("TraceTooltip_FileSize2", "{0} bytes ({1})"), FText::AsNumber(TracePin->Size), FText::AsMemory(TracePin->Size));
			}
			else
			{
				return FText::Format(LOCTEXT("TraceTooltip_FileSize1", "{0} bytes"), FText::AsNumber(TracePin->Size));
			}
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FSlateColor GetColorBySize() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			TSharedRef<ITypedTableView<TSharedPtr<FTraceViewModel>>> OwnerWidget = OwnerTablePtr.Pin().ToSharedRef();
			const TSharedPtr<FTraceViewModel>* MyItem = OwnerWidget->Private_ItemFromWidget(this);
			const bool IsSelected = OwnerWidget->Private_IsItemSelected(*MyItem);

			if (IsSelected)
			{
				return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
			}
			else if (TracePin->Size < 1024ULL * 1024ULL)
			{
				// < 1 MiB
				return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			}
			else if (TracePin->Size < 1024ULL * 1024ULL * 1024ULL)
			{
				// [1 MiB  .. 1 GiB)
				return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
			}
			else
			{
				// > 1 GiB
				return FSlateColor(FLinearColor(1.0f, 0.5f, 0.5f, 1.0f));
			}
		}
		else
		{
			return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		}
	}

	FText GetTraceStatus() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			if (TracePin->bIsLive)
			{
				return LOCTEXT("LiveTraceStatus", "LIVE");
			}
		}
		return FText::GetEmpty();
	}

	FText GetTraceStatusForTooltip() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			if (TracePin->bIsLive)
			{
				FString Ip = FString::Printf(TEXT("%d.%d.%d.%d"),
					(TracePin->IpAddress >> 24) & 0xFF,
					(TracePin->IpAddress >> 16) & 0xFF,
					(TracePin->IpAddress >> 8) & 0xFF,
					TracePin->IpAddress & 0xFF);
				return FText::Format(LOCTEXT("LiveTraceStatusFmt", "LIVE ({0})"), FText::FromString(Ip));
			}
			else
			{
				return LOCTEXT("OfflineTraceStatus", "Offline");
			}
		}
		return FText::GetEmpty();
	}

	TSharedPtr<IToolTip> GetTraceTooltip() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			TSharedPtr<SGridPanel> GridPanel;
			TSharedPtr<SToolTip> TraceTooltip =
			SNew(SToolTip)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(this, &STraceListRow::GetTraceName)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FLinearColor::White)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &STraceListRow::GetTraceIndexAndId)
						.ColorAndOpacity(FLinearColor::Gray)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(STextBlock)
					.Text(this, &STraceListRow::GetTraceUri)
					.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SAssignNew(GridPanel, SGridPanel)
				]
			];

			int32 Row = 0;
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Platform", "Platform:"), &STraceListRow::GetTracePlatform);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_AppName", "App Name:"), &STraceListRow::GetTraceAppName);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_CommandLine", "Command Line:"), &STraceListRow::GetTraceCommandLine);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_BuildConfig", "Build Config:"), &STraceListRow::GetTraceBuildConfiguration);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_BuildTarget", "Build Target:"), &STraceListRow::GetTraceBuildTarget);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Timestamp", "Timestamp:"), &STraceListRow::GetTraceTimestampForTooltip);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Size", "File Size:"), &STraceListRow::GetTraceSizeForTooltip);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Status", "Status:"), &STraceListRow::GetTraceStatusForTooltip);

			return TraceTooltip;
		}
		else
		{
			return nullptr;
		}
	}

	void AddGridPanelRow(TSharedPtr<SGridPanel> Grid, int32 Row, const FText& Name,
		typename TAttribute<FText>::FGetter::template TRawMethodDelegate_Const<STraceListRow>::FMethodPtr Value) const
	{
		Grid->AddSlot(0, Row)
			.Padding(2.0f)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(Name)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FLinearColor::Gray)
			];

		Grid->AddSlot(1, Row)
			.Padding(2.0f)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(this, Value)
				.WrapTextAt(512.0f)
				.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
				.ColorAndOpacity(FLinearColor::White)
			];
	}

private:
	TWeakPtr<FTraceViewModel> WeakTrace;
	TWeakPtr<SStartPageWindow> WeakParentWidget;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SStartPageWindow
////////////////////////////////////////////////////////////////////////////////////////////////////

SStartPageWindow::SStartPageWindow()
	: NotificationList()
	, ActiveNotifications()
	, OverlaySettingsSlot(nullptr)
	, DurationActive(0.0f)
	, ActiveTimerHandle()
	, MainContentPanel()
	, LiveSessionCount(0)
	, bAutoStartAnalysisForLiveSessions(false)
	, AutoStartedSessions()
	, AutoStartPlatformFilter()
	, AutoStartAppNameFilter()
	, AutoStartConfigurationTypeFilter(EBuildConfiguration::Unknown)
	, AutoStartTargetTypeFilter(EBuildTargetType::Unknown)
	, StoreBrowser(new Insights::FStoreBrowser())
	, TracesChangeSerial(0)
	, TraceViewModels()
	, TraceViewModelMap()
	, TraceListView()
	, SelectedTrace()
	, HostTextBox()
	, SplashScreenOverlayFadeTime(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SStartPageWindow::~SStartPageWindow()
{
#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.Insights.StartPage"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SStartPageWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SOverlay)

			// Version
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(0.0f, -16.0f, 0.0f, 0.0f)
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
				SAssignNew(MainContentPanel, SVerticalBox)

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.FillHeight(1.0f)
				.Padding(3.0f, 3.0f)
				[
					SNew(SBox)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
						.Padding(8.0f)
						[
							ConstructSessionsPanel()
						]
					]
				]

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.AutoHeight()
				.Padding(3.0f, 3.0f)
				[
					SNew(SBox)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
						.Padding(8.0f)
						[
							ConstructTraceStoreDirectoryPanel()
						]
					]
				]

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.AutoHeight()
				.Padding(3.0f, 3.0f)
				[
					SNew(SBox)
					.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
						.Padding(8.0f)
						[
							ConstructConnectPanel()
						]
					]
				]
			]

			// Overlay for fake splashscreen.
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(0.0f)
			[
				SNew(SBox)
				.Visibility(this, &SStartPageWindow::SplashScreenOverlay_Visibility)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
					.BorderBackgroundColor(this, &SStartPageWindow::SplashScreenOverlay_ColorAndOpacity)
					.Padding(0.0f)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SStartPageWindow::GetSplashScreenOverlayText)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(this, &SStartPageWindow::SplashScreenOverlay_TextColorAndOpacity)
						]
					]
				]
			]

			// Notification area overlay
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(16.0f)
			[
				SAssignNew(NotificationList, SNotificationList)
			]

			// Settings dialog overlay
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Expose(OverlaySettingsSlot)
		];

	RefreshTraceList();

	FSlateApplication::Get().SetKeyboardFocus(TraceListView);
	FSlateApplication::Get().SetUserFocus(0, TraceListView);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructSessionsPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SessionsPanelTitle", "Trace Sessions"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		.ColorAndOpacity(FLinearColor::Gray)
	]

	+ SVerticalBox::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	.Padding(0.0f, 1.0f, 0.0f, 2.0f)
	[
		SAssignNew(TraceListView, SListView<TSharedPtr<FTraceViewModel>>)
		.IsFocusable(true)
		.ItemHeight(20.0f)
		.SelectionMode(ESelectionMode::Single)
		.OnSelectionChanged(this, &SStartPageWindow::TraceList_OnSelectionChanged)
		.OnMouseButtonDoubleClick(this, &SStartPageWindow::TraceList_OnMouseButtonDoubleClick)
		.ListItemsSource(&TraceViewModels)
		.OnGenerateRow(this, &SStartPageWindow::TraceList_OnGenerateRow)
		.ConsumeMouseWheel(EConsumeMouseWheel::Always)
		//.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SStartPageWindow::TraceList_GetContextMenu))
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(FName(TEXT("Name")))
			.FillWidth(0.25f)
			.DefaultLabel(LOCTEXT("NameColumn", "Name"))

			+ SHeaderRow::Column(FName(TEXT("Platform")))
			.FillWidth(0.1f)
			.DefaultLabel(LOCTEXT("PlatformColumn", "Platform"))

			+ SHeaderRow::Column(FName(TEXT("AppName")))
			.FillWidth(0.1f)
			.DefaultLabel(LOCTEXT("AppNameColumn", "App Name"))

			+ SHeaderRow::Column(FName(TEXT("BuildConfig")))
			.FillWidth(0.1f)
			.DefaultLabel(LOCTEXT("BuildConfigColumn", "Build Config"))

			+ SHeaderRow::Column(FName(TEXT("BuildTarget")))
			.FillWidth(0.1f)
			.DefaultLabel(LOCTEXT("BuildTargetColumn", "Build Target"))

			+ SHeaderRow::Column(FName(TEXT("Size")))
			.FixedWidth(100.0f)
			.HAlignHeader(HAlign_Right)
			.HAlignCell(HAlign_Right)
			.DefaultLabel(LOCTEXT("SizeColumn", "File Size"))

			+ SHeaderRow::Column(FName(TEXT("Status")))
			.FixedWidth(60.0f)
			.HAlignHeader(HAlign_Right)
			.HAlignCell(HAlign_Right)
			.DefaultLabel(LOCTEXT("StatusColumn", "Status"))
		)
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 4.0f, 0.0f, 6.0f)
	[
		SNew(SSeparator)
		.Orientation(Orient_Horizontal)
		.SeparatorImage(FInsightsStyle::Get().GetBrush("WhiteBrush"))
		.ColorAndOpacity(FLinearColor::Black)
		.Thickness(1.0f)
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 2.0f)
	[
		ConstructLoadPanel()
	];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructLoadPanel()
{
	TSharedRef<SWidget> Widget = SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	[
		SNew(SBox)
		.HAlign(HAlign_Left)
		[
			ConstructAutoStartPanel()
		]
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	[
		SNew(SButton)
		.IsEnabled(this, &SStartPageWindow::Open_IsEnabled)
		.OnClicked(this, &SStartPageWindow::Open_OnClicked)
		.ToolTipText(LOCTEXT("OpenButtonTooltip", "Start analysis for selected trace session."))
		.ContentPadding(FMargin(4.0f, 1.0f))
		.Content()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("Open.Icon.Small"))
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OpenButtonText", "Open"))
				]
		]
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	[
		SNew(SComboButton)
		.ToolTipText(LOCTEXT("MRU_Tooltip", "Open a trace file or choose a trace session."))
		.OnGetMenuContent(this, &SStartPageWindow::MakeTraceListMenu)
		.HasDownArrow(true)
		.ContentPadding(FMargin(1.0f, 1.0f, 1.0f, 1.0f))
	];

	return Widget;
}
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructTraceStoreDirectoryPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("TraceStoreDirText", "Trace Store Directory:"))
		.ColorAndOpacity(FLinearColor::Gray)
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 0.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SStartPageWindow::GetTraceStoreDirectory)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("ExploreTraceStoreDirButton", "Explore"))
			.ToolTipText(LOCTEXT("ExploreTraceStoreDirButtonToolTip", "Explore the Trace Store Directory"))
			.OnClicked(this, &SStartPageWindow::ExploreTraceStoreDirectory_OnClicked)
		]
	];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructAutoStartPanel()
{
	TSharedRef<SWidget> Widget = SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 0.0f, 0.0f, 0.0f)
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(SCheckBox)
		.ToolTipText(LOCTEXT("AutoStart_Tooltip", "Enable auto-start analysis for LIVE trace sessions."))
		.IsChecked(this, &SStartPageWindow::AutoStart_IsChecked)
		.OnCheckStateChanged(this, &SStartPageWindow::AutoStart_OnCheckStateChanged)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AutoStart_Text", "Auto-start analysis for LIVE trace sessions"))
		]
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(4.0f, 0.0f, 0.0f, 0.0f)
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SAssignNew(AutoStartPlatformFilter, SSearchBox)
		.HintText(LOCTEXT("AutoStartPlatformFilter_Hint", "Platform"))
		.ToolTipText(LOCTEXT("AutoStartPlatformFilter_Tooltip", "Type here to specify the Platform filter.\nAuto-start analysis will be enabled only for live trace sessions with this specified Platform."))
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(4.0f, 0.0f, 0.0f, 0.0f)
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SAssignNew(AutoStartAppNameFilter, SSearchBox)
		.HintText(LOCTEXT("AutoStartAppNameFilter_Hint", "AppName"))
		.ToolTipText(LOCTEXT("AutoStartAppNameFilter_Tooltip", "Type here to specify the AppName filter.\nAuto-start analysis will be enabled only for live trace sessions with this specified AppName."))
	];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructRecorderPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	.Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RecorderPanelTitle", "Trace Recorder"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		.ColorAndOpacity(FLinearColor::Gray)
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RecorderStatusTitle", "Status:"))
			.ColorAndOpacity(FLinearColor::Gray)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SStartPageWindow::GetRecorderStatusText)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("StartRecorder", "Start"))
			.ToolTipText(LOCTEXT("StartRecorderToolTip", "Start the Trace Recorder"))
			.OnClicked(this, &SStartPageWindow::StartTraceRecorder_OnClicked)
			.Visibility(this, &SStartPageWindow::StartTraceRecorder_Visibility)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("StopRecorder", "Stop"))
			.ToolTipText(LOCTEXT("StopRecorderToolTip", "Stop the Trace Recorder"))
			.OnClicked(this, &SStartPageWindow::StopTraceRecorder_OnClicked)
			.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)
		]
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 2.0f, 0.0f, 1.0f)
	[
		SNew(SHorizontalBox)
		.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() -> FText
			{
				return FText::Format(LOCTEXT("ConnectionCountFormat", "Connections / live sessions: {0}"), FText::AsNumber(LiveSessionCount));
			})
			.ColorAndOpacity(FLinearColor::Gray)
		]
	];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructConnectPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ConnectPanelTitle", "New Connection"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		.ColorAndOpacity(FLinearColor::Gray)
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HostTitle", "Running instance IP:"))
			.ColorAndOpacity(FLinearColor::Gray)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(120.0f)
			[
				SAssignNew(HostTextBox, SEditableTextBox)
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("Connect", "Connect"))
			.ToolTipText(LOCTEXT("ConnectToolTip", "Connect the running instance at specified ip with the local trace recorder."))
			.OnClicked(this, &SStartPageWindow::Connect_OnClicked)
		]
	];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SStartPageWindow::TraceList_OnGenerateRow(TSharedPtr<FTraceViewModel> InTrace, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STraceListRow, InTrace, SharedThis(this), OwnerTable);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::ShowSplashScreenOverlay()
{
	SplashScreenOverlayFadeTime = 3.5f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::TickSplashScreenOverlay(const float InDeltaTime)
{
	if (SplashScreenOverlayFadeTime > 0.0f)
	{
		SplashScreenOverlayFadeTime = FMath::Max(0.0f, SplashScreenOverlayFadeTime - InDeltaTime);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float SStartPageWindow::SplashScreenOverlayOpacity() const
{
	constexpr float FadeInStartTime = 3.5f;
	constexpr float FadeInEndTime = 3.0f;
	constexpr float FadeOutStartTime = 1.0f;
	constexpr float FadeOutEndTime = 0.0f;

	const float Opacity =
		SplashScreenOverlayFadeTime > FadeInStartTime ? 0.0f :
		SplashScreenOverlayFadeTime > FadeInEndTime ? 1.0f - (SplashScreenOverlayFadeTime - FadeInEndTime) / (FadeInStartTime - FadeInEndTime) :
		SplashScreenOverlayFadeTime > FadeOutStartTime ? 1.0f :
		SplashScreenOverlayFadeTime > FadeOutEndTime ? (SplashScreenOverlayFadeTime - FadeOutEndTime) / (FadeOutStartTime - FadeOutEndTime) : 0.0f;

	return Opacity;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::SplashScreenOverlay_Visibility() const
{
	return SplashScreenOverlayFadeTime > 0.0f ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SStartPageWindow::SplashScreenOverlay_ColorAndOpacity() const
{
	return FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, SplashScreenOverlayOpacity()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SStartPageWindow::SplashScreenOverlay_TextColorAndOpacity() const
{
	return FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, SplashScreenOverlayOpacity()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetSplashScreenOverlayText() const
{
	return FText::Format(LOCTEXT("StartAnalysis", "Starting analysis...\n{0}"), FText::FromString(SplashScreenOverlayTraceFile));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::RefreshTraces_OnClicked()
{
	RefreshTraceList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Connect_OnClicked()
{
	FText HostText = HostTextBox->GetText();
	if (HostText.IsEmptyOrWhitespace())
	{
		// nothing to do
		return FReply::Handled();
	}

	bool bConnectedSuccessfully = false;
	Trace::FControlClient ControlClient;
	if (ControlClient.Connect(*HostText.ToString()))
	{
		TSharedPtr<FInternetAddr> RecorderAddr;
		if (ISocketSubsystem* Sockets = ISocketSubsystem::Get())
		{
			bool bCanBindAll = false;
			RecorderAddr = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
			if (RecorderAddr.IsValid())
			{
				ControlClient.SendSendTo(*RecorderAddr->ToString(false));
				bConnectedSuccessfully = true;
			}
		}
	}

	if (bConnectedSuccessfully)
	{
		FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("ConnectSuccess", "Successfully connected to \"{0}\"!"), HostText));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.ExpireDuration = 10.0f;
		SNotificationItemWeak NotificationItem = NotificationList->AddNotification(NotificationInfo);
		NotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem.Pin()->ExpireAndFadeout();
		ActiveNotifications.Add(TEXT("ConnectSuccess"), NotificationItem);
	}
	else
	{
		FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("ConnectFailed", "Failed to connect to \"{0}\"!"), HostText));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.ExpireDuration = 10.0f;
		SNotificationItemWeak NotificationItem = NotificationList->AddNotification(NotificationInfo);
		NotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Fail);
		NotificationItem.Pin()->ExpireAndFadeout();
		ActiveNotifications.Add(TEXT("ConnectFailed"), NotificationItem);
	}

	RefreshTraceList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::RefreshTraceList()
{
	Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
	if (StoreClient == nullptr)
	{
		return;
	}

	FStopwatch StopwatchTotal;
	StopwatchTotal.Start();

	int32 AddedTraces = 0;
	int32 RemovedTraces = 0;
	int32 UpdatedTraces = 0;

	{
		StoreBrowser->Lock();

		const uint64 NewChangeSerial = StoreBrowser->GetLockedTracesChangeSerial();
		if (NewChangeSerial != TracesChangeSerial)
		{
			TracesChangeSerial = NewChangeSerial;

			UE_LOG(TraceInsights, Log, TEXT("[StartPage] Synching the trace list with StoreBrowser..."));
	
			const TArray<TSharedPtr<Insights::FStoreBrowserTraceInfo>>& InTraces = StoreBrowser->GetLockedTraces();
			const TMap<uint32, TSharedPtr<Insights::FStoreBrowserTraceInfo>>& InTraceMap = StoreBrowser->GetLockedTraceMap();

			// Check for removed traces.
			{
				int32 TraceViewModelCount = TraceViewModels.Num();
				for (int32 TraceIndex = 0; TraceIndex < TraceViewModelCount; ++TraceIndex)
				{
					FTraceViewModel& Trace = *TraceViewModels[TraceIndex];
					const TSharedPtr<Insights::FStoreBrowserTraceInfo>* InTracePtrPtr = InTraceMap.Find(Trace.TraceId);
					if (!InTracePtrPtr)
					{
						// This trace was removed.
						RemovedTraces++;
						TraceViewModelMap.Remove(Trace.TraceId);
						TraceViewModels.RemoveAtSwap(TraceIndex);
						TraceIndex--;
						TraceViewModelCount--;
					}
				}
			}

			// Check for added traces and for updated traces.
			for (const TSharedPtr<Insights::FStoreBrowserTraceInfo>& InTracePtr : InTraces)
			{
				const Insights::FStoreBrowserTraceInfo& SourceTrace = *InTracePtr;
				TSharedPtr<FTraceViewModel>* TracePtrPtr = TraceViewModelMap.Find(SourceTrace.TraceId);
				if (TracePtrPtr)
				{
					FTraceViewModel& Trace = **TracePtrPtr;
					if (Trace.ChangeSerial != SourceTrace.ChangeSerial)
					{
						// This trace was updated.
						UpdatedTraces++;
						UpdateTrace(Trace, SourceTrace);
					}
				}
				else
				{
					// This trace was added.
					AddedTraces++;
					TSharedPtr<FTraceViewModel> TracePtr = MakeShared<FTraceViewModel>();
					FTraceViewModel& Trace = *TracePtr;
					Trace.TraceId = SourceTrace.TraceId;
					Trace.Name = FText::FromString(SourceTrace.Name);
					Trace.Uri = FText::FromString(FInsightsManager::Get()->GetStoreDir() + TEXT("/") + SourceTrace.Name + TEXT(".utrace"));
					UpdateTrace(Trace, SourceTrace);
					TraceViewModels.Add(TracePtr);
					TraceViewModelMap.Add(SourceTrace.TraceId, TracePtr);
				}
			}
		}

		StoreBrowser->Unlock();
	}

	if (AddedTraces > 0 || RemovedTraces > 0)
	{
		// If we have new or removed traces we need to rebuild the list view.
		OnTraceListChanged();
	}

	StopwatchTotal.Stop();
	if (UpdatedTraces > 0 || AddedTraces > 0 || RemovedTraces > 0)
	{
		UE_LOG(TraceInsights, Log, TEXT("[StartPage] The trace list refreshed in %llu ms (%d traces : %d updated, %d added, %d removed)."),
			StopwatchTotal.GetAccumulatedTimeMs(), TraceViewModels.Num(), UpdatedTraces, AddedTraces, RemovedTraces);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::UpdateTrace(FTraceViewModel& InOutTrace, const Insights::FStoreBrowserTraceInfo& InSourceTrace)
{
	//TraceId -- no need to update

	InOutTrace.ChangeSerial = InSourceTrace.ChangeSerial;
	InOutTrace.TraceIndex = InSourceTrace.TraceIndex;

	//Name -- no need to update
	//Uri -- no need to update

	InOutTrace.Timestamp = InSourceTrace.Timestamp;
	InOutTrace.Size = InSourceTrace.Size;

	InOutTrace.bIsLive = InSourceTrace.bIsLive;
	InOutTrace.IpAddress = InSourceTrace.IpAddress;

	// Is metadata updated?
	if (!InOutTrace.bIsMetadataUpdated && InSourceTrace.bIsMetadataUpdated)
	{
		InOutTrace.bIsMetadataUpdated = true;
		InOutTrace.Platform = FText::FromString(InSourceTrace.Platform);
		InOutTrace.AppName = FText::FromString(InSourceTrace.AppName);
		InOutTrace.CommandLine = FText::FromString(InSourceTrace.CommandLine);
		InOutTrace.ConfigurationType = InSourceTrace.ConfigurationType;
		InOutTrace.TargetType = InSourceTrace.TargetType;
	}

	// Auto start analysis for a live trace session.
	if (InOutTrace.bIsLive &&
		InOutTrace.bIsMetadataUpdated &&
		bAutoStartAnalysisForLiveSessions && // is auto start enabled?
		!AutoStartedSessions.Contains(InOutTrace.TraceId)) // is not already auto-started?
	{
		const FString AutoStartPlatformFilterStr = AutoStartPlatformFilter->GetText().ToString();
		const FString AutoStartAppNameFilterStr = AutoStartAppNameFilter->GetText().ToString();

		// matches filter?
		if ((AutoStartPlatformFilterStr.IsEmpty() || FCString::Strcmp(*AutoStartPlatformFilterStr, *InOutTrace.Platform.ToString()) == 0) &&
			(AutoStartAppNameFilterStr.IsEmpty() || FCString::Strcmp(*AutoStartAppNameFilterStr, *InOutTrace.AppName.ToString()) == 0) &&
			(AutoStartConfigurationTypeFilter == EBuildConfiguration::Unknown || AutoStartConfigurationTypeFilter == InOutTrace.ConfigurationType) &&
			(AutoStartTargetTypeFilter == EBuildTargetType::Unknown || AutoStartTargetTypeFilter == InOutTrace.TargetType))
		{
			UE_LOG(TraceInsights, Log, TEXT("[StartPage] Auto starting analysis for trace with id 0x%08X..."), InOutTrace.TraceId);
			AutoStartedSessions.Add(InOutTrace.TraceId);
			LoadTrace(InOutTrace.TraceId);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OnTraceListChanged()
{
	Algo::SortBy(TraceViewModels, &FTraceViewModel::Timestamp);

	//////////////////////////////////////////////////
	// TraceViewModels array has changed.
	// Now we need to rebuild the list in the ListView.

	TSharedPtr<FTraceViewModel> NewSelectedTrace;
	if (SelectedTrace)
	{
		// Identify the previously selected trace (if stil available) to ensure selection remains unchanged.
		TSharedPtr<FTraceViewModel>* TracePtrPtr = TraceViewModelMap.Find(SelectedTrace->TraceId);
		NewSelectedTrace = TracePtrPtr ? *TracePtrPtr : nullptr;
	}

	TraceListView->RebuildList();

	// If no selection, auto select the last (newest) trace.
	if (!NewSelectedTrace.IsValid() && TraceViewModels.Num() > 0)
	{
		NewSelectedTrace = TraceViewModels.Last();
	}

	TraceListView->ScrollToBottom();

	// Restore selection and ensure it is visible.
	if (NewSelectedTrace.IsValid())
	{
		TraceListView->SetItemSelection(NewSelectedTrace, true);
		TraceListView->RequestScrollIntoView(NewSelectedTrace);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::TraceList_OnSelectionChanged(TSharedPtr<FTraceViewModel> TraceSession, ESelectInfo::Type SelectInfo)
{
	SelectedTrace = TraceSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::TraceList_OnMouseButtonDoubleClick(TSharedPtr<FTraceViewModel> TraceSession)
{
	LoadTraceSession(TraceSession);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SStartPageWindow::AutoStart_IsChecked() const
{
	return bAutoStartAnalysisForLiveSessions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::AutoStart_OnCheckStateChanged(ECheckBoxState NewState)
{
	bAutoStartAnalysisForLiveSessions = (NewState == ECheckBoxState::Checked);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// We need to update the trace list, but not too often.
	static uint64 NextTimestamp = 0;
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.5 / FPlatformTime::GetSecondsPerCycle64()); // 500ms
		NextTimestamp = Time + WaitTime;
		RefreshTraceList();
	}

	TickSplashScreenOverlay(InDeltaTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EActiveTimerReturnType SStartPageWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SStartPageWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> DragDropOp = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (DragDropOp.IsValid())
	{
		if (DragDropOp->HasFiles())
		{
			const TArray<FString>& Files = DragDropOp->GetFiles();
			if (Files.Num() == 1)
			{
				const FString DraggedFileExtension = FPaths::GetExtension(Files[0], true);
				if (DraggedFileExtension == TEXT(".utrace"))
				{
					return FReply::Handled();
				}
			}
		}
	}

	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> DragDropOp = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (DragDropOp.IsValid())
	{
		if (DragDropOp->HasFiles())
		{
			// For now, only allow a single file.
			const TArray<FString>& Files = DragDropOp->GetFiles();
			if (Files.Num() == 1)
			{
				const FString DraggedFileExtension = FPaths::GetExtension(Files[0], true);
				if (DraggedFileExtension == TEXT(".utrace"))
				{
					LoadTraceFile(Files[0]);
					return FReply::Handled();
				}
			}
		}
	}

	return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStartPageWindow::Open_IsEnabled() const
{
	return TraceViewModels.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Open_OnClicked()
{
	LoadTraceSession(SelectedTrace);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OpenFileDialog()
{
	const FString ProfilingDirectory(FPaths::ConvertRelativePathToFull(FInsightsManager::Get()->GetStoreDir()));

	TArray<FString> OutFiles;
	bool bOpened = false;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr)
	{
		FSlateApplication::Get().CloseToolTip();

		bOpened = DesktopPlatform->OpenFileDialog
		(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("LoadTrace_FileDesc", "Open trace file...").ToString(),
			ProfilingDirectory,
			TEXT(""),
			LOCTEXT("LoadTrace_FileFilter", "Trace files (*.utrace)|*.utrace|All files (*.*)|*.*").ToString(),
			EFileDialogFlags::None,
			OutFiles
		);
	}

	if (bOpened == true)
	{
		if (OutFiles.Num() == 1)
		{
			LoadTraceFile(OutFiles[0]);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadTraceSession(TSharedPtr<FTraceViewModel> InTraceSession)
{
	if (InTraceSession.IsValid())
	{
		LoadTrace(InTraceSession->TraceId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadTraceFile(const FString& InTraceFile)
{
	if (FInsightsManager::Get()->ShouldOpenAnalysisInSeparateProcess())
	{
		UE_LOG(TraceInsights, Log, TEXT("[StartPage] Start analysis (in separate process) for trace file: \"%s\""), *InTraceFile);

		const TCHAR* ExecutablePath = FPlatformProcess::ExecutablePath();

		FString CmdLine = TEXT("-OpenTraceFile=\"") + InTraceFile + TEXT("\"");

		constexpr bool bLaunchDetached = true;
		constexpr bool bLaunchHidden = false;
		constexpr bool bLaunchReallyHidden = false;

		uint32 ProcessID = 0;
		const int32 PriorityModifier = 0;
		const TCHAR* OptionalWorkingDirectory = nullptr;

		void* PipeWriteChild = nullptr;
		void* PipeReadChild = nullptr;

		FProcHandle Handle = FPlatformProcess::CreateProc(ExecutablePath, *CmdLine, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, &ProcessID, PriorityModifier, OptionalWorkingDirectory, PipeWriteChild, PipeReadChild);
		if (Handle.IsValid())
		{
			FPlatformProcess::CloseProc(Handle);
		}

		SplashScreenOverlayTraceFile = FPaths::GetBaseFilename(InTraceFile);
		ShowSplashScreenOverlay();
	}
	else
	{
		UE_LOG(TraceInsights, Log, TEXT("[StartPage] Start analysis for trace file: \"%s\""), *InTraceFile);
		FInsightsManager::Get()->LoadTraceFile(InTraceFile);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadTrace(uint32 InTraceId)
{
	if (FInsightsManager::Get()->ShouldOpenAnalysisInSeparateProcess())
	{
		UE_LOG(TraceInsights, Log, TEXT("[StartPage] Start analysis (in separate process) for trace id: 0x%08X"), InTraceId);

		const TCHAR* ExecutablePath = FPlatformProcess::ExecutablePath();

		const uint32 StorePort = FInsightsManager::Get()->GetStoreClient()->GetStorePort();
		FString CmdLine = FString::Printf(TEXT("-OpenTraceId=%d -StorePort=%d"), InTraceId, StorePort);

		constexpr bool bLaunchDetached = true;
		constexpr bool bLaunchHidden = false;
		constexpr bool bLaunchReallyHidden = false;

		uint32 ProcessID = 0;
		const int32 PriorityModifier = 0;
		const TCHAR* OptionalWorkingDirectory = nullptr;

		void* PipeWriteChild = nullptr;
		void* PipeReadChild = nullptr;

		FProcHandle Handle = FPlatformProcess::CreateProc(ExecutablePath, *CmdLine, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, &ProcessID, PriorityModifier, OptionalWorkingDirectory, PipeWriteChild, PipeReadChild);
		if (Handle.IsValid())
		{
			FPlatformProcess::CloseProc(Handle);
		}

		TSharedPtr<FTraceViewModel>* TraceSessionPtrPtr = TraceViewModelMap.Find(InTraceId);
		if (TraceSessionPtrPtr)
		{
			FTraceViewModel& TraceSession = **TraceSessionPtrPtr;
			SplashScreenOverlayTraceFile = FPaths::GetBaseFilename(TraceSession.Uri.ToString());
		}
		ShowSplashScreenOverlay();
	}
	else
	{
		UE_LOG(TraceInsights, Log, TEXT("[StartPage] Start analysis for trace id: 0x%08X"), InTraceId);
		FInsightsManager::Get()->LoadTrace(InTraceId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::MakeTraceListMenu()
{
	RefreshTraceList();

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("Misc", LOCTEXT("MiscHeading", "Misc"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenFileButtonLabel", "Open File..."),
			LOCTEXT("OpenFileButtonTooltip", "Start analysis for a specified trace file."),
			FSlateIcon(FInsightsStyle::GetStyleSetName(), "OpenFile.Icon.Small"),
			FUIAction(FExecuteAction::CreateSP(this, &SStartPageWindow::OpenFileDialog)),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("AvailableTraces", LOCTEXT("AvailableTracesHeading", "Top Most Recently Created Traces"));
	{
		Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
		if (StoreClient != nullptr)
		{
			// Make a copy of the trace list (to allow list view to be sorted by other criteria).
			TArray<TSharedPtr<FTraceViewModel>> SortedTraces(TraceViewModels);
			Algo::SortBy(SortedTraces, &FTraceViewModel::Timestamp);

			int32 TraceCountLimit = 10; // top 10

			// Iterate in reverse order as we want most recently created traces first.
			for (int32 TraceIndex = SortedTraces.Num() - 1; TraceIndex >= 0 && TraceCountLimit > 0; --TraceIndex, --TraceCountLimit)
			{
				const FTraceViewModel& Trace = *SortedTraces[TraceIndex];

				FText Label = Trace.Name;
				if (Trace.bIsLive)
				{
					Label = FText::Format(LOCTEXT("LiveTraceTextFmt", "{0} (LIVE!)"), Label);
				}

				MenuBuilder.AddMenuEntry(
					Label,
					TAttribute<FText>(), // no tooltip
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SStartPageWindow::LoadTrace, Trace.TraceId)),
					NAME_None,
					EUserInterfaceActionType::Button
				);
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetTraceStoreDirectory() const
{
	return FText::FromString(FPaths::ConvertRelativePathToFull(FInsightsManager::Get()->GetStoreDir()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::ExploreTraceStoreDirectory_OnClicked()
{
	FString FullPath(FPaths::ConvertRelativePathToFull(FInsightsManager::Get()->GetStoreDir()));
	FPlatformProcess::ExploreFolder(*FullPath);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetRecorderStatusText() const
{
	Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
	const bool bIsRecorderServerRunning = (StoreClient != nullptr); // TODO: StoreClient->IsRecorderServerRunning();

	if (bIsRecorderServerRunning)
	{
		return FText(LOCTEXT("RecorderServerRunning", "Running"));
	}
	else
	{
		return FText(LOCTEXT("RecorderServerStopped", "Stopped"));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::StartTraceRecorder_Visibility() const
{
	Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
	const bool bIsRecorderServerRunning = (StoreClient != nullptr); // TODO: StoreClient->IsRecorderServerRunning();

	return bIsRecorderServerRunning ? EVisibility::Collapsed : EVisibility::Visible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::StopTraceRecorder_Visibility() const
{
	Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
	const bool bIsRecorderServerRunning = (StoreClient != nullptr); // TODO: StoreClient->IsRecorderServerRunning();

	return bIsRecorderServerRunning ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::StartTraceRecorder_OnClicked()
{
	//TODO: StoreClient->StartRecorderServer();
	RefreshTraceList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::StopTraceRecorder_OnClicked()
{
	//TODO: StoreClient->StopRecorderServer();
	RefreshTraceList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OpenSettings()
{
	MainContentPanel->SetEnabled(false);
	(*OverlaySettingsSlot)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
		.Padding(8.0f)
		[
			SNew(SInsightsSettings)
			.OnClose(this, &SStartPageWindow::CloseSettings)
			.SettingPtr(&FInsightsManager::GetSettings())
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::CloseSettings()
{
	// Close the profiler settings by simply replacing widget with a null one.
	(*OverlaySettingsSlot)
	[
		SNullWidget::NullWidget
	];
	MainContentPanel->SetEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
