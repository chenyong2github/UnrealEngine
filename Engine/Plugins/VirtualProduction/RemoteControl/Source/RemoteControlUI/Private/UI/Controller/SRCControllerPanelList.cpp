// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCControllerPanelList.h"

#include "Commands/RemoteControlCommands.h"
#include "Controller/RCControllerContainer.h"
#include "IDetailTreeNode.h"
#include "Interfaces/IMainFrameModule.h"
#include "IPropertyRowGenerator.h"
#include "RCControllerModel.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlPreset.h"
#include "SDropTarget.h"
#include "SlateOptMacros.h"
#include "SRCControllerPanel.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/Action/SRCActionPanelList.h"
#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRCPanelExposedEntity.h"
#include "UI/SRCPanelDragHandle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Views/SHeaderRow.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "SRCControllerPanelList"

namespace UE::RCControllerPanelList
{
	namespace Columns
	{
		const FName Name = TEXT("Controller Name");
		const FName Value = TEXT("Controller Value");
		const FName DragHandle = TEXT("Drag Handle");
	}

	class SControllerItemListRow : public SMultiColumnTableRow<TSharedRef<FRCControllerModel>>
	{
	public:
		void Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, TSharedRef<FRCControllerModel> InControllerItem, TSharedRef<SRCControllerPanelList> InControllerPanelList)
		{
			ControllerItem = InControllerItem;
			ControllerPanelList = InControllerPanelList;
			FSuperRowType::Construct(InArgs, OwnerTableView);
		}

		TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!ensure(ControllerItem.IsValid()))
				return SNullWidget::NullWidget;

			if (ColumnName == UE::RCControllerPanelList::Columns::Name)
			{
				return WrapWithDropTarget(ControllerItem->GetNameWidget());
			}
			else if (ColumnName == UE::RCControllerPanelList::Columns::Value)
			{
				return ControllerItem->GetWidget();
			}
			else if (ColumnName == UE::RCControllerPanelList::Columns::DragHandle)
			{
				SAssignNew(DragDropBorderWidget, SBorder)
					.BorderImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.ExposedFieldBorder"));

				TSharedRef<SWidget> DragHandleWidget = 
					SNew(SBox)
					.Padding(5.f)
					[
						SNew(SRCPanelDragHandle<FRCControllerDragDrop>, ControllerItem->GetId())
						.Widget(DragDropBorderWidget)
					];

				return WrapWithDropTarget(DragHandleWidget);
			}

			return SNullWidget::NullWidget;
		}

		TSharedPtr<SBorder> DragDropBorderWidget;

	private:

		TSharedRef<SWidget> WrapWithDropTarget(const TSharedRef<SWidget> InWidget)
		{
			return SNew(SDropTarget)
				.VerticalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.VerticalDash"))
				.HorizontalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HorizontalDash"))
				.OnDropped_Lambda([this](const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) { return SControllerItemListRow::OnControllerItemDragDrop(InDragDropEvent.GetOperation()); })
				.OnAllowDrop(this, &SControllerItemListRow::OnAllowDrop)
				.OnIsRecognized(this, &SControllerItemListRow::OnAllowDrop)
				[
					InWidget
				];
		}

		FReply OnControllerItemDragDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
		{
			if (!DragDropOperation)
			{
				return FReply::Handled();
			}

			if (DragDropOperation->IsOfType<FRCControllerDragDrop>())
			{
				if (TSharedPtr<FRCControllerDragDrop> DragDropOp = StaticCastSharedPtr<FRCControllerDragDrop>(DragDropOperation))
				{
					const FGuid DragDropControllerId = DragDropOp->GetId();

					if (ControllerPanelList.IsValid())
					{
						TSharedPtr<FRCControllerModel> DragDropControllerItem = ControllerPanelList->FindControllerItemById(DragDropControllerId);

						if (ensure(ControllerItem && DragDropControllerItem))
						{
							ControllerPanelList->ReorderControllerItem(DragDropControllerItem.ToSharedRef(), ControllerItem.ToSharedRef());
						}
					}
				}
			}

			return FReply::Handled();
		}

		bool OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
		{
			if (DragDropOperation && ControllerItem)
			{
				// Dragging Controllers onto Controllers (Reordering)
				if (DragDropOperation->IsOfType<FRCControllerDragDrop>())
				{
					if (TSharedPtr<FRCControllerDragDrop> DragDropOp = StaticCastSharedPtr<FRCControllerDragDrop>(DragDropOperation))
					{
						return true;
					}
				}
			}

			return false;
		}

		//~ SWidget Interface
		virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
		{
			if (ControllerItem.IsValid())
			{
				ControllerItem->EnterRenameMode();
			}

			return FSuperRowType::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
		}

private:
		TSharedPtr<FRCControllerModel> ControllerItem;
		TSharedPtr<SRCControllerPanelList> ControllerPanelList;
	};
} 

void SRCControllerPanelList::Construct(const FArguments& InArgs, const TSharedRef<SRCControllerPanel> InControllerPanel, const TSharedRef<SRemoteControlPanel> InRemoteControlPanel)
{
	SRCLogicPanelListBase::Construct(SRCLogicPanelListBase::FArguments(), InControllerPanel, InRemoteControlPanel);
	
	ControllerPanelWeakPtr = InControllerPanel;
	
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	ListView = SNew(SListView<TSharedPtr<FRCControllerModel>>)
		.ListItemsSource(&ControllerItems)
		.OnSelectionChanged(this, &SRCControllerPanelList::OnTreeSelectionChanged)
		.OnGenerateRow(this, &SRCControllerPanelList::OnGenerateWidgetForList)
		.SelectionMode(ESelectionMode::Single) // Current setup supports only single selection (and related display) of a Controller in the list
		.OnContextMenuOpening(this, &SRCLogicPanelListBase::GetContextMenuWidget)
		.HeaderRow(
			SNew(SHeaderRow)
			.Style(&RCPanelStyle->HeaderRowStyle)

			+ SHeaderRow::Column(UE::RCControllerPanelList::Columns::DragHandle)
			.DefaultLabel(FText::GetEmpty())
			.FillWidth(0.1f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(UE::RCControllerPanelList::Columns::Name)
			.DefaultLabel(LOCTEXT("ControllerNameColumnName", "Name"))
			.FillWidth(0.3f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(UE::RCControllerPanelList::Columns::Value)
			.DefaultLabel(LOCTEXT("ControllerValueColumnName", "Input"))
			.FillWidth(0.6f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)
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

		Preset->OnVirtualPropertyContainerModified().AddSP(this, &SRCControllerPanelList::OnControllerContainerModified);
	}

	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);
	
	Reset();
}

bool SRCControllerPanelList::IsEmpty() const
{
	return ControllerItems.IsEmpty();
}

int32 SRCControllerPanelList::Num() const
{
	return NumControllerItems();
}

int32 SRCControllerPanelList::NumSelectedLogicItems() const
{
	return ListView->GetNumItemsSelected();
}

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
	TArray<TSharedRef<IDetailTreeNode>> RootTreeNodes = PropertyRowGenerator->GetRootTreeNodes();

	for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootTreeNodes)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		CategoryNode->GetChildren(Children);

		ControllerItems.SetNumZeroed(Children.Num());

		for (TSharedRef<IDetailTreeNode>& Child : Children)
		{
			FProperty* Property = Child->CreatePropertyHandle()->GetProperty();
			check(Property);

			if (URCVirtualPropertyBase* Controller = Preset->GetVirtualProperty(Property->GetFName()))
			{
				if(ensureAlways(ControllerItems.IsValidIndex(Controller->DisplayIndex)))
					ControllerItems[Controller->DisplayIndex] = MakeShared<FRCControllerModel>(Controller, Child, RemoteControlPanel);
			}
		}
	}

	ListView->RebuildList();
}

TSharedRef<ITableRow> SRCControllerPanelList::OnGenerateWidgetForList(TSharedPtr<FRCControllerModel> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	typedef UE::RCControllerPanelList::SControllerItemListRow SControllerRowType;

	return SNew(SControllerRowType, OwnerTable, InItem.ToSharedRef(), SharedThis(this))
		.Style(&RCPanelStyle->TableRowStyle)
		.Padding(FMargin(3.f));
}

void SRCControllerPanelList::OnTreeSelectionChanged(TSharedPtr<FRCControllerModel> InItem, ESelectInfo::Type)
{
	if (TSharedPtr<SRCControllerPanel> ControllerPanel = ControllerPanelWeakPtr.Pin())
	{
		if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanel->GetRemoteControlPanel())
		{
			if (InItem != SelectedControllerItemWeakPtr.Pin())
			{
				SelectedControllerItemWeakPtr = InItem;
				RemoteControlPanel->OnControllerSelectionChanged.Broadcast(InItem);
				RemoteControlPanel->OnBehaviourSelectionChanged.Broadcast(InItem.IsValid() ? InItem->GetSelectedBehaviourModel() : nullptr);
			}
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

void SRCControllerPanelList::OnControllerContainerModified()
{
	Reset();
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
				if(URCVirtualPropertyBase* ControllerToRemove = SelectedController->GetVirtualProperty())
				{
					const int32 DisplayIndexToRemove = ControllerToRemove->DisplayIndex;

					FScopedTransaction Transaction(LOCTEXT("RemoveController", "Remove Controller"));
					Preset->Modify();

					// Remove Model from Data Container
					const bool bRemoved = Preset->RemoveVirtualProperty(SelectedController->GetPropertyName());
					if (bRemoved)
					{
						// Shift all display indices higher than the deleted index down by 1
						for (int32 ControllerIndex = 0; ControllerIndex < ControllerItems.Num(); ControllerIndex++)
						{
							if (ensure(ControllerItems[ControllerIndex]))
							{
								if (URCVirtualPropertyBase* Controller = ControllerItems[ControllerIndex]->GetVirtualProperty())
								{
									if (Controller->DisplayIndex > DisplayIndexToRemove)
									{
										Controller->Modify();

										Controller->SetDisplayIndex(Controller->DisplayIndex - 1);
									}
								}
							}
						}

						return 1; // Remove Count
					}
				}
			}
		}
	}

	return 0;
}

bool SRCControllerPanelList::IsListFocused() const
{
	return ListView->HasAnyUserFocus().IsSet() || ContextMenuWidgetCached.IsValid();
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

TSharedPtr<FRCControllerModel> SRCControllerPanelList::FindControllerItemById(const FGuid& InId) const
{
	for (TSharedPtr<FRCControllerModel> ControllerItem : ControllerItems)
	{
		if (ControllerItem && ControllerItem->GetId() == InId)
		{
			return ControllerItem;
		}
	}

	return nullptr;
}

void SRCControllerPanelList::ReorderControllerItem(TSharedRef<FRCControllerModel> ItemToMove, TSharedRef<FRCControllerModel> AnchorItem)
{
	int32 Index = ControllerItems.Find(AnchorItem);

	// Update UI list
	ControllerItems.RemoveSingle(ItemToMove);
	ControllerItems.Insert(ItemToMove, Index);

	// Update display indices
	for (int32 i = Index; i < ControllerItems.Num(); i++)
	{
		if (ensure(ControllerItems[i]))
		{
			if (URCVirtualPropertyBase* Controller = ControllerItems[i]->GetVirtualProperty())
			{
				Controller->SetDisplayIndex(i);
			}
		}
	}

	ListView->RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE
