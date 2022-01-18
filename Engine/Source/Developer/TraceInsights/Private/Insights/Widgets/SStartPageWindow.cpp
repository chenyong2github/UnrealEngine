// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStartPageWindow.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Text.h"
#include "IPAddress.h"
#include "SlateOptMacros.h"
#include "SocketSubsystem.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Trace/ControlClient.h"
#include "Trace/StoreClient.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Testing/SStarshipSuite.h"
#include "Widgets/Text/STextBlock.h"

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
#include "Insights/InsightsStyle.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SInsightsSettings.h"
#include "Insights/Widgets/SLazyToolTip.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "STraceStoreWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////
// STraceListRow
////////////////////////////////////////////////////////////////////////////////////////////////////

class STraceListRow : public SMultiColumnTableRow<TSharedPtr<FTraceViewModel>>, public ILazyToolTipCreator
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
	void Construct(const FArguments& InArgs, TSharedPtr<FTraceViewModel> InTrace, TSharedRef<STraceStoreWindow> InParentWidget, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		WeakTrace = MoveTemp(InTrace);
		WeakParentWidget = InParentWidget;

		SMultiColumnTableRow<TSharedPtr<FTraceViewModel>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
	}

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

	FText GetTraceBranch() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->Branch;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceBuildVersion() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->BuildVersion;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceChangelist() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return FText::AsNumber(TracePin->Changelist, &FNumberFormattingOptions::DefaultNoGrouping());
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	EVisibility TraceChangelistVisibility() const
	{
		TSharedPtr<FTraceViewModel> TracePin = WeakTrace.Pin();
		if (TracePin.IsValid())
		{
			return TracePin->Changelist != 0 ? EVisibility::Visible : EVisibility::Collapsed;
		}
		else
		{
			return EVisibility::Collapsed;
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
			if (TracePin->Size < 1024ULL * 1024ULL)
			{
				// < 1 MiB
				TSharedRef<ITypedTableView<TSharedPtr<FTraceViewModel>>> OwnerWidget = OwnerTablePtr.Pin().ToSharedRef();
				const TSharedPtr<FTraceViewModel>* MyItem = OwnerWidget->Private_ItemFromWidget(this);
				const bool IsSelected = OwnerWidget->Private_IsItemSelected(*MyItem);
				if (IsSelected)
				{
					return FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.0f));
				}
				else
				{
					return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
				}
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
		return SNew(SLazyToolTip, SharedThis(this));
	}

	// ILazyToolTipCreator
	virtual TSharedPtr<SToolTip> CreateTooltip() const override
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
				.Padding(FMargin(-7.0f, -7.0f, -7.0f, 0.0f))
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(FMargin(6.0f, 6.0f, 6.0f, 6.0f))
					.BorderImage(FInsightsStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FSlateColor(EStyleColor::Panel))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.Padding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
						.FillWidth(1.0f)
						[
							SNew(STextBlock)
							.Text(this, &STraceListRow::GetTraceName)
							//.Font(FAppStyle::Get().GetFontStyle("Font.Large")) // 14
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
							.ColorAndOpacity(FSlateColor(EStyleColor::Foreground))
						]

						+ SHorizontalBox::Slot()
						.Padding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
						.AutoWidth()
						[
							SNew(STextBlock)
							//.Font(FAppStyle::Get().GetFontStyle("Font.Large")) // 14
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
							.Text(this, &STraceListRow::GetTraceIndexAndId)
							.ColorAndOpacity(FSlateColor(EStyleColor::White25))
						]
					]
				]

				+ SVerticalBox::Slot()
				.Padding(FMargin(-7.0f, 1.0f, -7.0f, 0.0f))
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(FMargin(6.0f, 6.0f, 6.0f, 4.0f))
					.BorderImage(FInsightsStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FSlateColor(EStyleColor::Panel))
					[
						SNew(STextBlock)
						.Text(this, &STraceListRow::GetTraceUri)
						//.Font(FAppStyle::Get().GetFontStyle("SmallFont")) // 8
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(EStyleColor::Foreground))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(-7.0f, 0.0f, -7.0f, -7.0f))
				[
					SNew(SBorder)
					.Padding(FMargin(6.0f, 0.0f, 6.0f, 4.0f))
					.BorderImage(FInsightsStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FSlateColor(EStyleColor::Panel))
					[
						SAssignNew(GridPanel, SGridPanel)
					]
				]
			];

			int32 Row = 0;
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Platform", "Platform:"), &STraceListRow::GetTracePlatform);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_AppName", "App Name:"), &STraceListRow::GetTraceAppName);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_CommandLine", "Command Line:"), &STraceListRow::GetTraceCommandLine);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Branch", "Branch:"), &STraceListRow::GetTraceBranch);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_BuildVersion", "Build Version:"), &STraceListRow::GetTraceBuildVersion);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Changelist", "Changelist:"), &STraceListRow::GetTraceChangelist, &STraceListRow::TraceChangelistVisibility);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_BuildConfig", "Build Config:"), &STraceListRow::GetTraceBuildConfiguration);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_BuildTarget", "Build Target:"), &STraceListRow::GetTraceBuildTarget);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Timestamp", "Timestamp:"), &STraceListRow::GetTraceTimestampForTooltip);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Size", "File Size:"), &STraceListRow::GetTraceSizeForTooltip);
			AddGridPanelRow(GridPanel, Row++, LOCTEXT("TraceTooltip_Status", "Status:"), &STraceListRow::GetTraceStatusForTooltip);

			return TraceTooltip;
		}
		else
		{
			TSharedPtr<SToolTip> TraceTooltip =
				SNew(SToolTip)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TraceTooltip_NA", "N/A"))
				];

			return TraceTooltip;
		}
	}

private:
	void AddGridPanelRow(TSharedPtr<SGridPanel> Grid, int32 Row, const FText& InHeaderText,
		typename TAttribute<FText>::FGetter::template TConstMethodPtr<STraceListRow> InValueTextFn,
		typename TAttribute<EVisibility>::FGetter::template TConstMethodPtr<STraceListRow> InVisibilityFn = nullptr) const
	{
		SGridPanel::FSlot* Slot0 = nullptr;
		Grid->AddSlot(0, Row)
			.Expose(Slot0)
			.Padding(2.0f)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(InHeaderText)
				.ColorAndOpacity(FSlateColor(EStyleColor::White25))
			];

		SGridPanel::FSlot* Slot1 = nullptr;
		Grid->AddSlot(1, Row)
			.Expose(Slot1)
			.Padding(2.0f)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(this, InValueTextFn)
				.WrapTextAt(1024.0f)
				.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
				.ColorAndOpacity(FSlateColor(EStyleColor::Foreground))
			];

		if (InVisibilityFn)
		{
			Slot0->GetWidget()->SetVisibility(MakeAttributeSP(this, InVisibilityFn));
			Slot1->GetWidget()->SetVisibility(MakeAttributeSP(this, InVisibilityFn));
		}
		else
		{
			auto Fn = MakeAttributeSP(this, InValueTextFn);
			Slot0->GetWidget()->SetVisibility(MakeAttributeLambda([this, Fn]() { return Fn.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }));
			Slot1->GetWidget()->SetVisibility(MakeAttributeLambda([this, Fn]() { return Fn.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }));
		}
	}

private:
	TWeakPtr<FTraceViewModel> WeakTrace;
	TWeakPtr<STraceStoreWindow> WeakParentWidget;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// STraceStoreWindow
////////////////////////////////////////////////////////////////////////////////////////////////////

STraceStoreWindow::STraceStoreWindow()
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
	, SplashScreenOverlayFadeTime(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STraceStoreWindow::~STraceStoreWindow()
{
#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Insights.Usage.SessionBrowser"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void STraceStoreWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)

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

		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(0.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			[
				SNew(SBorder)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(0.0f)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FSlateColor(EStyleColor::Panel))
			]
		]

		// Overlay slot for the main window area
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(MainContentPanel, SVerticalBox)

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.AutoHeight()
			.Padding(12.0f, 8.0f, 12.0f, 4.0f)
			[
				ConstructTraceStoreDirectoryPanel()
			]

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillHeight(1.0f)
			.Padding(3.0f, 4.0f)
			[
				ConstructSessionsPanel()
			]

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.AutoHeight()
			.Padding(12.0f, 4.0f, 12.0f, 8.0f)
			[
				ConstructLoadPanel()
			]
		]

		// Overlay for fake splashscreen.
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(0.0f)
		[
			SNew(SBox)
			.Visibility(this, &STraceStoreWindow::SplashScreenOverlay_Visibility)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("PopupText.Background"))
				.BorderBackgroundColor(this, &STraceStoreWindow::SplashScreenOverlay_ColorAndOpacity)
				.Padding(0.0f)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &STraceStoreWindow::GetSplashScreenOverlayText)
						.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
						.ColorAndOpacity(this, &STraceStoreWindow::SplashScreenOverlay_TextColorAndOpacity)
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

TSharedRef<SWidget> STraceStoreWindow::ConstructSessionsPanel()
{
	TSharedRef<SWidget> Widget = SAssignNew(TraceListView, SListView<TSharedPtr<FTraceViewModel>>)
		.IsFocusable(true)
		.ItemHeight(20.0f)
		.SelectionMode(ESelectionMode::Single)
		.OnSelectionChanged(this, &STraceStoreWindow::TraceList_OnSelectionChanged)
		.OnMouseButtonDoubleClick(this, &STraceStoreWindow::TraceList_OnMouseButtonDoubleClick)
		.ListItemsSource(&TraceViewModels)
		.OnGenerateRow(this, &STraceStoreWindow::TraceList_OnGenerateRow)
		.ConsumeMouseWheel(EConsumeMouseWheel::Always)
		//.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &STraceStoreWindow::TraceList_GetContextMenu))
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
		);

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STraceStoreWindow::ConstructLoadPanel()
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
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
		.IsEnabled(this, &STraceStoreWindow::Open_IsEnabled)
		.OnClicked(this, &STraceStoreWindow::Open_OnClicked)
		.ToolTipText(LOCTEXT("OpenButtonTooltip", "Start analysis for selected trace session."))
		.ContentPadding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
		.Content()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("DialogButtonText"))
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("OpenButtonText", "Open Trace"))
			]
		]
	]

	+ SHorizontalBox::Slot()
	.Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
	.AutoWidth()
	[
		SNew(SComboButton)
		.ToolTipText(LOCTEXT("MRU_Tooltip", "Open a trace file or choose a trace session."))
		.OnGetMenuContent(this, &STraceStoreWindow::MakeTraceListMenu)
		.HasDownArrow(true)
	];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STraceStoreWindow::ConstructTraceStoreDirectoryPanel()
{
	TSharedRef<SWidget> Widget =

		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TraceStoreDirText", "Trace Store Directory"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.IsReadOnly(true)
			.BackgroundColor(FSlateColor(EStyleColor::Background))
			.Text(this, &STraceStoreWindow::GetTraceStoreDirectory)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ToolTipText(LOCTEXT("ExploreTraceStoreDirButtonToolTip", "Explore the Trace Store Directory"))
			.OnClicked(this, &STraceStoreWindow::ExploreTraceStoreDirectory_OnClicked)
			[
				SNew(SImage)
				.Image(FInsightsStyle::Get().GetBrush("Icons.FolderExplore"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STraceStoreWindow::ConstructAutoStartPanel()
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
		.IsChecked(this, &STraceStoreWindow::AutoStart_IsChecked)
		.OnCheckStateChanged(this, &STraceStoreWindow::AutoStart_OnCheckStateChanged)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AutoStart_Text", "Auto-start analysis for LIVE trace sessions"))
		]
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(6.0f, 0.0f, 0.0f, 0.0f)
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SAssignNew(AutoStartPlatformFilter, SSearchBox)
		.HintText(LOCTEXT("AutoStartPlatformFilter_Hint", "Platform"))
		.ToolTipText(LOCTEXT("AutoStartPlatformFilter_Tooltip", "Type here to specify the Platform filter.\nAuto-start analysis will be enabled only for live trace sessions with this specified Platform."))
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(6.0f, 0.0f, 0.0f, 0.0f)
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

TSharedRef<ITableRow> STraceStoreWindow::TraceList_OnGenerateRow(TSharedPtr<FTraceViewModel> InTrace, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STraceListRow, InTrace, SharedThis(this), OwnerTable);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::ShowSplashScreenOverlay()
{
	SplashScreenOverlayFadeTime = 3.5f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::TickSplashScreenOverlay(const float InDeltaTime)
{
	if (SplashScreenOverlayFadeTime > 0.0f)
	{
		SplashScreenOverlayFadeTime = FMath::Max(0.0f, SplashScreenOverlayFadeTime - InDeltaTime);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float STraceStoreWindow::SplashScreenOverlayOpacity() const
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

EVisibility STraceStoreWindow::SplashScreenOverlay_Visibility() const
{
	return SplashScreenOverlayFadeTime > 0.0f ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor STraceStoreWindow::SplashScreenOverlay_ColorAndOpacity() const
{
	return FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, SplashScreenOverlayOpacity()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor STraceStoreWindow::SplashScreenOverlay_TextColorAndOpacity() const
{
	return FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, SplashScreenOverlayOpacity()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STraceStoreWindow::GetSplashScreenOverlayText() const
{
	return FText::Format(LOCTEXT("StartAnalysis", "Starting analysis...\n{0}"), FText::FromString(SplashScreenOverlayTraceFile));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STraceStoreWindow::RefreshTraces_OnClicked()
{
	RefreshTraceList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::RefreshTraceList()
{
	UE::Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
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

			//UE_LOG(TraceInsights, Log, TEXT("[TraceStore] Synching the trace list with StoreBrowser..."));

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
	const double Duration = StopwatchTotal.GetAccumulatedTime();
	if ((Duration > 0.0001) && (UpdatedTraces > 0 || AddedTraces > 0 || RemovedTraces > 0))
	{
		UE_LOG(TraceInsights, Log, TEXT("[TraceStore] The trace list refreshed in %.0f ms (%d traces : %d updated, %d added, %d removed)."),
			Duration * 1000.0, TraceViewModels.Num(), UpdatedTraces, AddedTraces, RemovedTraces);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::UpdateTrace(FTraceViewModel& InOutTrace, const Insights::FStoreBrowserTraceInfo& InSourceTrace)
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
		InOutTrace.Branch = FText::FromString(InSourceTrace.Branch);
		InOutTrace.BuildVersion = FText::FromString(InSourceTrace.BuildVersion);
		InOutTrace.Changelist = InSourceTrace.Changelist;
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
			UE_LOG(TraceInsights, Log, TEXT("[TraceStore] Auto starting analysis for trace with id 0x%08X..."), InOutTrace.TraceId);
			AutoStartedSessions.Add(InOutTrace.TraceId);
			LoadTrace(InOutTrace.TraceId);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::OnTraceListChanged()
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

void STraceStoreWindow::TraceList_OnSelectionChanged(TSharedPtr<FTraceViewModel> TraceSession, ESelectInfo::Type SelectInfo)
{
	SelectedTrace = TraceSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::TraceList_OnMouseButtonDoubleClick(TSharedPtr<FTraceViewModel> TraceSession)
{
	LoadTraceSession(TraceSession);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState STraceStoreWindow::AutoStart_IsChecked() const
{
	return bAutoStartAnalysisForLiveSessions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::AutoStart_OnCheckStateChanged(ECheckBoxState NewState)
{
	bAutoStartAnalysisForLiveSessions = (NewState == ECheckBoxState::Checked);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
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

EActiveTimerReturnType STraceStoreWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &STraceStoreWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STraceStoreWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STraceStoreWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
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

FReply STraceStoreWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
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

bool STraceStoreWindow::Open_IsEnabled() const
{
	return TraceViewModels.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STraceStoreWindow::Open_OnClicked()
{
	LoadTraceSession(SelectedTrace);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::OpenFileDialog()
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

void STraceStoreWindow::LoadTraceSession(TSharedPtr<FTraceViewModel> InTraceSession)
{
	if (InTraceSession.IsValid())
	{
		LoadTrace(InTraceSession->TraceId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::LoadTraceFile(const FString& InTraceFile)
{
	UE_LOG(TraceInsights, Log, TEXT("[TraceStore] Start analysis (in separate process) for trace file: \"%s\""), *InTraceFile);

	const TCHAR* ExecutablePath = FPlatformProcess::ExecutablePath();

	FString CmdLine = TEXT("-OpenTraceFile=\"") + InTraceFile + TEXT("\"");

	FString ExtraCmdParams;
	GetExtraCommandLineParams(ExtraCmdParams);
	CmdLine += ExtraCmdParams;

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

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::LoadTrace(uint32 InTraceId)
{
	UE_LOG(TraceInsights, Log, TEXT("[TraceStore] Start analysis (in separate process) for trace id: 0x%08X"), InTraceId);

	const TCHAR* ExecutablePath = FPlatformProcess::ExecutablePath();

	const uint32 StorePort = FInsightsManager::Get()->GetStoreClient()->GetStorePort();
	FString CmdLine = FString::Printf(TEXT("-OpenTraceId=%d -StorePort=%d"), InTraceId, StorePort);

	FString ExtraCmdParams;
	GetExtraCommandLineParams(ExtraCmdParams);
	CmdLine += ExtraCmdParams;

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

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STraceStoreWindow::MakeTraceListMenu()
{
	RefreshTraceList();

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("Misc", LOCTEXT("TraceListMenu_Section_Misc", "Misc"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenFileButtonLabel", "Open File..."),
			LOCTEXT("OpenFileButtonTooltip", "Start analysis for a specified trace file."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.FolderOpen"),
			FUIAction(FExecuteAction::CreateSP(this, &STraceStoreWindow::OpenFileDialog)),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("AvailableTraces", LOCTEXT("TraceListMenu_Section_AvailableTraces", "Top Most Recently Created Traces"));
	{
		UE::Trace::FStoreClient* StoreClient = FInsightsManager::Get()->GetStoreClient();
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
					FUIAction(FExecuteAction::CreateSP(this, &STraceStoreWindow::LoadTrace, Trace.TraceId)),
					NAME_None,
					EUserInterfaceActionType::Button
				);
			}
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("DebugOptions", LOCTEXT("TraceListMenu_Section_DebugOptions", "Debug Options"));

	// Enable Automation Tests Option.
	{
		FUIAction ToogleAutomationTestsAction;
		ToogleAutomationTestsAction.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				this->SetEnableAutomaticTesting(!this->GetEnableAutomaticTesting());
			});
		ToogleAutomationTestsAction.GetActionCheckState = FGetActionCheckState::CreateLambda([this]()
			{
				return this->GetEnableAutomaticTesting() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			});

		MenuBuilder.AddMenuEntry(
			LOCTEXT("EnableAutomatedTesting", "Enable Automation Testing"),
			LOCTEXT("EnableAutomatedTestingDesc", "Activates the automatic test system for new sessions opened from this window."),
			FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.TestAutomation"),
			ToogleAutomationTestsAction,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	// Enable Debug Tools Option.
	{
		FUIAction ToogleDebugToolsAction;
		ToogleDebugToolsAction.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				this->SetEnableDebugTools(!this->GetEnableDebugTools());
			});
		ToogleDebugToolsAction.GetActionCheckState = FGetActionCheckState::CreateLambda([this]()
			{
				return this->GetEnableDebugTools() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			});

		MenuBuilder.AddMenuEntry(
			LOCTEXT("EnableDebugTools", "Enable Debug Tools"),
			LOCTEXT("EnableDebugToolsDesc", "Enables debug tools for new sessions opened from this window."),
			FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.Debug"),
			ToogleDebugToolsAction,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

#if !UE_BUILD_SHIPPING
	// Open Starship Test Suite
	{
		FUIAction OpenStarshipSuiteAction;
		OpenStarshipSuiteAction.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				RestoreStarshipSuite();
			});

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenStarshipSuite", "Starship Test Suite"),
			LOCTEXT("OpenStarshipSuiteDesc", "Opens the Starship UX test suite."),
			FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.Test"),
			OpenStarshipSuiteAction,
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
#endif // !UE_BUILD_SHIPPING

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STraceStoreWindow::GetTraceStoreDirectory() const
{
	return FText::FromString(FPaths::ConvertRelativePathToFull(FInsightsManager::Get()->GetStoreDir()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STraceStoreWindow::ExploreTraceStoreDirectory_OnClicked()
{
	FString FullPath(FPaths::ConvertRelativePathToFull(FInsightsManager::Get()->GetStoreDir()));
	FPlatformProcess::ExploreFolder(*FullPath);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::OpenSettings()
{
	MainContentPanel->SetEnabled(false);
	(*OverlaySettingsSlot)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("PopupText.Background"))
		.Padding(8.0f)
		[
			SNew(SInsightsSettings)
			.OnClose(this, &STraceStoreWindow::CloseSettings)
			.SettingPtr(&FInsightsManager::GetSettings())
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::CloseSettings()
{
	// Close the profiler settings by simply replacing widget with a null one.
	(*OverlaySettingsSlot)
	[
		SNullWidget::NullWidget
	];
	MainContentPanel->SetEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STraceStoreWindow::GetExtraCommandLineParams(FString& OutParams) const
{
	if (bEnableAutomaticTesting)
	{
		OutParams.Append(TEXT(" -InsightsTest"));
	}
	if (bEnableDebugTools)
	{
		OutParams.Append(TEXT(" -DebugTools"));
	}
	if (bStartProcessWithStompMalloc)
	{
		OutParams.Append(TEXT(" -stompmalloc"));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
#define LOCTEXT_NAMESPACE "SConnectionWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SConnectionWindow
////////////////////////////////////////////////////////////////////////////////////////////////////

SConnectionWindow::SConnectionWindow()
: NotificationList()
, ActiveNotifications()
, ConnectTask()
, bIsConnecting(false)
, bIsConnectedSuccessfully(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SConnectionWindow::~SConnectionWindow()
{
	if (ConnectTask && !ConnectTask->IsComplete())
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(ConnectTask);
		ConnectTask = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SConnectionWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)

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

		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(0.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			[
				SNew(SBorder)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(0.0f)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FSlateColor(EStyleColor::Panel))
			]
		]

		// Overlay slot for the main window area
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(MainContentPanel, SVerticalBox)

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.AutoHeight()
			.Padding(3.0f, 3.0f)
			[
				ConstructConnectPanel()
			]
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SConnectionWindow::ConstructConnectPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(12.0f, 12.0f, 12.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(180.0f)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TraceRecorderAddressText", "Trace recorder IP address"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(120.0f)
				[
					SAssignNew(TraceRecorderAddressTextBox, SEditableTextBox)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(12.0f, 8.0f, 12.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(180.0f)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RunningInstanceAddressText", "Running instance IP address"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(120.0f)
				[
					SAssignNew(RunningInstanceAddressTextBox, SEditableTextBox)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(12.0f, 8.0f, 12.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(180.0f)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("InitialChannelsText", "Initial channels"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(120.0f)
				[
					SAssignNew(ChannelsTextBox, SEditableTextBox)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(198.0f, 4.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("InitialChannelsNoteText", "Comma-separated list of channel names (or \"default\"=cpu,gpu,frame,log,bookmark) to enable when connected."))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(12.0f, 8.0f, 12.0f, 12.0f)
		[
			SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.Text(LOCTEXT("Connect", "Connect"))
				.ToolTipText(LOCTEXT("ConnectToolTip", "Connect the running instance at specified ip with the local trace recorder."))
				.OnClicked(this, &SConnectionWindow::Connect_OnClicked)
				.IsEnabled_Lambda([this]() { return !bIsConnecting; })
			]
		]

		// Notification area overlay
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(16.0f)
		[
			SAssignNew(NotificationList, SNotificationList)
		];

	const FText LocalHost = FText::FromString(TEXT("127.0.0.1"));

	TSharedPtr<FInternetAddr> RecorderAddr;
	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get())
	{
		bool bCanBindAll = false;
		RecorderAddr = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	}
	if (RecorderAddr.IsValid())
	{
		const FString RecorderAddrStr = RecorderAddr->ToString(false);
		TraceRecorderAddressTextBox->SetText(FText::FromString(RecorderAddrStr));
	}
	else
	{
		TraceRecorderAddressTextBox->SetText(LocalHost);
	}

	RunningInstanceAddressTextBox->SetText(LocalHost);
	ChannelsTextBox->SetText(FText::FromStringView(TEXT("default")));

	return Widget;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SConnectionWindow::Connect_OnClicked()
{
	FText TraceRecorderAddressText = TraceRecorderAddressTextBox->GetText();
	if (TraceRecorderAddressText.IsEmptyOrWhitespace())
	{
		// nothing to do
		return FReply::Handled();
	}
	const FString& TraceRecorderAddressStr = TraceRecorderAddressText.ToString();

	FText RunningInstanceAddressText = RunningInstanceAddressTextBox->GetText();
	if (RunningInstanceAddressText.IsEmptyOrWhitespace())
	{
		// nothing to do
		return FReply::Handled();
	}
	const FString& RunningInstanceAddressStr = RunningInstanceAddressText.ToString();

	FGraphEventArray Prerequisites;
	FGraphEventArray* PrerequisitesPtr = nullptr;
	if (ConnectTask.IsValid())
	{
		Prerequisites.Add(ConnectTask);
		PrerequisitesPtr = &Prerequisites;
	}

	const FString ChannelsExpandedStr = ChannelsTextBox->GetText().ToString().Replace(TEXT("default"), TEXT("cpu,gpu,frame,log,bookmark"));

	FGraphEventRef PreConnectTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
		[this, TraceRecorderAddressStr, RunningInstanceAddressStr, ChannelsExpandedStr]
		{
			bIsConnecting = true;

			UE_LOG(TraceInsights, Log, TEXT("[Connection] Try connecting to \"%s\"..."), *RunningInstanceAddressStr);

			UE::Trace::FControlClient ControlClient;
			if (ControlClient.Connect(*RunningInstanceAddressStr))
			{
				UE_LOG(TraceInsights, Log, TEXT("[Connection] SendSendTo(\"%s\")..."), *TraceRecorderAddressStr);
				ControlClient.SendSendTo(*TraceRecorderAddressStr);
				UE_LOG(TraceInsights, Log, TEXT("[Connection] ToggleChannel(\"%s\")..."), *ChannelsExpandedStr);
				ControlClient.SendToggleChannel(*ChannelsExpandedStr, true);
				bIsConnectedSuccessfully = true;
			}
			else
			{
				bIsConnectedSuccessfully = false;
			}
		},
		TStatId{}, PrerequisitesPtr, ENamedThreads::AnyBackgroundThreadNormalTask);

	ConnectTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
		[this, RunningInstanceAddressStr]
		{
			if (bIsConnectedSuccessfully)
			{
				UE_LOG(TraceInsights, Log, TEXT("[Connection] Successfully connected."));

				FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("ConnectSuccess", "Successfully connected to \"{0}\"!"), FText::FromString(RunningInstanceAddressStr)));
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
				UE_LOG(TraceInsights, Warning, TEXT("[Connection] Failed to connect to \"%s\"!"), *RunningInstanceAddressStr);

				FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("ConnectFailed", "Failed to connect to \"{0}\"!"), FText::FromString(RunningInstanceAddressStr)));
				NotificationInfo.bFireAndForget = false;
				NotificationInfo.bUseLargeFont = false;
				NotificationInfo.bUseSuccessFailIcons = true;
				NotificationInfo.ExpireDuration = 10.0f;
				SNotificationItemWeak NotificationItem = NotificationList->AddNotification(NotificationInfo);
				NotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Fail);
				NotificationItem.Pin()->ExpireAndFadeout();
				ActiveNotifications.Add(TEXT("ConnectFailed"), NotificationItem);
			}

			bIsConnecting = false;
		},
		TStatId{}, PreConnectTask, ENamedThreads::GameThread);

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
#define LOCTEXT_NAMESPACE "SLauncherWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SLauncherWindow
////////////////////////////////////////////////////////////////////////////////////////////////////

SLauncherWindow::SLauncherWindow()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SLauncherWindow::~SLauncherWindow()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SLauncherWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Launcher", "Launcher"))
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
