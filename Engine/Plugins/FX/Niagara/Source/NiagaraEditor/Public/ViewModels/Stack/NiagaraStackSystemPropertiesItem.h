// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraStackSystemPropertiesItem.generated.h"

class UNiagaraStackObject;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackSystemPropertiesItem : public UNiagaraStackItem
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

	virtual FText GetDisplayName() const override;

	virtual FText GetTooltipText() const override;

	virtual bool IsExpandedByDefault() const override;

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void SystemPropertiesChanged();

private:
	mutable TOptional<bool> bCanResetToBase;

	TWeakObjectPtr<UNiagaraSystem> System;

	UPROPERTY()
	UNiagaraStackObject* SystemObject;

};
