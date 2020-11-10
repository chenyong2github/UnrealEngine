// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSlateFrameSchematicView.h"
#include "SlateProvider.h"
#include "SSlateTraceFlags.h"

#include "TraceServices/Model/AnalysisSession.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/ViewModels/ITimingEvent.h"
#include "Widgets/InvalidateWidgetReason.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SHeader.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"

#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Regex.h"
#include "SlateInsightsStyle.h"
#include "ISourceCodeAccessModule.h"
#include "ISourceCodeAccessor.h"

#define LOCTEXT_NAMESPACE "SSlateFrameSchematicView"

namespace UE
{
namespace SlateInsights
{

namespace Private
{
	const FName ColumnWidgetId("WidgetId");
	const FName ColumnNumber("Number");
	const FName ColumnFlag("Flag");

	struct FWidgetUniqueInvalidatedInfo
	{
		FWidgetUniqueInvalidatedInfo(Message::FWidgetId InWidgetId, EInvalidateWidgetReason InReason)
			: WidgetId(InWidgetId), Reason(InReason), Count(1), bRoot(true)
		{ }

		Message::FWidgetId WidgetId;
		EInvalidateWidgetReason Reason;
		uint32 Count;
		TArray<TSharedPtr<FWidgetUniqueInvalidatedInfo>> Investigators;
		bool bRoot;
		FString ScriptTrace;
		FString Callstack;
	};

	struct FWidgetUpdateInfo
	{
		FWidgetUpdateInfo(Message::FWidgetId InWidgetId, EWidgetUpdateFlags InUpdateFlags)
			: WidgetId(InWidgetId), UpdateFlags(InUpdateFlags), Count(1)
		{ }
		Message::FWidgetId WidgetId;
		EWidgetUpdateFlags UpdateFlags;
		uint32 Count;
	};

	class FWidgetUniqueInvalidatedInfoRow : public SMultiColumnTableRow<TSharedPtr<FWidgetUniqueInvalidatedInfo>>
	{
	public:
		SLATE_BEGIN_ARGS(FWidgetUniqueInvalidatedInfoRow) {}
		SLATE_END_ARGS()

		TSharedPtr<FWidgetUniqueInvalidatedInfo> Info;
		FText WidgetName;

		void Construct(const FArguments& Args,
			const TSharedRef<STableViewBase>& OwnerTableView,
			TSharedPtr<FWidgetUniqueInvalidatedInfo> InItem,
			const FText& InWidgetName)
		{
			Info = InItem;
			WidgetName = InWidgetName;

			SMultiColumnTableRow<TSharedPtr<FWidgetUniqueInvalidatedInfo>>::Construct(
				FSuperRowType::FArguments()
				.Padding(5.0f),
				OwnerTableView
			);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (ColumnName == ColumnWidgetId)
			{
				const EVisibility ExpanderArrowVisibility = Info->Investigators.Num() ? EVisibility::Visible : EVisibility::Hidden;
				const FString TraceAndCallStackTipText = Info->ScriptTrace.IsEmpty() ? Info->Callstack : Info->ScriptTrace + "\n" + Info->Callstack;

				return SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(SExpanderArrow, SharedThis(this))
						.Visibility(ExpanderArrowVisibility)
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(WidgetName)
						.ToolTipText(FText::AsCultureInvariant(TraceAndCallStackTipText))
					];
			}
			if (ColumnName == ColumnNumber)
			{
				return SNew(STextBlock)
					.Text(FText::AsNumber(Info->Count));
			}
			if (ColumnName == ColumnFlag)
			{
				return SNew(SSlateTraceInvalidateWidgetReasonFlags)
					.Reason(Info->Reason);
			}

			return SNullWidget::NullWidget;
		}
	};

	struct FWidgetUpdateInfoRow : public SMultiColumnTableRow<TSharedPtr<FWidgetUpdateInfo>>
	{
		SLATE_BEGIN_ARGS(FWidgetUpdateInfoRow) {}
		SLATE_END_ARGS()

		TSharedPtr<FWidgetUpdateInfo> Info;
		FText WidgetName;

		void Construct(const FArguments& InArgs,
			const TSharedRef<STableViewBase>& InOwnerTable,
			TSharedPtr<FWidgetUpdateInfo> InItem,
			const FText& InWidgetName)
		{
			Info = InItem;
			WidgetName = InWidgetName;
			SMultiColumnTableRow<TSharedPtr<FWidgetUpdateInfo>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
		}

		TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column)
		{
			if (Column == ColumnWidgetId)
			{
				return SNew(STextBlock)
					.Text(WidgetName);
			}
			else if (Column == ColumnNumber)
			{
				return SNew(STextBlock)
					.Text(FText::AsNumber(Info->Count));
			}
			else if (Column == ColumnFlag)
			{
				return SNew(SSlateTraceWidgetUpdateFlags)
					.UpdateFlags(Info->UpdateFlags);
			}

			return SNullWidget::NullWidget;
		}
	};

	FText GetWidgetName(const Trace::IAnalysisSession* AnalysisSession, Message::FWidgetId WidgetId)
	{
		if (AnalysisSession)
		{
			const FSlateProvider* SlateProvider = AnalysisSession->ReadProvider<FSlateProvider>(FSlateProvider::ProviderName);
			if (SlateProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

				if (const Message::FWidgetInfo* WidgetInfo = SlateProvider->FindWidget(WidgetId))
				{
					return FText::FromString(WidgetInfo->DebugInfo);
				}
			}
		}
		return FText::GetEmpty();
	}
} //namespace Private

void SSlateFrameSchematicView::Construct(const FArguments& InArgs)
{
	StartTime = -1.0;
	EndTime = -1.0;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(2.0f))
		[
			SNew(SHeader)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Invalidation_Title", "Invalidation"))
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(0.5f)
		[
			SNew(SBox)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(InvalidationSummary, STextBlock)
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(WidgetInvalidateInfoListView, STreeView<TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>>)
					.ItemHeight(24.0f)
					.TreeItemsSource(&WidgetInvalidationInfos)
					.OnGenerateRow(this, &SSlateFrameSchematicView::HandleUniqueInvalidatedMakeTreeRowWidget)
					.OnGetChildren(this, &SSlateFrameSchematicView::HandleUniqueInvalidatedChildrenForInfo)
					.OnContextMenuOpening(this, &SSlateFrameSchematicView::HandleWidgetInvalidateListContextMenu)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow
					(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(Private::ColumnWidgetId)
						.DefaultLabel(LOCTEXT("WidgetColumn", "Widget"))
						.FillWidth(1.f)

						+ SHeaderRow::Column(Private::ColumnNumber)
						.DefaultLabel(LOCTEXT("AmountColumn", "Amount"))
						.FixedWidth(50.f)
						.HAlignCell(EHorizontalAlignment::HAlign_Right)
						
						+ SHeaderRow::Column(Private::ColumnFlag)
						.DefaultLabel(LOCTEXT("ReasonColumn", "Reason"))
						.FixedWidth(75.f)
						.HAlignCell(EHorizontalAlignment::HAlign_Right)
					)
				]
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(2.0f))
		[
			SNew(SHeader)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Update_Title", "Update"))
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(0.5f)
		[
			SNew(SBox)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(UpdateSummary, STextBlock)
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(WidgetUpdateInfoListView, SListView<TSharedPtr<Private::FWidgetUpdateInfo>>)
					.ScrollbarVisibility(EVisibility::Visible)
					.ItemHeight(24.0f)
					.ListItemsSource(&WidgetUpdateInfos)
					.SelectionMode(ESelectionMode::SingleToggle)
					.OnGenerateRow(this, &SSlateFrameSchematicView::HandleWidgetUpdateInfoGenerateWidget)
					.HeaderRow
					(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(Private::ColumnWidgetId)
						.DefaultLabel(LOCTEXT("WidgetColumn", "Widget"))
						.FillWidth(1.f)

						+ SHeaderRow::Column(Private::ColumnNumber)
						.DefaultLabel(LOCTEXT("AmountColumn", "Amount"))
						.FixedWidth(50.f)
						.HAlignCell(EHorizontalAlignment::HAlign_Right)

						+ SHeaderRow::Column(Private::ColumnFlag)
						.DefaultLabel(LOCTEXT("UpdateFlagColumn", "Update"))
						.FixedWidth(75.f)
						.HAlignCell(EHorizontalAlignment::HAlign_Right)
					)
				]
			]
		]
	];

	RefreshNodes();
}

SSlateFrameSchematicView::~SSlateFrameSchematicView()
{
	if (TimingViewSession)
	{
		TimingViewSession->OnTimeMarkerChanged().RemoveAll(this);
		TimingViewSession->OnSelectionChanged().RemoveAll(this);
	}
}

void SSlateFrameSchematicView::SetSession(Insights::ITimingViewSession* InTimingViewSession, const Trace::IAnalysisSession* InAnalysisSession)
{
	if (TimingViewSession)
	{
		TimingViewSession->OnTimeMarkerChanged().RemoveAll(this);
		TimingViewSession->OnSelectionChanged().RemoveAll(this);
		TimingViewSession->OnSelectedEventChanged().RemoveAll(this);
	}

	TimingViewSession = InTimingViewSession;
	AnalysisSession = InAnalysisSession;

	if (InTimingViewSession)
	{

		InTimingViewSession->OnTimeMarkerChanged().AddSP(this, &SSlateFrameSchematicView::HandleTimeMarkerChanged);
		InTimingViewSession->OnSelectionChanged().AddSP(this, &SSlateFrameSchematicView::HandleSelectionChanged);
		TimingViewSession->OnSelectedEventChanged().AddSP(this, &SSlateFrameSchematicView::HandleSelectionEventChanged);
	}

	RefreshNodes();
}

TSharedRef<ITableRow> SSlateFrameSchematicView::HandleUniqueInvalidatedMakeTreeRowWidget(TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> InInfo, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(Private::FWidgetUniqueInvalidatedInfoRow,
		OwnerTable,
		InInfo,
		Private::GetWidgetName(AnalysisSession, InInfo->WidgetId));
}

void SSlateFrameSchematicView::HandleUniqueInvalidatedChildrenForInfo(TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> InInfo, TArray<TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>>& OutChildren)
{
	OutChildren = InInfo->Investigators;
}

TSharedRef<ITableRow> SSlateFrameSchematicView::HandleWidgetUpdateInfoGenerateWidget(TSharedPtr<Private::FWidgetUpdateInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(Private::FWidgetUpdateInfoRow,
		OwnerTable,
		Item,
		Private::GetWidgetName(AnalysisSession, Item->WidgetId));
}

void SSlateFrameSchematicView::HandleTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker)
{
	if (!FMath::IsNearlyEqual(StartTime, InTimeMarker) && !FMath::IsNearlyEqual(EndTime, InTimeMarker))
	{
		StartTime = InTimeMarker;
		EndTime = InTimeMarker;

		RefreshNodes();
	}
}

void SSlateFrameSchematicView::HandleSelectionChanged(Insights::ETimeChangedFlags InFlags, double InStartTime, double InEndTime)
{
	if (!FMath::IsNearlyEqual(StartTime, InStartTime) && !FMath::IsNearlyEqual(EndTime, InEndTime))
	{
		StartTime = InStartTime;
		EndTime = InEndTime;

		RefreshNodes();
	}
}

void SSlateFrameSchematicView::HandleSelectionEventChanged(const TSharedPtr<const ITimingEvent> InEvent)
{
	if (InEvent)
	{
		const double EventStartTime = InEvent->GetStartTime();
		const double EventEndTime = InEvent->GetEndTime();
		if (!FMath::IsNearlyEqual(StartTime, EventStartTime) && !FMath::IsNearlyEqual(EndTime, EventEndTime))
		{
			StartTime = EventStartTime;
			EndTime = EventEndTime;

			RefreshNodes();
		}
	}
}

TSharedPtr<SWidget> SSlateFrameSchematicView::HandleWidgetInvalidateListContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("GotoRootInvalidationWidget", "Go to root widget(s)"),
		LOCTEXT("GotoRootInvalidationWidgetTooltip", "Go to child widget that caused invalidation. Stops early if multiple widgets caused invalidation."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SSlateFrameSchematicView::HandleWidgetInvalidateListGotoRootWidget),
			FCanExecuteAction::CreateSP(this, &SSlateFrameSchematicView::CanWidgetInvalidateListGotoRootWidget)
		));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ViewScriptAndCallStack", "View script and call stack"),
		LOCTEXT("ViewScriptAndCallStackTooltip", "Open a window containing the script stack and the call stack. Script stack may be empty."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SSlateFrameSchematicView::HandleWidgetInvalidateListViewScriptAndCallStack),
			FCanExecuteAction::CreateSP(this, &SSlateFrameSchematicView::CanWidgetInvalidateListViewScriptAndCallStack)
		));

	return MenuBuilder.MakeWidget();
}

bool SSlateFrameSchematicView::CanWidgetInvalidateListGotoRootWidget()
{
	return WidgetInvalidateInfoListView->GetSelectedItems().Num() == 1;
}

void SSlateFrameSchematicView::HandleWidgetInvalidateListGotoRootWidget()
{
	TArray<TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>> SelectedItems = WidgetInvalidateInfoListView->GetSelectedItems();
	
	if (SelectedItems.Num() != 1)
	{
		return;
	}

	TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> SelectedItem = SelectedItems.Last();
	
	bool bExpandedItem = false;
	while (SelectedItem->Investigators.Num() == 1)
	{
		WidgetInvalidateInfoListView->SetItemExpansion(SelectedItem, true /** InShouldExpandItem */);
		SelectedItem = SelectedItem->Investigators.Last();
		bExpandedItem = true;
	}

	if (bExpandedItem)
	{
		WidgetInvalidateInfoListView->ClearSelection();
		WidgetInvalidateInfoListView->SetItemSelection(SelectedItem, true, ESelectInfo::Direct);
		WidgetInvalidateInfoListView->RequestNavigateToItem(SelectedItem);
	}
}

bool SSlateFrameSchematicView::CanWidgetInvalidateListViewScriptAndCallStack()
{
	if (WidgetInvalidateInfoListView->GetSelectedItems().Num() == 1)
	{
		TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> SelectedItem = WidgetInvalidateInfoListView->GetSelectedItems().Last();
		return !SelectedItem->Callstack.IsEmpty();
	}

	return false;
}

void SSlateFrameSchematicView::HandleWidgetInvalidateListViewScriptAndCallStack()
{
	if (WidgetInvalidateInfoListView->GetSelectedItems().Num() == 1)
	{
		TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> Info = WidgetInvalidateInfoListView->GetSelectedItems().Last();
		const FString TraceAndCallStackTipText = Info->ScriptTrace.IsEmpty() ? Info->Callstack : Info->ScriptTrace + "\n" + Info->Callstack;
		const FText WidgetName = Private::GetWidgetName(AnalysisSession, Info->WidgetId);

		/** Create the window to host our package dialog widget */
		TSharedRef< SWindow > ViewScriptAndCallstackWindow = SNew(SWindow)
		.Title(WidgetName)
		.ClientSize(FVector2D(1200, 600))
		.ActivationPolicy(EWindowActivationPolicy::Never)
		.IsInitiallyMaximized(false)
		[
				
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.OnMouseDoubleClick(this, &SSlateFrameSchematicView::OnMouseButtonDoubleClick)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(ScriptAndCallStackTextBox, SMultiLineEditableTextBox)
					.Style(FSlateInsightsStyle::Get(), "Callstack.TextBox")
					.Text(FText::AsCultureInvariant(TraceAndCallStackTipText))
					.SelectWordOnMouseDoubleClick(false)
				]
			]
		];

		/** Show the package dialog window as a modal window */
		FSlateApplication::Get().AddModalWindow(ViewScriptAndCallstackWindow, FSlateApplication::Get().GetActiveModalWindow());
	}
}

bool ExtractFilepathAndLineNumber(FString& PotentialFilePath, int32& LineNumber)
{
	// Extract filename and line number using regex	
#if PLATFORM_WINDOWS
	const FRegexPattern SourceCodeRegexPattern(TEXT("([a-zA-Z]:/[^:\\n\\r()]+(h|cpp)):([0-9]+)"));
	const int32 LineNumberCaptureGroupID = 3;
#else
	const FRegexPattern SourceCodeRegexPattern(TEXT("((//([^:/\\n]+[/])*)([^/]+)(h|cpp)):([0-9]+))"));
	const int32 LineNumberCaptureGroupID = 6;
#endif

	FRegexMatcher SourceCodeRegexMatcher(SourceCodeRegexPattern, PotentialFilePath);
	if (SourceCodeRegexMatcher.FindNext())
	{
		PotentialFilePath = SourceCodeRegexMatcher.GetCaptureGroup(1);
		LineNumber = FCString::Strtoi(*SourceCodeRegexMatcher.GetCaptureGroup(LineNumberCaptureGroupID), nullptr, 10);
		return true;
	}

	return false;
}

FReply SSlateFrameSchematicView::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// grab cursor location's line of text
		FString PotentialCodeFilePath;
		if (ScriptAndCallStackTextBox)
		{
			ScriptAndCallStackTextBox->GetCurrentTextLine(PotentialCodeFilePath);

			int32 LeftBrace = INDEX_NONE;
			int32 RightBrace = INDEX_NONE;
			if (PotentialCodeFilePath.FindChar('[', LeftBrace) && PotentialCodeFilePath.FindChar(']', RightBrace))
			{
				PotentialCodeFilePath.MidInline(LeftBrace + 1, RightBrace - LeftBrace - 1);

				// Extract potential .cpp./h files file path & line number
				int32 LineNumber = 0;
				PotentialCodeFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*PotentialCodeFilePath);
				if (ExtractFilepathAndLineNumber(PotentialCodeFilePath, LineNumber) && PotentialCodeFilePath.Len() && IFileManager::Get().FileSize(*PotentialCodeFilePath) != INDEX_NONE)
				{
					ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
					SourceCodeAccessModule.GetAccessor().OpenFileAtLine(PotentialCodeFilePath, LineNumber);
				}

				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

void SSlateFrameSchematicView::RefreshNodes()
{
	WidgetInvalidationInfos.Reset();
	WidgetUpdateInfos.Reset();

	if (TimingViewSession && AnalysisSession)
	{
		if (StartTime <= EndTime && EndTime >= 0.0)
		{
			const FSlateProvider* SlateProvider = AnalysisSession->ReadProvider<FSlateProvider>(FSlateProvider::ProviderName);
			if (SlateProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

				if (FMath::IsNearlyEqual(StartTime, EndTime))
				{
					// Find the Application and its delta time
					const FSlateProvider::TApplicationTickedTimeline& ApplicationTimeline = SlateProvider->GetApplicationTickedTimeline();
					FSlateProvider::FScopedEnumerateOutsideRange<FSlateProvider::TApplicationTickedTimeline> ScopedRange(ApplicationTimeline);

					ApplicationTimeline.EnumerateEvents(StartTime, EndTime,
						[this](double EventStartTime, double EventEndTime, uint32 /*Depth*/, const Message::FApplicationTickedMessage& Message)
						{
							this->StartTime = FMath::Min(StartTime, EventStartTime);
							this->EndTime = FMath::Max(EndTime, EventEndTime+Message.DeltaTime);
							return Trace::EEventEnumerate::Continue;
						});
				}

				RefreshNodes_Invalidation(SlateProvider);
				RefreshNodes_Update(SlateProvider);
			}
			else
			{
				FText InvalidText = LOCTEXT("Summary_NoSession", "No session selected");
				InvalidationSummary->SetText(InvalidText);
				UpdateSummary->SetText(InvalidText);
			}
		}
		else
		{
			FText InvalidText = LOCTEXT("Summary_NoSelection", "No frame selected");
			InvalidationSummary->SetText(InvalidText);
			UpdateSummary->SetText(InvalidText);
		}
	}
	else
	{
		FText InvalidText = LOCTEXT("Summary_NoSession", "No session selected");
		InvalidationSummary->SetText(InvalidText);
		UpdateSummary->SetText(InvalidText);
	}

	WidgetInvalidateInfoListView->RebuildList();
	WidgetUpdateInfoListView->RebuildList();
}

void SSlateFrameSchematicView::RefreshNodes_Invalidation(const FSlateProvider* SlateProvider)
{
	TMap<Message::FWidgetId, TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>> InvalidationMap;

	// Build a flat list of all the invalidation
	const FSlateProvider::TWidgetInvalidatedTimeline& InvalidatedTimeline = SlateProvider->GetWidgetInvalidatedTimeline();
	InvalidatedTimeline.EnumerateEvents(StartTime, EndTime,
		[&InvalidationMap](double EventStartTime, double EventEndTime, uint32 /*Depth*/, const Message::FWidgetInvalidatedMessage& Message)
		{
			TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> WidgetInfo;
			TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> InvestigatorInfo;

			if (TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>* FoundInfo = InvalidationMap.Find(Message.WidgetId))
			{
				WidgetInfo = *FoundInfo;
				WidgetInfo->Reason |= Message.InvalidationReason;
				++WidgetInfo->Count;
			}
			else
			{
				WidgetInfo = MakeShared<Private::FWidgetUniqueInvalidatedInfo>(Message.WidgetId, Message.InvalidationReason);
				InvalidationMap.Add(Message.WidgetId, WidgetInfo);
			}

			if (Message.InvestigatorId)
			{
				if (TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>* FoundInfo = InvalidationMap.Find(Message.InvestigatorId))
				{
					InvestigatorInfo = *FoundInfo;
				}
				else
				{
					InvestigatorInfo = MakeShared<Private::FWidgetUniqueInvalidatedInfo>(Message.InvestigatorId, EInvalidateWidgetReason::None);
					InvalidationMap.Add(Message.InvestigatorId, InvestigatorInfo);
				}
				InvestigatorInfo->bRoot = false;
				WidgetInfo->Investigators.Add(InvestigatorInfo);
			}

			if (!Message.ScriptTrace.IsEmpty())
			{
				WidgetInfo->ScriptTrace = Message.ScriptTrace;
			}

			if (!Message.Callstack.IsEmpty())
			{
				WidgetInfo->Callstack = Message.Callstack;
			}

			return Trace::EEventEnumerate::Continue;
		});

	for (const auto& Itt : InvalidationMap)
	{
		const TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>& Info = Itt.Value;
		if (Info->bRoot)
		{
			WidgetInvalidationInfos.Add(Info);
		}
	}
	InvalidationSummary->SetText(FText::Format(LOCTEXT("InvalidationSummary_Formated", "{0} widgets invalidated."), FText::AsNumber(WidgetInvalidationInfos.Num())));
}

void SSlateFrameSchematicView::RefreshNodes_Update(const FSlateProvider* SlateProvider)
{
	TMap<Message::FWidgetId, TSharedPtr<Private::FWidgetUpdateInfo>> WidgetUpdateInfosMap;
	const FSlateProvider::TWidgetUpdatedTimeline& UpdatedTimeline = SlateProvider->GetWidgetUpdatedTimeline();
	UpdatedTimeline.EnumerateEvents(StartTime, EndTime,
		[&WidgetUpdateInfosMap](double EventStartTime, double EventEndTime, uint32 /*Depth*/, const Message::FWidgetUpdatedMessage& Message)
		{
			if (TSharedPtr<Private::FWidgetUpdateInfo>* Info = WidgetUpdateInfosMap.Find(Message.WidgetId))
			{
				++((*Info)->Count);
				(*Info)->UpdateFlags |= Message.UpdateFlags;
			}
			else
			{
				WidgetUpdateInfosMap.Add(
					Message.WidgetId,
					MakeShared<Private::FWidgetUpdateInfo>(Message.WidgetId, Message.UpdateFlags));
			}
			return Trace::EEventEnumerate::Continue;
		});
	WidgetUpdateInfosMap.GenerateValueArray(WidgetUpdateInfos);

	UpdateSummary->SetText(FText::Format(LOCTEXT("UpdateSummary_Formated", "{0} widgets updated."), FText::AsNumber(WidgetUpdateInfos.Num())));
}

} //namespace SlateInsights
} //namespace UE

#undef LOCTEXT_NAMESPACE
