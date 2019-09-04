// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/ListView.h"
#include "Widgets/Views/SListView.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Blueprint/ListViewDesignerPreviewItem.h"
#include "UMGPrivate.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UListView

UListView::UListView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Orientation(EOrientation::Orient_Vertical)
{
}

void UListView::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyListView.Reset();
}

#if WITH_EDITOR
void UListView::OnRefreshDesignerItems()
{
	RefreshDesignerItems<UObject*>(ListItems, [this] () {return NewObject<UListViewDesignerPreviewItem>(this); });
}
#endif

void UListView::AddItem(UObject* Item)
{
	ListItems.Add(Item);

	TArray<UObject*> Added;
	TArray<UObject*> Removed;
	Added.Add(Item);
	OnItemsChanged(Added, Removed);

	RequestRefresh();
}

void UListView::RemoveItem(UObject* Item)
{
	ListItems.Remove(Item);

	TArray<UObject*> Added;
	TArray<UObject*> Removed;
	Removed.Add(Item);
	OnItemsChanged(Added, Removed);

	RequestRefresh();
}

UObject* UListView::GetItemAt(int32 Index) const
{
	return ListItems.IsValidIndex(Index) ? ListItems[Index] : nullptr;
}

int32 UListView::GetNumItems() const
{
	return ListItems.Num();
}

int32 UListView::GetIndexForItem(UObject* Item) const
{
	return ListItems.Find(Item);
}

void UListView::ClearListItems()
{
	TArray<UObject*> Added;
	TArray<UObject*> Removed = MoveTemp(ListItems);

	ListItems.Reset();

	OnItemsChanged(Added, Removed);

	RequestRefresh();
}

void UListView::SetSelectionMode(TEnumAsByte<ESelectionMode::Type> InSelectionMode)
{
	SelectionMode = InSelectionMode;
	if (MyListView)
	{
		MyListView->SetSelectionMode(InSelectionMode);
	}
}

int32 UListView::BP_GetNumItemsSelected() const
{
	return GetNumItemsSelected();
}

void UListView::BP_SetListItems(const TArray<UObject*>& InListItems)
{
	SetListItems(InListItems);
}

UObject* UListView::BP_GetSelectedItem() const
{
	return GetSelectedItem();
}

void UListView::HandleOnEntryInitializedInternal(UObject* Item, const TSharedRef<ITableRow>& TableRow)
{
	BP_OnEntryInitialized.Broadcast(Item, GetEntryWidgetFromItem(Item));
}

bool UListView::BP_GetSelectedItems(TArray<UObject*>& Items) const
{
	return GetSelectedItems(Items) > 0;
}

bool UListView::BP_IsItemVisible(UObject* Item) const
{
	return IsItemVisible(Item);
}

void UListView::BP_NavigateToItem(UObject* Item)
{
	if (Item)
	{
		RequestNavigateToItem(Item);
	}
}

void UListView::NavigateToIndex(int32 Index)
{
	RequestNavigateToItem(GetItemAt(Index));
}

void UListView::BP_ScrollItemIntoView(UObject* Item)
{
	if (Item)
	{
		RequestScrollItemIntoView(Item);
	}
}

void UListView::ScrollIndexIntoView(int32 Index)
{
	BP_ScrollItemIntoView(GetItemAt(Index));
}

void UListView::BP_CancelScrollIntoView()
{
	if (MyListView.IsValid())
	{
		MyListView->CancelScrollIntoView();
	}
}

bool UListView::IsRefreshPending() const
{
	if (MyListView.IsValid())
	{
		return MyListView->IsPendingRefresh();
	}
	return false;
}

void UListView::BP_SetSelectedItem(UObject* Item)
{
	if (MyListView.IsValid())
	{
		MyListView->SetSelection(Item, ESelectInfo::Direct);
	}
}

void UListView::SetSelectedItem(const UObject* Item)
{
	ITypedUMGListView<UObject*>::SetSelectedItem(const_cast<UObject*>(Item));
}

void UListView::SetSelectedIndex(int32 Index)
{
	SetSelectedItem(GetItemAt(Index));
}

void UListView::BP_SetItemSelection(UObject* Item, bool bSelected)
{
	SetItemSelection(Item, bSelected);
}

void UListView::BP_ClearSelection()
{
	ClearSelection();
}

void UListView::OnItemsChanged(const TArray<UObject*>& AddedItems, const TArray<UObject*>& RemovedItems)
{
	// Allow subclasses to do special things when objects are added or removed from the list.
}

TSharedRef<STableViewBase> UListView::RebuildListWidget()
{
	return ConstructListView<SListView>();
}

void UListView::HandleListEntryHovered(UUserWidget& EntryWidget)
{
	if (UObject* const* ListItem = ItemFromEntryWidget(EntryWidget))
	{
		OnItemIsHoveredChanged().Broadcast(*ListItem, true);
		BP_OnItemIsHoveredChanged.Broadcast(*ListItem, true);
	}
}

void UListView::HandleListEntryUnhovered(UUserWidget& EntryWidget)
{
	if (UObject* const* ListItem = ItemFromEntryWidget(EntryWidget))
	{
		OnItemIsHoveredChanged().Broadcast(*ListItem, false);
		BP_OnItemIsHoveredChanged.Broadcast(*ListItem, false);
	}
}

FMargin UListView::GetDesiredEntryPadding(UObject* Item) const
{
	if (ListItems.Num() > 0 && ListItems[0] != Item)
	{
		if (Orientation == EOrientation::Orient_Horizontal)
		{
			// For all entries after the first one, add the spacing as left padding
			return FMargin(EntrySpacing, 0.f, 0.0f, 0.f);
		}
		else
		{
			// For all entries after the first one, add the spacing as top padding
			return FMargin(0.f, EntrySpacing, 0.f, 0.f);
		}
	}

	return FMargin(0.f);
}

UUserWidget& UListView::OnGenerateEntryWidgetInternal(UObject* Item, TSubclassOf<UUserWidget> DesiredEntryClass, const TSharedRef<STableViewBase>& OwnerTable)
{
	return GenerateTypedEntry(DesiredEntryClass, OwnerTable);
}

void UListView::OnItemClickedInternal(UObject* ListItem)
{
	BP_OnItemClicked.Broadcast(ListItem);
}

void UListView::OnItemDoubleClickedInternal(UObject* ListItem)
{
	BP_OnItemDoubleClicked.Broadcast(ListItem);
}

void UListView::OnSelectionChangedInternal(UObject* FirstSelectedItem)
{
	BP_OnItemSelectionChanged.Broadcast(FirstSelectedItem, FirstSelectedItem != nullptr);
}

void UListView::OnItemScrolledIntoViewInternal(UObject* ListItem, UUserWidget& EntryWidget)
{
	BP_OnItemScrolledIntoView.Broadcast(ListItem, &EntryWidget);
}

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE