// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraStackRenderItemGroup.generated.h"

class UNiagaraRendererProperties;
class UNiagaraEmitter;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackRenderItemGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void EmitterRenderersChanged();

	virtual void FinalizeInternal() override;

private:
	TSharedPtr<INiagaraStackItemGroupAddUtilities> AddUtilities;

	TWeakObjectPtr<UNiagaraEmitter> EmitterWeak;
};
