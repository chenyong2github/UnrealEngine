// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCControllerPanelList.h"

#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "RCControllerModel.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SRCControllerPanel.h"
#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UI/SRemoteControlPanel.h"

#define LOCTEXT_NAMESPACE "SRCControllerPanelList"

namespace UE::RCControllerPanelList
{
	static const FName ControllerNameColumn(TEXT("Controller Name"));
	static const FName ControllerValueColumn(TEXT("Controller Value"));

	class SControllerItemListRow : public SMultiColumnTableRow<TSharedRef<FRCControllerModel>>
	{
	private:
		TSharedPtr<FRCControllerModel> ControllerItem;

	public:
		void Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, TSharedRef<FRCControllerModel> InControllerItem)
		{
			ControllerItem = InControllerItem;
			FSuperRowType::Construct(InArgs, OwnerTableView);
		}

		TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!ensure(ControllerItem.IsValid()))
				return SNullWidget::NullWidget;

			if (ColumnName == UE::RCControllerPanelList::ControllerNameColumn)
			{
				return ControllerItem->GetNameWidget();
			}
			else if (ColumnName == UE::RCControllerPanelList::ControllerValueColumn)
			{
				return ControllerItem->GetWidget();
			}

			return SNullWidget::NullWidget;
		}
	};
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCControllerPanelList::Construct(const FArguments& InArgs, const TSharedRef<SRCControllerPanel> InControllerPanel)
{
	SRCLogicPanelListBase::Construct(SRCLogicPanelListBase::FArguments());
	
	ControllerPanelWeakPtr = InControllerPanel;
	
	ListView = SNew(SListView<TSharedPtr<FRCControllerModel>>)
		.ListItemsSource(&ControllerItems)
		.OnSelectionChanged(this, &SRCControllerPanelList::OnTreeSelectionChanged)
		.OnGenerateRow(this, &SRCControllerPanelList::OnGenerateWidgetForList)
		.SelectionMode(ESelectionMode::Single) // Current setup supports only single selection (and related display) of a Controller in the list
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(UE::RCControllerPanelList::ControllerNameColumn)
			.DefaultLabel(LOCTEXT("Controller Name Column Name", "Name"))
			.FillWidth(0.25f)

			+ SHeaderRow::Column(UE::RCControllerPanelList::ControllerValueColumn)
			.DefaultLabel(LOCTEXT("Controller Value Column Name", "Input"))
			.FillWidth(0.75f)
		);

	ChildSlot
	[
		ListView.ToSharedRef()
	];

	// Add delegates
	if (const URemoteControlPreset* Preset = ControllerPanelWeakPtr.Pin()->GetPreset())
	{
		// Refresh list
		const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanelWeakPtr.Pin()->GetRemoteControlPanel();
		check(RemoteControlPanel)
		RemoteControlPanel->OnControllerAdded.AddSP(this, &SRCControllerPanelList::OnControllerAdded);
		RemoteControlPanel->OnEmptyControllers.AddSP(this, &SRCControllerPanelList::OnEmptyControllers);
	}

	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);
	
	Reset();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCControllerPanelList::Reset()
{
	ControllerItems.Empty();

	check(ControllerPanelWeakPtr.IsValid());

	TSharedPtr<SRCControllerPanel> ControllerPanel = ControllerPanelWeakPtr.Pin();
	URemoteControlPreset* Preset = ControllerPanel->GetPreset();
	TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanel->GetRemoteControlPanel();

	check(Preset);

	PropertyRowGenerator->SetStructure(Preset->GetControllerContainerStructOnScope());
	PropertyRowGenerator->OnFinishedChangingProperties().AddSP(this, &SRCControllerPanelList::OnFinishedChangingProperties);

	// Generator should be moved to separate class
	for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGenerator->GetRootTreeNodes())
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		CategoryNode->GetChildren(Children);
		for (TSharedRef<IDetailTreeNode>& Child : Children)
		{
			FProperty* Property = Child->CreatePropertyHandle()->GetProperty();
			check(Property);

			if (URCVirtualPropertyBase* Controller = Preset->GetVirtualProperty(Property->GetFName()))
			{
				ControllerItems.Add(MakeShared<FRCControllerModel>(Controller, Child, RemoteControlPanel));
			}
		}
	}

	ListView->RebuildList();
}

TSharedRef<ITableRow> SRCControllerPanelList::OnGenerateWidgetForList(TSharedPtr<FRCControllerModel> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	typedef UE::RCControllerPanelList::SControllerItemListRow ControllerRowType;

	const TSharedRef<ControllerRowType> NewRow =
		SNew(ControllerRowType, OwnerTable, InItem.ToSharedRef())
		.Padding(FMargin(3.f));

	return NewRow;
}

void SRCControllerPanelList::OnTreeSelectionChanged(TSharedPtr<FRCControllerModel> InItem, ESelectInfo::Type)
{
	if (TSharedPtr<SRCControllerPanel> ControllerPanel = ControllerPanelWeakPtr.Pin())
	{
		if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanel->GetRemoteControlPanel())
		{
			if (InItem.IsValid())
			{
				if (InItem != SelectedControllerItemWeakPtr.Pin())
				{
					RemoteControlPanel->OnControllerSelectionChanged.Broadcast(InItem);
					RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(InItem->GetSelectedBehaviourModel());
				}
			}

			SelectedControllerItemWeakPtr = InItem;
		}
	}
}

void SRCControllerPanelList::OnControllerAdded(const FName& InNewPropertyName)
{
	Reset();
}

void SRCControllerPanelList::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (TSharedPtr< SRCControllerPanel> ControllerPanel = ControllerPanelWeakPtr.Pin())
	{
		if (URemoteControlPreset* Preset = ControllerPanel->GetPreset())
		{
			Preset->OnModifyVirtualProperty(PropertyChangedEvent);
		}
	}
}

void SRCControllerPanelList::OnEmptyControllers()
{
	if (TSharedPtr< SRCControllerPanel> ControllerPanel = ControllerPanelWeakPtr.Pin())
	{
		if (TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanel->GetRemoteControlPanel())
		{
			RemoteControlPanel->OnControllerSelectionChanged.Broadcast(nullptr);
			RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(nullptr);
		}

		Reset();
	}
}

void SRCControllerPanelList::BroadcastOnItemRemoved()
{
	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanelWeakPtr.Pin()->GetRemoteControlPanel())
	{
		RemoteControlPanel->OnControllerSelectionChanged.Broadcast(nullptr);
		RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(nullptr);
	}
}

URemoteControlPreset* SRCControllerPanelList::GetPreset()
{
	if (ControllerPanelWeakPtr.IsValid())
	{
		return ControllerPanelWeakPtr.Pin()->GetPreset();
	}

	return nullptr;	
}

int32 SRCControllerPanelList::RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel)
{
	if(ControllerPanelWeakPtr.IsValid())
	{
		if (URemoteControlPreset* Preset = ControllerPanelWeakPtr.Pin()->GetPreset())
		{
			if(const TSharedPtr<FRCControllerModel> SelectedController = StaticCastSharedPtr<FRCControllerModel>(InModel))
			{
				// Remove Model from Data Container
				const bool bRemoved = Preset->RemoveVirtualProperty(SelectedController->GetPropertyName());
				if (bRemoved)
				{
					return 1; // Remove Count
				}
			}
		}
	}

	return 0;
}

bool SRCControllerPanelList::IsListFocused() const
{
	return ListView->HasAnyUserFocus().IsSet();
}

void SRCControllerPanelList::DeleteSelectedPanelItem()
{
	DeleteItemFromLogicPanel<FRCControllerModel>(ControllerItems, ListView->GetSelectedItems());
}

void SRCControllerPanelList::EnterRenameMode()
{
	if (TSharedPtr<FRCControllerModel> SelectedItem = SelectedControllerItemWeakPtr.Pin())
	{
		SelectedItem->EnterRenameMode();
	}
}

#undef LOCTEXT_NAMESPACE