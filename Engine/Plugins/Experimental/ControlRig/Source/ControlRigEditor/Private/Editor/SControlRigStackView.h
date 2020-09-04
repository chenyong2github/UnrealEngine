// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/ControlRigGraph.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "Widgets/Views/STreeView.h"
#include "RigVMCore/RigVM.h"

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
	FRigStackEntry(int32 InEntryIndex, ERigStackEntry::Type InEntryType, int32 InInstructionIndex, ERigVMOpCode InOpCode, const FString& InLabel);

	TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SControlRigStackView> InStackView);

	int32 EntryIndex;
	ERigStackEntry::Type EntryType;
	int32 InstructionIndex;
	ERigVMOpCode OpCode;
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
	void RefreshTreeView(URigVM* InVM);

private:

	/** Bind commands that this widget handles */
	void BindCommands();

	/** Make a row widget for the table */
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FRigStackEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get children for the tree */
	void HandleGetChildrenForTree(TSharedPtr<FRigStackEntry> InItem, TArray<TSharedPtr<FRigStackEntry>>& OutChildren);

	/** Focus on the selected operator in the graph*/
	void HandleFocusOnSelectedGraphNode();

	void OnVMCompiled(UBlueprint* InCompiledBlueprint, URigVM* InCompiledVM);

	bool bSuspendModelNotifications;
	bool bSuspendControllerSelection;
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);
	void HandleControlRigInitializedEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName);
	void HandlePreviewControlRigUpdated(FControlRigEditor* InEditor);

	TSharedPtr<STreeView<TSharedPtr<FRigStackEntry>>> TreeView;

	/** Command list we bind to */
	TSharedPtr<FUICommandList> CommandList;

	TWeakPtr<FControlRigEditor> ControlRigEditor;
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprint;
	TWeakObjectPtr<UControlRigGraph> Graph;

	TArray<TSharedPtr<FRigStackEntry>> Operators;

	FDelegateHandle OnModelModified;
	FDelegateHandle OnControlRigInitializedHandle;
	FDelegateHandle OnVMCompiledHandle;
	FDelegateHandle OnPreviewControlRigUpdatedHandle;
};
