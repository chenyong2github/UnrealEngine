// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraStackEmitterSettingsGroup.generated.h"

class FNiagaraEmitterViewModel;
class FNiagaraScriptViewModel;
class UNiagaraStackObject;

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

	virtual bool IsExpandedByDefault() const override;
	virtual bool SupportsIcon() const { return true; }
	virtual const FSlateBrush* GetIconBrush() const override;

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
	UNiagaraStackObject* EmitterObject;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackEmitterSettingsGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	UNiagaraStackEmitterSettingsGroup();

protected:
	void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	UPROPERTY()
	UNiagaraStackEmitterPropertiesItem* PropertiesItem;
};
