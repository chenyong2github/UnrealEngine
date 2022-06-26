// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDeleteActivityDependenciesDialog.h"

#include "ConcertSyncSessionDatabase.h"
#include "SActivityDependencyView.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SessionTabs/Archived/ArchivedSessionHistoryController.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void SDeleteActivityDependenciesDialog::Construct(const FArguments& InArgs, const FGuid& SessionId, const TSharedRef<IConcertSyncServer>& SyncServer, UE::ConcertSyncCore::FHistoryEditionArgs InDeletionRequirements)
{
	DeletionRequirements = MoveTemp(InDeletionRequirements);
	OnConfirmDeletionFunc = InArgs._OnConfirmDeletion;

	check(OnConfirmDeletionFunc.IsBound());
	
	SCustomDialog::Construct(
		SCustomDialog::FArguments()
		.Title(LOCTEXT("RemoveActivityTitle", "Remove activity"))
		.Buttons({
			FButton(LOCTEXT("DeleteActivity.ConfirmButtonLabel", "Delete"))
				.SetOnClicked(FSimpleDelegate::CreateSP(this, &SDeleteActivityDependenciesDialog::OnConfirmPressed)),
			FButton(LOCTEXT("DeleteActivity.CancelButtonLabel", "Cancel"))
				.SetPrimary(true)
		})
		.Content()
		[
			SNew(SVerticalBox)
			
			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.AutoHeight()
			.Padding(0, 5, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DeleteActivity.HeaderBaseText", "Review the activities that will be deleted:"))
			]

			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				CreateBody(SessionId, SyncServer, InDeletionRequirements)
			]
		]
	);
}

TSharedRef<SWidget> SDeleteActivityDependenciesDialog::CreateBody(const FGuid& InSessionId, const TSharedRef<IConcertSyncServer>& InSyncServer, UE::ConcertSyncCore::FHistoryEditionArgs InDeletionRequirements)
{
	for (FActivityID HardDependency : DeletionRequirements.HardDependencies)
	{
		ActivitiesToDelete.Add(HardDependency, true);
	}
	for (FActivityID HardDependency : DeletionRequirements.PossibleDependencies)
	{
		ActivitiesToDelete.Add(HardDependency, false);
	}
	
	return SNew(SActivityDependencyView, DeletionRequirements)
		.CreateSessionHistory_Lambda([this, InSessionId, InSyncServer](const SSessionHistory::FArguments& InArgs)
		{
			const bool bOnlyCalledOnce = !FilteredSessionHistoryController.IsValid();
			check(bOnlyCalledOnce);
			
			FilteredSessionHistoryController = UE::MultiUserServer::CreateForDeletionDialog(
				InSessionId,
				InSyncServer,
				InArgs
				);
			return FilteredSessionHistoryController->GetSessionHistory();
		})
		.CreateActivityColumnWidget_Lambda([this](FActivityID ActivityID)
		{
			const bool bIsHardDependency = DeletionRequirements.HardDependencies.Contains(ActivityID);
			return SNew(SCheckBox)
				.IsEnabled(!bIsHardDependency)
				.IsChecked_Lambda([this, ActivityID]()
				{
					return ActivitiesToDelete[ActivityID] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, ActivityID](ECheckBoxState NewState)
				{
					ActivitiesToDelete[ActivityID] = NewState == ECheckBoxState::Checked;
				})
				.ToolTipText_Lambda([this, ActivityID, bIsHardDependency]()
				{
					if (bIsHardDependency)
					{
						return LOCTEXT("DeleteActivity.CheckBox.TooltipDisabled", "This is a hard dependency and must be deleted.");
					}

					const bool bWillBeDeleted = ActivitiesToDelete[ActivityID];
					if (bWillBeDeleted)
					{
						return LOCTEXT("DeleteActivity.CheckBox.TooltipEnabled.Deleted", "This is a possible dependency.\nWill be deleted.");
					}
					return LOCTEXT("DeleteActivity.CheckBox.TooltipEnabled.NotDeleted", "This is a possible dependency.\nWill not be deleted.");
				});
		});
}

void SDeleteActivityDependenciesDialog::OnConfirmPressed()
{
	for (const FActivityID& ActivityID : TSet<FActivityID>(DeletionRequirements.PossibleDependencies))
	{
		const bool bShouldDelete = ActivitiesToDelete[ActivityID];
		if (!bShouldDelete)
		{
			DeletionRequirements.PossibleDependencies.Remove(ActivityID);
		}
	}
	
	OnConfirmDeletionFunc.Execute(DeletionRequirements);
}

#undef LOCTEXT_NAMESPACE
