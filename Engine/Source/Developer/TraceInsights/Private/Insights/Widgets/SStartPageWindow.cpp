// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStartPageWindow.h"

#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManagerGeneric.h"
#include "Input/Events.h"
#include "Internationalization/Text.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
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
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SInsightsSettings.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SStartPageWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////
// STraceSessionRow
////////////////////////////////////////////////////////////////////////////////////////////////////

class STraceSessionRow : public SMultiColumnTableRow<TSharedPtr<FTraceSession>>
{
	SLATE_BEGIN_ARGS(STraceSessionRow) {}
	SLATE_END_ARGS()

public:
	/**
	 * Constructs the widget.
	 *
	 * @param InArgs The construction arguments.
	 * @param InTraceSession The trace session displayed by this row.
	 * @param InOwnerTableView The table to which the row must be added.
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FTraceSession> InTraceSession, TSharedRef<SStartPageWindow> InParentWidget, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		WeakTraceSession = MoveTemp(InTraceSession);
		WeakParentWidget = InParentWidget;

		SMultiColumnTableRow<TSharedPtr<FTraceSession>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
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
					.Text(this, &STraceSessionRow::GetTraceSessionName)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("Uri")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionUri)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("Platform")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionPlatform)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("AppName")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionAppName)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("BuildConfig")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionBuildConfiguration)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("BuildTarget")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionBuildTarget)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("Size")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionSize)
					.ColorAndOpacity(this, &STraceSessionRow::GetColorBySize)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else if (ColumnName == FName(TEXT("Status")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &STraceSessionRow::GetTraceSessionStatus)
					.ToolTipText(this, &STraceSessionRow::GetTraceSessionTooltip)
				];
		}
		else
		{
			return SNew(STextBlock).Text(LOCTEXT("UnknownColumn", "Unknown Column"));
		}
	}

	FText GetTraceSessionName() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			return TraceSessionPin->Name;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSessionUri() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			return TraceSessionPin->Uri;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSessionPlatform() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			return TraceSessionPin->Platform;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSessionAppName() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			return TraceSessionPin->AppName;
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSessionBuildConfiguration() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			if (TraceSessionPin->ConfigurationType != EBuildConfiguration::Unknown)
			{
				return EBuildConfigurations::ToText(TraceSessionPin->ConfigurationType);
			}
		}
		return FText::GetEmpty();
	}

	FText GetTraceSessionBuildTarget() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			if (TraceSessionPin->TargetType != EBuildTargetType::Unknown)
			{
				return FText::FromString(LexToString(TraceSessionPin->TargetType));
			}
		}
		return FText::GetEmpty();
	}

	FText GetTraceSessionTimeStamp() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			return FText::AsDate(TraceSessionPin->TimeStamp);
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FText GetTraceSessionSize() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			//FNumberFormattingOptions FormattingOptions;
			//FormattingOptions.MinimumFractionalDigits = 1;
			//FormattingOptions.MaximumFractionalDigits = 1;
			//return FText::AsMemory(TraceSessionPin->Size, &FormattingOptions);
			return FText::Format(LOCTEXT("SessionFileSizeFormatKiB", "{0} KiB"), TraceSessionPin->Size / 1024);
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FSlateColor GetColorBySize() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			TSharedRef<ITypedTableView<TSharedPtr<FTraceSession>>> OwnerWidget = OwnerTablePtr.Pin().ToSharedRef();
			const TSharedPtr<FTraceSession>* MyItem = OwnerWidget->Private_ItemFromWidget(this);
			const bool IsSelected = OwnerWidget->Private_IsItemSelected(*MyItem);

			if (IsSelected)
			{
				return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
			}
			else if (TraceSessionPin->Size < 1024ULL * 1024ULL)
			{
				// < 1 MiB
				return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			}
			else if (TraceSessionPin->Size < 1024ULL * 1024ULL * 1024ULL)
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

	FText GetTraceSessionStatus() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			if (TraceSessionPin->bIsLive)
			{
				return LOCTEXT("LiveTraceSessionStatus", "LIVE");
			}
		}
		return FText::GetEmpty();
	}

	FText GetTraceSessionTooltip() const
	{
		TSharedPtr<FTraceSession> TraceSessionPin = WeakTraceSession.Pin();
		if (TraceSessionPin.IsValid())
		{
			FTextBuilder TextBuilder;

			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_Name", "{0}"), TraceSessionPin->Name);
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_Uri", "{0}"), TraceSessionPin->Uri);
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_Platform", "Platform: {0}"), TraceSessionPin->Platform);
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_AppName", "App Name: {0}"), TraceSessionPin->AppName);
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_CommandLine", "Command Line: {0}"), TraceSessionPin->CommandLine);
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_BuildConfig", "Build Configuration: {0}"),
				TraceSessionPin->ConfigurationType == EBuildConfiguration::Unknown ? FText::GetEmpty() : EBuildConfigurations::ToText(TraceSessionPin->ConfigurationType));
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_BuildTarget", "Build Target: {0}"),
				TraceSessionPin->TargetType == EBuildTargetType::Unknown ? FText::GetEmpty() : FText::FromString(LexToString(TraceSessionPin->TargetType)));
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_Timestamp", "Timestamp: {0}"), FText::AsDateTime(TraceSessionPin->TimeStamp));
			if (TraceSessionPin->Size > 1024)
			{
				TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_FileSize2", "File Size: {0} bytes ({1})"), FText::AsNumber(TraceSessionPin->Size), FText::AsMemory(TraceSessionPin->Size));
			}
			else
			{
				TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_FileSize1", "File Size: {0} bytes"), FText::AsNumber(TraceSessionPin->Size));
			}
			const FText Status = TraceSessionPin->bIsLive ? LOCTEXT("LiveTraceSessionStatus", "LIVE") : LOCTEXT("OfflineTraceSessionStatus", "Offline");
			TextBuilder.AppendLineFormat(LOCTEXT("TraceSessionTooltip_Status", "Status: {0}"), Status);

			return TextBuilder.ToText();
		}
		else
		{
			return FText::GetEmpty();
		}
	}

private:
	TWeakPtr<FTraceSession> WeakTraceSession;
	TWeakPtr<SStartPageWindow> WeakParentWidget;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SStartPageWindow
////////////////////////////////////////////////////////////////////////////////////////////////////

SStartPageWindow::SStartPageWindow()
	: DurationActive(0.0f)
	, AvailableSessionCount(0)
	, LiveSessionCount(0)
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
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBox)
							.WidthOverride(1024.0f)
							[
								SNew(SBorder)
								.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
								.Padding(8.0f)
								.HAlign(HAlign_Fill)
								[
									ConstructSessionsPanel()
								]
							]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBox)
							.WidthOverride(256.0f)
							[
								SNew(SBorder)
								.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
								.Padding(8.0f)
								.HAlign(HAlign_Fill)
								[
									ConstructRecorderPanel()
								]
							]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBox)
							.WidthOverride(512.0f)
							.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)
							[
								SNew(SBorder)
								.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
								.Padding(8.0f)
								.HAlign(HAlign_Fill)
								[
									ConstructConnectPanel()
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

	FSlateApplication::Get().SetKeyboardFocus(TraceSessionsListView);
	FSlateApplication::Get().SetUserFocus(0, TraceSessionsListView);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructSessionsPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SessionsPanelTitle", "Trace Sessions"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.ColorAndOpacity(FLinearColor::Gray)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(0.0f, 1.0f, 0.0f, 2.0f)
		.MaxHeight(22.0f + 20 * 14.0f) // max 20 rows
		[
			SAssignNew(TraceSessionsListView, SListView<TSharedPtr<FTraceSession>>)
			.IsFocusable(true)
			.ItemHeight(20.0f)
			.SelectionMode(ESelectionMode::Single)
			.OnSelectionChanged(this, &SStartPageWindow::TraceSessions_OnSelectionChanged)
			.OnMouseButtonDoubleClick(this, &SStartPageWindow::TraceSessions_OnMouseButtonDoubleClick)
			.ListItemsSource(&TraceSessions)
			.OnGenerateRow(this, &SStartPageWindow::TraceSessions_OnGenerateRow)
			.ConsumeMouseWheel(EConsumeMouseWheel::Always)
			//.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SStartPageWindow::TraceSessions_GetContextMenu))
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
		.HAlign(HAlign_Right)
		.Padding(0.0f, 2.0f)
		[
			ConstructLoadPanel()
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0f, 2.0f)
		[
			ConstructLocalSessionDirectoryPanel()
		]
	;

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructLoadPanel()
{
	TSharedRef<SWidget> Widget = SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.IsEnabled(this, &SStartPageWindow::Open_IsEnabled)
			.OnClicked(this, &SStartPageWindow::Open_OnClicked)
			.ToolTipText(LOCTEXT("OpenButtonTooltip", "Start analysis for selected session."))
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
			.ToolTipText(LOCTEXT("MRU_Tooltip", "Open a file or choose a session."))
			.OnGetMenuContent(this, &SStartPageWindow::MakeSessionListMenu)
			.HasDownArrow(true)
			.ContentPadding(FMargin(1.0f, 1.0f, 1.0f, 1.0f))
		]
	;

	return Widget;
}
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructLocalSessionDirectoryPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("LocalSessionDirectoryText", "Local Session Directory:"))
			.ColorAndOpacity(FLinearColor::Gray)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.Padding(0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SStartPageWindow::GetLocalSessionDirectory)
					.Justification(ETextJustify::Right)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ExploreLocalSessionDirButton", "..."))
					.ToolTipText(LOCTEXT("ExploreLocalSessionDirButtonToolTip", "Explore the Local Session Directory"))
					.OnClicked(this, &SStartPageWindow::ExploreLocalSessionDirectory_OnClicked)
				]
		]
	;

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
		]

	;

	RefreshTraceSessionList();

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructConnectPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
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
				.FillWidth(1.0)
				.VAlign(VAlign_Center)
				[
					SAssignNew(HostTextBox, SEditableTextBox)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Connect", "Connect"))
					.ToolTipText(LOCTEXT("ConnectToolTip", "Try connecting to host."))
					.OnClicked(this, &SStartPageWindow::Connect_OnClicked)
				]
		]
	;

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SStartPageWindow::TraceSessions_OnGenerateRow(TSharedPtr<FTraceSession> InTraceSession, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STraceSessionRow, InTraceSession, SharedThis(this), OwnerTable);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::RefreshTraceSessions_OnClicked()
{
	RefreshTraceSessionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Connect_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

	FText HostText = HostTextBox->GetText();
	if (HostText.IsEmptyOrWhitespace())
	{
		// nothing to do
	}
	else if (SessionService->ConnectSession(*HostText.ToString()))
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

	RefreshTraceSessionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::RefreshTraceSessionList()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

	TArray<Trace::FSessionHandle> AvailableSessions;
	SessionService->GetAvailableSessions(AvailableSessions);

	AvailableSessionCount = AvailableSessions.Num();

	bool bSessionChanged = false;

	// Count number of live sessions and update file sizes.
	int32 OldLiveSessionCount = LiveSessionCount;
	LiveSessionCount = 0;
	for (Trace::FSessionHandle SessionHandle : AvailableSessions)
	{
		Trace::FSessionInfo SessionInfo;
		SessionService->GetSessionInfo(SessionHandle, SessionInfo);
		if (SessionInfo.bIsLive)
		{
			++LiveSessionCount;
		}

		TSharedPtr<FTraceSession>* TraceSessionPtrPtr = TraceSessionsMap.Find(SessionHandle);
		if (TraceSessionPtrPtr)
		{
			(*TraceSessionPtrPtr)->Size = SessionInfo.Size;
		}
		else
		{
			bSessionChanged = true;
		}
	}

	// If session list has changed on analysis side, recreate the TraceSessions list view widget.
	//TODO: if (AvailableSessionsChangeNumber != SessionService->GetAvailableSessionsChangeNumber())
	if (bSessionChanged ||
		AvailableSessionCount != TraceSessions.Num() ||
		LiveSessionCount != OldLiveSessionCount)
	{
		TSharedPtr<FTraceSession> NewSelectedTraceSession;

		TraceSessions.Reset();
		TraceSessionsMap.Reset();

		for (Trace::FSessionHandle SessionHandle : AvailableSessions)
		{
			Trace::FSessionInfo SessionInfo;
			SessionService->GetSessionInfo(SessionHandle, SessionInfo);

			const TSharedPtr<FTraceSession> TraceSessionPtr = MakeShareable(new FTraceSession(SessionHandle, SessionInfo));
			TraceSessions.Add(TraceSessionPtr);
			TraceSessionsMap.Add(SessionHandle, TraceSessionPtr);

			// Identify the previously selected session (if stil available) to ensure selection remains unchanged.
			if (SelectedTraceSession && SelectedTraceSession->SessionHandle == SessionHandle)
			{
				NewSelectedTraceSession = TraceSessionPtr;
			}
		}
		Algo::SortBy(TraceSessions, &FTraceSession::TimeStamp);

		TraceSessionsListView->RebuildList();

		// If no selection, auto select the last (newest) session.
		if (!NewSelectedTraceSession.IsValid() && TraceSessions.Num() > 0)
		{
			NewSelectedTraceSession = TraceSessions.Last();
		}

		TraceSessionsListView->ScrollToBottom();

		// Restore selection and ensure is visible.
		if (NewSelectedTraceSession.IsValid())
		{
			TraceSessionsListView->SetItemSelection(NewSelectedTraceSession, true);
			TraceSessionsListView->RequestScrollIntoView(NewSelectedTraceSession);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::TraceSessions_OnSelectionChanged(TSharedPtr<FTraceSession> TraceSession, ESelectInfo::Type SelectInfo)
{
	SelectedTraceSession = TraceSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::TraceSessions_OnMouseButtonDoubleClick(TSharedPtr<FTraceSession> TraceSession)
{
	if (TraceSession.IsValid())
	{
		FInsightsManager::Get()->LoadSession(TraceSession->SessionHandle);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::TraceSessions_Visibility() const
{
	return (TraceSessions.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// We need to check if there is any available session, but not too often.
	static uint64 NextTimestamp = 0;
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.5 / FPlatformTime::GetSecondsPerCycle64()); // 500ms
		NextTimestamp = Time + WaitTime;
		RefreshTraceSessionList();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::IsSessionOverlayVisible() const
{
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Hidden;
	}

	return EVisibility::Visible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStartPageWindow::IsSessionValid() const
{
	return FInsightsManager::Get()->GetSession().IsValid();
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
					FInsightsManager::Get()->LoadTraceFile(Files[0]);
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
	return AvailableSessionCount > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Open_OnClicked()
{
	if (SelectedTraceSession.IsValid())
	{
		Trace::FSessionHandle SessionHandle = SelectedTraceSession->SessionHandle;
		FInsightsManager::Get()->LoadSession(SessionHandle);
	}
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OpenFileDialog()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	const FString ProfilingDirectory(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));

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
			FInsightsManager::Get()->LoadTraceFile(OutFiles[0]);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadTraceFile(const TCHAR* TraceFile)
{
	FInsightsManager::Get()->LoadTraceFile(FString(TraceFile));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadSession(Trace::FSessionHandle SessionHandle)
{
	FInsightsManager::Get()->LoadSession(SessionHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::MakeSessionListMenu()
{
	RefreshTraceSessionList();

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

	/* TODO: persistent MRU
	MenuBuilder.BeginSection("MostRecentlyUsedSessions", LOCTEXT("MostRecentlyUsedSessionsHeading", "Most Recently Used Sessions"));
	{
		TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

		TArray<Trace::FSessionHandle> MruSessionList;

		//TODO: real mru list
		SessionService->GetAvailableSessions(MruSessionList); // mock the list using available sessions
		int32 NumSessions = FMath::Min(3, MruSessionList.Num());

		for (int32 SessionIndex = 0; SessionIndex < NumSessions; ++SessionIndex)
		{
			Trace::FSessionHandle SessionHandle = MruSessionList[SessionIndex];

			Trace::FSessionInfo SessionInfo;
			SessionService->GetSessionInfo(SessionHandle, SessionInfo);

			FText Label = FText::FromString(SessionInfo.Name);
			if (SessionInfo.bIsLive)
			{
				Label = FText::Format(LOCTEXT("LiveSessionTextFmt", "{0} (LIVE!)"), Label);
			}

			MenuBuilder.AddMenuEntry(
				Label,
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SStartPageWindow::LoadSession, SessionHandle)),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
	}
	MenuBuilder.EndSection();
	*/

	MenuBuilder.BeginSection("AvailableSessions", LOCTEXT("AvailableSessionsHeading", "Top 10 Most Recently Created Sessions"));
	{
		TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

		TArray<Trace::FSessionHandle> AvailableSessions;
		SessionService->GetAvailableSessions(AvailableSessions);

		struct FSessionInfoEx
		{
			FDateTime GetTimeStamp() const { return SessionInfo.TimeStamp; }

			Trace::FSessionHandle SessionHandle;
			Trace::FSessionInfo  SessionInfo;
		};

		TArray<TSharedPtr<FSessionInfoEx>> SortedAvailableSessions;
		SortedAvailableSessions.Reserve(AvailableSessions.Num());

		for (Trace::FSessionHandle SessionHandle : AvailableSessions)
		{
			FSessionInfoEx* SessionInfoExPtr = new FSessionInfoEx;
			SessionInfoExPtr->SessionHandle = SessionHandle;
			SessionService->GetSessionInfo(SessionHandle, SessionInfoExPtr->SessionInfo);
			SortedAvailableSessions.Add(MakeShareable(SessionInfoExPtr));
		}

		Algo::SortBy(SortedAvailableSessions, &FSessionInfoEx::GetTimeStamp);

		int32 SessionCountLimit = 10; // top 10

		// Iterate in reverse order as we want most recent sessions first.
		for (int32 SessionIndex = SortedAvailableSessions.Num() - 1; SessionIndex >= 0 && SessionCountLimit > 0; --SessionIndex, --SessionCountLimit)
		{
			const Trace::FSessionHandle SessionHandle = SortedAvailableSessions[SessionIndex]->SessionHandle;
			const Trace::FSessionInfo& SessionInfo = SortedAvailableSessions[SessionIndex]->SessionInfo;

			FText Label = FText::FromString(SessionInfo.Name);
			if (SessionInfo.bIsLive)
			{
				Label = FText::Format(LOCTEXT("LiveSessionTextFmt", "{0} (LIVE!)"), Label);
			}

			MenuBuilder.AddMenuEntry(
				Label,
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SStartPageWindow::LoadSession, SessionHandle)),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetLocalSessionDirectory() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return FText::FromString(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::ExploreLocalSessionDirectory_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	FString FullPath(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));
	FPlatformProcess::ExploreFolder(*FullPath);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetRecorderStatusText() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	if (SessionService->IsRecorderServerRunning())
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
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return SessionService->IsRecorderServerRunning() ? EVisibility::Collapsed : EVisibility::Visible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::StopTraceRecorder_Visibility() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return SessionService->IsRecorderServerRunning() ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::StartTraceRecorder_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	SessionService->StartRecorderServer();
	RefreshTraceSessionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::StopTraceRecorder_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	SessionService->StopRecorderServer();
	RefreshTraceSessionList();
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
