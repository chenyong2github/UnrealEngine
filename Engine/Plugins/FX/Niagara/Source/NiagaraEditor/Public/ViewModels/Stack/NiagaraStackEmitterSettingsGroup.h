// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraStackEmitterSettingsGroup.generated.h"

class FNiagaraEmitterViewModel;
class FNiagaraScriptViewModel;
class UNiagaraStackObject;
class UNiagaraStackSummaryViewObject;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackEmitterPropertiesItem : public UNiagaraStackItem
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

	virtual FText GetDisplayName() const override;

	virtual FText GetTooltipText() const override;

	virtual bool SupportsResetToBase() const override { return true; }
	virtual bool TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const override;
	virtual void ResetToBase() override;

	virtual bool SupportsIcon() const override { return true; }
	virtual const FSlateBrush* GetIconBrush() const override;

	virtual bool IsExpandedByDefault() const override;

	virtual bool GetShouldShowInOverview() const override { return false; }

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void RefreshIssues(TArray<FStackIssue>& NewIssues);

private:
	void EmitterPropertiesChanged();

private:
	mutable TOptional<bool> bCanResetToBaseCache;

	TWeakObjectPtr<UNiagaraEmitter> Emitter;

	UPROPERTY()
	TObjectPtr<UNiagaraStackObject> EmitterObject;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackEmitterSummaryItem : public UNiagaraStackItem
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

	virtual FText GetDisplayName() const override;
	virtual FText GetTooltipText() const override;

	virtual bool SupportsResetToBase() const override { return false; }
	virtual bool IsExpandedByDefault() const override { return false; }
	virtual bool SupportsIcon() const override { return false; }
	virtual const FSlateBrush* GetIconBrush() const override;
	virtual bool GetShouldShowInOverview() const override { return false; }

protected:

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	TWeakObjectPtr<UNiagaraEmitter> Emitter;

	UPROPERTY()
	TObjectPtr<UNiagaraStackSummaryViewObject> FilteredObject;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackEmitterSummaryGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	UNiagaraStackEmitterSummaryGroup();

	virtual bool SupportsIcon() const override { return true; }
	virtual const FSlateBrush* GetIconBrush() const override;
	virtual bool GetCanExpandInOverview() const override { return false; }
	virtual bool GetShouldShowInStack() const override { return false; }

protected:
	void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;


private:
	UPROPERTY()
	TObjectPtr<UNiagaraStackEmitterSummaryItem> SummaryItem;
};


UCLASS()
class NIAGARAEDITOR_API UNiagaraStackSummaryViewCollapseButton : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

	//~ UNiagaraStackEntry interface
	virtual FText GetDisplayName() const override;
	virtual EStackRowStyle GetStackRowStyle() const override;
	virtual FText GetTooltipText() const override;

	virtual bool GetCanExpandInOverview() const override { return true; }
	virtual bool GetIsEnabled() const override;
	virtual void SetIsEnabled(bool bEnabled) {}
	virtual bool SupportsChangeEnabled() const { return false; }
	
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

};