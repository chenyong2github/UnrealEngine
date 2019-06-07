// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SSessionHistory.h"
#include "IConcertSession.h"
#include "IConcertSyncClient.h"
#include "ConcertTransactionEvents.h"
#include "ConcertWorkspaceData.h"
#include "ConcertMessageData.h"
#include "ConcertMessages.h"
#include "ConcertFrontendStyle.h"
#include "Editor/Transactor.h"
#include "Algo/Transform.h"
#include "EditorStyleSet.h"
#include "SPackageDetails.h"
#include "Widgets/SUndoHistoryDetails.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "ConcertFrontendUtils.h"
#include "SConcertScrollBox.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SSessionHistory"

namespace ConcertSessionHistoryUI
{
	bool PackageNamePassesFilter(const FName& PackageNameFilter, const TStructOnScope<FConcertSyncActivitySummary>& InActivitySummary)
	{
		if (PackageNameFilter.IsNone())
		{
			return true;
		}

		if (const FConcertSyncLockActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncLockActivitySummary>())
		{
			return Summary->PrimaryPackageName == PackageNameFilter;
		}

		if (const FConcertSyncTransactionActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
		{
			return Summary->PrimaryPackageName == PackageNameFilter;
		}

		if (const FConcertSyncPackageActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncPackageActivitySummary>())
		{
			return Summary->PackageName == PackageNameFilter;
		}

		return false;
	}
}

void SSessionHistory::Construct(const FArguments& InArgs, TSharedPtr<IConcertSyncClient> InConcertSyncClient)
{
	PackageNameFilter = InArgs._PackageFilter;

	ActivityMap.Reserve(MaximumNumberOfActivities);
	Activities.Reserve(MaximumNumberOfActivities);

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+SSplitter::Slot()
		.Value(0.75)
		[
			SAssignNew(ScrollBox, SConcertScrollBox)
			+SConcertScrollBox::Slot()
			[
				SAssignNew(ActivityListView, SListView<TSharedPtr<FConcertClientSessionActivity>>)
				.OnGenerateRow(this, &SSessionHistory::HandleGenerateRow)
				.OnSelectionChanged(this, &SSessionHistory::HandleSelectionChanged)
				.ListItemsSource(&Activities)
			]
		]

		+SSplitter::Slot()
		.Value(0.25)
		.SizeRule(TAttribute<SSplitter::ESizeRule>(this, &SSessionHistory::GetDetailsAreaSizeRule))
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.0f)
			[
				SAssignNew(ExpandableDetails, SExpandableArea)
				.Visibility(EVisibility::Visible)
				.InitiallyCollapsed(true)
				.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
				.BorderImage_Lambda([this]() { return ConcertFrontendUtils::GetExpandableAreaBorderImage(*ExpandableDetails); })
				.BodyBorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.BodyBorderBackgroundColor(FLinearColor::White)
				.OnAreaExpansionChanged(this, &SSessionHistory::OnDetailsAreaExpansionChanged)
				.Padding(0.0f)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString("Details")))
					.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.ShadowOffset(FVector2D(1.0f, 1.0f))
				]
				.BodyContent()
				[
					SNew(SScrollBox)
					.ScrollBarThickness(FVector2D(8.0f, 5.0f)) // To have same thickness than the ListView scroll bar and SConcertScrollBox
					+SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.Padding(0.0f)
						[
							SAssignNew(TransactionDetails, SUndoHistoryDetails)
							.Visibility(EVisibility::Collapsed)
						]

						+SVerticalBox::Slot()
						.Padding(0.0f)
						[
							SAssignNew(PackageDetails, SPackageDetails)
							.Visibility(EVisibility::Collapsed)
						]
					]
				]
			]
		]
	];

	ExpandableDetails->SetEnabled(false);

	if (InConcertSyncClient.IsValid())
	{
		InConcertSyncClient->OnWorkspaceStartup().AddSP(this, &SSessionHistory::HandleWorkspaceStartup);
		InConcertSyncClient->OnWorkspaceShutdown().AddSP(this, &SSessionHistory::HandleWorkspaceShutdown);

		if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = InConcertSyncClient->GetWorkspace())
		{
			Workspace = WorkspacePtr;
			RegisterWorkspaceHandler();
			ReloadActivities();
		}
	}
}

void SSessionHistory::Refresh()
{
	ReloadActivities();
}

TSharedRef<ITableRow> SSessionHistory::HandleGenerateRow(TSharedPtr<FConcertClientSessionActivity> InSessionActivity, const TSharedRef<STableViewBase>& OwnerTable) const
{
	FText ActivityText = LOCTEXT("InvalidActivity", "INVALID_ACTIVITY");
	FText ActivityTooltipText;
	FLinearColor AvatarColor;

	if (InSessionActivity.IsValid())
	{
		const FConcertClientInfo& ClientInfo = EndpointClientInfoMap.FindChecked(InSessionActivity->Activity.EndpointId);
		
		ActivityText = FText::Format(INVTEXT("{0}  {1}"), FText::AsDateTime(InSessionActivity->Activity.EventTime), InSessionActivity->ActivitySummary->ToDisplayText(FText::AsCultureInvariant(ClientInfo.DisplayName), true));
		ActivityTooltipText = InSessionActivity->ActivitySummary->ToDisplayText(FText::AsCultureInvariant(ClientInfo.DisplayName), false);

		AvatarColor = ClientInfo.AvatarColor;
		if (ClientInfo.DisplayName.IsEmpty())
		{
			// The client info is malformed or the user has disconnected.
			AvatarColor = FConcertFrontendStyle::Get()->GetColor("Concert.DisconnectedColor");
		}
	}

	return SNew(STableRow<TSharedPtr<FText>>, OwnerTable)
		.Padding(2.0)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(0.f, 0.f, 3.f, 0.f)
			.AutoWidth()
			[
				SNew(SColorBlock)
				.Color(AvatarColor)
				.Size(FVector2D(4.f, 20.f))
			]

			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SRichTextBlock)
				.DecoratorStyleSet(FConcertFrontendStyle::Get().Get())
				.Text(ActivityText)
				.ToolTipText(ActivityTooltipText)
			]
		];
}

void SSessionHistory::HandleSelectionChanged(TSharedPtr<FConcertClientSessionActivity> InSessionActivity, ESelectInfo::Type SelectInfo)
{
	if (!InSessionActivity.IsValid())
	{
		return;
	}

	if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin())
	{
		switch (InSessionActivity->Activity.EventType)
		{
		case EConcertSyncActivityEventType::Transaction:
		{
			FConcertSyncTransactionEvent TransactionEvent;
			if (WorkspacePtr->FindTransactionEvent(InSessionActivity->Activity.EventId, TransactionEvent, /*bMetaDataOnly*/false))
			{
				FText TransactionTitle;
				if (const FConcertSyncTransactionActivitySummary* Summary = InSessionActivity->ActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
				{
					TransactionTitle = Summary->TransactionTitle;
				}

				// TODO: Request full transaction data if it was only partially synced?
				DisplayTransactionDetails(TransactionEvent.Transaction, TransactionTitle.ToString());
				return;
			}
		}
		break;

		case EConcertSyncActivityEventType::Package:
		{
			FConcertSyncPackageEvent PackageEvent;
			if (WorkspacePtr->FindPackageEvent(InSessionActivity->Activity.EventId, PackageEvent, /*bMetaDataOnly*/true))
			{
				const FConcertClientInfo& ClientInfo = EndpointClientInfoMap.FindChecked(InSessionActivity->Activity.EndpointId);

				// TODO: Request full package data if it was only partially synced?
				DisplayPackageDetails(PackageEvent.Package.Info, PackageEvent.PackageRevision, ClientInfo.DisplayName);
				return;
			}
		}
		break;

		default:
			break;
		}
	}

	TransactionDetails->SetVisibility(EVisibility::Collapsed);
	PackageDetails->SetVisibility(EVisibility::Collapsed);
	ExpandableDetails->SetEnabled(false);
	ExpandableDetails->SetExpanded(false);
}

void SSessionHistory::ReloadActivities()
{
	EndpointClientInfoMap.Reset();
	ActivityMap.Reset();
	Activities.Reset();

	if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin())
	{
		const int64 LastActivityId = WorkspacePtr->GetLastActivityId();
		const int64 FirstActivityIdToFetch = FMath::Max<int64>(1, LastActivityId - MaximumNumberOfActivities);

		TArray<FConcertClientSessionActivity> FetchedActivities;
		WorkspacePtr->GetActivities(FirstActivityIdToFetch, MaximumNumberOfActivities, EndpointClientInfoMap, FetchedActivities);

		for (FConcertClientSessionActivity& FetchedActivity : FetchedActivities)
		{
			if (ConcertSessionHistoryUI::PackageNamePassesFilter(PackageNameFilter, FetchedActivity.ActivitySummary))
			{
				TSharedRef<FConcertClientSessionActivity> NewActivity = MakeShared<FConcertClientSessionActivity>(MoveTemp(FetchedActivity));
				ActivityMap.Add(NewActivity->Activity.ActivityId, NewActivity);
				Activities.Add(NewActivity);
			}
		}
	}

	ActivityListView->RequestListRefresh();
}

void SSessionHistory::HandleActivityAddedOrUpdated(const FConcertClientInfo& InClientInfo, const FConcertSyncActivity& InActivity, const FStructOnScope& InActivitySummary)
{
	TStructOnScope<FConcertSyncActivitySummary> ActivitySummary;
	ActivitySummary.InitializeFromChecked(InActivitySummary);

	if (ConcertSessionHistoryUI::PackageNamePassesFilter(PackageNameFilter, ActivitySummary))
	{
		EndpointClientInfoMap.Add(InActivity.EndpointId, InClientInfo);

		if (TSharedPtr<FConcertClientSessionActivity> ExistingActivity = ActivityMap.FindRef(InActivity.ActivityId))
		{
			ExistingActivity->Activity = InActivity;
			ExistingActivity->ActivitySummary = MoveTemp(ActivitySummary);
		}
		else
		{
			TSharedRef<FConcertClientSessionActivity> NewActivity = MakeShared<FConcertClientSessionActivity>();
			NewActivity->Activity = InActivity;
			NewActivity->ActivitySummary = MoveTemp(ActivitySummary);

			ActivityMap.Add(NewActivity->Activity.ActivityId, NewActivity);
			Activities.Add(NewActivity);
		}
	}

	ActivityListView->RequestListRefresh();
}

void SSessionHistory::HandleWorkspaceStartup(const TSharedPtr<IConcertClientWorkspace>& NewWorkspace)
{
	Workspace = NewWorkspace;
	RegisterWorkspaceHandler();
}

void SSessionHistory::HandleWorkspaceShutdown(const TSharedPtr<IConcertClientWorkspace>& WorkspaceShuttingDown)
{
	if (WorkspaceShuttingDown == Workspace)
	{
		Workspace.Reset();
		ReloadActivities();
	}
}

void SSessionHistory::RegisterWorkspaceHandler()
{
	TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin();
	if (WorkspacePtr.IsValid())
	{
		WorkspacePtr->OnActivityAddedOrUpdated().AddSP(this, &SSessionHistory::HandleActivityAddedOrUpdated);
		WorkspacePtr->OnWorkspaceSynchronized().AddSP(this, &SSessionHistory::ReloadActivities);
	}
}

void SSessionHistory::DisplayTransactionDetails(const FConcertTransactionEventBase& InTransaction, const FString& InTransactionTitle)
{
	FTransactionDiff TransactionDiff{ InTransaction.TransactionId, InTransactionTitle };

	for (const auto& ExportedObject : InTransaction.ExportedObjects)
	{
		FTransactionObjectDeltaChange DeltaChange;

		Algo::Transform(ExportedObject.PropertyDatas, DeltaChange.ChangedProperties, [](const FConcertSerializedPropertyData& PropertyData) { return PropertyData.PropertyName; });

		DeltaChange.bHasNameChange = ExportedObject.ObjectData.NewOuterPathName != FName();
		DeltaChange.bHasOuterChange = ExportedObject.ObjectData.NewOuterPathName != FName();
		DeltaChange.bHasPendingKillChange = ExportedObject.ObjectData.bIsPendingKill;

		FString ObjectPathName = ExportedObject.ObjectId.ObjectOuterPathName.ToString() + TEXT(".") + ExportedObject.ObjectId.ObjectName.ToString();

		TSharedPtr<FTransactionObjectEvent> Event = MakeShared<FTransactionObjectEvent>(InTransaction.TransactionId, InTransaction.OperationId, ETransactionObjectEventType::Finalized, MoveTemp(DeltaChange), nullptr, ExportedObject.ObjectId.ObjectName, FName(*MoveTemp(ObjectPathName)), ExportedObject.ObjectId.ObjectOuterPathName, ExportedObject.ObjectId.ObjectClassPathName);

		TransactionDiff.DiffMap.Emplace(FName(*ObjectPathName), MoveTemp(Event));
	}

	TransactionDetails->SetSelectedTransaction(MoveTemp(TransactionDiff));

	PackageDetails->SetVisibility(EVisibility::Collapsed);
	TransactionDetails->SetVisibility(EVisibility::Visible);

	ExpandableDetails->SetEnabled(true);
	ExpandableDetails->SetExpanded(true);
}

void SSessionHistory::DisplayPackageDetails(const FConcertPackageInfo& InPackageInfo, const int64 InRevision, const FString& InModifiedBy)
{
	PackageDetails->SetPackageInfo(InPackageInfo, InRevision, InModifiedBy);

	TransactionDetails->SetVisibility(EVisibility::Collapsed);
	PackageDetails->SetVisibility(EVisibility::Visible);

	ExpandableDetails->SetEnabled(true);
	ExpandableDetails->SetExpanded(true);
}

#undef LOCTEXT_NAMESPACE /* SSessionHistory */
