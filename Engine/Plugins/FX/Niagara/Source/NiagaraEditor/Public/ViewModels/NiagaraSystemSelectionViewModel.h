// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectKey.h"
#include "NiagaraSystemSelectionViewModel.generated.h"


class FNiagaraSystemViewModel;
class UNiagaraStackEntry;
class UNiagaraStackSelection;
class UNiagaraStackViewModel;

UCLASS()
class NIAGARAEDITOR_API UNiagaraSystemSelectionViewModel : public UObject
{
public:
	enum class ESelectionChangeSource
	{
		EntrySelection,
		TopObjectLevelSelection,
		Refresh,
		Clear
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, ESelectionChangeSource);

public:
	GENERATED_BODY()

	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);

	void Finalize();

	const TArray<UNiagaraStackEntry*> GetSelectedEntries() const;

	bool GetSystemIsSelected() const;

	const TArray<FGuid>& GetSelectedEmitterHandleIds() const;

	void UpdateSelectionFromEntries(const TArray<UNiagaraStackEntry*> InSelectedEntries, const TArray<UNiagaraStackEntry*> InDeselectedEntries, bool bClearCurrentSelection);

	void UpdateSelectionFromTopLevelObjects(bool bInSystemIsSelected, const TArray<FGuid> InSelectedEmitterHandleIds, bool bClearCurrentSelection);

	UNiagaraStackViewModel* GetSelectionStackViewModel();

	void RemoveEntryFromSelectionByDisplayedObject(const UObject* InObject);

	void AddEntriesToSelectionByDisplayedObjectsDeferred(const TArray<const UObject*>& InObjects);

	void AddEntryToSelectionByDisplayedObjectDeferred(const UObject* InObjects);

	void Refresh();

	void RefreshDeferred();

	FOnSelectionChanged& OnSelectionChanged();

	void Tick();

private:
	TSharedRef<FNiagaraSystemViewModel> GetSystemViewModel();

	void RemoveEntryFromSelectionInternal(UNiagaraStackEntry* DeselectedEntry);

	void UpdateStackSelectionEntry();

private:
	TWeakPtr<FNiagaraSystemViewModel> SystemViewModelWeak;

	TArray<UNiagaraStackEntry*> SelectedEntries;

	bool bSystemIsSelected;

	TArray<FGuid> SelectedEmitterHandleIds;

	UPROPERTY()
	UNiagaraStackSelection* StackSelection;

	UPROPERTY()
	UNiagaraStackViewModel* SelectionStackViewModel;

	FOnSelectionChanged OnSelectionChangedDelegate;

	TArray<FObjectKey> DeferredDisplayedObjectKeysToAddToSelection;

	bool bRefreshIsPending;
};