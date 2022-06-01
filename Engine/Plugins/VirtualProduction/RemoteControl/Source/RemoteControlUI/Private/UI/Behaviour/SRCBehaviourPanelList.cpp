// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourPanelList.h"

#include "Controller/RCController.h"
#include "RCBehaviourModel.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SRCBehaviourPanel.h"
#include "UI/Behaviour/Builtin/RCBehaviourIsEqualModel.h"
#include "UI/SRemoteControlPanel.h"
#include "UI/Controller/RCControllerModel.h"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanelList::Construct(const FArguments& InArgs, TSharedRef<SRCBehaviourPanel> InBehaviourPanel, TSharedPtr<FRCControllerModel> InControllerItem)
{
	SRCLogicPanelListBase::Construct(SRCLogicPanelListBase::FArguments());
	
	BehaviourPanelWeakPtr = InBehaviourPanel;
	ControllerItemWeakPtr = InControllerItem;
	
	ListView = SNew(SListView<TSharedPtr<FRCBehaviourModel>>)
		.ListItemsSource( &BehaviourItems )
		.OnSelectionChanged(this, &SRCBehaviourPanelList::OnTreeSelectionChanged)
		.OnGenerateRow(this, &SRCBehaviourPanelList::OnGenerateWidgetForList );
	
	ChildSlot
	[
		ListView.ToSharedRef()
	];

	// Add delegates
	const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = InBehaviourPanel->GetRemoteControlPanel();
	check(RemoteControlPanel)
	RemoteControlPanel->OnBehaviourAdded.AddSP(this, &SRCBehaviourPanelList::OnBehaviourAdded);
	RemoteControlPanel->OnEmptyBehaviours.AddSP(this, &SRCBehaviourPanelList::OnEmptyBehaviours);

	Reset();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanelList::Reset()
{
	BehaviourItems.Empty();

	if (TSharedPtr<FRCControllerModel> ControllerItem = ControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			for (URCBehaviour* Behaviour : Controller->Behaviours)
			{
				if (URCIsEqualBehaviour* IsEqualBehaviour = Cast<URCIsEqualBehaviour>(Behaviour))
				{
					BehaviourItems.Add(MakeShared<FRCIsEqualBehaviourModel>(IsEqualBehaviour));
				}
				else
				{
					BehaviourItems.Add(MakeShared<FRCBehaviourModel>(Behaviour));
				}
			}
		}
	}

	ListView->RebuildList();
}

TSharedRef<ITableRow> SRCBehaviourPanelList::OnGenerateWidgetForList(TSharedPtr<FRCBehaviourModel> InItem,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
	[
		InItem->GetWidget()
	];
}

void SRCBehaviourPanelList::OnTreeSelectionChanged(TSharedPtr<FRCBehaviourModel> InItem, ESelectInfo::Type)
{
	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanelWeakPtr.Pin()->GetRemoteControlPanel())
	{
		if (InItem.IsValid())
		{
			if (InItem != SelectedBehaviourItemWeakPtr.Pin())
			{
				RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(InItem);
			}
		}

		SelectedBehaviourItemWeakPtr = InItem;

		if (const TSharedPtr<FRCControllerModel> ControllerItem = ControllerItemWeakPtr.Pin())
		{
			ControllerItem->UpdateSelectedBehaviourModel(InItem);
		}
	}
}

void SRCBehaviourPanelList::OnBehaviourAdded(URCBehaviour* InBehaviour)
{
	Reset();
}

void SRCBehaviourPanelList::OnEmptyBehaviours()
{
	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanelWeakPtr.Pin()->GetRemoteControlPanel())
	{
		RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(nullptr);	
	}
	
	Reset();
}

void SRCBehaviourPanelList::BroadcastOnItemRemoved()
{
	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanelWeakPtr.Pin()->GetRemoteControlPanel())
	{
		RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(nullptr);
	}
}

URemoteControlPreset* SRCBehaviourPanelList::GetPreset()
{
	if (BehaviourPanelWeakPtr.IsValid())
	{
		return BehaviourPanelWeakPtr.Pin()->GetPreset();
	}

	return nullptr;
}

int32 SRCBehaviourPanelList::RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel)
{
	if (const TSharedPtr<FRCControllerModel> ControllerModel = ControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerModel->GetVirtualProperty()))
		{
			if(const TSharedPtr<FRCBehaviourModel> SelectedBehaviour = StaticCastSharedPtr<FRCBehaviourModel>(InModel))
			{
				// Remove Model from Data Container
				const int32 RemoveCount = Controller->RemoveBehaviour(SelectedBehaviour->GetBehaviour());

				return RemoveCount;
			}
		}
	}

	return 0;
}

bool SRCBehaviourPanelList::IsListFocused() const
{
	return ListView->HasAnyUserFocus().IsSet();
}

void SRCBehaviourPanelList::DeleteSelectedPanelItem()
{
	DeleteItemFromLogicPanel<FRCBehaviourModel>(BehaviourItems, ListView->GetSelectedItems());
}
