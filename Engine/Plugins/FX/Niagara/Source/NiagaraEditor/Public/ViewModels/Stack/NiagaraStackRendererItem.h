// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraTypes.h"
#include "NiagaraStackRendererItem.generated.h"

class UNiagaraEmitter;
class UNiagaraRendererProperties;
class UNiagaraStackObject;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackRendererItem : public UNiagaraStackItem
{
	GENERATED_BODY()
		
public:
	UNiagaraStackRendererItem();

	void Initialize(FRequiredEntryData InRequiredEntryData, UNiagaraRendererProperties* InRendererProperties);

	UNiagaraRendererProperties* GetRendererProperties();

	virtual FText GetDisplayName() const override;

	virtual bool SupportsDelete() const override { return true; }
	virtual bool TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const;

	bool HasBaseRenderer() const;

	bool CanResetToBase() const;

	void ResetToBase();

	virtual bool SupportsChangeEnabled() const override { return true; }
	virtual bool GetIsEnabled() const override;

	static TArray<FNiagaraVariable> GetMissingVariables(UNiagaraRendererProperties* RendererProperties, UNiagaraEmitter* Emitter);
	static bool AddMissingVariable(UNiagaraEmitter* Emitter, const FNiagaraVariable& Variable);

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void SetIsEnabledInternal(bool bInIsEnabled) override;
	virtual void DeleteInternal() override;

private:
	void RendererChanged();
	void RefreshIssues(TArray<FStackIssue>& NewIssues);

private:
	TWeakObjectPtr<UNiagaraRendererProperties> RendererProperties;

	mutable TOptional<bool> bHasBaseRendererCache;

	mutable TOptional<bool> bCanResetToBaseCache;

	TArray<FNiagaraVariable> MissingAttributes;

	UPROPERTY()
	UNiagaraStackObject* RendererObject;
};
