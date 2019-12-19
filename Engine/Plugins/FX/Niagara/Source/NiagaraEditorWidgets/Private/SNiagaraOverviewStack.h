// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackEntry.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "UObject/ObjectKey.h"
#include "Framework/Commands/UICommandList.h"

class UNiagaraStackEntry;
class UNiagaraStackItem;
class UNiagaraStackViewModel;
class UNiagaraSystemSelectionViewModel;
class FNiagaraStackCommandContext;

class SNiagaraOverviewStack : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewStack)
	{}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackViewModel& InStackViewModel, UNiagaraSystemSelectionViewModel& InOverviewSelectionViewModel);

	~SNiagaraOverviewStack();

	virtual bool SupportsKeyboardFocus() const override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void AddEntriesRecursive(UNiagaraStackEntry& EntryToAdd, TArray<UNiagaraStackEntry*>& EntryList, const TArray<UClass*>& AcceptableClasses, TArray<UNiagaraStackEntry*> ParentChain);

	void RefreshEntryList();

	void EntryStructureChanged();

	TSharedRef<ITableRow> OnGenerateRowForEntry(UNiagaraStackEntry* Item, const TSharedRef<STableViewBase>& OwnerTable);

	EVisibility GetEnabledCheckBoxVisibility(UNiagaraStackItem* Item) const;

	void OnSelectionChanged(UNiagaraStackEntry* InNewSelection, ESelectInfo::Type SelectInfo);

	void SystemSelectionChanged();

	void UpdateCommandContextSelection();

	FReply OnRowDragDetected(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent, TWeakObjectPtr<UNiagaraStackEntry> InStackEntryWeak);

	void OnRowDragLeave(const FDragDropEvent& InDragDropEvent);

	TOptional<EItemDropZone> OnRowCanAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry);

	FReply OnRowAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry);

	EVisibility GetIssueIconVisibility() const;

private:
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraSystemSelectionViewModel* OverviewSelectionViewModel;

	TArray<UNiagaraStackEntry*> FlattenedEntryList;
	TMap<FObjectKey, TArray<UNiagaraStackEntry*>> EntryObjectKeyToParentChain;
	TSharedPtr<SListView<UNiagaraStackEntry*>> EntryListView;

	TArray<TWeakObjectPtr<UNiagaraStackEntry>> PreviousSelection;

	TSharedPtr<FNiagaraStackCommandContext> StackCommandContext;

	bool bRefreshEntryListPending;
	bool bUpdatingOverviewSelectionFromStackSelection;
	bool bUpdatingStackSelectionFromOverviewSelection;
};