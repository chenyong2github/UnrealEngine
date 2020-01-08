// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/ControlRigGraph.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "Widgets/Views/STreeView.h"

class FControlRigEditor;
class SControlRigStackView;
class FUICommandList;

namespace ERigStackEntry
{
	enum Type
	{
		Operator,
		Info,
		Warning,
		Error,
	};
}

/** An item in the stack */
class FRigStackEntry : public TSharedFromThis<FRigStackEntry>
{
public:
	FRigStackEntry(int32 InEntryIndex, ERigStackEntry::Type InEntryType, int32 InOpIndex, EControlRigOpCode InOpCode, const FName& InName, const FString& InLabel);

	TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SControlRigStackView> InStackView);

	int32 EntryIndex;
	ERigStackEntry::Type EntryType;
	int32 OpIndex;
	EControlRigOpCode OpCode;
	FName Name;
	FString Label;
	TArray<TSharedPtr<FRigStackEntry>> Children;
};

class SRigStackItem : public STableRow<TSharedPtr<FRigStackEntry>>
{
	SLATE_BEGIN_ARGS(SRigStackItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigStackEntry> InStackEntry, TSharedRef<FUICommandList> InCommandList);

private:
	TWeakPtr<FRigStackEntry> WeakStackEntry;
	TWeakPtr<FUICommandList> WeakCommandList;

	FText GetIndexText() const;
	FText GetLabelText() const;
};

class SControlRigStackView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SControlRigStackView) {}
	SLATE_END_ARGS()

	~SControlRigStackView();

	void Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor);

	/** Set Selection Changed */
	void OnSelectionChanged(TSharedPtr<FRigStackEntry> Selection, ESelectInfo::Type SelectInfo);

	TSharedPtr< SWidget > CreateContextMenu();

protected:

	/** Rebuild the tree view */
	void RefreshTreeView(UControlRig* ControlRig = nullptr);

private:

	/** Bind commands that this widget handles */
	void BindCommands();

	/** Make a row widget for the table */
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FRigStackEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get children for the tree */
	void HandleGetChildrenForTree(TSharedPtr<FRigStackEntry> InItem, TArray<TSharedPtr<FRigStackEntry>>& OutChildren);

	/** Focus on the selected operator in the graph*/
	void HandleFocusOnSelectedGraphNode();

	void OnBlueprintCompiled(UBlueprint* InCompiledBlueprint);

	void OnControlRigInitialized(UControlRig* ControlRig, EControlRigState State);

	bool bSuspendModelNotifications;
	void HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload);

	TSharedPtr<STreeView<TSharedPtr<FRigStackEntry>>> TreeView;

	/** Command list we bind to */
	TSharedPtr<FUICommandList> CommandList;

	TWeakPtr<FControlRigEditor> ControlRigEditor;
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprint;
	TWeakObjectPtr<UControlRigGraph> Graph;

	TArray<TSharedPtr<FRigStackEntry>> Operators;

	FDelegateHandle OnModelModified;
	FDelegateHandle OnBlueprintCompiledHandle;
	FDelegateHandle OnControlRigInitializedHandle;
};
