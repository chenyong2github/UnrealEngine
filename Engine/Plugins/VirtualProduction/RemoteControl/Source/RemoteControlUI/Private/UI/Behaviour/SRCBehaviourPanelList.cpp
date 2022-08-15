// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourPanelList.h"

#include "Behaviour/Builtin/Bind/RCBehaviourBind.h"
#include "Behaviour/Builtin/Conditional/RCBehaviourConditional.h"
#include "Behaviour/RCSetAssetByPathBehaviour.h"
#include "Controller/RCController.h"
#include "RCBehaviourModel.h"
#include "RemoteControlPreset.h"
#include "SRCBehaviourPanel.h"
#include "SlateOptMacros.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/Behaviour/Builtin/Bind/RCBehaviourBindModel.h"
#include "UI/Behaviour/Builtin/Conditional/RCBehaviourConditionalModel.h"
#include "UI/Behaviour/Builtin/RCBehaviourIsEqualModel.h"
#include "UI/Behaviour/Builtin/RCBehaviourSetAssetByPathModel.h"
#include "UI/Controller/RCControllerModel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelBehavioursList"

namespace FRemoteControlBehaviourColumns
{
	const FName Behaviours = TEXT("Behaviors");
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanelList::Construct(const FArguments& InArgs, TSharedRef<SRCBehaviourPanel> InBehaviourPanel, TSharedPtr<FRCControllerModel> InControllerItem)
{
	SRCLogicPanelListBase::Construct(SRCLogicPanelListBase::FArguments());
	
	BehaviourPanelWeakPtr = InBehaviourPanel;
	ControllerItemWeakPtr = InControllerItem;
	
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	ListView = SNew(SListView<TSharedPtr<FRCBehaviourModel>>)
		.ListItemsSource(&BehaviourItems)
		.OnSelectionChanged(this, &SRCBehaviourPanelList::OnTreeSelectionChanged)
		.OnGenerateRow(this, &SRCBehaviourPanelList::OnGenerateWidgetForList)
		.ListViewStyle(&RCPanelStyle->TableViewStyle)
		.HeaderRow(
			SNew(SHeaderRow)
			.Style(&RCPanelStyle->HeaderRowStyle)

			+ SHeaderRow::Column(FRemoteControlBehaviourColumns::Behaviours)
			.DefaultLabel(LOCTEXT("RCBehaviourColumnHeader", "Behaviors"))
			.FillWidth(1.f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)
		);
	
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

bool SRCBehaviourPanelList::IsEmpty() const
{
	return BehaviourItems.IsEmpty();
}

int32 SRCBehaviourPanelList::Num() const
{
	return BehaviourItems.Num();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanelList::Reset()
{
	BehaviourItems.Empty();

	const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanelWeakPtr.IsValid() ? BehaviourPanelWeakPtr.Pin()->GetRemoteControlPanel() : nullptr;

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
				else if (URCBehaviourConditional* ConditionalBehaviour = Cast<URCBehaviourConditional>(Behaviour))
				{
					BehaviourItems.Add(MakeShared<FRCBehaviourConditionalModel>(ConditionalBehaviour));
				}
				else if (URCSetAssetByPathBehaviour* SetAssetByPathBehaviour = Cast<URCSetAssetByPathBehaviour>(Behaviour))
				{
					BehaviourItems.Add(MakeShared<FRCSetAssetByPathBehaviourModel>(SetAssetByPathBehaviour));
				}
				else if (URCBehaviourBind* BindBehaviour = Cast<URCBehaviourBind>(Behaviour))
				{
					BehaviourItems.Add(MakeShared<FRCBehaviourBindModel>(BindBehaviour, RemoteControlPanel));
				}
				else
				{
					BehaviourItems.Add(MakeShared<FRCBehaviourModel>(Behaviour, RemoteControlPanel));
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
		.Style(&RCPanelStyle->TableRowStyle)
		[
			InItem->GetWidget()
		];
}

void SRCBehaviourPanelList::OnTreeSelectionChanged(TSharedPtr<FRCBehaviourModel> InItem, ESelectInfo::Type)
{
	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanelWeakPtr.Pin()->GetRemoteControlPanel())
	{
		if (InItem != SelectedBehaviourItemWeakPtr.Pin())
		{
			SelectedBehaviourItemWeakPtr = InItem;

			RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(InItem);
		}

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

#undef LOCTEXT_NAMESPACE
