// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerActorSCCColumn.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/STreeView.h"
#include "ActorTreeItem.h"
#include "ActorDescTreeItem.h"
#include "ActorFolderTreeItem.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "ISourceControlModule.h"
#include "SourceControlOperations.h"
#include "FileHelpers.h"
#include "SourceControlWindows.h"
#include "AssetViewUtils.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerSourceControlColumn"

FName FSceneOutlinerActorSCCColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FSceneOutlinerActorSCCColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(FText::FromName(GetColumnID()))
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(this, &FSceneOutlinerActorSCCColumn::GetHeaderIcon)
		];
}

const TSharedRef<SWidget> FSceneOutlinerActorSCCColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->IsA<FActorTreeItem>() || 
		TreeItem->IsA<FActorDescTreeItem>() || 
		(TreeItem->IsA<FActorFolderTreeItem>() && TreeItem->CastTo<FActorFolderTreeItem>()->GetActorFolder()))
	{
		TSharedRef<SSourceControlWidget> Widget = SNew(SSourceControlWidget, WeakSceneOutliner, TreeItem);

		ItemWidgets.Add(FSceneOutlinerTreeItemPtr(TreeItem), Widget);
		
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Widget
			];
	}
	return SNullWidget::NullWidget;
}

const FSlateBrush* FSceneOutlinerActorSCCColumn::GetHeaderIcon() const
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		return FAppStyle::GetBrush("SourceControl.StatusIcon.On");
	}
	else
	{
		return FAppStyle::GetBrush("SourceControl.StatusIcon.Off");
	}
}

bool FSceneOutlinerActorSCCColumn::AddSourceControlMenuOptions(UToolMenu* Menu, TArray<FSceneOutlinerTreeItemPtr> InSelectedItems)
{
	SelectedItems = InSelectedItems;

	CacheCanExecuteVars();

	FToolMenuSection& Section = Menu->AddSection("AssetContextSourceControl");
	
	if ( ISourceControlModule::Get().IsEnabled() )
	{
		// SCC sub menu
		Section.AddSubMenu(
			"SourceControlSubMenu",
			LOCTEXT("SourceControlSubMenuLabel", "Source Control"),
			LOCTEXT("SourceControlSubMenuToolTip", "Source control actions."),
			FNewToolMenuDelegate::CreateSP(this, &FSceneOutlinerActorSCCColumn::FillSourceControlSubMenu),
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::CanExecuteSourceControlActions )
				),
			EUserInterfaceActionType::Button,
			false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.StatusIcon.On")
			);
	}

	return true;
}


bool FSceneOutlinerActorSCCColumn::CanExecuteSourceControlActions() const
{
	return SelectedItems.Num() > 0;
}

void FSceneOutlinerActorSCCColumn::CacheCanExecuteVars()
{
	bCanExecuteSCCCheckOut = false;
	bCanExecuteSCCCheckIn = false;
	bCanExecuteSCCHistory = false;
	bCanExecuteSCCRevert = false;

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( ISourceControlModule::Get().IsEnabled() )
	{
		for (FSceneOutlinerTreeItemPtr SelectedItem : SelectedItems)
		{
			TSharedRef<SSourceControlWidget> * WidgetLookup = ItemWidgets.Find(SelectedItem);
			if (WidgetLookup == nullptr)
			{
				continue;
			}

			TSharedRef<SSourceControlWidget> Widget = *WidgetLookup;

			// Check the SCC state for each package in the selected paths
			FSourceControlStatePtr SourceControlState = Widget->GetSourceControlState();
			if(SourceControlState.IsValid())
			{
				if ( SourceControlState->CanCheckout() )
				{
					bCanExecuteSCCCheckOut = true;
				}

				if( SourceControlState->IsSourceControlled() && !SourceControlState->IsAdded() )
				{
					bCanExecuteSCCHistory = true;
				}

				if ( SourceControlState->CanCheckIn() )
				{
					bCanExecuteSCCCheckIn = true;
				}

				if (SourceControlState->CanRevert())
				{
					bCanExecuteSCCRevert = true;
				}
			}

			if ( bCanExecuteSCCCheckOut
				&& bCanExecuteSCCCheckIn
				&& bCanExecuteSCCHistory
				&& bCanExecuteSCCRevert
				)
			{
				// All options are available, no need to keep iterating
				break;
			}
		}
	}
}

bool FSceneOutlinerActorSCCColumn::CanExecuteSCCCheckOut() const
{
	return bCanExecuteSCCCheckOut;
}

bool FSceneOutlinerActorSCCColumn::CanExecuteSCCCheckIn() const
{
	return bCanExecuteSCCCheckIn;
}

bool FSceneOutlinerActorSCCColumn::CanExecuteSCCHistory() const
{
	return bCanExecuteSCCHistory;
}

bool FSceneOutlinerActorSCCColumn::CanExecuteSCCRevert() const
{
	return bCanExecuteSCCRevert;
}

bool FSceneOutlinerActorSCCColumn::CanExecuteSCCRefresh() const
{
	return ISourceControlModule::Get().IsEnabled();
}

void FSceneOutlinerActorSCCColumn::FillSourceControlSubMenu(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->AddSection("AssetSourceControlActions", LOCTEXT("AssetSourceControlActionsMenuHeading", "Source Control"));

	if ( CanExecuteSCCCheckOut() )
	{
		Section.AddMenuEntry(
			"SCCCheckOut",
			LOCTEXT("SCCCheckOut", "Check Out"),
			LOCTEXT("SCCCheckOutTooltip", "Checks out the selected asset from source control."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.CheckOut"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::ExecuteSCCCheckOut ),
				FCanExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::CanExecuteSCCCheckOut )
			)
		);
	}

	if ( CanExecuteSCCCheckIn() )
	{
		Section.AddMenuEntry(
			"SCCCheckIn",
			LOCTEXT("SCCCheckIn", "Check In"),
			LOCTEXT("SCCCheckInTooltip", "Checks in the selected asset to source control."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Submit"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::ExecuteSCCCheckIn ),
				FCanExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::CanExecuteSCCCheckIn )
			)
		);
	}

	Section.AddMenuEntry(
		"SCCRefresh",
		LOCTEXT("SCCRefresh", "Refresh"),
		LOCTEXT("SCCRefreshTooltip", "Updates the source control status of the asset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Refresh"),
		FUIAction(
			FExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::ExecuteSCCRefresh ),
			FCanExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::CanExecuteSCCRefresh )
			)
		);

	if( CanExecuteSCCHistory() )
	{
		Section.AddMenuEntry(
			"SCCHistory",
			LOCTEXT("SCCHistory", "History"),
			LOCTEXT("SCCHistoryTooltip", "Displays the source control revision history of the selected asset."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.History"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::ExecuteSCCHistory ),
				FCanExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::CanExecuteSCCHistory )
			)
		);
	}

	if( CanExecuteSCCRevert() )
	{
		Section.AddMenuEntry(
			"SCCRevert",
			LOCTEXT("SCCRevert", "Revert"),
			LOCTEXT("SCCRevertTooltip", "Reverts the asset to the state it was before it was checked out."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Revert"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::ExecuteSCCRevert ),
				FCanExecuteAction::CreateSP( this, &FSceneOutlinerActorSCCColumn::CanExecuteSCCRevert )
			)
		);
	}
}

void FSceneOutlinerActorSCCColumn::GetSelectedPackageNames(TArray<FString>& OutPackageNames) const
{
	for (FSceneOutlinerTreeItemPtr SelectedItem : SelectedItems)
	{
		const TSharedRef<SSourceControlWidget> * WidgetLookup = ItemWidgets.Find(SelectedItem);
		if (WidgetLookup == nullptr)
		{
			continue;
		}

		TSharedRef<SSourceControlWidget> Widget = *WidgetLookup;
		FString PackageName = Widget->GetPackageName();
		if (!PackageName.IsEmpty()) {
			OutPackageNames.Add(PackageName);
		}
	}
}

void FSceneOutlinerActorSCCColumn::GetSelectedPackages(TArray<UPackage*>& OutPackages) const
{
	for (FSceneOutlinerTreeItemPtr SelectedItem : SelectedItems)
	{
		const TSharedRef<SSourceControlWidget> * WidgetLookup = ItemWidgets.Find(SelectedItem);
		if (WidgetLookup == nullptr)
		{
			continue;
		}

		TSharedRef<SSourceControlWidget> Widget = *WidgetLookup;
		UPackage* Package = Widget->GetPackage();
		if (Package != nullptr) {
			OutPackages.Add(Package);
		}
	}
}

void FSceneOutlinerActorSCCColumn::ExecuteSCCRefresh()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);

	ISourceControlModule::Get().GetProvider().Execute(ISourceControlOperation::Create<FUpdateStatus>(), SourceControlHelpers::PackageFilenames(PackageNames), EConcurrency::Asynchronous);
}

void FSceneOutlinerActorSCCColumn::ExecuteSCCCheckOut()
{
	TArray<UPackage*> PackagesToCheckOut;
	GetSelectedPackages(PackagesToCheckOut);

	if ( PackagesToCheckOut.Num() > 0 )
	{
		FEditorFileUtils::CheckoutPackages(PackagesToCheckOut);
	}
}

void FSceneOutlinerActorSCCColumn::ExecuteSCCCheckIn()
{
	TArray<UPackage*> Packages;
	GetSelectedPackages(Packages);

	// Prompt the user to ask if they would like to first save any dirty packages they are trying to check-in
	const FEditorFileUtils::EPromptReturnCode UserResponse = FEditorFileUtils::PromptForCheckoutAndSave( Packages, true, true );

	// If the user elected to save dirty packages, but one or more of the packages failed to save properly OR if the user
	// canceled out of the prompt, don't follow through on the check-in process
	const bool bShouldProceed = ( UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Success || UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Declined );
	if ( bShouldProceed )
	{
		TArray<FString> PackageNames;
		GetSelectedPackageNames(PackageNames);

		const bool bUseSourceControlStateCache = true;
		const bool bCheckinGood = FSourceControlWindows::PromptForCheckin(bUseSourceControlStateCache, PackageNames);

		if (!bCheckinGood)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "SCC_Checkin_Failed", "Check-in failed as a result of save failure."));
		}
	}
	else
	{
		// If a failure occurred, alert the user that the check-in was aborted. This warning shouldn't be necessary if the user cancelled
		// from the dialog, because they obviously intended to cancel the whole operation.
		if ( UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Failure )
		{
			FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "SCC_Checkin_Aborted", "Check-in aborted as a result of save failure.") );
		}
	}
}

void FSceneOutlinerActorSCCColumn::ExecuteSCCHistory()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);
	FSourceControlWindows::DisplayRevisionHistory(SourceControlHelpers::PackageFilenames(PackageNames));
}

void FSceneOutlinerActorSCCColumn::ExecuteSCCRevert()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);
	FSourceControlWindows::PromptForRevert(PackageNames);
}

#undef LOCTEXT_NAMESPACE