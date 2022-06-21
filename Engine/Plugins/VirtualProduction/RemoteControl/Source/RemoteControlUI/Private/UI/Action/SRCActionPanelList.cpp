// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCActionPanelList.h"

#include "Action/RCAction.h"
#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "RCActionModel.h"
#include "SlateOptMacros.h"
#include "SRCActionPanel.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/RCUIHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "SRCActionPanelList"

namespace FRemoteControlActionColumns
{
	const FName DragDropHandle = TEXT("DragDropHandle");
	const FName Description = TEXT("Description");
	const FName Value = TEXT("Value");
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCActionPanelList::Construct(const FArguments& InArgs, const TSharedRef<SRCActionPanel> InActionPanel, TSharedPtr<FRCBehaviourModel> InBehaviourItem)
{
	SRCLogicPanelListBase::Construct(SRCLogicPanelListBase::FArguments());

	ActionPanelWeakPtr = InActionPanel;
	BehaviourItemWeakPtr = InBehaviourItem;
	
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	ListView = SNew(SListView<TSharedPtr<FRCActionModel>>)
		.ListItemsSource( &ActionItems )
		.OnGenerateRow(this, &SRCActionPanelList::OnGenerateWidgetForList )
		.ListViewStyle(&RCPanelStyle->TableViewStyle)
		.HeaderRow(
			SNew(SHeaderRow)
			.Style(&RCPanelStyle->HeaderRowStyle)

			+ SHeaderRow::Column(FRemoteControlActionColumns::DragDropHandle)
			.DefaultLabel(LOCTEXT("RCActionDragDropHandleColumnHeader", ""))
			.FixedWidth(25.f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(FRemoteControlActionColumns::Description)
			.DefaultLabel(LOCTEXT("RCActionDescColumnHeader", "Description"))
			.FillWidth(0.5f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(FRemoteControlActionColumns::Value)
			.DefaultLabel(LOCTEXT("RCActionValueColumnHeader", "Value"))
			.FillWidth(0.5f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)
		);
	
	ChildSlot
	[
		ListView.ToSharedRef()
	];

	// Add delegates
	const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = InActionPanel->GetRemoteControlPanel();
	if (ensure(RemoteControlPanel))
	{
		RemoteControlPanel->OnActionAdded.AddSP(this, &SRCActionPanelList::OnActionAdded);
		RemoteControlPanel->OnEmptyActions.AddSP(this, &SRCActionPanelList::OnEmptyActions);
	}

	Reset();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool SRCActionPanelList::IsEmpty() const
{
	return ActionItems.IsEmpty();
}

int32 SRCActionPanelList::Num() const
{
	return ActionItems.Num();
}

void SRCActionPanelList::Reset()
{
	ActionItems.Empty();

	if (TSharedPtr<FRCBehaviourModel> BehaviourItem = BehaviourItemWeakPtr.Pin())
	{
		if (URCBehaviour* Behaviour = Cast<URCBehaviour>(BehaviourItem->GetBehaviour()))
		{
			for (URCAction* Action : Behaviour->ActionContainer->Actions)
			{
				if (URCPropertyAction* PropertyAction = Cast<URCPropertyAction>(Action))
				{
					ActionItems.Add(MakeShared<FRCPropertyActionModel>(PropertyAction));
				}
				else if (URCFunctionAction* FunctionAction = Cast<URCFunctionAction>(Action))
				{
					ActionItems.Add(MakeShared<FRCFunctionActionModel>(FunctionAction));
				}
			}
		}
	}

	ListView->RebuildList();
}

TSharedRef<ITableRow> SRCActionPanelList::OnGenerateWidgetForList(TSharedPtr<FRCActionModel> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Style(&RCPanelStyle->TableRowStyle)
		[
			InItem->GetWidget()
		];
}

void SRCActionPanelList::OnActionAdded(URCAction* InAction)
{
	Reset();
}

void SRCActionPanelList::OnEmptyActions()
{
	Reset();
}

URemoteControlPreset* SRCActionPanelList::GetPreset()
{
	if (ActionPanelWeakPtr.IsValid())
	{
		return ActionPanelWeakPtr.Pin()->GetPreset();
	}

	return nullptr;
}

int32 SRCActionPanelList::RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel)
{
	if (const TSharedPtr<FRCBehaviourModel> BehaviourModel = BehaviourItemWeakPtr.Pin())
	{
		if (const URCBehaviour* Behaviour = BehaviourModel->GetBehaviour())
		{
			if(const TSharedPtr<FRCActionModel> SelectedAction = StaticCastSharedPtr<FRCActionModel>(InModel))
			{
				// Remove Model from Data Container
				const int32 RemoveCount = Behaviour->ActionContainer->RemoveAction(SelectedAction->GetAction());

				return RemoveCount;
			}
		}
	}

	return 0;
}

bool SRCActionPanelList::IsListFocused() const
{
	return ListView->HasAnyUserFocus().IsSet();
}

void SRCActionPanelList::DeleteSelectedPanelItem()
{
	DeleteItemFromLogicPanel<FRCActionModel>(ActionItems, ListView->GetSelectedItems());
}

#undef LOCTEXT_NAMESPACE