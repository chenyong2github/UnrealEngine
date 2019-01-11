// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ListViewBase.h"
#include "Widgets/Views/SListView.h"

#include "ListView.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimpleListItemEventDynamic, UObject*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnListItemSelectionChangedDynamic, UObject*, Item, bool, bIsSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemIsHoveredChangedDynamic, UObject*, Item, bool, bIsHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnListItemScrolledIntoViewDynamic, UObject*, Item, UUserWidget*, Widget);

/**
 * A virtualized list that allows up to thousands of items to be displayed.
 * 
 * An important distinction to keep in mind here is "Item" vs. "Entry"
 * The list itself is based on a list of n items, but only creates as many entry widgets as can fit on screen.
 * For example, a scrolling ListView of 200 items with 5 currently visible will only have created 5 entry widgets.
 *
 * To make a widget usable an an entry in a ListView, it must inherit from the IUserObjectListEntry interface.
 */
UCLASS(meta = (EntryInterface = UserObjectListEntry))
class UMG_API UListView : public UListViewBase, public ITypedUMGListView<UObject*>
{
	GENERATED_BODY()

	IMPLEMENT_TYPED_UMG_LIST(UObject*, MyListView)

public:
	UListView(const FObjectInitializer& Initializer);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	
	/** Set the list of items to display within this listview */
	template <typename ItemObjectT, typename AllocatorType = FDefaultAllocator>
	void SetListItems(const TArray<ItemObjectT, AllocatorType>& InListItems)
	{
		ClearListItems();
		ListItems.Append(InListItems);
		RequestRefresh();
	}

	ESelectionMode::Type GetSelectionMode() const { return SelectionMode; }

	template <typename RowWidgetT = UUserWidget>
	RowWidgetT* GetEntryWidgetFromItem(const UObject* Item) const
	{
		return Item ? ITypedUMGListView<UObject*>::GetEntryWidgetFromItem<RowWidgetT>(const_cast<UObject*>(Item)) : nullptr;
	}
	void SetSelectedItem(const UObject* Item);

	/** Gets the first selected item, if any; recommended that you only use this for single selection lists. */
	template <typename ObjectT = UObject>
	ObjectT* GetSelectedItem() const
	{
		return Cast<ObjectT>(ITypedUMGListView<UObject*>::GetSelectedItem());
	}

	/**
	 * Gets the list of all items in the list.
	 * Note that each of these items only has a corresponding entry widget when visible. Use GetDisplayedEntryWidgets to get the currently displayed widgets.
	 */
	UFUNCTION(BlueprintCallable, Category = ListView)
	const TArray<UObject*>& GetListItems() const { return ListItems; }
	
	/** Adds an the item to the list */
	UFUNCTION(BlueprintCallable, Category = ListView)
	void AddItem(UObject* Item);

	/** Returns the item at the given index */
	UFUNCTION(BlueprintCallable, Category = ListView)
	UObject* GetItemAt(int32 Index) const;

	/** Returns the total number of items */
	UFUNCTION(BlueprintCallable, Category = ListView)
	int32 GetNumItems() const;

	/** Returns the index that the specified item is at. Will return the first found, or -1 for not found */
	UFUNCTION(BlueprintCallable, Category = ListView)
	int32 GetIndexForItem(UObject* Item) const;

	/** Removes all items from the list */
	UFUNCTION(BlueprintCallable, Category = ListView)
	void ClearListItems();

	/** Sets the new selection mode, preserving the current selection where possible. */
	UFUNCTION(BlueprintCallable, Category = ListView)
	void SetSelectionMode(TEnumAsByte<ESelectionMode::Type> SelectionMode);

	/** @return True if a refresh is pending and the list will be rebuilt on the next tick */
	UFUNCTION(BlueprintCallable, Category = ListView)
	bool IsRefreshPending() const;

	/** Requests that the item at the given index is scrolled into view */
	UFUNCTION(BlueprintCallable, Category = ListView)
	void ScrollIndexIntoView(int32 Index);

	/** Sets the item at the given index as the sole selected item. */
	UFUNCTION(BlueprintCallable, Category = ListView)
	void SetSelectedIndex(int32 Index);

	/** Requests that the item at the given index navigated to, scrolling it into view if needed. */
	UFUNCTION(BlueprintCallable, Category = ListView)
	void NavigateToIndex(int32 Index);

protected:
	virtual TSharedRef<STableViewBase> RebuildListWidget() override;
	virtual void HandleListEntryHovered(UUserWidget& EntryWidget) override;
	virtual void HandleListEntryUnhovered(UUserWidget& EntryWidget) override;
	
#if WITH_EDITOR
	virtual void OnRefreshDesignerItems() override;
#endif

	virtual UUserWidget& OnGenerateEntryWidgetInternal(UObject* Item, TSubclassOf<UUserWidget> DesiredEntryClass, const TSharedRef<STableViewBase>& OwnerTable) override;
	virtual FMargin GetDesiredEntryPadding(UObject* Item) const override;

	virtual void OnItemClickedInternal(UObject* Item) override;
	virtual void OnItemDoubleClickedInternal(UObject* Item) override;
	virtual void OnSelectionChangedInternal(UObject* FirstSelectedItem) override;
	virtual void OnItemScrolledIntoViewInternal(UObject* Item, UUserWidget& EntryWidget) override;

	/** SListView construction helper - useful if using a custom STreeView subclass */
	template <template<typename> class ListViewT = SListView>
	TSharedRef<ListViewT<UObject*>> ConstructListView()
	{
		MyListView = ITypedUMGListView<UObject*>::ConstructListView<ListViewT>(this, ListItems, bIsFocusable, SelectionMode, bClearSelectionOnClick, ConsumeMouseWheel, bReturnFocusToSelection);
		return StaticCastSharedRef<ListViewT<UObject*>>(MyListView.ToSharedRef());
	}

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ListView)
	TEnumAsByte<ESelectionMode::Type> SelectionMode = ESelectionMode::Single;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ListView)
	EConsumeMouseWheel ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ListView)
	bool bClearSelectionOnClick = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ListView)
	bool bIsFocusable = true;

	UPROPERTY(EditAnywhere, Category = ListEntries, meta = (ClampMin = 0))
	float EntrySpacing = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ListView)
	bool bReturnFocusToSelection = false;

	UPROPERTY(Transient)
	TArray<UObject*> ListItems;

	TSharedPtr<SListView<UObject*>> MyListView;

private:
	// BP exposure of ITypedUMGListView API

	/** Sets the given item as the sole selected item. */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Set Selected Item"))
	void BP_SetSelectedItem(UObject* Item);

	/** Sets whether the given item is selected. */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Set Item Selection"))
	void BP_SetItemSelection(UObject* Item, bool bSelected);

	/** Clear selection */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Clear Selection"))
	void BP_ClearSelection();

	/** Gets the number of items currently selected in the list */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Get Num Items Selected"))
	int32 BP_GetNumItemsSelected() const;

	/** Gets a list of all the currently selected items */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "GetSelectedItems"))
	bool BP_GetSelectedItems(TArray<UObject*>& Items) const;

	/** Gets whether the entry for the given object is currently visible in the list */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Is Item Visible"))
	bool BP_IsItemVisible(UObject* Item) const;

	/** Requests that the given item is navigated to, scrolling it into view if needed. */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Navigate To Item"))
	void BP_NavigateToItem(UObject* Item);

	/** Requests that the given item is scrolled into view */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Scroll Item Into View"))
	void BP_ScrollItemIntoView(UObject* Item);

	/** Cancels a previous request to scroll and item into view. */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Cancel Scroll Into View"))
	void BP_CancelScrollIntoView();

	/** Sets the array of objects to display rows for in the list */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (AllowPrivateAccess = true, DisplayName = "Set List Items"))
	void BP_SetListItems(const TArray<UObject*>& InListItems);

	/** Gets the first selected item, if any; recommended that you only use this for single selection lists. */
	UFUNCTION(BlueprintCallable, Category = ListView, meta = (DisplayName = "Get Selected Item", AllowPrivateAccess = true))
	UObject* BP_GetSelectedItem() const;

private:
	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Item Clicked"))
	FSimpleListItemEventDynamic BP_OnItemClicked;

	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Item Double Clicked"))
	FSimpleListItemEventDynamic BP_OnItemDoubleClicked;

	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Item Is Hovered Changed"))
	FOnItemIsHoveredChangedDynamic BP_OnItemIsHoveredChanged;

	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Item Selection Changed"))
	FOnListItemSelectionChangedDynamic BP_OnItemSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Item Scrolled Into View"))
	FOnListItemScrolledIntoViewDynamic BP_OnItemScrolledIntoView;
};
