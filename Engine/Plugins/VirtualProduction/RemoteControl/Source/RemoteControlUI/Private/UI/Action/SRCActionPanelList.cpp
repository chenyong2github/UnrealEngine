// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCActionPanelList.h"

#include "Action/RCAction.h"
#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "RCActionModel.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SRCActionPanel.h"
#include "SDropTarget.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/Action/Conditional/RCActionConditionalModel.h"
#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/RCUIHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRCPanelExposedField.h"
#include "UI/SRCPanelFieldGroup.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "SRCActionPanelList"

template <class ActionType>
void SRCActionPanelList<ActionType>::Construct(const FArguments& InArgs, const TSharedRef<SRCActionPanel> InActionPanel, TSharedPtr<FRCBehaviourModel> InBehaviourItem)
{
	SRCLogicPanelListBase::Construct(SRCLogicPanelListBase::FArguments());

	ActionPanelWeakPtr = InActionPanel;
	BehaviourItemWeakPtr = InBehaviourItem;
	
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	ListView = SNew(SListView<TSharedPtr<ActionType>>)
		.ListItemsSource(&ActionItems)
		.OnGenerateRow(this, &SRCActionPanelList::OnGenerateWidgetForList)
		.ListViewStyle(&RCPanelStyle->TableViewStyle)
		.HeaderRow(ActionType::GetHeaderRow());

	ChildSlot
	[
		SNew(SDropTarget)
		.VerticalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.VerticalDash"))
		.HorizontalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HorizontalDash"))
		.OnDropped_Lambda([this](const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) { return SRCActionPanelList::OnExposedFieldDrop(InDragDropEvent.GetOperation()); })
		.OnAllowDrop(this, &SRCActionPanelList::OnAllowDrop)
		.OnIsRecognized(this, &SRCActionPanelList::OnAllowDrop)
		[
			ListView.ToSharedRef()
		]
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

template <class ActionType>
bool SRCActionPanelList<ActionType>::IsEmpty() const
{
	return ActionItems.IsEmpty();
}

template <class ActionType>
int32 SRCActionPanelList<ActionType>::Num() const
{
	return ActionItems.Num();
}

template <class ActionType>
void SRCActionPanelList<ActionType>::Reset()
{
	if (TSharedPtr<SRCActionPanel> ActionPanel = GetActionPanel())
	{
		if (TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ActionPanel->GetRemoteControlPanel())
		{
			ActionItems.Empty();

			if (TSharedPtr<FRCBehaviourModel> BehaviourItem = BehaviourItemWeakPtr.Pin())
			{
				if (URCBehaviour* Behaviour = Cast<URCBehaviour>(BehaviourItem->GetBehaviour()))
				{
					for (URCAction* Action : Behaviour->ActionContainer->Actions)
					{
						TSharedPtr<ActionType> ActionItem = ActionType::GetModelByActionType(Action, BehaviourItem, RemoteControlPanel);

						ActionItems.Add(ActionItem);
					}
				}
			}

			ListView->RebuildList();
		}
	}
}

template <class ActionType>
URCAction* SRCActionPanelList<ActionType>::AddAction(const FGuid& InRemoteControlFieldId)
{
	if (const URemoteControlPreset* Preset = GetPreset())
	{
		if (TSharedPtr<const FRemoteControlField> RemoteControlField = Preset->GetExposedEntity<FRemoteControlField>(InRemoteControlFieldId).Pin())
		{
			if (const TSharedPtr<SRCActionPanel> ActionPanel = GetActionPanel())
			{
				return ActionPanel->AddAction(RemoteControlField.ToSharedRef());
			}
		}
	}

	return nullptr;
}

template <class ActionType>
FReply SRCActionPanelList<ActionType>::OnExposedFieldDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation)
	{
		if (DragDropOperation->IsOfType<FExposedEntityDragDrop>())
		{
			if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = StaticCastSharedPtr<FExposedEntityDragDrop>(DragDropOperation))
			{
				// Fetch the Exposed Entity
				const FGuid ExposedEntityId = DragDropOp->GetId();

				// Add Action
				AddAction(ExposedEntityId);
			}
		}
		else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
		{
			if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
			{
				if (URemoteControlPreset* Preset = GetPreset())
				{
					// Fetch the Group
					const FGuid GroupId = DragDropOp->GetGroupId();
					const FRemoteControlPresetGroup* Group = Preset->Layout.GetGroup(GroupId);

					if (ensure(Group))
					{
						const TArray<FGuid> GroupFields = Group->GetFields();

						// Add Action for all fields in the Group
						for (const FGuid RemoteControlFieldId : GroupFields)
						{
							AddAction(RemoteControlFieldId);
						}
					}
				}
			}
		}
	}

	return FReply::Handled();
}

template <class ActionType>
bool SRCActionPanelList<ActionType>::OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	return true;
}

template <class ActionType>
TSharedRef<ITableRow> SRCActionPanelList<ActionType>::OnGenerateWidgetForList(TSharedPtr<ActionType> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (ensure(InItem))
	{
		return InItem->OnGenerateWidgetForList(InItem, OwnerTable);
	}

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		[
			SNullWidget::NullWidget
		];
}

template <class ActionType>
void SRCActionPanelList<ActionType>::OnActionAdded(URCAction* InAction)
{
	Reset();
}

template <class ActionType>
void SRCActionPanelList<ActionType>::OnEmptyActions()
{
	Reset();
}

template <class ActionType>
URemoteControlPreset* SRCActionPanelList<ActionType>::GetPreset()
{
	if (ActionPanelWeakPtr.IsValid())
	{
		return ActionPanelWeakPtr.Pin()->GetPreset();
	}

	return nullptr;
}

template <class ActionType>
int32 SRCActionPanelList<ActionType>::RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel)
{
	if (const TSharedPtr<FRCBehaviourModel> BehaviourModel = BehaviourItemWeakPtr.Pin())
	{
		if (const URCBehaviour* Behaviour = BehaviourModel->GetBehaviour())
		{
			if(const TSharedPtr<ActionType> SelectedAction = StaticCastSharedPtr<ActionType>(InModel))
			{
				// Remove Model from Data Container
				const int32 RemoveCount = Behaviour->ActionContainer->RemoveAction(SelectedAction->GetAction());

				return RemoveCount;
			}
		}
	}

	return 0;
}

template <class ActionType>
bool SRCActionPanelList<ActionType>::IsListFocused() const
{
	return ListView->HasAnyUserFocus().IsSet();
}

template <class ActionType>
void SRCActionPanelList<ActionType>::DeleteSelectedPanelItem()
{
	DeleteItemFromLogicPanel<ActionType>(ActionItems, ListView->GetSelectedItems());
}

#undef LOCTEXT_NAMESPACE

template class SRCActionPanelList<FRCActionModel>;
template class SRCActionPanelList<FRCActionConditionalModel>;
