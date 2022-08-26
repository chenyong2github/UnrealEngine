// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlChangelists.h"

#include "Styling/AppStyle.h"
#include "Algo/Transform.h"
#include "Logging/MessageLog.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "ISourceControlWindowsModule.h"
#include "UncontrolledChangelistsModule.h"
#include "SourceControlOperations.h"
#include "ToolMenus.h"
#include "SSourceControlDescription.h"
#include "SourceControlWindows.h"
#include "SourceControlHelpers.h"
#include "SourceControlPreferences.h"
#include "SourceControlMenuContext.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Algo/AnyOf.h"
#include "HAL/PlatformTime.h"

#include "SSourceControlSubmit.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "SourceControlChangelist"

namespace
{

/** Wraps the execution of a changelist operations with a slow task. */
void ExecuteChangelistOperationWithSlowTaskWrapper(const FText& Message, const TFunction<void()>& ChangelistTask)
{
	FScopedSlowTask Progress(0.f, Message);
	Progress.MakeDialog();
	ChangelistTask();
}

/** Wraps the execution of an uncontrolled changelist operations with a slow task. */
void ExecuteUncontrolledChangelistOperationWithSlowTaskWrapper(const FText& Message, const TFunction<void()>& UncontrolledChangelistTask)
{
	ExecuteChangelistOperationWithSlowTaskWrapper(Message, UncontrolledChangelistTask);
}

/** Displays toast notification to report the status of task. */
void DisplaySourceControlOperationNotification(const FText& Message, SNotificationItem::ECompletionState CompletionState)
{
	if (Message.IsEmpty())
	{
		return;
	}

	FNotificationInfo NotificationInfo(Message);
	NotificationInfo.ExpireDuration = 6.0f;
	NotificationInfo.Hyperlink = FSimpleDelegate::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); });
	NotificationInfo.HyperlinkText = LOCTEXT("ShowOutputLogHyperlink", "Show Output Log");
	FSlateNotificationManager::Get().AddNotification(NotificationInfo)->SetCompletionState(CompletionState);
}

/** Returns true if a source control provider is enable and support changeslists. */
bool AreControlledChangelistsEnabled()
{
	return ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().UsesChangelists();
};

/** Returns true if Uncontrolled changelists are enabled. */
bool AreUncontrolledChangelistsEnabled()
{
	return FUncontrolledChangelistsModule::Get().IsEnabled();
};

/** Returns true if there are changelists to display. */
bool AreChangelistsEnabled()
{
	return AreControlledChangelistsEnabled() || AreUncontrolledChangelistsEnabled();
};

/**
 * Returns a new changelist description if needed, appending validation tag.
 * 
 * @param bInValidationResult	The result of the validation step
 * @param InOriginalChangelistDescription	Description of the changelist before modification
 * 
 * @return The new changelist description
 */
FText UpdateChangelistDescriptionToSubmitIfNeeded(const bool bInValidationResult, const FText& InChangelistDescription)
{
	auto GetChangelistValidationTag = []
	{
		return LOCTEXT("ValidationTag", "#changelist validated");
	};

	auto ContainsValidationFlag = [&GetChangelistValidationTag](const FText& InChangelistDescription)
	{
		FString DescriptionString = InChangelistDescription.ToString();
		FString ValidationString = GetChangelistValidationTag().ToString();
		return DescriptionString.Find(ValidationString) != INDEX_NONE;
	};

	if (bInValidationResult && USourceControlPreferences::IsValidationTagEnabled() && !ContainsValidationFlag(InChangelistDescription))
	{
		FStringOutputDevice Str;

		Str.SetAutoEmitLineTerminator(true);
		Str.Log(InChangelistDescription);
		Str.Log(GetChangelistValidationTag());

		return FText::FromString(Str);
	}

	return InChangelistDescription;
}

} // Anonymous namespace

/** Implements drag and drop operation. */
struct FSCCFileDragDropOp : public FDragDropOperation
{
	DRAG_DROP_OPERATOR_TYPE(FSCCFileDragDropOp, FDragDropOperation);

	using FDragDropOperation::Construct;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		FSourceControlStateRef FileState = Files.IsEmpty() ? UncontrolledFiles[0] : Files[0];

		return SSourceControlCommon::GetSCCFileWidget(MoveTemp(FileState));
	}

	TArray<FSourceControlStateRef> Files;
	TArray<FSourceControlStateRef> UncontrolledFiles;
};


DECLARE_DELEGATE(FOnSearchBoxExpanded)

/** A button that expands a search box below itself when clicked. */
class SExpandableSearchButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SExpandableSearchButton)
		: _Style(&FAppStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
	{}
		/** Search box style (used to match the glass icon) */
		SLATE_STYLE_ARGUMENT(FSearchBoxStyle, Style)

		/** Event fired when the associated search box is made visible */
		SLATE_EVENT(FOnSearchBoxExpanded, OnSearchBoxExpanded)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<SSearchBox> SearchBox)
	{
		OnSearchBoxExpanded = InArgs._OnSearchBoxExpanded;
		SearchStyle = InArgs._Style;

		SearchBox->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SExpandableSearchButton::GetSearchBoxVisibility));
		SearchBoxPtr = SearchBox;

		ChildSlot
		[
			SNew(SCheckBox)
			.IsChecked(this, &SExpandableSearchButton::GetToggleButtonState)
			.OnCheckStateChanged(this, &SExpandableSearchButton::OnToggleButtonStateChanged)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.Padding(4.0f)
			.ToolTipText(NSLOCTEXT("ExpandableSearchArea", "ExpandCollapseSearchButton", "Expands or collapses the search text box"))
			[
				SNew(SImage)
				.Image(&SearchStyle->GlassImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
	}

private:
	/** Sets whether or not the search area is expanded to expose the search box */
	void OnToggleButtonStateChanged(ECheckBoxState CheckBoxState)
	{
		bIsExpanded = CheckBoxState == ECheckBoxState::Checked;

		if (TSharedPtr<SSearchBox> SearchBox = SearchBoxPtr.Pin())
		{
			if (bIsExpanded)
			{
				OnSearchBoxExpanded.ExecuteIfBound();

				// Focus the search box when it's shown
				FSlateApplication::Get().SetUserFocus(FSlateApplication::Get().GetUserIndexForKeyboard(), SearchBox, EFocusCause::SetDirectly);
			}
			else
			{
				// Clear the search box when it's hidden
				SearchBox->SetText(FText::GetEmpty());
			}
		}
	}

	ECheckBoxState GetToggleButtonState() const { return bIsExpanded ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }
	EVisibility GetSearchBoxVisibility() const { return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed; }

private:
	const FSearchBoxStyle* SearchStyle;
	FOnSearchBoxExpanded OnSearchBoxExpanded;
	TWeakPtr<SSearchBox> SearchBoxPtr;
	bool bIsExpanded = false;
};

/** An expanded area to contain the changelists tree view or then uncontrolled changelists tree view. */
class SExpandableChangelistArea : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SExpandableChangelistArea)
		: _Style(&FAppStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
		, _HeaderText()
		, _ChangelistView()
		, _OnSearchBoxExpanded()
		, _OnNewChangelist()
		, _OnNewChangelistTooltip()
		, _NewButtonVisibility(EVisibility::Visible)
		, _SearchButtonVisibility(EVisibility::Visible)
	{}
		/** Search box style (used to match the glass icon) */
		SLATE_STYLE_ARGUMENT(FSearchBoxStyle, Style)
		/** Text displayed on the expandable area */
		SLATE_ATTRIBUTE(FText, HeaderText)
		/** The tree element displayed as body. */
		SLATE_ARGUMENT(TSharedPtr<SChangelistTree>, ChangelistView)
		/** Event fired when the associated search box is made visible */
		SLATE_EVENT(FOnSearchBoxExpanded, OnSearchBoxExpanded)
		/** Event fired when the 'plus' button is clicked. */
		SLATE_EVENT(FOnClicked, OnNewChangelist)
		/** Tooltip displayed over the 'plus' button. */
		SLATE_ATTRIBUTE(FText, OnNewChangelistTooltip)
		/** Make the 'plus' button visible or not. */
		SLATE_ARGUMENT(EVisibility, NewButtonVisibility)
		/** Make the 'search' button visible or not. */
		SLATE_ARGUMENT(EVisibility, SearchButtonVisibility)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SearchBox = SNew(SSearchBox);

		ChildSlot
		[
			SAssignNew(ExpandableArea, SExpandableArea)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
			.BodyBorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			.HeaderPadding(FMargin(4.0f, 3.0f))
			.AllowAnimatedTransition(false)
			.HeaderContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._HeaderText)
					.TextStyle(FAppStyle::Get(), "ButtonText")
					.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(InArgs._OnNewChangelistTooltip)
					.OnClicked(InArgs._OnNewChangelist)
					.ContentPadding(FMargin(1, 0))
					.Visibility(InArgs._NewButtonVisibility)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility(InArgs._SearchButtonVisibility)
					[
						SNew(SExpandableSearchButton, SearchBox.ToSharedRef())
					]
				]
			]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					// Should blend in visually with the header but technically acts like part of the body
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
					.Padding(FMargin(4.0f, 2.0f))
					[
						SearchBox.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
					[
						InArgs._ChangelistView.ToSharedRef()
					]
				]
			]
		];
	}

	bool IsExpanded() const { return ExpandableArea->IsExpanded(); }

private:
	TSharedPtr<SExpandableArea> ExpandableArea;
	TSharedPtr<SSearchBox> SearchBox;
};


void SSourceControlChangelistsWidget::Construct(const FArguments& InArgs)
{
	// Register delegates
	ISourceControlModule& SCCModule = ISourceControlModule::Get();
	FUncontrolledChangelistsModule& UncontrolledChangelistModule = FUncontrolledChangelistsModule::Get();

	SCCModule.RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlProviderChanged));
	SourceControlStateChangedDelegateHandle = SCCModule.GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged));
	UncontrolledChangelistModule.OnUncontrolledChangelistModuleChanged.AddSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged);

	ChangelistTreeView = CreateChangelistTreeView(ChangelistTreeNodes);
	UncontrolledChangelistTreeView = CreateChangelistTreeView(UncontrolledChangelistTreeNodes);
	FileTreeView = CreateChangelistFilesView();

	ChangelistExpandableArea = SNew(SExpandableChangelistArea)
		.HeaderText_Lambda([this]() { return FText::Format(LOCTEXT("SourceControl_ChangeLists", "Changelists ({0})"), ChangelistTreeNodes.Num()); })
		.ChangelistView(ChangelistTreeView.ToSharedRef())
		.OnNewChangelist_Lambda([this](){ OnNewChangelist(); return FReply::Handled(); })
		.OnNewChangelistTooltip(LOCTEXT("Create_New_Changelist", "Create a new changelist."))
		.SearchButtonVisibility(EVisibility::Collapsed); // Functionality is planned but not fully implemented yet.

	UncontrolledChangelistExpandableArea = SNew(SExpandableChangelistArea)
		.HeaderText_Lambda([this]() { return FText::Format(LOCTEXT("SourceControl_UncontrolledChangeLists", "Uncontrolled Changelists ({0})"), UncontrolledChangelistTreeNodes.Num()); })
		.ChangelistView(UncontrolledChangelistTreeView.ToSharedRef())
		.NewButtonVisibility(EVisibility::Collapsed) // Functionality is planned but not implemented yet.
		.OnNewChangelistTooltip(LOCTEXT("Create_New_Uncontrolled_Changelist", "Create a new uncontrolled changelist."))
		.SearchButtonVisibility(EVisibility::Collapsed); // Functionality is planned but not fully implemented yet.

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot() // For the toolbar (Refresh button)
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					MakeToolBar()
				]
			]
		]
		+SVerticalBox::Slot() // Everything below the tools bar: changelist expandable areas + files views + status bar at the bottom
		[
			SNew(SOverlay)
			+SOverlay::Slot()
			[
				SNew(SBox)
				.Visibility_Lambda([](){ return !AreChangelistsEnabled() ? EVisibility::Visible: EVisibility::Collapsed; })
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SourceControl_Disabled", "The source control is disabled or it doesn't support changelists."))
				]
			]
			+SOverlay::Slot()
			[
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Horizontal)
				.ResizeMode(ESplitterResizeMode::FixedPosition)
				.Visibility_Lambda([]() { return AreChangelistsEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })

				// Left slot: Changelists and uncontrolled changelists areas
				+SSplitter::Slot()
				.Resizable(true)
				.Value(0.30)
				[
					SNew(SOverlay) // Visible when both Controlled and Uncontrolled changelists are enabled (Need to add a splitter)
					+SOverlay::Slot()
					[
						SNew(SSplitter)
						.Orientation(EOrientation::Orient_Vertical)
						.Visibility_Lambda([]() { return AreControlledChangelistsEnabled() && AreUncontrolledChangelistsEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
				
						// Top slot: Changelists
						+SSplitter::Slot()
						.SizeRule_Lambda([this](){ return ChangelistExpandableArea->IsExpanded() ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; })
						.Value(0.7)
						[
							ChangelistExpandableArea.ToSharedRef()
						]

						// Bottom slot: Uncontrolled Changelists
						+SSplitter::Slot()
						.SizeRule_Lambda([this](){ return UncontrolledChangelistExpandableArea->IsExpanded() ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; })
						.Value(0.3)
						[
							UncontrolledChangelistExpandableArea.ToSharedRef()
						]
					]
					+SOverlay::Slot() // Visibile when controlled changelists are enabled but not the uncontrolled ones.
					[
						SNew(SBox)
						.Visibility_Lambda([](){ return AreControlledChangelistsEnabled() && !AreUncontrolledChangelistsEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							ChangelistExpandableArea.ToSharedRef()
						]
					]
					+SOverlay::Slot() // Visible when uncontrolled changelist are enabled, but not the controlled ones.
					[
						SNew(SBox)
						.Visibility_Lambda([](){ return !AreControlledChangelistsEnabled() && AreUncontrolledChangelistsEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							UncontrolledChangelistExpandableArea.ToSharedRef()
						]
					]
				]

				// Right slot: Files associated to the selected the changelist/uncontrolled changelist.
				+SSplitter::Slot()
				.Resizable(true)
				[
					SNew(SScrollBorder, FileTreeView.ToSharedRef())
					[
						FileTreeView.ToSharedRef()
					]
				]
			]
		]
		+SVerticalBox::Slot() // Status bar (Always visible if uncontrolled changelist are enabled to keep the reconcile status visible at all time)
		.AutoHeight()
		[
			SNew(SBox)
			.Padding(0, 3)
			.Visibility_Lambda([this](){ return FUncontrolledChangelistsModule::Get().IsEnabled() || !RefreshStatus.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return RefreshStatus; })
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([]() { return FUncontrolledChangelistsModule::Get().GetReconcileStatus(); })
					.Visibility_Lambda([]() { return FUncontrolledChangelistsModule::Get().IsEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
				]
			]
		]
	];

	bShouldRefresh = true;
}

TSharedRef<SWidget> SSourceControlChangelistsWidget::MakeToolBar()
{
	FSlimHorizontalToolBarBuilder ToolBarBuilder(nullptr, FMultiBoxCustomization::None);

	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this]() { RequestRefresh(); })),
			NAME_None,
			LOCTEXT("SourceControl_RefreshButton", "Refresh"),
			LOCTEXT("SourceControl_RefreshButton_Tooltip", "Refreshes changelists from source control provider."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Refresh"));

	return ToolBarBuilder.MakeWidget();
}

void SSourceControlChangelistsWidget::EditChangelistDescription(const FText& InNewChangelistDescription, const FSourceControlChangelistStatePtr& InChangelistState)
{
	TSharedRef<FEditChangelist> EditChangelistOperation = ISourceControlOperation::Create<FEditChangelist>();
	EditChangelistOperation->SetDescription(InNewChangelistDescription);
	Execute(LOCTEXT("Updating_Changelist_Description", "Updating changelist description..."), EditChangelistOperation, InChangelistState->GetChangelist(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
		{
			if (InResult == ECommandResult::Succeeded)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Update_Changelist_Description_Succeeded", "Changelist description successfully updated."), SNotificationItem::CS_Success);
			}
			else if (InResult == ECommandResult::Failed)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Update_Changelist_Description_Failed", "Failed to update changelist description."), SNotificationItem::CS_Fail);
			}
		}));
}

void SSourceControlChangelistsWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Detect transitions of the source control being available/unavailable. Ex: When the user changes the source control in UI, the provider gets selected,
	// but it is not connected/available until the user accepts the settings. The source control doesn't have callback for availability and we want to refresh everything
	// once it gets available.
	if (ISourceControlModule::Get().IsEnabled() && !bSourceControlAvailable && ISourceControlModule::Get().GetProvider().IsAvailable())
	{
		bSourceControlAvailable = true;
		bShouldRefresh = true;
	}

	if (bShouldRefresh)
	{
		if (ISourceControlModule::Get().IsEnabled() || FUncontrolledChangelistsModule::Get().IsEnabled())
		{
			RequestRefresh();
		}
		else
		{
			// No provider available, clear changelist tree
			ClearChangelistsTree();
		}

		bShouldRefresh = false;
	}
	
	if (bIsRefreshing)
	{
		TickRefreshStatus(InDeltaTime);
	}
}

void SSourceControlChangelistsWidget::RequestRefresh()
{
	bool bAnyProviderAvailable = false;

	if (ISourceControlModule::Get().IsEnabled())
	{
		bAnyProviderAvailable = true;
		StartRefreshStatus();

		TSharedRef<FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
		UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);
		UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);
		UpdatePendingChangelistsOperation->SetUpdateShelvedFilesStates(true);

		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		SourceControlProvider.Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateSP(this, &SSourceControlChangelistsWidget::OnChangelistsStatusUpdated));
		OnStartSourceControlOperation(UpdatePendingChangelistsOperation, LOCTEXT("SourceControl_UpdatingChangelist", "Updating changelists..."));
	}

	if (FUncontrolledChangelistsModule::Get().IsEnabled())
	{
		bAnyProviderAvailable = true;

		// This operation is synchronous and completes right away.
		FUncontrolledChangelistsModule::Get().UpdateStatus();
	}

	if (!bAnyProviderAvailable)
	{
		// No provider available, clear changelist tree
		ClearChangelistsTree();
	}
}

void SSourceControlChangelistsWidget::StartRefreshStatus()
{
	bIsRefreshing = true;
	RefreshStatusStartSecs = FPlatformTime::Seconds();
}

void SSourceControlChangelistsWidget::TickRefreshStatus(double InDeltaTime)
{
	int32 RefreshStatusTimeElapsed = static_cast<int32>(FPlatformTime::Seconds() - RefreshStatusStartSecs);
	RefreshStatus = FText::Format(LOCTEXT("SourceControl_RefreshStatus", "Refreshing changelists... ({0} s)"), FText::AsNumber(RefreshStatusTimeElapsed));
}

void SSourceControlChangelistsWidget::EndRefreshStatus()
{
	bIsRefreshing = false;
}

void SSourceControlChangelistsWidget::ClearChangelistsTree()
{
	if (!ChangelistTreeNodes.IsEmpty() || !UncontrolledChangelistTreeNodes.IsEmpty())
	{
		ChangelistTreeNodes.Reset();
		UncontrolledChangelistTreeNodes.Reset();
		ChangelistTreeView->RequestTreeRefresh();
		UncontrolledChangelistTreeView->RequestTreeRefresh();
	}

	if (!FileTreeNodes.IsEmpty())
	{
		FileTreeNodes.Reset();
		FileTreeView->RequestTreeRefresh();
	}
}

void SSourceControlChangelistsWidget::OnRefresh()
{
	if (!AreChangelistsEnabled())
	{
		ClearChangelistsTree();
		return;
	}

	// Views will be teared down and rebuilt from scratch, save the items that are expanded and/or selected to be able to restore those states after the rebuild.
	FExpandedAndSelectionStates ExpandedAndSelectedStates;
	SaveExpandedAndSelectionStates(ExpandedAndSelectedStates);

	// Query the source control
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	FUncontrolledChangelistsModule& UncontrolledChangelistModule = FUncontrolledChangelistsModule::Get();
	TArray<FSourceControlChangelistRef> Changelists = SourceControlProvider.GetChangelists(EStateCacheUsage::Use);
	TArray<TSharedRef<FUncontrolledChangelistState>> UncontrolledChangelistStates = UncontrolledChangelistModule.GetChangelistStates();

	TArray<FSourceControlChangelistStateRef> ChangelistsStates;
	SourceControlProvider.GetState(Changelists, ChangelistsStates, EStateCacheUsage::Use);

	// Count number of steps for slow task...
	int32 ElementsToProcess = ChangelistsStates.Num();
	ElementsToProcess += UncontrolledChangelistStates.Num();

	for (const TSharedRef<ISourceControlChangelistState>& ChangelistState : ChangelistsStates)
	{
		ElementsToProcess += ChangelistState->GetFilesStates().Num();
		ElementsToProcess += ChangelistState->GetShelvedFilesStates().Num();
	}

	for (const TSharedRef<FUncontrolledChangelistState>& UncontrolledChangelistState : UncontrolledChangelistStates)
	{
		ElementsToProcess += UncontrolledChangelistState->GetFilesStates().Num();
		ElementsToProcess += UncontrolledChangelistState->GetOfflineFiles().Num();
	}

	FScopedSlowTask SlowTask(ElementsToProcess, LOCTEXT("SourceControl_RebuildTree", "Refreshing Tree Items"));
	SlowTask.MakeDialogDelayed(1.5f, /*bShowCancelButton=*/true);

	// Rebuild the tree data models
	bool bBeautifyPaths = true;
	ChangelistTreeNodes.Reset(ChangelistsStates.Num());
	UncontrolledChangelistTreeNodes.Reset(UncontrolledChangelistStates.Num());
	FileTreeNodes.Reset();

	for (const TSharedRef<ISourceControlChangelistState>& ChangelistState : ChangelistsStates)
	{
		// Add a changelist.
		TSharedRef<IChangelistTreeItem> ChangelistNode = MakeShared<FChangelistTreeItem>(ChangelistState);
		ChangelistTreeNodes.Add(ChangelistNode);

		for (const TSharedRef<ISourceControlState>& FileState : ChangelistState->GetFilesStates())
		{
			ChangelistNode->AddChild(MakeShared<FFileTreeItem>(FileState, bBeautifyPaths));
			SlowTask.EnterProgressFrame();
			bBeautifyPaths &= !SlowTask.ShouldCancel();
		}

		if (ChangelistState->GetShelvedFilesStates().Num() > 0)
		{
			// Add a shelved files node under the changelist.
			TSharedRef<IChangelistTreeItem> ShelvedFilesNode = MakeShared<FShelvedChangelistTreeItem>();
			ChangelistNode->AddChild(ShelvedFilesNode);

			for (const TSharedRef<ISourceControlState>& ShelvedFileState : ChangelistState->GetShelvedFilesStates())
			{
				ShelvedFilesNode->AddChild(MakeShared<FShelvedFileTreeItem>(ShelvedFileState, bBeautifyPaths));
				SlowTask.EnterProgressFrame();
				bBeautifyPaths &= !SlowTask.ShouldCancel();
			}
		}

		SlowTask.EnterProgressFrame();
		bBeautifyPaths &= !SlowTask.ShouldCancel();
	}

	for (const TSharedRef<FUncontrolledChangelistState>& UncontrolledChangelistState : UncontrolledChangelistStates)
	{
		// Add an uncontrolled changelist.
		TSharedRef<IChangelistTreeItem> UncontrolledChangelistNode = MakeShared<FUncontrolledChangelistTreeItem>(UncontrolledChangelistState);
		UncontrolledChangelistTreeNodes.Add(UncontrolledChangelistNode);

		for (const TSharedRef<ISourceControlState>& FileState : UncontrolledChangelistState->GetFilesStates())
		{
			UncontrolledChangelistNode->AddChild(MakeShared<FFileTreeItem>(FileState, bBeautifyPaths));
			SlowTask.EnterProgressFrame();
			bBeautifyPaths &= !SlowTask.ShouldCancel();
		}

		for (const FString& Filename : UncontrolledChangelistState->GetOfflineFiles())
		{
			UncontrolledChangelistNode->AddChild(MakeShared<FOfflineFileTreeItem>(Filename));
			SlowTask.EnterProgressFrame();
			bBeautifyPaths &= !SlowTask.ShouldCancel();
		}

		SlowTask.EnterProgressFrame();
		bBeautifyPaths &= !SlowTask.ShouldCancel();
	}

	// Views were rebuilt from scratch, try expanding and selecting the nodes that were in that state before the update.
	RestoreExpandedAndSelectionStates(ExpandedAndSelectedStates);

	ChangelistTreeView->RequestTreeRefresh();
	UncontrolledChangelistTreeView->RequestTreeRefresh();
	FileTreeView->RequestTreeRefresh();
}

void SSourceControlChangelistsWidget::OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
	SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged));

	bSourceControlAvailable = NewProvider.IsAvailable(); // Check if it is connected.
	bShouldRefresh = true;
}

void SSourceControlChangelistsWidget::OnSourceControlStateChanged()
{
	// NOTE: No need to call RequestRefresh() to force the SCC to update internal states. We are being invoked because it was update, we just
	//       need to update the UI to reflect those state changes.
	OnRefresh();
}

void SSourceControlChangelistsWidget::OnChangelistsStatusUpdated(const TSharedRef<ISourceControlOperation>& InOperation, ECommandResult::Type InType)
{
	// NOTE: This is invoked when the 'FUpdatePendingChangelistsStatus' completes. No need to refresh the tree views because OnSourceControlStateChanged() is also called.
	OnEndSourceControlOperation(InOperation, InType);
	EndRefreshStatus(); // TODO PL: Need to uniformize all operations status update. The 'Status Update' is different as it displays the time it takes.
}

void SChangelistTree::Private_SetItemSelection(FChangelistTreeItemPtr TheItem, bool bShouldBeSelected, bool bWasUserDirected)
{
	bool bAllowSelectionChange = true;

	if (bShouldBeSelected && !SelectedItems.IsEmpty())
	{
		// Prevent selecting changelists and files at the same time.
		FChangelistTreeItemPtr CurrentlySelectedItem = (*SelectedItems.begin());
		if (TheItem->GetTreeItemType() != CurrentlySelectedItem->GetTreeItemType())
		{
			bAllowSelectionChange = false;
		}
		// Prevent selecting items that don't share the same root
		else if (TheItem->GetParent() != CurrentlySelectedItem->GetParent())
		{
			bAllowSelectionChange = false;
		}
	}

	if (bAllowSelectionChange)
	{
		STreeView::Private_SetItemSelection(TheItem, bShouldBeSelected, bWasUserDirected);
	}
}

FSourceControlChangelistStatePtr SSourceControlChangelistsWidget::GetCurrentChangelistState()
{
	if (!ChangelistTreeView)
	{
		return nullptr;
	}

	TArray<TSharedPtr<IChangelistTreeItem>> SelectedItems = ChangelistTreeView->GetSelectedItems();
	if (SelectedItems.Num() != 1 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::Changelist)
	{
		return nullptr;
	}

	return StaticCastSharedPtr<FChangelistTreeItem>(SelectedItems[0])->ChangelistState;
}

FUncontrolledChangelistStatePtr SSourceControlChangelistsWidget::GetCurrentUncontrolledChangelistState()
{
	if (!UncontrolledChangelistTreeView)
	{
		return nullptr;
	}

	TArray<TSharedPtr<IChangelistTreeItem>> SelectedItems = UncontrolledChangelistTreeView->GetSelectedItems();
	if (SelectedItems.Num() != 1 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::UncontrolledChangelist)
	{
		return nullptr;
	}

	return StaticCastSharedPtr<FUncontrolledChangelistTreeItem>(SelectedItems[0])->UncontrolledChangelistState;
}

FSourceControlChangelistPtr SSourceControlChangelistsWidget::GetCurrentChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();
	return ChangelistState ? (FSourceControlChangelistPtr)(ChangelistState->GetChangelist()) : nullptr;
}

FSourceControlChangelistStatePtr SSourceControlChangelistsWidget::GetChangelistStateFromSelection()
{
	TArray<FChangelistTreeItemPtr> SelectedItems = ChangelistTreeView->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		return nullptr;
	}

	FChangelistTreeItemPtr Item = SelectedItems[0];
	while (Item)
	{
		if (Item->GetTreeItemType() == IChangelistTreeItem::Changelist)
		{
			return StaticCastSharedPtr<FChangelistTreeItem>(Item)->ChangelistState;
		}
		Item = Item->GetParent();
	}

	return nullptr;
}

FSourceControlChangelistPtr SSourceControlChangelistsWidget::GetChangelistFromSelection()
{
	FSourceControlChangelistStatePtr ChangelistState = GetChangelistStateFromSelection();
	return ChangelistState ? (FSourceControlChangelistPtr)(ChangelistState->GetChangelist()) : nullptr;
}

TArray<FString> SSourceControlChangelistsWidget::GetSelectedFiles()
{
	TArray<FChangelistTreeItemPtr> SelectedItems = FileTreeView->GetSelectedItems();
	TArray<FString> Files;

	for (const TSharedPtr<IChangelistTreeItem>& Item : SelectedItems)
	{
		if (Item->GetTreeItemType() == IChangelistTreeItem::File)
		{
			Files.Add(StaticCastSharedPtr<FFileTreeItem>(Item)->FileState->GetFilename());
		}
	}

	return Files;
}

void SSourceControlChangelistsWidget::GetSelectedFiles(TArray<FString>& OutControlledFiles, TArray<FString>& OutUncontrolledFiles)
{
	TArray<FChangelistTreeItemPtr> SelectedItems = FileTreeView->GetSelectedItems();

	for (const TSharedPtr<IChangelistTreeItem>& Item : SelectedItems)
	{
		if (Item->GetTreeItemType() == IChangelistTreeItem::File)
		{
			if (TSharedPtr<IChangelistTreeItem> Parent = Item->GetParent())
			{
				const FString& Filename = StaticCastSharedPtr<FFileTreeItem>(Item)->FileState->GetFilename();

				if (Parent->GetTreeItemType() == IChangelistTreeItem::Changelist)
				{
					OutControlledFiles.Add(Filename);
				}
				else if (Parent->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
				{
					OutUncontrolledFiles.Add(Filename);
				}
			}
		}
		else if (Item->GetTreeItemType() == IChangelistTreeItem::OfflineFile)
		{
			if (TSharedPtr<IChangelistTreeItem> Parent = Item->GetParent())
			{
				if (Parent->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
				{
					const FString& Filename = StaticCastSharedPtr<FOfflineFileTreeItem>(Item)->GetFilename();
					OutUncontrolledFiles.Add(Filename);
				}
			}
		}
	}
}

void SSourceControlChangelistsWidget::GetSelectedFileStates(TArray<FSourceControlStateRef>& OutControlledFileStates, TArray<FSourceControlStateRef>& OutUncontrolledFileStates)
{
	TArray<TSharedPtr<IChangelistTreeItem>> SelectedItems = FileTreeView->GetSelectedItems();

	for (const TSharedPtr<IChangelistTreeItem>& Item : SelectedItems)
	{
		if (Item->GetTreeItemType() != IChangelistTreeItem::File)
		{
			continue;
		}

		if (const TSharedPtr<IChangelistTreeItem>& Parent = Item->GetParent())
		{
			if (Parent->GetTreeItemType() == IChangelistTreeItem::Changelist)
			{
				OutControlledFileStates.Add(StaticCastSharedPtr<FFileTreeItem>(Item)->FileState);
			}
			else if (Parent->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
			{
				OutUncontrolledFileStates.Add(StaticCastSharedPtr<FFileTreeItem>(Item)->FileState);
			}
		}
	}
}

TArray<FString> SSourceControlChangelistsWidget::GetSelectedShelvedFiles()
{
	TArray<FString> ShelvedFiles;

	for (const TSharedPtr<IChangelistTreeItem>& Item : FileTreeView->GetSelectedItems())
	{
		if (Item->GetTreeItemType() == IChangelistTreeItem::ShelvedFile)
		{
			ShelvedFiles.Add(StaticCastSharedPtr<FShelvedFileTreeItem>(Item)->FileState->GetFilename());
		}
	}

	// No individual 'shelved file' selected?
	if (ShelvedFiles.IsEmpty())
	{
		// Check if the user selected the 'Shelved Files' changelist.
		for (const TSharedPtr<IChangelistTreeItem>& Item : ChangelistTreeView->GetSelectedItems())
		{
			if (Item->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
			{
				// Add all items of the 'Shelved Files' changelist.
				for (const TSharedPtr<IChangelistTreeItem>& Children : Item->GetChildren())
				{
					if (Children->GetTreeItemType() == IChangelistTreeItem::ShelvedFile)
					{
						ShelvedFiles.Add(StaticCastSharedPtr<FShelvedFileTreeItem>(Children)->FileState->GetFilename());
					}
				}

				break; // UI only allows to select one changelist at the time.
			}
		}
	}

	return ShelvedFiles;
}

void SSourceControlChangelistsWidget::Execute(const FText& Message, const TSharedRef<ISourceControlOperation>& InOperation, const EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	return Execute(Message, InOperation, nullptr, TArray<FString>(), InConcurrency, InOperationCompleteDelegate);
}

void SSourceControlChangelistsWidget::Execute(const FText& Message, const TSharedRef<ISourceControlOperation>& InOperation, TSharedPtr<ISourceControlChangelist> InChangelist, const EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	return Execute(Message, InOperation, MoveTemp(InChangelist), TArray<FString>(), InConcurrency, InOperationCompleteDelegate);
}

void SSourceControlChangelistsWidget::Execute(const FText& Message, const TSharedRef<ISourceControlOperation>& InOperation, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	return Execute(Message, InOperation, nullptr, InFiles, InConcurrency, InOperationCompleteDelegate);
}

void SSourceControlChangelistsWidget::Execute(const FText& Message, const TSharedRef<ISourceControlOperation>& InOperation, TSharedPtr<ISourceControlChangelist> InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	// Start the operation.
	OnStartSourceControlOperation(InOperation, Message);

	if (InConcurrency == EConcurrency::Asynchronous)
	{
		// Pass a weak ptr to the lambda to protect in case the 'this' widget is closed/destroyed before the source control operation completes.
		TWeakPtr<SSourceControlChangelistsWidget> ThisWeak(StaticCastSharedRef<SSourceControlChangelistsWidget>(AsShared()));

		SourceControlProvider.Execute(InOperation, MoveTemp(InChangelist), InFiles, InConcurrency, FSourceControlOperationComplete::CreateLambda(
			[ThisWeak, InOperationCompleteDelegate](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
			{
				if (TSharedPtr<SSourceControlChangelistsWidget> ThisPtr = ThisWeak.Pin())
				{
					InOperationCompleteDelegate.ExecuteIfBound(Operation, InResult);
					ThisPtr->OnEndSourceControlOperation(Operation, InResult);
				}
			}));
	}
	else
	{
		FScopedSlowTask Progress(0.f, Message);
		Progress.MakeDialog();
		ECommandResult::Type Result = SourceControlProvider.Execute(InOperation, InChangelist, InFiles, InConcurrency, InOperationCompleteDelegate);
		OnEndSourceControlOperation(InOperation, Result);
	}
}

void SSourceControlChangelistsWidget::ExecuteUncontrolledChangelistOperation(const FText& Message, const TFunction<void()>& UncontrolledOperation)
{
	ExecuteUncontrolledChangelistOperationWithSlowTaskWrapper(Message, UncontrolledOperation);
}

void SSourceControlChangelistsWidget::OnStartSourceControlOperation(TSharedRef<ISourceControlOperation> Operation, const FText& Message)
{
	RefreshStatus = Message; // TODO: Should have a queue to stack async operations going on to correctly display concurrent async operations.
}

void SSourceControlChangelistsWidget::OnEndSourceControlOperation(const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InType)
{
	RefreshStatus = FText::GetEmpty(); // TODO: Should have a queue to stack async operations going on to correctly display concurrent async operations.
}

void SSourceControlChangelistsWidget::OnNewChangelist()
{
	FText ChangelistDescription;
	bool bOk = GetChangelistDescription(
		nullptr,
		LOCTEXT("SourceControl.Changelist.New.Title", "New Changelist..."),
		LOCTEXT("SourceControl.Changelist.New.Label", "Enter a description for the changelist:"),
		ChangelistDescription);

	if (!bOk)
	{
		return;
	}

	TSharedRef<FNewChangelist> NewChangelistOperation = ISourceControlOperation::Create<FNewChangelist>();
	NewChangelistOperation->SetDescription(ChangelistDescription);
	Execute(LOCTEXT("Creating_Changelist", "Creating changelist..."), NewChangelistOperation, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
			[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
			{
				if (InResult == ECommandResult::Succeeded)
				{
					DisplaySourceControlOperationNotification(LOCTEXT("Create_Changelist_Succeeded", "Changelist successfully created."), SNotificationItem::CS_Success);
				}
				else if (InResult == ECommandResult::Failed)
				{
					DisplaySourceControlOperationNotification(LOCTEXT("Create_Changelist_Failed", "Failed to create the changelist."), SNotificationItem::CS_Fail);
				}
			}));
}

void SSourceControlChangelistsWidget::OnDeleteChangelist()
{
	if (GetCurrentChangelist() == nullptr)
	{
		return;
	}

	TSharedRef<FDeleteChangelist> DeleteChangelistOperation = ISourceControlOperation::Create<FDeleteChangelist>();
	
	Execute(LOCTEXT("Deleting_Changelist", "Deleting changelist..."), DeleteChangelistOperation, GetCurrentChangelist(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
		{
			if (InResult == ECommandResult::Succeeded)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Delete_Changelist_Succeeded", "Changelist successfully deleted."), SNotificationItem::CS_Success);
			}
			else if (InResult == ECommandResult::Failed)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Delete_Changelist_Failed", "Failed to delete the selected changelist."), SNotificationItem::CS_Fail);
			}
		}));
}

bool SSourceControlChangelistsWidget::CanDeleteChangelist()
{
	FSourceControlChangelistStatePtr Changelist = GetCurrentChangelistState();
	return Changelist != nullptr && Changelist->GetFilesStates().Num() == 0 && Changelist->GetShelvedFilesStates().Num() == 0;
}

void SSourceControlChangelistsWidget::OnEditChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();

	if(ChangelistState == nullptr)
	{
		return;
	}

	FText NewChangelistDescription = ChangelistState->GetDescriptionText();

	bool bOk = GetChangelistDescription(
		nullptr,
		LOCTEXT("SourceControl.Changelist.New.Title2", "Edit Changelist..."),
		LOCTEXT("SourceControl.Changelist.New.Label2", "Enter a new description for the changelist:"),
		NewChangelistDescription);

	if (!bOk)
	{
		return;
	}

	EditChangelistDescription(NewChangelistDescription, ChangelistState);
}

void SSourceControlChangelistsWidget::OnRevertUnchanged()
{
	TSharedRef<FRevertUnchanged> RevertUnchangedOperation = ISourceControlOperation::Create<FRevertUnchanged>();
	Execute(LOCTEXT("Reverting_Unchanged_Files", "Reverting unchanged file(s)..."), RevertUnchangedOperation, GetChangelistFromSelection(), GetSelectedFiles(), EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
		[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InType)
		{
			// NOTE: This operation message should tell how many files were reverted and how many weren't.
			if (Operation->GetResultInfo().ErrorMessages.Num() == 0)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Revert_Unchanged_Files_Succeeded", "Unchanged files were reverted."), SNotificationItem::CS_Success);
			}
			else if (InType == ECommandResult::Failed)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Revert_Unchanged_Files_Failed", "Failed to revert unchanged files."), SNotificationItem::CS_Fail);
			}
		}));
}

bool SSourceControlChangelistsWidget::CanRevertUnchanged()
{
	return GetSelectedFiles().Num() > 0 || (GetCurrentChangelistState() && GetCurrentChangelistState()->GetFilesStates().Num() > 0);
}

void SSourceControlChangelistsWidget::OnRevert()
{
	FText DialogText;
	FText DialogTitle;

	TArray<FString> SelectedControlledFiles;
	TArray<FString> SelectedUncontrolledFiles;

	GetSelectedFiles(SelectedControlledFiles, SelectedUncontrolledFiles);

	// Apply to the entire changelist only of there are no files selected.
	const bool bApplyOnChangelist = (SelectedControlledFiles.Num() == 0 && SelectedUncontrolledFiles.Num() == 0);

	if (bApplyOnChangelist)
	{
		DialogText = LOCTEXT("SourceControl_ConfirmRevertChangelist", "Are you sure you want to revert this changelist?");
		DialogTitle = LOCTEXT("SourceControl_ConfirmRevertChangelist_Title", "Confirm changelist revert");
	}
	else
	{
		DialogText = LOCTEXT("SourceControl_ConfirmRevertFiles", "Are you sure you want to revert the selected files?");
		DialogTitle = LOCTEXT("SourceControl_ConfirmReverFiles_Title", "Confirm files revert");
	}
	
	EAppReturnType::Type UserConfirmation = FMessageDialog::Open(EAppMsgType::OkCancel, EAppReturnType::Ok, DialogText, &DialogTitle);

	if (UserConfirmation != EAppReturnType::Ok)
	{
		return;
	}

	// Can only have one changelist selected at the time in the left split view (either a 'Changelist' or a 'Uncontrolled Changelist')
	if (TSharedPtr<ISourceControlChangelist> SelectedChangelist = GetChangelistFromSelection())
	{
		// No specific files selected, pick all the files in the selected the changelist.
		if (SelectedControlledFiles.IsEmpty())
		{
			// Find all the files in that changelist.
			if (FSourceControlChangelistStatePtr ChangelistState = ISourceControlModule::Get().GetProvider().GetState(SelectedChangelist.ToSharedRef(), EStateCacheUsage::Use))
			{
				Algo::Transform(ChangelistState->GetFilesStates(), SelectedControlledFiles, [](const FSourceControlStateRef& FileState)
				{
					return FileState->GetFilename();
				});
			}
		}

		if (!SelectedControlledFiles.IsEmpty())
		{
			ExecuteChangelistOperationWithSlowTaskWrapper(LOCTEXT("Reverting_Files", "Reverting file(s)..."), [&SelectedControlledFiles]()
			{
				if (SourceControlHelpers::RevertAndReloadPackages(SelectedControlledFiles))
				{
					DisplaySourceControlOperationNotification(LOCTEXT("Revert_Files_Succeeded", "The selected file(s) were reverted."), SNotificationItem::CS_Success);
				}
				else
				{
					DisplaySourceControlOperationNotification(LOCTEXT("Revert_Files_Failed", "Failed to revert the selected file(s)."), SNotificationItem::CS_Fail);
				}
			});
		}
	}
	else if (TSharedPtr<FUncontrolledChangelistState> SelectedUncontrolledChangelist = GetCurrentUncontrolledChangelistState())
	{
		// No individual uncontrolled files were selected, revert all the files from the selected uncontrolled changelist.
		if (SelectedUncontrolledFiles.IsEmpty())
		{
			Algo::Transform(SelectedUncontrolledChangelist->GetFilesStates(), SelectedUncontrolledFiles, [](const FSourceControlStateRef& State) { return State->GetFilename(); });
		}

		// Revert uncontrolled files (if any).
		if (!SelectedUncontrolledFiles.IsEmpty())
		{
			ExecuteUncontrolledChangelistOperation(LOCTEXT("Reverting_Uncontrolled_Files", "Reverting uncontrolled files..."), [&SelectedUncontrolledFiles]()
			{
				FUncontrolledChangelistsModule::Get().OnRevert(SelectedUncontrolledFiles);
			});
		}
	}
	// No changelist selected (and consequently, no files displayed that could be selected).
}

bool SSourceControlChangelistsWidget::CanRevert()
{
	FSourceControlChangelistStatePtr CurrentChangelistState = GetCurrentChangelistState();
	FUncontrolledChangelistStatePtr CurrentUncontrolledChangelistState = GetCurrentUncontrolledChangelistState();

	return GetSelectedFiles().Num() > 0
		|| (CurrentChangelistState.IsValid() && CurrentChangelistState->GetFilesStates().Num() > 0)
		|| (CurrentUncontrolledChangelistState.IsValid() && CurrentUncontrolledChangelistState->GetFilesStates().Num() > 0);
}

void SSourceControlChangelistsWidget::OnShelve()
{
	FSourceControlChangelistStatePtr CurrentChangelist = GetChangelistStateFromSelection();

	if (!CurrentChangelist)
	{
		return;
	}

	FText ChangelistDescription = CurrentChangelist->GetDescriptionText();

	if (ChangelistDescription.IsEmptyOrWhitespace())
	{
		bool bOk = GetChangelistDescription(
			nullptr,
			LOCTEXT("SourceControl.Changelist.NewShelve", "Shelving files..."),
			LOCTEXT("SourceControl.Changelist.NewShelve.Label", "Enter a description for the changelist holding the shelve:"),
			ChangelistDescription);

		if (!bOk)
		{
			// User cancelled entering a changelist description; abort shelve
			return;
		}
	}

	TSharedRef<FShelve> ShelveOperation = ISourceControlOperation::Create<FShelve>();
	ShelveOperation->SetDescription(ChangelistDescription);
	Execute(LOCTEXT("Shelving_Files", "Shelving file(s)..."), ShelveOperation, CurrentChangelist->GetChangelist(), GetSelectedFiles(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
		{
			if (InResult == ECommandResult::Succeeded)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Shelve_Files_Succeeded", "The selected file(s) were shelved."), SNotificationItem::CS_Success);
			}
			else if (InResult == ECommandResult::Failed)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Shelve_Files_Failed", "Failed to shelved the selected file(s)."), SNotificationItem::CS_Fail);
			}
		}));
}

void SSourceControlChangelistsWidget::OnUnshelve()
{
	TSharedRef<FUnshelve> UnshelveOperation = ISourceControlOperation::Create<FUnshelve>();
	Execute(LOCTEXT("Unshelving_Files", "Unshelving file(s)..."), UnshelveOperation, GetChangelistFromSelection(), GetSelectedShelvedFiles(), EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
		[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
		{
			if (InResult == ECommandResult::Succeeded)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Unshelve_Files_Succeeded", "The selected file(s) were unshelved."), SNotificationItem::CS_Success);
			}
			else if (InResult == ECommandResult::Failed)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Unshelve_Files_Failed", "Failed to unshelved the selected file(s)."), SNotificationItem::CS_Fail);
			}
		}));
}

void SSourceControlChangelistsWidget::OnDeleteShelvedFiles()
{
	TSharedRef<FDeleteShelved> DeleteShelvedOperation = ISourceControlOperation::Create<FDeleteShelved>();
	Execute(LOCTEXT("Deleting_Shelved_Files", "Deleting shelved file(s)..."), DeleteShelvedOperation, GetChangelistFromSelection(), GetSelectedShelvedFiles(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
		{
			if (InResult == ECommandResult::Succeeded)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Delete_Shelved_Files_Succeeded", "The selected shelved file(s) were deleted."), SNotificationItem::CS_Success);
			}
			else if (InResult == ECommandResult::Failed)
			{
				DisplaySourceControlOperationNotification(LOCTEXT("Delete_Shelved_Files_Failed", "Failed to delete the selected shelved file(s)."), SNotificationItem::CS_Fail);
			}
		}));
}

static bool GetChangelistValidationResult(FSourceControlChangelistPtr InChangelist, FString& OutValidationTitleText, FString& OutValidationWarningsText, FString& OutValidationErrorsText)
{
	FSourceControlPreSubmitDataValidationDelegate ValidationDelegate = ISourceControlModule::Get().GetRegisteredPreSubmitDataValidation();

	EDataValidationResult ValidationResult = EDataValidationResult::NotValidated;
	TArray<FText> ValidationErrors;
	TArray<FText> ValidationWarnings;

	bool bValidationResult = true;

	if (ValidationDelegate.ExecuteIfBound(InChangelist, ValidationResult, ValidationErrors, ValidationWarnings))
	{
		EMessageSeverity::Type MessageSeverity = EMessageSeverity::Info;

		if (ValidationResult == EDataValidationResult::Invalid || ValidationErrors.Num() > 0)
		{
			OutValidationTitleText = LOCTEXT("SourceControl.Submit.ChangelistValidationError", "Changelist validation failed!").ToString();
			bValidationResult = false;
			MessageSeverity = EMessageSeverity::Error;
		}
		else if (ValidationResult == EDataValidationResult::NotValidated || ValidationWarnings.Num() > 0)
		{
			OutValidationTitleText = LOCTEXT("SourceControl.Submit.ChangelistValidationWarning", "Changelist validation has warnings!").ToString();
			MessageSeverity = EMessageSeverity::Warning;
		}
		else
		{
			OutValidationTitleText = LOCTEXT("SourceControl.Submit.ChangelistValidationSuccess", "Changelist validation successful!").ToString();
		}

		FMessageLog SourceControlLog("SourceControl");
		
		SourceControlLog.Message(MessageSeverity, FText::FromString(*OutValidationTitleText));

		auto AppendInfo = [](const TArray<FText>& Info, const FString& InfoType, FString& OutText)
		{
			const int32 MaxNumLinesDisplayed = 5;
			int32 NumLinesDisplayed = 0;

			if (Info.Num() > 0)
			{
				OutText += LINE_TERMINATOR;
				OutText += FString::Printf(TEXT("Encountered %d %s:"), Info.Num(), *InfoType);

				for (const FText& Line : Info)
				{
					if (NumLinesDisplayed >= MaxNumLinesDisplayed)
					{
						OutText += LINE_TERMINATOR;
						OutText += FString::Printf(TEXT("See log for complete list of %s"), *InfoType);
						break;
					}

					OutText += LINE_TERMINATOR;
					OutText += Line.ToString();

					++NumLinesDisplayed;
				}
			}
		};

		auto LogInfo = [&SourceControlLog](const TArray<FText>& Info, const FString& InfoType, const EMessageSeverity::Type LogVerbosity)
		{
			if (Info.Num() > 0)
			{
				SourceControlLog.Message(LogVerbosity, FText::Format(LOCTEXT("SourceControl.Validation.ErrorEncountered", "Encountered {0} {1}:"), FText::AsNumber(Info.Num()), FText::FromString(*InfoType)));

				for (const FText& Line : Info)
				{
					SourceControlLog.Message(LogVerbosity, Line);
				}
			}
		};

		AppendInfo(ValidationErrors, TEXT("errors"), OutValidationErrorsText);
		AppendInfo(ValidationWarnings, TEXT("warnings"), OutValidationWarningsText);

		LogInfo(ValidationErrors, TEXT("errors"), EMessageSeverity::Error);
		LogInfo(ValidationWarnings, TEXT("warnings"), EMessageSeverity::Warning);
	}

	return bValidationResult;
}

static bool GetOnPresubmitResult(FSourceControlChangelistStatePtr Changelist, FChangeListDescription& Description)
{
	const TArray<FSourceControlStateRef>& FileStates = Changelist->GetFilesStates();
	TArray<FString> LocalFilepathList;
	LocalFilepathList.Reserve(FileStates.Num());
	for (const FSourceControlStateRef& State : FileStates)
	{
		LocalFilepathList.Add(State->GetFilename());
	}

	FText FailureMsg;
	if (!TryToVirtualizeFilesToSubmit(LocalFilepathList, Description.Description, FailureMsg))
	{
		// Setup the notification for operation feedback
		FNotificationInfo Info(FailureMsg);

		Info.Text = LOCTEXT("SCC_Checkin_Failed", "Failed to check in files!");
		Info.ExpireDuration = 8.0f;
		Info.HyperlinkText = LOCTEXT("SCC_Checkin_ShowLog", "Show Message Log");
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]() { FMessageLog("SourceControl").Open(EMessageSeverity::Error, true); });

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		Notification->SetCompletionState(SNotificationItem::CS_Fail);

		return false;
	}

	return true;
}

void SSourceControlChangelistsWidget::OnSubmitChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();
	
	if (!ChangelistState)
	{
		return;
	}

	FString ChangelistValidationTitle;
	FString ChangelistValidationWarningsText;
	FString ChangelistValidationErrorsText;
	bool bValidationResult = GetChangelistValidationResult(ChangelistState->GetChangelist(), ChangelistValidationTitle, ChangelistValidationWarningsText, ChangelistValidationErrorsText);

	// The description from the source control.
	const FText CurrentChangelistDescription = ChangelistState->GetDescriptionText();
	const bool bAskForChangelistDescription = (CurrentChangelistDescription.IsEmptyOrWhitespace());

	// The description possibly updated with the #validated proposed to the user.
	FText ChangelistDescriptionToSubmit = UpdateChangelistDescriptionToSubmitIfNeeded(bValidationResult, CurrentChangelistDescription);

	// The description once edited by the user in the Submit window.
	FText UserEditChangelistDescription = ChangelistDescriptionToSubmit;

	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(NSLOCTEXT("SourceControl.ConfirmSubmit", "Title", "Confirm changelist submit"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(600, 400))
		.SupportsMaximize(true)
		.SupportsMinimize(false);

	TSharedRef<SSourceControlSubmitWidget> SourceControlWidget =
		SNew(SSourceControlSubmitWidget)
		.ParentWindow(NewWindow)
		.Items(ChangelistState->GetFilesStates())
		.Description(ChangelistDescriptionToSubmit)
		.ChangeValidationResult(ChangelistValidationTitle)
		.ChangeValidationWarnings(ChangelistValidationWarningsText)
		.ChangeValidationErrors(ChangelistValidationErrorsText)
		.AllowDescriptionChange(true)
		.AllowUncheckFiles(false)
		.AllowKeepCheckedOut(true)
		.AllowSubmit(bValidationResult)
		.OnSaveChangelistDescription(
			FSourceControlSaveChangelistDescription::CreateLambda([this, &ChangelistState, &bValidationResult, &UserEditChangelistDescription](const FText& NewDescription)
			{
				// NOTE this is called from a modal dialog, so adding a slow task on top of it doesn't really look good. Just run a synchronous operation.
				TSharedRef<FEditChangelist> EditChangelistOperation = ISourceControlOperation::Create<FEditChangelist>();
				EditChangelistOperation->SetDescription(NewDescription);
				ISourceControlModule::Get().GetProvider().Execute(EditChangelistOperation, ChangelistState->GetChangelist(), EConcurrency::Synchronous);
				UserEditChangelistDescription = NewDescription;
			}));

	NewWindow->SetContent(
		SourceControlWidget
	);

	FSlateApplication::Get().AddModalWindow(NewWindow, NULL);

	if (SourceControlWidget->GetResult() == ESubmitResults::SUBMIT_ACCEPTED)
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FChangeListDescription Description;
		TSharedRef<FCheckIn> SubmitChangelistOperation = ISourceControlOperation::Create<FCheckIn>();
		SubmitChangelistOperation->SetKeepCheckedOut(SourceControlWidget->WantToKeepCheckedOut());
		bool bCheckinSuccess = false;

		// Get the changelist description the user had when he hit the 'submit' button.
		SourceControlWidget->FillChangeListDescription(Description);
		UserEditChangelistDescription = Description.Description;

		// Check if any of the presubmit hooks fail. (This might also update the changelist description)
		if (GetOnPresubmitResult(ChangelistState, Description))
		{
			// If the description was modified, add it to the operation to update the changelist
			if (!ChangelistDescriptionToSubmit.EqualTo(Description.Description))
			{
				SubmitChangelistOperation->SetDescription(UpdateChangelistDescriptionToSubmitIfNeeded(bValidationResult, Description.Description));
			}

			Execute(LOCTEXT("Submitting_Changelist", "Submitting changelist..."), SubmitChangelistOperation, ChangelistState->GetChangelist(), EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
				[&SubmitChangelistOperation, &bCheckinSuccess](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
				{
					if (InResult == ECommandResult::Succeeded)
					{
						DisplaySourceControlOperationNotification(SubmitChangelistOperation->GetSuccessMessage(), SNotificationItem::CS_Success);
						bCheckinSuccess = true;
					}
					else if (InResult == ECommandResult::Failed)
					{
						DisplaySourceControlOperationNotification(LOCTEXT("SCC_Checkin_Failed", "Failed to check in files!"), SNotificationItem::CS_Fail);
					}
				}));
		}

		// If something went wrong with the submit, try to preserve the changelist edited by the user (if he edited).
		if (!bCheckinSuccess && !UserEditChangelistDescription.EqualTo(ChangelistDescriptionToSubmit))
		{
			TSharedRef<FEditChangelist> EditChangelistOperation = ISourceControlOperation::Create<FEditChangelist>();
			EditChangelistOperation->SetDescription(UserEditChangelistDescription);
			ISourceControlModule::Get().GetProvider().Execute(EditChangelistOperation, ChangelistState->GetChangelist(), EConcurrency::Synchronous);
		}

		if (bCheckinSuccess)
		{
			// Clear the description saved by the 'submit window'. Useful when the submit window is opened from the Editor menu rather than the changelist window.
			// Opening the 'submit window' from the Editor menu is intended for source controls that do not support changelists (SVN/Git), but remains available to
			// all source controls at the moment.
			SourceControlWidget->ClearChangeListDescription();
		}
	}
}

bool SSourceControlChangelistsWidget::CanSubmitChangelist()
{
	FSourceControlChangelistStatePtr Changelist = GetCurrentChangelistState();
	return Changelist != nullptr && Changelist->GetFilesStates().Num() > 0 && Changelist->GetShelvedFilesStates().Num() == 0;
}

void SSourceControlChangelistsWidget::OnValidateChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();

	if (!ChangelistState)
	{
		return;
	}

	FString ChangelistValidationTitle;
	FString ChangelistValidationWarningsText;
	FString ChangelistValidationErrorsText;
	bool bValidationResult = GetChangelistValidationResult(ChangelistState->GetChangelist(), ChangelistValidationTitle, ChangelistValidationWarningsText, ChangelistValidationErrorsText);

	// Setup the notification for operation feedback
	FNotificationInfo Info(LOCTEXT("SCC_Validation_Success", "Changelist validated"));

	// Override the notification fields for failure ones
	if (!bValidationResult)
	{
		Info.Text = LOCTEXT("SCC_Validation_Failed", "Failed to validate the changelist");
	}

	Info.ExpireDuration = 8.0f;
	Info.HyperlinkText = LOCTEXT("SCC_Validation_ShowLog", "Show Message Log");
	Info.Hyperlink = FSimpleDelegate::CreateLambda([]() { FMessageLog("SourceControl").Open(EMessageSeverity::Info, true); });

	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	Notification->SetCompletionState(bValidationResult ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
}

bool SSourceControlChangelistsWidget::CanValidateChangelist()
{
	FSourceControlChangelistStatePtr Changelist = GetCurrentChangelistState();
	return Changelist != nullptr && Changelist->GetFilesStates().Num() > 0;
}

void SSourceControlChangelistsWidget::OnMoveFiles()
{
	TArray<FString> SelectedControlledFiles;
	TArray<FString> SelectedUncontrolledFiles;
	
	GetSelectedFiles(SelectedControlledFiles, SelectedUncontrolledFiles);

	if (SelectedControlledFiles.IsEmpty() && SelectedUncontrolledFiles.IsEmpty())
	{
		return;
	}

	const bool bAddNewChangelistEntry = true;

	// Build selection list for changelists
	TArray<SSourceControlDescriptionItem> Items;
	Items.Reset(ChangelistTreeNodes.Num() + UncontrolledChangelistTreeNodes.Num() + (bAddNewChangelistEntry ? 1 : 0));

	if (bAddNewChangelistEntry)
	{
		// First item in the 'Move To' list is always 'new changelist'
		Items.Emplace(
			LOCTEXT("SourceControl_NewChangelistText", "New Changelist"),
			LOCTEXT("SourceControl_NewChangelistDescription", "<enter description here>"),
			/*bCanEditDescription=*/true);
	}

	const bool bCanEditAlreadyExistingChangelistDescription = false;

	for (TSharedPtr<IChangelistTreeItem>& Changelist : ChangelistTreeNodes)
	{
		if (Changelist->GetTreeItemType() == IChangelistTreeItem::Changelist)
		{
			const TSharedPtr<FChangelistTreeItem>& TypedChangelist = StaticCastSharedPtr<FChangelistTreeItem>(Changelist);
			Items.Emplace(TypedChangelist->GetDisplayText(), TypedChangelist->GetDescriptionText(), bCanEditAlreadyExistingChangelistDescription);
		}
	}

	for (TSharedPtr<IChangelistTreeItem>& UncontrolledChangelist : UncontrolledChangelistTreeNodes)
	{
		if (UncontrolledChangelist->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
		{
			const TSharedPtr<FUncontrolledChangelistTreeItem>& TypedChangelist = StaticCastSharedPtr<FUncontrolledChangelistTreeItem>(UncontrolledChangelist);
			Items.Emplace(TypedChangelist->GetDisplayText(), TypedChangelist->GetDescriptionText(), bCanEditAlreadyExistingChangelistDescription);
		}
	}

	int32 PickedItem = 0;
	FText ChangelistDescription;
	
	bool bOk = PickChangelistOrNewWithDescription(
		nullptr,
		LOCTEXT("SourceControl.MoveFiles.Title", "Move Files To..."),
		LOCTEXT("SourceControl.MoveFIles.Label", "Target Changelist:"),
		Items,
		PickedItem,
		ChangelistDescription);

	if (!bOk)
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	// Move files to a new changelist
	if (bAddNewChangelistEntry && PickedItem == 0)
	{
		// NOTE: To perform async move, we would need to copy the list of selected uncontrolled files and ensure the list wasn't modified when callback occurs. For now run synchronously.
		TSharedRef<FNewChangelist> NewChangelistOperation = ISourceControlOperation::Create<FNewChangelist>();
		NewChangelistOperation->SetDescription(ChangelistDescription);
		Execute(LOCTEXT("Moving_Files_New_Changelist", "Moving file(s) to a new changelist..."), NewChangelistOperation, SelectedControlledFiles, EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
			[this, SelectedUncontrolledFiles](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
			{
				if (InResult == ECommandResult::Succeeded)
				{
					// NOTE: Perform uncontrolled move only if the new changelist was created and the controlled file were move.
					if ((!SelectedUncontrolledFiles.IsEmpty()) && static_cast<FNewChangelist&>(Operation.Get()).GetNewChangelist().IsValid())
					{
						FUncontrolledChangelistsModule::Get().MoveFilesToControlledChangelist(SelectedUncontrolledFiles, static_cast<FNewChangelist&>(Operation.Get()).GetNewChangelist());
					}

					DisplaySourceControlOperationNotification(LOCTEXT("Move_Files_New_Changelist_Succeeded", "Files were successfully moved to a new changelist."), SNotificationItem::CS_Success);
				}
				if (InResult == ECommandResult::Failed)
				{
					DisplaySourceControlOperationNotification(LOCTEXT("Move_Files_New_Changelist_Failed", "Failed to move the file to the new changelist."), SNotificationItem::CS_Fail);
				}
			}));
	}
	else // Move files to an existing changelist or uncontrolled changelist.
	{
		// NOTE: The combo box indices are in this order: New changelist, existing changelist(s), existing uncontrolled changelist(s)
		FChangelistTreeItemPtr MoveDestination;
		const int32 ChangelistIndex = (bAddNewChangelistEntry ? PickedItem - 1 : PickedItem);

		if (ChangelistIndex < ChangelistTreeNodes.Num()) // Move files to a changelist
		{
			MoveDestination = ChangelistTreeNodes[ChangelistIndex];
		}
		else // Move files to an uncontrolled changelist. All uncontrolled CL were listed after the controlled CL in the combo box, compute the offset.
		{
			MoveDestination = UncontrolledChangelistTreeNodes[ChangelistIndex - ChangelistTreeNodes.Num()];
		}

		// Move file to a changelist.
		if (MoveDestination->GetTreeItemType() == IChangelistTreeItem::Changelist)
		{
			FSourceControlChangelistPtr Changelist = StaticCastSharedPtr<FChangelistTreeItem>(MoveDestination)->ChangelistState->GetChangelist();

			if (!SelectedControlledFiles.IsEmpty())
			{
				Execute(LOCTEXT("Moving_File_Between_Changelists", "Moving file(s) to the selected changelist..."), ISourceControlOperation::Create<FMoveToChangelist>(), Changelist, SelectedControlledFiles, EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
					[SelectedUncontrolledFiles, Changelist](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
					{
						if (InResult == ECommandResult::Succeeded)
						{
							// Perform an uncontrolled move only if the controlled file were move successfully.
							if (!SelectedUncontrolledFiles.IsEmpty())
							{
								FUncontrolledChangelistsModule::Get().MoveFilesToControlledChangelist(SelectedUncontrolledFiles, Changelist);
							}

							DisplaySourceControlOperationNotification(LOCTEXT("Move_Files_Between_Changelist_Succeeded", "File(s) successfully moved to the selected changelist."), SNotificationItem::CS_Success);
						}
						else if (InResult == ECommandResult::Failed)
						{
							DisplaySourceControlOperationNotification(LOCTEXT("Move_Files_Between_Changelist_Failed", "Failed to move the file(s) to the selected changelist."), SNotificationItem::CS_Fail);
						}
					}));
			}
		}
		else if (MoveDestination->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
		{
			const FUncontrolledChangelist UncontrolledChangelist = StaticCastSharedPtr<FUncontrolledChangelistTreeItem>(MoveDestination)->UncontrolledChangelistState->Changelist;
			
			TArray<FSourceControlStateRef> SelectedControlledFileStates;
			TArray<FSourceControlStateRef> SelectedUnControlledFileStates;

			GetSelectedFileStates(SelectedControlledFileStates, SelectedUnControlledFileStates);

			if ((!SelectedControlledFileStates.IsEmpty()) || (!SelectedUnControlledFileStates.IsEmpty()))
			{
				ExecuteUncontrolledChangelistOperation(LOCTEXT("Moving_Uncontrolled_Changelist_To", "Moving uncontrolled files..."), [&]()
				{
					FUncontrolledChangelistsModule::Get().MoveFilesToUncontrolledChangelist(SelectedControlledFileStates, SelectedUnControlledFileStates, UncontrolledChangelist);
				});
			}
		}
	}
}

void SSourceControlChangelistsWidget::OnShowHistory()
{
	TArray<FString> SelectedFiles = GetSelectedFiles();
	if (SelectedFiles.Num() > 0)
	{
		FSourceControlWindows::DisplayRevisionHistory(SelectedFiles);
	}
}

void SSourceControlChangelistsWidget::OnDiffAgainstDepot()
{
	TArray<FString> SelectedFiles = GetSelectedFiles();
	if (SelectedFiles.Num() > 0)
	{
		FSourceControlWindows::DiffAgainstWorkspace(SelectedFiles[0]);
	} 
}

bool SSourceControlChangelistsWidget::CanDiffAgainstDepot()
{
	return GetSelectedFiles().Num() == 1;
}

void SSourceControlChangelistsWidget::OnDiffAgainstWorkspace()
{
	if (GetSelectedShelvedFiles().Num() > 0)
	{
		FSourceControlStateRef FileState = StaticCastSharedPtr<FShelvedFileTreeItem>(FileTreeView->GetSelectedItems()[0])->FileState;
		FSourceControlWindows::DiffAgainstShelvedFile(FileState);
	}
}

bool SSourceControlChangelistsWidget::CanDiffAgainstWorkspace()
{
	return GetSelectedShelvedFiles().Num() == 1;
}

TSharedPtr<SWidget> SSourceControlChangelistsWidget::OnOpenContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	static const FName MenuName = "SourceControl.ChangelistContextMenu";
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		UToolMenu* RegisteredMenu = ToolMenus->RegisterMenu(MenuName);
		// Add section so it can be used as insert position for menu extensions
		RegisteredMenu->AddSection("Source Control");
	}

	TArray<TSharedPtr<IChangelistTreeItem>> SelectedChangelistNodes = ChangelistTreeView->GetSelectedItems();
	TArray<TSharedPtr<IChangelistTreeItem>> SelectedUncontrolledChangelistNodes = UncontrolledChangelistTreeView->GetSelectedItems();

	bool bHasSelectedChangelist = SelectedChangelistNodes.Num() > 0 &&  SelectedChangelistNodes[0]->GetTreeItemType() == IChangelistTreeItem::Changelist;
	bool bHasSelectedShelvedChangelistNode = SelectedChangelistNodes.Num() > 0 &&  SelectedChangelistNodes[0]->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist;
	bool bHasSelectedUncontrolledChangelist = SelectedUncontrolledChangelistNodes.Num() > 0 &&  SelectedUncontrolledChangelistNodes[0]->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist;
	bool bHasSelectedFiles = (GetSelectedFiles().Num() > 0);
	bool bHasSelectedShelvedFiles = (GetSelectedShelvedFiles().Num() > 0);
	bool bHasEmptySelection = (!bHasSelectedChangelist && !bHasSelectedFiles && !bHasSelectedShelvedFiles);

	// Build up the menu for a selection
	USourceControlMenuContext* ContextObject = NewObject<USourceControlMenuContext>();
	FToolMenuContext Context(ContextObject);

	// Fill Context Object
	TArray<FString> SelectedControlledFiles;
	TArray<FString> SelectedUncontrolledFiles;
	GetSelectedFiles(SelectedControlledFiles, SelectedUncontrolledFiles);
	ContextObject->SelectedFiles.Append(SelectedControlledFiles);
	ContextObject->SelectedFiles.Append(SelectedUncontrolledFiles);

	UToolMenu* Menu = ToolMenus->GenerateMenu(MenuName, Context);

	FToolMenuSection& Section = *Menu->FindSection("Source Control");
	
	// This should appear only on change lists
	if (bHasSelectedChangelist)
	{
		Section.AddMenuEntry(
			"SubmitChangelist",
			LOCTEXT("SourceControl_SubmitChangelist", "Submit Changelist..."),
			LOCTEXT("SourceControl_SubmitChangeslit_Tooltip", "Submits a changelist"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnSubmitChangelist),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanSubmitChangelist)));

		Section.AddMenuEntry(
			"ValidateChangelist",
			LOCTEXT("SourceControl_ValidateChangelist", "Validate Changelist"), LOCTEXT("SourceControl_ValidateChangeslit_Tooltip", "Validates a changelist"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnValidateChangelist), 
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanValidateChangelist)));

		Section.AddMenuEntry(
			"RevertUnchanged",
			LOCTEXT("SourceControl_RevertUnchanged", "Revert Unchanged"),
			LOCTEXT("SourceControl_Revert_Unchanged_Tooltip", "Reverts unchanged files & changelists"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnRevertUnchanged),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanRevertUnchanged)));
	}

	if (bHasSelectedChangelist || bHasSelectedUncontrolledChangelist)
	{
		Section.AddMenuEntry(
			"Revert",
			LOCTEXT("SourceControl_Revert", "Revert Files"),
			LOCTEXT("SourceControl_Revert_Tooltip", "Reverts all files in the changelist or from the selection"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnRevert),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanRevert)));
	}

	if (bHasSelectedChangelist && (bHasSelectedFiles || bHasSelectedShelvedFiles || (bHasSelectedChangelist && (GetCurrentChangelistState()->GetFilesStates().Num() > 0 || GetCurrentChangelistState()->GetShelvedFilesStates().Num() > 0))))
	{
		Section.AddSeparator("ShelveSeparator");
	}

	if (bHasSelectedChangelist && (bHasSelectedFiles || (bHasSelectedChangelist && GetCurrentChangelistState()->GetFilesStates().Num() > 0)))
	{
		Section.AddMenuEntry("Shelve",
			LOCTEXT("SourceControl_Shelve", "Shelve Files"),
			LOCTEXT("SourceControl_Shelve_Tooltip", "Shelves the changelist or the selected files"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnShelve)));
	}

	if (bHasSelectedShelvedFiles || bHasSelectedShelvedChangelistNode)
	{
		Section.AddMenuEntry(
			"Unshelve",
			LOCTEXT("SourceControl_Unshelve", "Unshelve Files"),
			LOCTEXT("SourceControl_Unshelve_Tooltip", "Unshelve selected files or changelist"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnUnshelve)));

		Section.AddMenuEntry(
			"DeleteShelved",
			LOCTEXT("SourceControl_DeleteShelved", "Delete Shelved Files"),
			LOCTEXT("SourceControl_DeleteShelved_Tooltip", "Delete selected shelved files or all from changelist"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDeleteShelvedFiles)));
	}

	// Shelved files-only operations
	if (bHasSelectedShelvedFiles)
	{
		// Diff against workspace
		Section.AddMenuEntry(
			"DiffAgainstWorkspace",
			LOCTEXT("SourceControl_DiffAgainstWorkspace", "Diff Against Workspace Files..."),
			LOCTEXT("SourceControl_DiffAgainstWorkspace_Tooltip", "Diff shelved file against the (local) workspace file"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDiffAgainstWorkspace),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDiffAgainstWorkspace)));
	}

	if (bHasEmptySelection || bHasSelectedChangelist)
	{
		Section.AddSeparator("ChangelistsSeparator");
	}

	if (bHasSelectedChangelist)
	{
		Section.AddMenuEntry(
			"EditChangelist",
			LOCTEXT("SourceControl_EditChangelist", "Edit Changelist..."),
			LOCTEXT("SourceControl_Edit_Changelist_Tooltip", "Edit a changelist description"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnEditChangelist)));

		Section.AddMenuEntry(
			"DeleteChangelist",
			LOCTEXT("SourceControl_DeleteChangelist", "Delete Empty Changelist"),
			LOCTEXT("SourceControl_Delete_Changelist_Tooltip", "Deletes an empty changelist"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDeleteChangelist),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDeleteChangelist)));
	}

	// Files-only operations
	if(bHasSelectedFiles)
	{
		Section.AddSeparator("FilesSeparator");

		Section.AddMenuEntry(
			"MoveFiles", LOCTEXT("SourceControl_MoveFiles", "Move Files To..."),
			LOCTEXT("SourceControl_MoveFiles_Tooltip", "Move Files To A Different Changelist..."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnMoveFiles)));

		Section.AddMenuEntry(
			"ShowHistory",
			LOCTEXT("SourceControl_ShowHistory", "Show History..."),
			LOCTEXT("SourceControl_ShowHistory_ToolTip", "Show File History From Selection..."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnShowHistory)));

		Section.AddMenuEntry(
			"DiffAgainstLocalVersion",
			LOCTEXT("SourceControl_DiffAgainstDepot", "Diff Against Depot..."),
			LOCTEXT("SourceControl_DiffAgainstLocal_Tooltip", "Diff local file against depot revision."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDiffAgainstDepot),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDiffAgainstDepot)));
	}

	if (FUncontrolledChangelistsModule::Get().IsEnabled())
	{
		Section.AddSeparator("ReconcileSeparator");

		Section.AddMenuEntry(
			"Reconcile assets",
			LOCTEXT("SourceControl_ReconcileAssets", "Reconcile assets"),
			LOCTEXT("SourceControl_ReconcileAssets_Tooltip", "Look for uncontrolled modification in currently added assets."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]() { FUncontrolledChangelistsModule::Get().OnReconcileAssets(); })));
	}

	return ToolMenus->GenerateWidget(Menu);
}

TSharedRef<SChangelistTree> SSourceControlChangelistsWidget::CreateChangelistTreeView(TArray<TSharedPtr<IChangelistTreeItem>>& ItemSources)
{
	return SNew(SChangelistTree)
		.ItemHeight(24.0f)
		.TreeItemsSource(&ItemSources)
		.OnGenerateRow(this, &SSourceControlChangelistsWidget::OnGenerateRow)
		.OnGetChildren(this, &SSourceControlChangelistsWidget::OnGetChangelistChildren)
		.SelectionMode(ESelectionMode::Single)
		.OnContextMenuOpening(this, &SSourceControlChangelistsWidget::OnOpenContextMenu)
		.OnSelectionChanged(this, &SSourceControlChangelistsWidget::OnChangelistSelectionChanged);
}

TSharedRef<STreeView<FChangelistTreeItemPtr>> SSourceControlChangelistsWidget::CreateChangelistFilesView()
{
	return SNew(STreeView<FChangelistTreeItemPtr>)
		.ItemHeight(24.0f)
		.TreeItemsSource(&FileTreeNodes)
		.OnGenerateRow(this, &SSourceControlChangelistsWidget::OnGenerateRow)
		.OnGetChildren(this, &SSourceControlChangelistsWidget::OnGetFileChildren)
		.SelectionMode(ESelectionMode::Multi)
		.OnContextMenuOpening(this, &SSourceControlChangelistsWidget::OnOpenContextMenu)
		.OnMouseButtonDoubleClick(this, &SSourceControlChangelistsWidget::OnItemDoubleClicked)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+SHeaderRow::Column("Icon")
			.DefaultLabel(FText::GetEmpty())
			.FillSized(18)

			+SHeaderRow::Column("Name")
			.DefaultLabel(LOCTEXT("Name", "Name"))
			.FillWidth(0.2f)

			+SHeaderRow::Column("Path")
			.DefaultLabel(LOCTEXT("Path", "Path"))
			.FillWidth(0.6f)

			+SHeaderRow::Column("Type")
			.DefaultLabel(LOCTEXT("Type", "Type"))
			.FillWidth(0.2f)
		);
}

/** Displays a changed list row (icon, cl number, description) */
class SChangelistTableRow : public STableRow<TSharedPtr<IChangelistTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SChangelistTableRow)
		: _TreeItemToVisualize()
		, _OnPostDrop()
	{}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_EVENT(FSimpleDelegate, OnPostDrop)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());
		OnPostDrop = InArgs._OnPostDrop;

		const FSlateBrush* IconBrush = (TreeItem != nullptr) ?
			FAppStyle::GetBrush(TreeItem->ChangelistState->GetSmallIconName()) :
			FAppStyle::GetBrush("SourceControl.Changelist");

		SetToolTipText(GetChangelistDescriptionText());

		STableRow<FChangelistTreeItemPtr>::Construct(
			STableRow<FChangelistTreeItemPtr>::FArguments()
			.Style(FAppStyle::Get(), "TableView.Row")
			.Content()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot() // Icon
				.AutoWidth()
				[
					SNew(SImage)
					.Image(IconBrush)
				]
				+SHorizontalBox::Slot() // Changelist number.
				.Padding(2, 0, 0, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(this, &SChangelistTableRow::GetChangelistText)
				]
				+SHorizontalBox::Slot() // Files count.
				.Padding(4, 0, 4, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::Format(INVTEXT("({0})"), TreeItem->GetFileCount()))
				]
				+SHorizontalBox::Slot()
				.Padding(2, 0, 0, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(this, &SChangelistTableRow::GetChangelistDescriptionText)
				]
			], InOwner);
	}

	FText GetChangelistText() const
	{
		return TreeItem->GetDisplayText();
	}

	FText GetChangelistDescriptionText() const
	{
		FString DescriptionString = TreeItem->GetDescriptionText().ToString();
		// Here we'll both remove \r\n (when edited from the dialog) and \n (when we get it from the SCC)
		DescriptionString.ReplaceInline(TEXT("\r"), TEXT(""));
		DescriptionString.ReplaceInline(TEXT("\n"), TEXT(" "));
		DescriptionString.TrimEndInline();
		return FText::FromString(DescriptionString);
	}

protected:
	//~ Begin STableRow Interface.
	virtual FReply OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) override
	{
		TSharedPtr<FSCCFileDragDropOp> DropOperation = InDragDropEvent.GetOperationAs<FSCCFileDragDropOp>();
		if (DropOperation.IsValid())
		{
			FSourceControlChangelistPtr DestChangelist = TreeItem->ChangelistState->GetChangelist();
			check(DestChangelist.IsValid());

			// NOTE: The UI don't show 'source controlled files' and 'uncontrolled files' at the same time. User cannot select and drag/drop both file types at the same time.
			if (!DropOperation->Files.IsEmpty())
			{
				TArray<FString> Files;
				Algo::Transform(DropOperation->Files, Files, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

				FScopedSlowTask Progress(0.f, LOCTEXT("Dropping_Files_On_Changelist", "Moving file(s) to the selected changelist..."));
				Progress.MakeDialog();

				ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
				SourceControlProvider.Execute(ISourceControlOperation::Create<FMoveToChangelist>(), DestChangelist, Files, EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
					[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
					{
						if (InResult == ECommandResult::Succeeded)
						{
							DisplaySourceControlOperationNotification(LOCTEXT("Drop_Files_On_Changelist_Succeeded", "File(s) successfully moved to the selected changelist."), SNotificationItem::CS_Success);
						}
						else if (InResult == ECommandResult::Failed)
						{
							DisplaySourceControlOperationNotification(LOCTEXT("Drop_Files_On_Changelist_Failed", "Failed to move the file(s) to the selected changelist."), SNotificationItem::CS_Fail);
						}
					}));
			}
			else if (!DropOperation->UncontrolledFiles.IsEmpty())
			{
				// NOTE: This function does several operations that can fails but we don't get feedback.
				ExecuteUncontrolledChangelistOperationWithSlowTaskWrapper(LOCTEXT("Dropping_Uncontrolled_Files_On_Changelist", "Moving uncontrolled file(s) to the selected changelist..."), 
					[&DropOperation, &DestChangelist]()
				{
					FUncontrolledChangelistsModule::Get().MoveFilesToControlledChangelist(DropOperation->UncontrolledFiles, DestChangelist);
					
					// TODO: Fix MoveFilesToControlledChangelist() to report the possible errors and display a notification.
				});

				OnPostDrop.ExecuteIfBound();
			}
		}

		return FReply::Handled();
	}
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FChangelistTreeItem* TreeItem;

	/** Delegate invoked once the drag and drop operation finished. */
	FSimpleDelegate OnPostDrop;
};

/** Displays an uncontrolled changed list (icon, cl number, description) */
class SUncontrolledChangelistTableRow : public STableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SUncontrolledChangelistTableRow)
		: _TreeItemToVisualize()
		, _OnPostDrop()
	{
	}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_EVENT(FSimpleDelegate, OnPostDrop)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FUncontrolledChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());
		OnPostDrop = InArgs._OnPostDrop;

			const FSlateBrush* IconBrush = (TreeItem != nullptr) ?
				FAppStyle::GetBrush(TreeItem->UncontrolledChangelistState->GetSmallIconName()) :
				FAppStyle::GetBrush("SourceControl.Changelist");

		SetToolTipText(GetChangelistDescriptionText());

		STableRow<FChangelistTreeItemPtr>::Construct(
			STableRow<FChangelistTreeItemPtr>::FArguments()
			.Style(FAppStyle::Get(), "TableView.Row")
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(IconBrush)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SUncontrolledChangelistTableRow::GetChangelistText)
				]
				+SHorizontalBox::Slot() // Files/Offline file count.
				.Padding(4, 0, 4, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::Format(INVTEXT("({0})"), TreeItem->GetFileCount() + TreeItem->GetOfflineFileCount()))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SUncontrolledChangelistTableRow::GetChangelistDescriptionText)
				]
			], InOwner);
	}

	FText GetChangelistText() const
	{
		return TreeItem->GetDisplayText();
	}

	FText GetChangelistDescriptionText() const
	{
		FString DescriptionString = TreeItem->GetDescriptionText().ToString();
		// Here we'll both remove \r\n (when edited from the dialog) and \n (when we get it from the SCC)
		DescriptionString.ReplaceInline(TEXT("\r"), TEXT(""));
		DescriptionString.ReplaceInline(TEXT("\n"), TEXT(" "));
		DescriptionString.TrimEndInline();
		return FText::FromString(DescriptionString);
	}

protected:
	//~ Begin STableRow Interface.
	virtual FReply OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) override
	{
		TSharedPtr<FSCCFileDragDropOp> Operation = InDragDropEvent.GetOperationAs<FSCCFileDragDropOp>();
		
		if (Operation.IsValid())
		{
			ExecuteUncontrolledChangelistOperationWithSlowTaskWrapper(LOCTEXT("Drag_File_To_Uncontrolled_Changelist", "Moving file(s) to the selected uncontrolled changelists..."),
				[this, &Operation]()
			{
				FUncontrolledChangelistsModule::Get().MoveFilesToUncontrolledChangelist(Operation->Files, Operation->UncontrolledFiles, TreeItem->UncontrolledChangelistState->Changelist);
			});

			OnPostDrop.ExecuteIfBound();
		}

		return FReply::Handled();
	}
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FUncontrolledChangelistTreeItem* TreeItem;

	/** Invoked once a drag and drop operation completes. */
	FSimpleDelegate OnPostDrop;
};

/** Display the shelved files group node. It displays 'Shelved Files (x)' where X is the nubmer of file shelved. */
class SShelvedFilesTableRow : public STableRow<TSharedPtr<IChangelistTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SShelvedFilesTableRow)
		: _Icon(nullptr)
	{
	}
		SLATE_ARGUMENT(const FSlateBrush*, Icon)
		SLATE_ARGUMENT(FText, Text)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		STableRow<FChangelistTreeItemPtr>::Construct(
			STableRow<FChangelistTreeItemPtr>::FArguments()
			.Content()
			[
				SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(5, 0, 0, 0)
					[
						SNew(SImage)
						.Image(InArgs._Icon)
					]
					+SHorizontalBox::Slot()
					.Padding(2.0f, 1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Text)
					]
			],
			InOwnerTableView);
	}
};

/** Display information about a file (icon, name, location, type, etc.) */
class SFileTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SFileTableRow)
		: _TreeItemToVisualize()
	{}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_EVENT(FOnDragDetected, OnDragDetected)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FFileTreeItem*>(InArgs._TreeItemToVisualize.Get());

		FSuperRowType::FArguments Args = FSuperRowType::FArguments()
			.OnDragDetected(InArgs._OnDragDetected)
			.ShowSelection(true);
		FSuperRowType::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Icon"))
		{
			return SNew(SBox)
				.WidthOverride(16) // Small Icons are usually 16x16
				.HAlign(HAlign_Center)
				[
					SSourceControlCommon::GetSCCFileWidget(TreeItem->FileState, TreeItem->IsShelved())
				];
		}
		else if (ColumnName == TEXT("Name"))
		{
			return SNew(STextBlock)
				.Text(this, &SFileTableRow::GetDisplayName);
		}
		else if (ColumnName == TEXT("Path"))
		{
			return SNew(STextBlock)
				.Text(this, &SFileTableRow::GetDisplayPath)
				.ToolTipText(this, &SFileTableRow::GetFilename);
		}
		else if (ColumnName == TEXT("Type"))
		{
			return SNew(STextBlock)
				.Text(this, &SFileTableRow::GetDisplayType)
				.ColorAndOpacity(this, &SFileTableRow::GetDisplayColor);
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	FText GetDisplayName() const
	{
		return TreeItem->GetAssetName();
	}

	FText GetFilename() const
	{
		return TreeItem->GetFileName();
	}

	FText GetDisplayPath() const
	{
		return TreeItem->GetAssetPath();
	}

	FText GetDisplayType() const
	{
		return TreeItem->GetAssetType();
	}

	FSlateColor GetDisplayColor() const
	{
		return TreeItem->GetAssetTypeColor();
	}

protected:
	//~ Begin STableRow Interface.
	virtual void OnDragEnter(FGeometry const& InGeometry, FDragDropEvent const& InDragDropEvent) override
	{
		TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
		DragOperation->SetCursorOverride(EMouseCursor::SlashedCircle);
	}

	virtual void OnDragLeave(FDragDropEvent const& InDragDropEvent) override
	{
		TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
		DragOperation->SetCursorOverride(EMouseCursor::None);
	}
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FFileTreeItem* TreeItem;
};

/** Display information about an offline file (icon, name, location, type, etc.). */
class SOfflineFileTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SOfflineFileTableRow)
		: _TreeItemToVisualize()
	{
	}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FOfflineFileTreeItem*>(InArgs._TreeItemToVisualize.Get());

		FSuperRowType::FArguments Args = FSuperRowType::FArguments().ShowSelection(true);
		FSuperRowType::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Icon"))
		{
			return SNew(SBox)
				.WidthOverride(16) // Small Icons are usually 16x16
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(FName("SourceControl.OfflineFile_Small")))
				];
		}
		else if (ColumnName == TEXT("Name"))
		{
			return SNew(STextBlock)
				.Text(this, &SOfflineFileTableRow::GetDisplayName);
		}
		else if (ColumnName == TEXT("Path"))
		{
			return SNew(STextBlock)
				.Text(this, &SOfflineFileTableRow::GetDisplayPath)
				.ToolTipText(this, &SOfflineFileTableRow::GetFilename);
		}
		else if (ColumnName == TEXT("Type"))
		{
			return SNew(STextBlock)
				.Text(this, &SOfflineFileTableRow::GetDisplayType)
				.ColorAndOpacity(this, &SOfflineFileTableRow::GetDisplayColor);
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	FText GetDisplayName() const
	{
		return TreeItem->GetDisplayName();
	}

	FText GetFilename() const
	{
		return TreeItem->GetPackageName();
	}

	FText GetDisplayPath() const
	{
		return TreeItem->GetDisplayPath();
	}

	FText GetDisplayType() const
	{
		return TreeItem->GetDisplayType();
	}

	FSlateColor GetDisplayColor() const
	{
		return TreeItem->GetDisplayColor();
	}

private:
	/** The info about the widget that we are visualizing. */
	FOfflineFileTreeItem* TreeItem;
};


TSharedRef<ITableRow> SSourceControlChangelistsWidget::OnGenerateRow(TSharedPtr<IChangelistTreeItem> InTreeItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	switch (InTreeItem->GetTreeItemType())
	{
	case IChangelistTreeItem::Changelist:
		return SNew(SChangelistTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem)
			.OnPostDrop(this, &SSourceControlChangelistsWidget::OnRefresh);

	case IChangelistTreeItem::UncontrolledChangelist:
		return SNew(SUncontrolledChangelistTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem)
			.OnPostDrop(this, &SSourceControlChangelistsWidget::OnRefresh);

	case IChangelistTreeItem::File:
		return SNew(SFileTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem)
			.OnDragDetected(this, &SSourceControlChangelistsWidget::OnFilesDragged);

	case IChangelistTreeItem::OfflineFile:
		return SNew(SOfflineFileTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

	case IChangelistTreeItem::ShelvedChangelist:
		return SNew(SShelvedFilesTableRow, OwnerTable)
			.Icon(FAppStyle::GetBrush("SourceControl.ShelvedChangelist"))
			.Text(static_cast<const FShelvedChangelistTreeItem*>(InTreeItem.Get())->GetDisplayText());

	case IChangelistTreeItem::ShelvedFile:
		return SNew(SFileTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

	default:
		check(false);
	};

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable);
}

FReply SSourceControlChangelistsWidget::OnFilesDragged(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && !FileTreeView->GetSelectedItems().IsEmpty())
	{
		TSharedRef<FSCCFileDragDropOp> Operation = MakeShared<FSCCFileDragDropOp>();

		for (FChangelistTreeItemPtr InTreeItem : FileTreeView->GetSelectedItems())
		{
			if (InTreeItem->GetTreeItemType() == IChangelistTreeItem::File)
			{
				TSharedRef<FFileTreeItem> FileTreeItem = StaticCastSharedRef<FFileTreeItem>(InTreeItem.ToSharedRef());
				FSourceControlStateRef FileState = FileTreeItem->FileState;

				if (FileTreeItem->GetParent()->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
				{
					Operation->UncontrolledFiles.Add(MoveTemp(FileState));
				}
				else
				{
					Operation->Files.Add(MoveTemp(FileState));
				}
			}
		}
		
		Operation->Construct();

 		return FReply::Handled().BeginDragDrop(Operation);
	}

	return FReply::Unhandled();
}

void SSourceControlChangelistsWidget::OnGetFileChildren(TSharedPtr<IChangelistTreeItem> InParent, TArray<FChangelistTreeItemPtr>& OutChildren)
{
	// Files are leave and don't have children.
}

void SSourceControlChangelistsWidget::OnGetChangelistChildren(FChangelistTreeItemPtr InParent, TArray<FChangelistTreeItemPtr>& OutChildren)
{
	if (InParent->GetTreeItemType() == IChangelistTreeItem::Changelist)
	{
		// In the data model, a changelist has files as children, but in UI, only the 'Shelved Files' node is displayed under the changelist,
		// and the files are displayed in the file view at the right.
		for (const TSharedPtr<IChangelistTreeItem>& Child : InParent->GetChildren())
		{
			if (Child->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
			{
				if (Child->GetChildren().Num() > 0)
				{
					OutChildren.Add(Child); // Add the 'Shelved Files' only if there are shelved files.
					break; // Found the only possible child for the UI.
				}
			}
		}
	}
	else if (InParent->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
	{
		// Uncontrolled changelist nodes do not have children at the moment.
	}
}

void SSourceControlChangelistsWidget::OnItemDoubleClicked(TSharedPtr<IChangelistTreeItem> Item)
{
	if (Item->GetTreeItemType() == IChangelistTreeItem::OfflineFile)
	{
		const FString& Filename = StaticCastSharedPtr<FOfflineFileTreeItem>(Item)->GetFilename();
		ISourceControlWindowsModule::Get().OnChangelistFileDoubleClicked().Broadcast(Filename);
	}
	else if (Item->GetTreeItemType() == IChangelistTreeItem::File)
	{
		const FString& Filename = StaticCastSharedPtr<FFileTreeItem>(Item)->FileState->GetFilename();
		ISourceControlWindowsModule::Get().OnChangelistFileDoubleClicked().Broadcast(Filename);
	}
}

void SSourceControlChangelistsWidget::OnChangelistSelectionChanged(TSharedPtr<IChangelistTreeItem> SelectedChangelist, ESelectInfo::Type SelectionType)
{
	FileTreeNodes.Reset();

	// Add the children of the parent item to the file tree node.
	auto AddChangelistFilesToFileView = [this](TSharedPtr<IChangelistTreeItem> ParentItem, IChangelistTreeItem::TreeItemType DesiredChildrenType)
	{
		for (const TSharedPtr<IChangelistTreeItem>& Child : ParentItem->GetChildren())
		{
			if (Child->GetTreeItemType() == DesiredChildrenType)
			{
				FileTreeNodes.Add(Child);
			}
		}
	};

	if (SelectedChangelist) // Can be a Changelist, Uncontrolled Changelist or Shelved Changelist
	{
		IChangelistTreeItem::TreeItemType ChangelistType = SelectedChangelist->GetTreeItemType();
		switch (ChangelistType)
		{
			case IChangelistTreeItem::Changelist:
			case IChangelistTreeItem::ShelvedChangelist:
				UncontrolledChangelistTreeView->ClearSelection(); // Don't have a changelists selected at the same time than an uncontrolled one, they share the same file view.
				AddChangelistFilesToFileView(SelectedChangelist, ChangelistType == IChangelistTreeItem::Changelist ? IChangelistTreeItem::File : IChangelistTreeItem::ShelvedFile);
				break;

			case IChangelistTreeItem::UncontrolledChangelist:
				ChangelistTreeView->ClearSelection();
				AddChangelistFilesToFileView(SelectedChangelist, IChangelistTreeItem::File);
				AddChangelistFilesToFileView(SelectedChangelist, IChangelistTreeItem::OfflineFile);
				break;

			default:
				break;
		}
	}

	FileTreeView->RequestTreeRefresh();
}

void SSourceControlChangelistsWidget::SaveExpandedAndSelectionStates(FExpandedAndSelectionStates& OutStates)
{
	// Save the selected item from the 'changelists' tree.
	TArray<TSharedPtr<IChangelistTreeItem>> SelectedChangelistItems = ChangelistTreeView->GetSelectedItems();
	OutStates.SelectedChangelistNode = SelectedChangelistItems.IsEmpty() ? TSharedPtr<IChangelistTreeItem>() : SelectedChangelistItems[0];
	OutStates.bShelvedFilesNodeSelected = false;
	if (OutStates.SelectedChangelistNode && OutStates.SelectedChangelistNode->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
	{
		OutStates.SelectedChangelistNode = OutStates.SelectedChangelistNode->GetParent();
		OutStates.bShelvedFilesNodeSelected = true;
	}

	// Save the selected item from 'uncontrolled changelists' tree.
	SelectedChangelistItems = UncontrolledChangelistTreeView->GetSelectedItems();
	OutStates.SelectedUncontrolledChangelistNode = SelectedChangelistItems.IsEmpty() ? TSharedPtr<IChangelistTreeItem>() : SelectedChangelistItems[0];

	// Remember the expanded nodes.
	check(OutStates.ExpandedTreeNodes.IsEmpty());
	ChangelistTreeView->GetExpandedItems(OutStates.ExpandedTreeNodes);
	UncontrolledChangelistTreeView->GetExpandedItems(OutStates.ExpandedTreeNodes);

	// Remember the selected files.
	OutStates.SelectedFileNodes.Reset(FileTreeView->GetNumItemsSelected());
	OutStates.SelectedFileNodes.Append(FileTreeView->GetSelectedItems());
}

void SSourceControlChangelistsWidget::RestoreExpandedAndSelectionStates(const FExpandedAndSelectionStates& InStates)
{
	// Returns whether two changelist nodes represent the same changelist.
	auto ChangelistEquals = [](const TSharedPtr<IChangelistTreeItem>& Lhs, const TSharedPtr<IChangelistTreeItem>& Rhs)
	{
		// NOTE: This TRUSTS the source control to return the same ' state' pointer before and after an update if the changelist still exists.
		return static_cast<FChangelistTreeItem*>(Lhs.Get())->ChangelistState == static_cast<FChangelistTreeItem*>(Rhs.Get())->ChangelistState;
	};

	// Returns whether two uncontrolled changelist nodes represent the same changelist.
	auto UncontrolledChangelistEquals = [](const TSharedPtr<IChangelistTreeItem>& Lhs, const TSharedPtr<IChangelistTreeItem>& Rhs)
	{
		// NOTE: This TRUSTS the source control to return the same 'state' pointer before and after an update if the changelist still exists.
		return static_cast<FUncontrolledChangelistTreeItem*>(Lhs.Get())->UncontrolledChangelistState == static_cast<FUncontrolledChangelistTreeItem*>(Rhs.Get())->UncontrolledChangelistState;
	};

	// Find a specified item in a list. The nodes were deleted and recreated during the update and this function is used to match the new node corresponding to the old node.
	auto Find = [](const TArray<TSharedPtr<IChangelistTreeItem>>& Nodes, const TSharedPtr<IChangelistTreeItem> SearchedItem,
					const TFunction<bool(const TSharedPtr<IChangelistTreeItem>& Lhs, const TSharedPtr<IChangelistTreeItem>& Rhs)>& Predicate)
	{
		if (const TSharedPtr<IChangelistTreeItem>* Node = Nodes.FindByPredicate(
			[&SearchedItem, &Predicate](const TSharedPtr<IChangelistTreeItem>& Candidate) { return Predicate(SearchedItem, Candidate); }))
		{
			return *Node;
		}
		return TSharedPtr<IChangelistTreeItem>(); // return nullptr;
	};

	// Restore the expansion states (Tree is only one level deep)
	for (const TSharedPtr<IChangelistTreeItem>& ExpandedNode : InStates.ExpandedTreeNodes)
	{
		if (ExpandedNode->GetTreeItemType() == IChangelistTreeItem::Changelist)
		{
			// Check if the node still exist after the update.
			if (TSharedPtr<IChangelistTreeItem> MatchingNode = Find(ChangelistTreeNodes, ExpandedNode, ChangelistEquals))
			{
				ChangelistTreeView->SetItemExpansion(MatchingNode, true);
			}
		}
		else if (ExpandedNode->GetTreeItemType() == IChangelistTreeItem::UncontrolledChangelist)
		{
			// Check if the node still exist after the update.
			if (TSharedPtr<IChangelistTreeItem> MatchingNode = Find(UncontrolledChangelistTreeNodes, ExpandedNode, UncontrolledChangelistEquals))
			{
				UncontrolledChangelistTreeView->SetItemExpansion(MatchingNode, true);
			}
		}
	}

	// Restore the selected nodes.
	if (InStates.SelectedChangelistNode)
	{
		if (TSharedPtr<IChangelistTreeItem> MatchingNode = Find(ChangelistTreeNodes, InStates.SelectedChangelistNode, ChangelistEquals))
		{
			if (InStates.bShelvedFilesNodeSelected && static_cast<const FChangelistTreeItem*>(MatchingNode.Get())->GetShelvedFileCount() > 0)
			{
				for (const TSharedPtr<IChangelistTreeItem>& Child : MatchingNode->GetChildren())
				{
					if (Child->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
					{
						ChangelistTreeView->SetSelection(Child); // Select 'Shelved Files' node under the changelist.
						break;
					}
				}
			}
			else
			{
				ChangelistTreeView->SetSelection(MatchingNode); // Select the 'changelist' node
			}
		}
	}
	else if (InStates.SelectedUncontrolledChangelistNode)
	{
		if (TSharedPtr<IChangelistTreeItem> MatchingNode = Find(UncontrolledChangelistTreeNodes, InStates.SelectedUncontrolledChangelistNode, UncontrolledChangelistEquals))
		{
			UncontrolledChangelistTreeView->SetSelection(MatchingNode); // Select the 'uncontrolled changelist' node
		}
	}

	FileTreeView->ClearSelection();

	// Try to reselect the files.
	for (const TSharedPtr<IChangelistTreeItem>& FileNode : FileTreeNodes)
	{
		switch (FileNode->GetTreeItemType())
		{
			case IChangelistTreeItem::File:
				if (InStates.SelectedFileNodes.ContainsByPredicate(
						[&FileNode](const TSharedPtr<IChangelistTreeItem>& Candidate)
						{
							return Candidate->GetTreeItemType() == IChangelistTreeItem::File &&
								static_cast<const FFileTreeItem*>(Candidate.Get())->GetAssetPath().EqualTo(static_cast<const FFileTreeItem*>(FileNode.Get())->GetAssetPath()) &&
								static_cast<const FFileTreeItem*>(Candidate.Get())->GetFileName().EqualTo(static_cast<const FFileTreeItem*>(FileNode.Get())->GetFileName());
						}))
				{
					FileTreeView->SetItemSelection(FileNode, true);
				}
				break;

			case IChangelistTreeItem::ShelvedFile:
				if (InStates.SelectedFileNodes.ContainsByPredicate(
						[&FileNode](const TSharedPtr<IChangelistTreeItem>& Candidate)
						{
							return Candidate->GetTreeItemType() == IChangelistTreeItem::ShelvedFile &&
								static_cast<const FShelvedFileTreeItem*>(Candidate.Get())->GetAssetPath().EqualTo(static_cast<const FShelvedFileTreeItem*>(FileNode.Get())->GetAssetPath()) &&
								static_cast<const FShelvedFileTreeItem*>(Candidate.Get())->GetFileName().EqualTo(static_cast<const FShelvedFileTreeItem*>(FileNode.Get())->GetFileName());
						}))
				{
					FileTreeView->SetItemSelection(FileNode, true);
				}
				break;

			case IChangelistTreeItem::OfflineFile:
				if (InStates.SelectedFileNodes.ContainsByPredicate(
						[&FileNode](const TSharedPtr<IChangelistTreeItem>& Candidate)
						{
							return Candidate->GetTreeItemType() == IChangelistTreeItem::OfflineFile &&
								static_cast<const FOfflineFileTreeItem*>(Candidate.Get())->GetDisplayPath().EqualTo(static_cast<const FOfflineFileTreeItem*>(FileNode.Get())->GetDisplayPath()) &&
								static_cast<const FOfflineFileTreeItem*>(Candidate.Get())->GetDisplayName().EqualTo(static_cast<const FOfflineFileTreeItem*>(FileNode.Get())->GetDisplayName());
						}))
				{
					FileTreeView->SetItemSelection(FileNode, true);
				}
				break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
