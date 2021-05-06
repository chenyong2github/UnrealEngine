// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlChangelists.h"

#include "EditorStyleSet.h"

#include "Algo/Transform.h"

#include "Logging/MessageLog.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "UncontrolledChangelistsModule.h"
#include "SourceControlOperations.h"
#include "ToolMenus.h"
#include "Widgets/Images/SLayeredImage.h"
#include "SSourceControlDescription.h"
#include "SourceControlWindows.h"
#include "SourceControlHelpers.h"
#include "SourceControlSettings.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Algo/AnyOf.h"

#include "SSourceControlSubmit.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"


#define LOCTEXT_NAMESPACE "SourceControlChangelist"

const FText SSourceControlChangelistsWidget::ChangelistValidatedTag = LOCTEXT("ValidationTag", "#changelist validated");

DEFINE_LOG_CATEGORY_STATIC(LogSourceControlChangelist, All, All);

//////////////////////////////
struct FSCCFileDragDropOp : public FDragDropOperation
{
	DRAG_DROP_OPERATOR_TYPE(FSCCFileDragDropOp, FDragDropOperation);

	using FDragDropOperation::Construct;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SSourceControlCommon::GetSCCFileWidget(Files[0]);
	}

	TArray<FSourceControlStateRef> Files;
};

//////////////////////////////
SSourceControlChangelistsWidget::SSourceControlChangelistsWidget()
{
	bIsRefreshing = false;
}

void SSourceControlChangelistsWidget::Construct(const FArguments& InArgs)
{
	// Register delegates
	ISourceControlModule& SCCModule = ISourceControlModule::Get();
	SCCModule.RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlProviderChanged));
	SourceControlStateChangedDelegateHandle = SCCModule.GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged));

	TreeView = CreateTreeviewWidget();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
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
		+ SVerticalBox::Slot()
		[
			SNew(SScrollBorder, TreeView.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([]()->EVisibility { return ISourceControlModule::Get().IsEnabled() ? EVisibility::Visible : EVisibility::Hidden; })))
			[
				TreeView.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() -> EVisibility
			{
				return bIsRefreshing ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return RefreshStatus; })
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
			FExecuteAction::CreateLambda([this]() {
				RequestRefresh();
				})),
		NAME_None,
		LOCTEXT("SourceControl_RefreshButton", "Refresh"),
		LOCTEXT("SourceControl_RefreshButton_Tooltip", "Refreshes changelists from source control provider."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Refresh"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnNewChangelist)),
		NAME_None,
		LOCTEXT("SourceControl_NewChangelistButton", "New Changelist"),
		LOCTEXT("SourceControl_NewChangelistButton_Tooltip", "Creates an empty changelist"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Add"));

	return ToolBarBuilder.MakeWidget();
}

bool SSourceControlChangelistsWidget::HasValidationTag(const FText& InChangelistDescription) const
{
	FString DescriptionString = InChangelistDescription.ToString();
	FString ValidationString = ChangelistValidatedTag.ToString();

	return DescriptionString.Find(ValidationString) != INDEX_NONE;
}

void SSourceControlChangelistsWidget::EditChangelistDescription(const FText& InNewChangelistDescription, const FSourceControlChangelistStatePtr& InChangelistState) const
{
	auto EditChangelistOperation = ISourceControlOperation::Create<FEditChangelist>();
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	EditChangelistOperation->SetDescription(InNewChangelistDescription);
	SourceControlProvider.Execute(EditChangelistOperation, InChangelistState->GetChangelist());
}

void SSourceControlChangelistsWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bShouldRefresh)
	{
		if (ISourceControlModule::Get().IsEnabled())
		{
			RequestRefresh();
			bShouldRefresh = false;
		}
		else
		{
			// No provider available, clear changelist tree
			ClearChangelistsTree();
		}
	}
	
	if (bIsRefreshing)
	{
		TickRefreshStatus(InDeltaTime);
	}
}

void SSourceControlChangelistsWidget::RequestRefresh()
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		StartRefreshStatus();

		TSharedRef<FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
		UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);
		UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);
		UpdatePendingChangelistsOperation->SetUpdateShelvedFilesStates(true);

		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		SourceControlProvider.Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous);

		FUncontrolledChangelistsModule& UncontrolledChangelistModule = FUncontrolledChangelistsModule::Get();
		UncontrolledChangelistModule.UpdateStatus();
	}
	else
	{
		// No provider available, clear changelist tree
		ClearChangelistsTree();
	}
}

void SSourceControlChangelistsWidget::StartRefreshStatus()
{
	bIsRefreshing = true;
	RefreshStatusTimeElapsed = 0;
}

void SSourceControlChangelistsWidget::TickRefreshStatus(double InDeltaTime)
{
	RefreshStatusTimeElapsed += InDeltaTime;
	const int SecondsElapsed = (int)RefreshStatusTimeElapsed;
	RefreshStatus = FText::Format(LOCTEXT("SourceControl_RefreshStatus", "Refreshing changelists... ({0} s)"), FText::AsNumber(SecondsElapsed));
}

void SSourceControlChangelistsWidget::EndRefreshStatus()
{
	bIsRefreshing = false;
}

void SSourceControlChangelistsWidget::ClearChangelistsTree()
{
	if (!ChangelistsNodes.IsEmpty())
	{
		ChangelistsNodes.Empty();
		TreeView->RequestTreeRefresh();
	}
}

void SSourceControlChangelistsWidget::Refresh()
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		TMap<FSourceControlChangelistStateRef, ExpandedState> ExpandedStates;
		SaveExpandedState(ExpandedStates);

		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FUncontrolledChangelistsModule& UncontrolledChangelistModule = FUncontrolledChangelistsModule::Get();
		TArray<FSourceControlChangelistRef> Changelists = SourceControlProvider.GetChangelists(EStateCacheUsage::Use);
		TArray<FUncontrolledChangelistStateRef> UncontrolledChangelistStates = UncontrolledChangelistModule.GetChangelistStates();

		TArray<FSourceControlChangelistStateRef> ChangelistsStates;
		SourceControlProvider.GetState(Changelists, ChangelistsStates, EStateCacheUsage::Use);

		ChangelistsNodes.Reset(ChangelistsStates.Num());

		// Count number of steps for slow task...
		int32 ElementsToProcess = ChangelistsStates.Num();
		ElementsToProcess += UncontrolledChangelistStates.Num();

		for (FSourceControlChangelistStateRef ChangelistState : ChangelistsStates)
		{
			ElementsToProcess += ChangelistState->GetFilesStates().Num();
			ElementsToProcess += ChangelistState->GetShelvedFilesStates().Num();
		}

		for (FUncontrolledChangelistStateRef UncontrolledChangelistState : UncontrolledChangelistStates)
		{
			ElementsToProcess += UncontrolledChangelistState->GetFilesStates().Num();
		}

		FScopedSlowTask SlowTask(ElementsToProcess, LOCTEXT("SourceControl_RebuildTree", "Beautifying Changelist Tree Items Paths"));
		SlowTask.MakeDialog(/*bShowCancelButton=*/true);

		bool bBeautifyPaths = true;

		for (FSourceControlChangelistStateRef ChangelistState : ChangelistsStates)
		{
			FChangelistTreeItemRef ChangelistTreeItem = MakeShareable(new FChangelistTreeItem(ChangelistState));

			for (FSourceControlStateRef FileRef : ChangelistState->GetFilesStates())
			{
				FChangelistTreeItemRef FileTreeItem = MakeShareable(new FFileTreeItem(FileRef, bBeautifyPaths));
				ChangelistTreeItem->AddChild(FileTreeItem);
				SlowTask.EnterProgressFrame();
				bBeautifyPaths &= !SlowTask.ShouldCancel();
			}

			if (ChangelistState->GetShelvedFilesStates().Num() > 0)
			{
				FChangelistTreeItemRef ShelvedChangelistTreeItem = MakeShareable(new FShelvedChangelistTreeItem());
				ChangelistTreeItem->AddChild(ShelvedChangelistTreeItem);

				for (FSourceControlStateRef ShelvedFileRef : ChangelistState->GetShelvedFilesStates())
				{
					FChangelistTreeItemRef ShelvedFileTreeItem = MakeShareable(new FShelvedFileTreeItem(ShelvedFileRef, bBeautifyPaths));
					ShelvedChangelistTreeItem->AddChild(ShelvedFileTreeItem);
					SlowTask.EnterProgressFrame();
					bBeautifyPaths &= !SlowTask.ShouldCancel();
				}
			}

			ChangelistsNodes.Add(ChangelistTreeItem);
			SlowTask.EnterProgressFrame();
			bBeautifyPaths &= !SlowTask.ShouldCancel();
		}

		for (FUncontrolledChangelistStateRef UncontrolledChangelistState : UncontrolledChangelistStates)
		{
			FChangelistTreeItemRef UncontrolledChangelistTreeItem = MakeShareable(new FUncontrolledChangelistTreeItem(UncontrolledChangelistState));
			
			for (FSourceControlStateRef FileRef : UncontrolledChangelistState->GetFilesStates())
			{
				FChangelistTreeItemRef FileTreeItem = MakeShareable(new FFileTreeItem(FileRef, bBeautifyPaths));
				UncontrolledChangelistTreeItem->AddChild(FileTreeItem);
				SlowTask.EnterProgressFrame();
				bBeautifyPaths &= !SlowTask.ShouldCancel();
			}

			ChangelistsNodes.Add(UncontrolledChangelistTreeItem);
			SlowTask.EnterProgressFrame();
			bBeautifyPaths &= !SlowTask.ShouldCancel();
		}

		RestoreExpandedState(ExpandedStates);

		TreeView->RequestTreeRefresh();
	}
	else
	{
		ClearChangelistsTree();
	}

	EndRefreshStatus();
}

void SSourceControlChangelistsWidget::OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
	SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged));

	bShouldRefresh = true;
}

void SSourceControlChangelistsWidget::OnSourceControlStateChanged()
{
	Refresh();
}

void SSourceControlChangelistsWidget::OnChangelistsStatusUpdated(const FSourceControlOperationRef& InOperation, ECommandResult::Type InType)
{
	Refresh();
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
	if (!TreeView)
	{
		return nullptr;
	}

	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	if (SelectedItems.Num() != 1 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::Changelist)
	{
		return nullptr;
	}
	else
	{
		return StaticCastSharedPtr<FChangelistTreeItem>(SelectedItems[0])->ChangelistState;
	}
}

FSourceControlChangelistPtr SSourceControlChangelistsWidget::GetCurrentChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();
	return ChangelistState ? (FSourceControlChangelistPtr)(ChangelistState->GetChangelist()) : nullptr;
}

FSourceControlChangelistStatePtr SSourceControlChangelistsWidget::GetChangelistStateFromSelection()
{
	if (!TreeView)
	{
		return nullptr;
	}

	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	if (SelectedItems.Num() == 0 || SelectedItems[0]->GetTreeItemType() == IChangelistTreeItem::Invalid)
	{
		return nullptr;
	}

	FChangelistTreeItemPtr Item = SelectedItems[0];

	while (Item && Item->GetTreeItemType() != IChangelistTreeItem::Invalid)
	{
		if (Item->GetTreeItemType() == IChangelistTreeItem::Changelist)
			return StaticCastSharedPtr<FChangelistTreeItem>(Item)->ChangelistState;
		else
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
	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	if (SelectedItems.Num() == 0 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::File)
	{
		return TArray<FString>();
	}
	else
	{
		TArray<FString> Files;
		for (FChangelistTreeItemPtr Item : SelectedItems)
		{
			Files.Add(StaticCastSharedPtr<FFileTreeItem>(Item)->FileState->GetFilename());
		}

		return Files;
	}
}

TArray<FString> SSourceControlChangelistsWidget::GetSelectedShelvedFiles()
{
	TArray<FString> ShelvedFiles;
	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();
	
	if (SelectedItems.Num() > 0)
	{
		if (SelectedItems[0]->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
		{
			check(SelectedItems.Num() == 1);
			const TArray<FChangelistTreeItemPtr>& ShelvedChildren = SelectedItems[0]->GetChildren();
			for (FChangelistTreeItemPtr Item : ShelvedChildren)
			{
				ShelvedFiles.Add(StaticCastSharedPtr<FShelvedFileTreeItem>(Item)->FileState->GetFilename());
			}
		}
		else if (SelectedItems[0]->GetTreeItemType() == IChangelistTreeItem::ShelvedFile)
		{
			for (FChangelistTreeItemPtr Item : SelectedItems)
			{
				ShelvedFiles.Add(StaticCastSharedPtr<FShelvedFileTreeItem>(Item)->FileState->GetFilename());
			}
		}
	}

	return ShelvedFiles;
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

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto NewChangelistOperation = ISourceControlOperation::Create<FNewChangelist>();
	NewChangelistOperation->SetDescription(ChangelistDescription);

	SourceControlProvider.Execute(NewChangelistOperation);
}

void SSourceControlChangelistsWidget::OnDeleteChangelist()
{
	if (GetCurrentChangelist() == nullptr)
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.Execute(ISourceControlOperation::Create<FDeleteChangelist>(), GetCurrentChangelist());
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
		LOCTEXT("SourceControl.Changelist.New.Title", "Edit Changelist..."),
		LOCTEXT("SourceControl.Changelist.New.Label", "Enter a new description for the changelist:"),
		NewChangelistDescription);

	if (!bOk)
	{
		return;
	}

	EditChangelistDescription(NewChangelistDescription, ChangelistState);

}

void SSourceControlChangelistsWidget::OnRevertUnchanged()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	auto RevertUnchangedOperation = ISourceControlOperation::Create<FRevertUnchanged>();
	SourceControlProvider.Execute(RevertUnchangedOperation, GetChangelistFromSelection(), GetSelectedFiles());
}

bool SSourceControlChangelistsWidget::CanRevertUnchanged()
{
	return GetSelectedFiles().Num() > 0 || (GetCurrentChangelistState() && GetCurrentChangelistState()->GetFilesStates().Num() > 0);
}

void SSourceControlChangelistsWidget::OnRevert()
{
	FText DialogText;
	FText DialogTitle;

	const bool bApplyOnChangelist = (GetCurrentChangelist() != nullptr);

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

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto RevertOperation = ISourceControlOperation::Create<FRevert>();
	SourceControlProvider.Execute(RevertOperation, GetChangelistFromSelection(), GetSelectedFiles());
}

bool SSourceControlChangelistsWidget::CanRevert()
{
	return GetSelectedFiles().Num() > 0 || (GetCurrentChangelistState() && GetCurrentChangelistState()->GetFilesStates().Num() > 0);
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

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto ShelveOperation = ISourceControlOperation::Create<FShelve>();
	ShelveOperation->SetDescription(ChangelistDescription);
	SourceControlProvider.Execute(ShelveOperation, CurrentChangelist->GetChangelist(), GetSelectedFiles());
}

void SSourceControlChangelistsWidget::OnUnshelve()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto UnshelveOperation = ISourceControlOperation::Create<FUnshelve>();
	SourceControlProvider.Execute(UnshelveOperation, GetChangelistFromSelection(), GetSelectedShelvedFiles());
}

void SSourceControlChangelistsWidget::OnDeleteShelvedFiles()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto DeleteShelvedOperation = ISourceControlOperation::Create<FDeleteShelved>();
	SourceControlProvider.Execute(DeleteShelvedOperation, GetChangelistFromSelection(), GetSelectedShelvedFiles());
}

static bool GetChangelistValidationResult(FSourceControlChangelistPtr InChangelist, FString& OutValidationText, FName& OutValidationIcon)
{
	FSourceControlPreSubmitDataValidationDelegate ValidationDelegate = ISourceControlModule::Get().GetRegisteredPreSubmitDataValidation();

	EDataValidationResult ValidationResult = EDataValidationResult::NotValidated;
	TArray<FText> ValidationErrors;
	TArray<FText> ValidationWarnings;

	bool bValidationResult = true;

	if (ValidationDelegate.ExecuteIfBound(InChangelist, ValidationResult, ValidationErrors, ValidationWarnings))
	{
		if (ValidationResult == EDataValidationResult::Invalid || ValidationErrors.Num() > 0)
		{
			OutValidationIcon = "MessageLog.Error";
			OutValidationText = LOCTEXT("SourceControl.Submit.ChangelistValidationError", "Changelist validation failed!").ToString();
			bValidationResult = false;
		}
		else if (ValidationResult == EDataValidationResult::NotValidated || ValidationWarnings.Num() > 0)
		{
			OutValidationIcon = "MessageLog.Warning";
			OutValidationText = LOCTEXT("SourceControl.Submit.ChangelistValidationWarning", "Changelist validation has warnings!").ToString();
		}
		else
		{
			OutValidationIcon = "MessageLog.Note";
			OutValidationText = LOCTEXT("SourceControl.Submit.ChangelistValidationSuccess", "Changelist validation succesful!").ToString();
		}

		int32 NumLinesDisplayed = 0;

		auto AppendInfo = [&OutValidationText, &NumLinesDisplayed](const TArray<FText>& Info, const FString& InfoType)
		{
			const int32 MaxNumLinesDisplayed = 5;

			if (Info.Num() > 0)
			{
				OutValidationText += LINE_TERMINATOR;
				OutValidationText += FString::Printf(TEXT("Encountered %d %s:"), Info.Num(), *InfoType);

				for (const FText& Line : Info)
				{
					if (NumLinesDisplayed >= MaxNumLinesDisplayed)
					{
						OutValidationText += LINE_TERMINATOR;
						OutValidationText += FString::Printf(TEXT("See log for complete list of %s"), *InfoType);
						break;
					}

					OutValidationText += LINE_TERMINATOR;
					OutValidationText += Line.ToString();

					++NumLinesDisplayed;
				}
			}
		};

		auto LogInfo = [](const TArray<FText>& Info, const FString& InfoType, const ELogVerbosity::Type LogVerbosity)
		{
			if (Info.Num() > 0)
			{
				FMsg::Logf(nullptr, 0, LogSourceControlChangelist.GetCategoryName(), LogVerbosity, TEXT("Encountered %d %s:"), Info.Num(), *InfoType);
				for (const FText& Line : Info)
				{
					FMsg::Logf(nullptr, 0, LogSourceControlChangelist.GetCategoryName(), LogVerbosity, TEXT("%s"), *Line.ToString());
				}
			}
		};

		AppendInfo(ValidationErrors, TEXT("errors"));
		AppendInfo(ValidationWarnings, TEXT("warnings"));

		LogInfo(ValidationErrors, TEXT("errors"), ELogVerbosity::Error);
		LogInfo(ValidationWarnings, TEXT("warnings"), ELogVerbosity::Warning);
	}

	return bValidationResult;
}

void SSourceControlChangelistsWidget::OnSubmitChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();
	
	if (!ChangelistState)
	{
		return;
	}

	FString ChangelistValidationText;
	FName ChangelistValidationIconName;
	bool bValidationResult = GetChangelistValidationResult(ChangelistState->GetChangelist(), ChangelistValidationText, ChangelistValidationIconName);

	// Build list of states for the dialog
	const FText OriginalChangelistDescription = ChangelistState->GetDescriptionText();
	const bool bAskForChangelistDescription = (OriginalChangelistDescription.IsEmptyOrWhitespace());
	FText ChangelistDescriptionToSubmit = UpdateChangelistDescriptionToSubmitIfNeeded(bValidationResult, OriginalChangelistDescription);

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
		.ChangeValidationDescription(ChangelistValidationText)
		.ChangeValidationIcon(ChangelistValidationIconName)
		.AllowDescriptionChange(bAskForChangelistDescription)
		.AllowUncheckFiles(false)
		.AllowKeepCheckedOut(false)
		.AllowSubmit(bValidationResult);

	NewWindow->SetContent(
		SourceControlWidget
	);

	FSlateApplication::Get().AddModalWindow(NewWindow, NULL);

	if (SourceControlWidget->GetResult() == ESubmitResults::SUBMIT_ACCEPTED)
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FChangeListDescription Description;
		auto SubmitChangelistOperation = ISourceControlOperation::Create<FCheckIn>();
		bool bCheckinSuccess = false;

		SourceControlWidget->FillChangeListDescription(Description);

		// If the description was modified, we add it to the operation to update the changelist
		if (!OriginalChangelistDescription.EqualTo(Description.Description))
		{
			SubmitChangelistOperation->SetDescription(Description.Description);
		}

		bCheckinSuccess = SourceControlProvider.Execute(SubmitChangelistOperation, ChangelistState->GetChangelist()) == ECommandResult::Succeeded;

		// Setup the notification for operation feedback
		FNotificationInfo Info(SubmitChangelistOperation->GetSuccessMessage());

		// Override the notification fields for failure ones
		if (!bCheckinSuccess)
		{
			Info.Text = LOCTEXT("SCC_Checkin_Failed", "Failed to check in files!");
			Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
		}
		
		Info.ExpireDuration = 8.0f;
		Info.HyperlinkText = LOCTEXT("SCC_Checkin_ShowLog", "Show Message Log");
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]() { FMessageLog("SourceControl").Open(EMessageSeverity::Info, true); });

		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

FText SSourceControlChangelistsWidget::UpdateChangelistDescriptionToSubmitIfNeeded(const bool bInValidationResult, const FText& InOriginalChangelistDescription) const
{
	if (bInValidationResult && USourceControlSettings::IsValidationTagEnabled() && (!HasValidationTag(InOriginalChangelistDescription)))
	{
		FStringOutputDevice Str;

		Str.SetAutoEmitLineTerminator(true);
		Str.Log(InOriginalChangelistDescription);
		Str.Log(ChangelistValidatedTag);

		FText ChangelistDescription = FText::FromString(Str);

		return ChangelistDescription;
	}

	return InOriginalChangelistDescription;
}

bool SSourceControlChangelistsWidget::CanSubmitChangelist()
{
	FSourceControlChangelistStatePtr Changelist = GetCurrentChangelistState();
	return Changelist != nullptr && Changelist->GetFilesStates().Num() > 0 && Changelist->GetShelvedFilesStates().Num() == 0;
}

void SSourceControlChangelistsWidget::OnMoveFiles()
{
	TArray<FString> SelectedFiles = GetSelectedFiles();

	if (SelectedFiles.Num() == 0)
	{
		return;
	}

	const bool bAddNewChangelistEntry = true;

	// Build selection list for changelists
	TArray<SSourceControlDescriptionItem> Items;
	Items.Reset(ChangelistsNodes.Num() + (bAddNewChangelistEntry ? 1 : 0));

	if (bAddNewChangelistEntry)
	{
		// First is always new changelist
		Items.Emplace(
			LOCTEXT("SourceControl_NewChangelistText", "New Changelist"),
			LOCTEXT("SourceControl_NewChangelistDescription", "<enter description here>"),
			/*bCanEditDescription=*/true);
	}

	const bool bCanEditAlreadyExistingChangelistDescription = false;

	for (FChangelistTreeItemPtr Changelist : ChangelistsNodes)
	{
		if (!Changelist || Changelist->GetTreeItemType() != IChangelistTreeItem::Changelist)
		{
			continue;
		}

		auto TypedChangelist = StaticCastSharedPtr<FChangelistTreeItem>(Changelist);
		Items.Emplace(TypedChangelist->GetDisplayText(), TypedChangelist->GetDescriptionText(), bCanEditAlreadyExistingChangelistDescription);
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

	// Create new changelist
	if (bAddNewChangelistEntry && PickedItem == 0)
	{
		auto NewChangelistOperation = ISourceControlOperation::Create<FNewChangelist>();
		NewChangelistOperation->SetDescription(ChangelistDescription);
		SourceControlProvider.Execute(NewChangelistOperation, SelectedFiles);
	}
	else
	{
		const int32 ChangelistIndex = (bAddNewChangelistEntry ? PickedItem - 1 : PickedItem);
		FSourceControlChangelistPtr Changelist = StaticCastSharedPtr<FChangelistTreeItem>(ChangelistsNodes[ChangelistIndex])->ChangelistState->GetChangelist();
		SourceControlProvider.Execute(ISourceControlOperation::Create<FMoveToChangelist>(), Changelist, SelectedFiles);
	}
}

void SSourceControlChangelistsWidget::OnLocateFile()
{
	TArray<FAssetData> AssetsToSync;
	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	for (const FChangelistTreeItemPtr& SelectedItem : SelectedItems)
	{
		if (SelectedItem->GetTreeItemType() == IChangelistTreeItem::File)
		{
			AssetsToSync.Append(StaticCastSharedPtr<FFileTreeItem>(SelectedItem)->GetAssetData());
		}
	}

	if (AssetsToSync.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync, true);
	}
}

bool SSourceControlChangelistsWidget::CanLocateFile()
{
	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	auto HasAssetData = [](const FChangelistTreeItemPtr& SelectedItem)
	{
		return (SelectedItem->GetTreeItemType() == IChangelistTreeItem::File) && (StaticCastSharedPtr<FFileTreeItem>(SelectedItem)->GetAssetData().Num() > 0);
	};

	// Checks if at least one selected item has asset data (ie: accessible from ContentBrowser)
	return SelectedItems.FindByPredicate(HasAssetData) != nullptr;
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
		FSourceControlStateRef FileState = StaticCastSharedPtr<FShelvedFileTreeItem>(TreeView->GetSelectedItems()[0])->FileState;
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
		ToolMenus->RegisterMenu(MenuName);
	}

	// Build up the menu for a selection
	FToolMenuContext Context;
	UToolMenu* Menu = ToolMenus->GenerateMenu(MenuName, Context);
		
	bool bHasSelectedChangelist = (GetCurrentChangelist() != nullptr);
	bool bHasSelectedFiles = (GetSelectedFiles().Num() > 0);
	bool bHasSelectedShelvedFiles = (GetSelectedShelvedFiles().Num() > 0);
	bool bHasEmptySelection = (!bHasSelectedChangelist && !bHasSelectedFiles && !bHasSelectedShelvedFiles);

	FToolMenuSection& Section = Menu->AddSection("Source Control");
	
	// This should appear only on change lists
	if (bHasSelectedChangelist)
	{
		Section.AddMenuEntry("SubmitChangelist", LOCTEXT("SourceControl_SubmitChangelist", "Submit Changelist..."), LOCTEXT("SourceControl_SubmitChangeslit_Tooltip", "Submits a changelist"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnSubmitChangelist),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanSubmitChangelist)));
	}

	// This can appear on both files & changelist
	if (bHasSelectedChangelist || bHasSelectedFiles)
	{
		Section.AddMenuEntry("RevertUnchanged", LOCTEXT("SourceControl_RevertUnchanged", "Revert Unchanged"), LOCTEXT("SourceControl_Revert_Unchanged_Tooltip", "Reverts unchanged files & changelists"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnRevertUnchanged),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanRevertUnchanged)));

		Section.AddMenuEntry("Revert", LOCTEXT("SourceControl_Revert", "Revert Files"), LOCTEXT("SourceControl_Revert_Tooltip", "Reverts all files in the changelist or from the selection"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnRevert),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanRevert)));
	}

	if(bHasSelectedFiles || bHasSelectedShelvedFiles || (bHasSelectedChangelist && (GetCurrentChangelistState()->GetFilesStates().Num() > 0 || GetCurrentChangelistState()->GetShelvedFilesStates().Num() > 0)))
	{
		Section.AddSeparator("ShelveSeparator");
	}

	if(bHasSelectedFiles || (bHasSelectedChangelist && GetCurrentChangelistState()->GetFilesStates().Num() > 0))
	{
		Section.AddMenuEntry("Shelve", LOCTEXT("SourceControl_Shelve", "Shelve Files"), LOCTEXT("SourceControl_Shelve_Tooltip", "Shelves the changelist or the selected files"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnShelve)));
	}

	if (bHasSelectedShelvedFiles || (bHasSelectedChangelist && GetCurrentChangelistState()->GetShelvedFilesStates().Num() > 0))
	{
		Section.AddMenuEntry("Unshelve", LOCTEXT("SourceControl_Unshelve", "Unshelve Files"), LOCTEXT("SourceControl_Unshelve_Tooltip", "Unshelve selected files or changelist"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnUnshelve)));

		Section.AddMenuEntry("DeleteShelved", LOCTEXT("SourceControl_DeleteShelved", "Delete Shelved Files"), LOCTEXT("SourceControl_DeleteShelved_Tooltip", "Delete selected shelved files or all from changelist"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDeleteShelvedFiles)));
	}

	// Shelved files-only operations
	if (bHasSelectedShelvedFiles)
	{
		// Diff against workspace
		Section.AddMenuEntry("DiffAgainstWorkspace", LOCTEXT("SourceControl_DiffAgainstWorkspace", "Diff Against Workspace Files..."), LOCTEXT("SourceControl_DiffAgainstWorkspace_Tooltip", "Diff shelved file against the (local) workspace file"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDiffAgainstWorkspace),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDiffAgainstWorkspace)));
	}

	if (bHasEmptySelection || bHasSelectedChangelist)
	{
		Section.AddSeparator("ChangelistsSeparator");
	}

	// This should appear only if we have no selection
	if (bHasEmptySelection)
	{
		Section.AddMenuEntry("NewChangelist", LOCTEXT("SourceControl_NewChangelist", "New Changelist..."), LOCTEXT("SourceControl_NewChangelist_Tooltip", "Creates an empty changelist"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnNewChangelist)));
	}

	if (bHasSelectedChangelist)
	{
		Section.AddMenuEntry("EditChangelist", LOCTEXT("SourceControl_EditChangelist", "Edit Changelist..."), LOCTEXT("SourceControl_Edit_Changelist_Tooltip", "Edit a changelist description"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnEditChangelist)));

		Section.AddMenuEntry("DeleteChangelist", LOCTEXT("SourceControl_DeleteChangelist", "Delete Empty Changelist"), LOCTEXT("SourceControl_Delete_Changelist_Tooltip", "Deletes an empty changelist"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDeleteChangelist),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDeleteChangelist)));
	}

	// Files-only operations
	if(bHasSelectedFiles)
	{
		Section.AddSeparator("FilesSeparator");

		Section.AddMenuEntry("MoveFiles", LOCTEXT("SourceControl_MoveFiles", "Move Files To..."), LOCTEXT("SourceControl_MoveFiles_Tooltip", "Move Files To A Different Changelist..."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnMoveFiles)));

		Section.AddMenuEntry("LocateFile", LOCTEXT("SourceControl_LocateFile", "Locate File..."), LOCTEXT("SourceControl_LocateFile_Tooltip", "Locate File in Project..."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnLocateFile),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanLocateFile)));

		Section.AddMenuEntry("ShowHistory", LOCTEXT("SourceControl_ShowHistory", "Show History..."), LOCTEXT("SourceControl_ShowHistory_ToolTip", "Show File History From Selection..."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnShowHistory)));

		Section.AddMenuEntry("DiffAgainstLocalVersion", LOCTEXT("SourceControl_DiffAgainstDepot", "Diff Against Depot..."), LOCTEXT("SourceControl_DiffAgainstLocal_Tooltip", "Diff local file against depot revision."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDiffAgainstDepot),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDiffAgainstDepot)));
	}

	return ToolMenus->GenerateWidget(Menu);
}

TSharedRef<SChangelistTree> SSourceControlChangelistsWidget::CreateTreeviewWidget()
{
	return SAssignNew(TreeView, SChangelistTree)
		.ItemHeight(24.0f)
		.TreeItemsSource(&ChangelistsNodes)
		.OnGenerateRow(this, &SSourceControlChangelistsWidget::OnGenerateRow)
		.OnGetChildren(this, &SSourceControlChangelistsWidget::OnGetChildren)
		.SelectionMode(ESelectionMode::Multi)
		.OnContextMenuOpening(this, &SSourceControlChangelistsWidget::OnOpenContextMenu)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+ SHeaderRow::Column("Change")
			.DefaultLabel(LOCTEXT("Change", "Change"))
			.FillWidth(0.2f)
			+ SHeaderRow::Column("Description")
			.DefaultLabel(LOCTEXT("Description", "Description"))
			.FillWidth(0.6f)
			+ SHeaderRow::Column("Type")
			.DefaultLabel(LOCTEXT("Type", "Type"))
			.FillWidth(0.2f)
		);
}


class SChangelistTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SChangelistTableRow)
		: _TreeItemToVisualize()
	{}
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
		TreeItem = static_cast<FChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());

		auto Args = FSuperRowType::FArguments();
		SMultiColumnTableRow<FChangelistTreeItemPtr>::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Change"))
		{
			const FSlateBrush* IconBrush = (TreeItem != nullptr) ? FEditorStyle::GetBrush(TreeItem->ChangelistState->GetSmallIconName())
																 : FEditorStyle::GetBrush("SourceControl.Changelist");

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SExpanderArrow, SharedThis(this))
					]

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
						.Text(this, &SChangelistTableRow::GetChangelistText)
					];
		}
		else if (ColumnName == TEXT("Description"))
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SChangelistTableRow::GetChangelistDescriptionText)
				];
		}
		else
		{
			return SNullWidget::NullWidget;
		}
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
			FSourceControlChangelistPtr Changelist = TreeItem->ChangelistState->GetChangelist();
			check(Changelist.IsValid());

			TArray<FString> Files;
			Algo::Transform(Operation->Files, Files, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

			ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
			SourceControlProvider.Execute(ISourceControlOperation::Create<FMoveToChangelist>(), Changelist, Files);
		}

		return FReply::Handled();
	}
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FChangelistTreeItem* TreeItem;
};

class SUncontrolledChangelistTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SUncontrolledChangelistTableRow)
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
		TreeItem = static_cast<FUncontrolledChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());

		auto Args = FSuperRowType::FArguments();
		SMultiColumnTableRow<FChangelistTreeItemPtr>::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Change"))
		{
			const FSlateBrush* IconBrush = (TreeItem != nullptr) ? FEditorStyle::GetBrush(TreeItem->UncontrolledChangelistState->GetSmallIconName())
				: FEditorStyle::GetBrush("SourceControl.Changelist");

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

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
				];
		}
		else if (ColumnName == TEXT("Description"))
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SUncontrolledChangelistTableRow::GetChangelistDescriptionText)
				];
		}
		else
		{
			return SNullWidget::NullWidget;
		}
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
			TArray<FString> Files;
			Algo::Transform(Operation->Files, Files, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

			ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
			auto RevertOperation = ISourceControlOperation::Create<FRevert>();

			RevertOperation->SetSoftRevert(true);
			SourceControlProvider.Execute(RevertOperation, Files);

			FUncontrolledChangelistsModule::Get().OnFilesMovedToUncontrolledChangelist(Files, TreeItem->UncontrolledChangelistState->Changelist);
		}

		return FReply::Handled();
	}
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FUncontrolledChangelistTreeItem* TreeItem;
};

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

		auto Args = FSuperRowType::FArguments()
			.OnDragDetected(InArgs._OnDragDetected)
			.ShowSelection(true);
		FSuperRowType::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Change")) // eq. to name
		{
			const int32 LeftOffset = (TreeItem->IsShelved() ? 60 : 40);

			return SNew(SHorizontalBox)

			// Icon
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(LeftOffset, 0, 4, 0)
				[
					SSourceControlCommon::GetSCCFileWidget(TreeItem->FileState, TreeItem->IsShelved())
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SFileTableRow::GetDisplayName)
				];
		}
		else if (ColumnName == TEXT("Description")) // eq. to path
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

class SShelvedChangelistTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SShelvedChangelistTableRow)
		: _TreeItemToVisualize()
	{}
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
		TreeItem = static_cast<FShelvedChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());

		auto Args = FSuperRowType::FArguments();
		SMultiColumnTableRow<FChangelistTreeItemPtr>::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Change"))
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(5, 0, 4, 0)
					[
						SNew(SExpanderArrow, SharedThis(this))
					]
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(5, 0, 0, 0)
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("SourceControl.ShelvedChangelist"))
					]
				+ SHorizontalBox::Slot()
					.Padding(2.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SShelvedChangelistTableRow::GetText)
					];
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

protected:
	FText GetText() const
	{
		return TreeItem->GetDisplayText();
	}

private:
	/** The info about the widget that we are visualizing. */
	FShelvedChangelistTreeItem* TreeItem;
};

TSharedRef<ITableRow> SSourceControlChangelistsWidget::OnGenerateRow(FChangelistTreeItemPtr InTreeItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	switch (InTreeItem->GetTreeItemType())
	{
	case IChangelistTreeItem::Changelist:
		return SNew(SChangelistTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

	case IChangelistTreeItem::UncontrolledChangelist:
		return SNew(SUncontrolledChangelistTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

	case IChangelistTreeItem::File:
		return SNew(SFileTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem)
			.OnDragDetected(this, &SSourceControlChangelistsWidget::OnFilesDragged);

	case IChangelistTreeItem::ShelvedChangelist:
		return SNew(SShelvedChangelistTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

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
	if (InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && !TreeView->GetSelectedItems().IsEmpty())
	{
		TSharedRef<FSCCFileDragDropOp> Operation = MakeShareable(new FSCCFileDragDropOp());

		Algo::Transform(TreeView->GetSelectedItems(), Operation->Files, [](FChangelistTreeItemPtr InTreeItem) { check(InTreeItem->GetTreeItemType() == IChangelistTreeItem::File); return static_cast<FFileTreeItem*>(InTreeItem.Get())->FileState; });
		Operation->Construct();

		return FReply::Handled().BeginDragDrop(Operation);
	}

	return FReply::Unhandled();
}

void SSourceControlChangelistsWidget::OnGetChildren(FChangelistTreeItemPtr InParent, TArray<FChangelistTreeItemPtr>& OutChildren)
{
	for (auto& Child : InParent->GetChildren())
	{
		// Should never have bogus entries in this list
		check(Child.IsValid());
		OutChildren.Add(Child);
	}
}

void SSourceControlChangelistsWidget::SaveExpandedState(TMap<FSourceControlChangelistStateRef, ExpandedState>& ExpandedStates) const
{
	for (FChangelistTreeItemPtr Root : ChangelistsNodes)
	{
		if (Root->GetTreeItemType() != IChangelistTreeItem::Changelist)
		{
			continue;
		}

		bool bChangelistExpanded = TreeView->IsItemExpanded(Root);

		bool bShelveExpanded = false;
		for (FChangelistTreeItemPtr Child : Root->GetChildren())
		{
			if (Child->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
			{
				bShelveExpanded = TreeView->IsItemExpanded(Child);
				break;
			}
		}

		ExpandedState State;
		State.bChangelistExpanded = bChangelistExpanded;
		State.bShelveExpanded = bShelveExpanded;

		ExpandedStates.Add(StaticCastSharedPtr<FChangelistTreeItem>(Root)->ChangelistState, State);
	}
}

void SSourceControlChangelistsWidget::RestoreExpandedState(const TMap<FSourceControlChangelistStateRef, ExpandedState>& ExpandedStates)
{
	for (FChangelistTreeItemPtr Root : ChangelistsNodes)
	{
		if (Root->GetTreeItemType() != IChangelistTreeItem::Changelist)
		{
			continue;
		}

		FSourceControlChangelistStateRef ChangelistState = StaticCastSharedPtr<FChangelistTreeItem>(Root)->ChangelistState;
		const ExpandedState* State = ExpandedStates.Find(ChangelistState);

		if (!State)
		{
			continue;
		}

		TreeView->SetItemExpansion(Root, State->bChangelistExpanded);

		for (FChangelistTreeItemPtr Child : Root->GetChildren())
		{
			if (Child->GetTreeItemType() == IChangelistTreeItem::ShelvedChangelist)
			{
				TreeView->SetItemExpansion(Child, State->bShelveExpanded);
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE