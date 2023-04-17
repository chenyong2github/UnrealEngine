// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEmitter.h"
#include "NiagaraStackFunctionInputCollection.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "NiagaraStackSummaryViewInputCollection.generated.h"

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackSummaryViewCollection : public UNiagaraStackValueCollection
{
	GENERATED_BODY()
		
public:
	DECLARE_DELEGATE_TwoParams(FOnSelectRootNodes, TArray<TSharedRef<IDetailTreeNode>>, TArray<TSharedRef<IDetailTreeNode>>*);

public:
	UNiagaraStackSummaryViewCollection() {}

	void Initialize(FRequiredEntryData InRequiredEntryData, FVersionedNiagaraEmitterWeakPtr InEmitter, FString InOwningStackItemEditorDataKey);
	virtual void FinalizeInternal() override;

	virtual FText GetDisplayName() const override;
	virtual bool GetIsEnabled() const override;
	
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void GetSectionsInternal(TArray<FNiagaraStackSection>& OutStackSections) const override;
	const TArray<UNiagaraHierarchySection*>& GetHierarchySections() const;

	virtual bool FilterByActiveSection(const UNiagaraStackEntry& Child) const override;
private:
	void OnViewStateChanged();
private:

	FVersionedNiagaraEmitterWeakPtr Emitter;
};

