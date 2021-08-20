// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class FDebugLineItem;
class FTraceStackParentItem;
class UBlueprint;
class UBlueprintGeneratedClass;
class FBreakpointParentItem;

//////////////////////////////////////////////////////////////////////////
// FDebugLineItem

// Shared pointer to a debugging tree line entry
typedef TSharedPtr<class FDebugLineItem> FDebugTreeItemPtr;

// The base class for a line entry in the debugging tree view
class FDebugLineItem : public TSharedFromThis<FDebugLineItem>
{
public:
	friend class FLineItemWithChildren; // used by FLineItemWithChildren::EnsureChildIsAdded
	enum EDebugLineType
	{
		DLT_Message,
		DLT_TraceStackParent,
		DLT_TraceStackChild,
		DLT_Parent,
		DLT_Watch,
		DLT_WatchChild,
		DLT_LatentAction,
		DLT_Breakpoint,
		DLT_BreakpointParent
	};

	virtual ~FDebugLineItem() {}
	
	// Create the widget for the name column
	virtual TSharedRef<SWidget> GenerateNameWidget();

	// Create the widget for the value column
	virtual TSharedRef<SWidget> GenerateValueWidget();

	// Add any context menu items that can act on this node
	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) { }

	// Gather all of the children
	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch = true) {}

	// returns whether this tree node has children (used by drop down arrows)
	virtual bool HasChildren();

	// only line items inherriting from FLineItemWithChildren can have children
	virtual bool CanHaveChildren() { return false; }

	// @return The object that will act as a parent to more items in the tree, or NULL if this is a leaf node
	virtual UObject* GetParentObject() { return NULL; }

	virtual EDebugLineType GetType() const
	{
		return Type;
	}

	// returns a widget that will go to the left of the Name Widget.
	virtual TSharedRef<SWidget> GetNameIcon();

	// returns a widget that will go to the left of the Value Widget.
	virtual TSharedRef<SWidget> GetValueIcon();

	// Helper function to try to get the blueprint for a given object;
	//   Returns the blueprint that was used to create the instance if there was one
	//   Returns the object itself if it is already a blueprint
	//   Otherwise returns NULL
	static UBlueprint* GetBlueprintForObject(UObject* ParentObject);

	static UBlueprintGeneratedClass* GetClassForObject(UObject* ParentObject);

	static bool IsDebugLineTypeActive(EDebugLineType Type);
	static void OnDebugLineTypeActiveChanged(ECheckBoxState CheckState, EDebugLineType Type);

	// updates bVisible and bParentsMatchSearch based on this node alone
	void UpdateSearchFlags(bool bIsRootNode = false);

	bool IsVisible();
	bool DoParentsMatchSearch();
protected:
	TSharedPtr<SSearchBox> SearchBox;
	
	// Cannot create an instance of this class, it's just for use as a base class
	FDebugLineItem(EDebugLineType InType, TSharedPtr<SSearchBox> InSearchBox)
		: SearchBox(MoveTemp(InSearchBox)), Type(InType)
	{
	}

	// Duplicate this item
	virtual FDebugLineItem* Duplicate() const=0;

	// Compare this item to another of the same type
	virtual bool Compare(const FDebugLineItem* Other) const=0;

	// Used to update the state of a line item rather than replace it.
	// called after Compare returns true
	[[maybe_unused]] virtual void UpdateData(const FDebugLineItem& NewerData) {}

	// @return The text to display in the name column, unless GenerateNameWidget is overridden
	virtual FText GetDisplayName() const;

	// @return The text to display in the value column, unless GenerateValueWidget is overridden
	virtual FText GetDescription() const;
protected:
	// Type of action (poor mans RTTI for the tree, really only used to accelerate Compare checks)
	EDebugLineType Type;

	static uint16 ActiveTypeBitset;

	// true if self or any recursive children match the search
	bool bVisible = false;
	// true if self or any recursive parents match the search
	bool bParentsMatchSearch = false;
};

//////////////////////////////////////////////////////////////////////////
// SKismetDebuggingView

class SKismetDebuggingView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SKismetDebuggingView )
		: _BlueprintToWatch()
		{}

		SLATE_ARGUMENT( TWeakObjectPtr<UBlueprint>, BlueprintToWatch )
	SLATE_END_ARGS()
public:
	void Construct( const FArguments& InArgs );

	// SWidget interface
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	// End of SWidget interface

	/* set to an object that's paused at a breakpoint and null otherwise */
	static TWeakObjectPtr<const UObject> CurrentActiveObject;
	
	FText GetTabLabel() const;
protected:
	FText GetTopText() const;
	bool CanDisableAllBreakpoints() const;
	FReply OnDisableAllBreakpointsClicked();

	void OnBlueprintClassPicked(UClass* PickedClass);
	TSharedRef<SWidget> ConstructBlueprintClassPicker();

	TSharedRef<ITableRow> OnGenerateRowForWatchTree(FDebugTreeItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildrenForWatchTree(FDebugTreeItemPtr InParent, TArray<FDebugTreeItemPtr>& OutChildren);

	static TSharedRef<SHorizontalBox> GetDebugLineTypeToggle(FDebugLineItem::EDebugLineType Type, const FText& Text);
	
	TSharedPtr<SWidget> OnMakeContextMenu();

	// called when SearchBox query is committed by the user
	void OnSearchTextCommitted(const FText& Text, ETextCommit::Type);
protected:
	TSharedPtr<STreeView<FDebugTreeItemPtr>> DebugTreeView;
	TMap<UObject*, FDebugTreeItemPtr> ObjectToTreeItemMap;
	TArray<FDebugTreeItemPtr> RootTreeItems;

	// includes items such as breakpoints and Exectution trace
	TSharedPtr< STreeView<FDebugTreeItemPtr> > OtherTreeView;
	TArray<FDebugTreeItemPtr> OtherTreeItems;

	// UI tree entries for stack trace and breakpoints
	TSharedPtr< FTraceStackParentItem > TraceStackItem;
	TSharedPtr< FBreakpointParentItem > BreakpointParentItem;

	// Combo button for selecting which blueprint is being watched
	TSharedPtr<SComboButton> DebugClassComboButton;
	TWeakObjectPtr<UBlueprint> BlueprintToWatchPtr;

	// Search Box for tree
	TSharedPtr<SSearchBox> SearchBox;
};
