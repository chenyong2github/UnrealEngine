// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraCommon.h"
#include "NiagaraParameterStore.h"
#include "ViewModels/Stack/NiagaraStackParameterStoreEntry.h"
#include "NiagaraStackSystemSettingsGroup.generated.h"

class FNiagaraScriptViewModel;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackSystemSettingsGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()
		
public:
	void Initialize(FRequiredEntryData InRequiredEntryData,	UObject* InOwner, FNiagaraParameterStore* InParameterStore);

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void ParameterAdded(FNiagaraVariable AddedParameter);

private:
	TWeakObjectPtr<UObject> Owner;
	FNiagaraParameterStore* UserParameterStore;
	FDelegateHandle ParameterStoreChangedHandle;
	TSharedPtr<INiagaraStackItemGroupAddUtilities> AddUtilities;
};

UCLASS()
class UNiagaraStackParameterStoreItem : public UNiagaraStackItem
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData, UObject* InOwner, FNiagaraParameterStore* InParameterStore);

	virtual FText GetDisplayName() const override;

	virtual FText GetTooltipText() const override;

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void ParameterDeleted();

private:
	TWeakObjectPtr<UObject> Owner;
	FNiagaraParameterStore* ParameterStore;
};