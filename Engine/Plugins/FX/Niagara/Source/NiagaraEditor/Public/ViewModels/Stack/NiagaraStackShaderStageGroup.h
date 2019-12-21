// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraEmitter.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "NiagaraStackShaderStageGroup.generated.h"

class FNiagaraScriptViewModel;
class UNiagaraStackObject;
struct FNiagaraEventScriptProperties;
class IDetailTreeNode;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackShaderStagePropertiesItem : public UNiagaraStackItem
{
	GENERATED_BODY()
		 
public:
	void Initialize(FRequiredEntryData InRequiredEntryData, UNiagaraShaderStageBase* InShaderStage);

	virtual FText GetDisplayName() const override;

	virtual bool SupportsResetToBase() const override { return true; }
	virtual bool TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const override;
	virtual void ResetToBase() override;

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void ShaderStagePropertiesChanged();

	bool HasBaseShaderStage() const;

private:
	TWeakObjectPtr<UNiagaraShaderStageBase> ShaderStage;

	mutable TOptional<bool> bHasBaseShaderStageCache;

	mutable TOptional<bool> bCanResetToBaseCache;

	UPROPERTY()
	UNiagaraStackObject* ShaderStageObject;
};

UCLASS()
/** Meant to contain a single binding of a Emitter::EventScriptProperties to the stack.*/
class NIAGARAEDITOR_API UNiagaraStackShaderStageGroup : public UNiagaraStackScriptItemGroup
{
	GENERATED_BODY()

public:
	DECLARE_DELEGATE(FOnModifiedShaderStages);

public:
	void Initialize(
		FRequiredEntryData InRequiredEntryData,
		TSharedRef<FNiagaraScriptViewModel> InScriptViewModel,
		UNiagaraShaderStageBase* InShaderStage);

	UNiagaraShaderStageBase* GetShaderStage() const;

	void SetOnModifiedShaderStages(FOnModifiedShaderStages OnModifiedShaderStages);

	virtual bool SupportsDelete() const override { return true; }
	virtual bool TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const override;
	virtual void Delete() override;

	virtual bool CanDrag() const override { return true; }

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual TOptional<FDropRequestResponse> CanDropInternal(const FDropRequest& DropRequest) override;

	virtual TOptional<FDropRequestResponse> DropInternal(const FDropRequest& DropRequest) override;

private:
	void ShaderStagePropertiesChanged();

	bool HasBaseShaderStage() const;
	
private:
	TWeakObjectPtr<UNiagaraShaderStageBase> ShaderStage;

	mutable TOptional<bool> bHasBaseShaderStageCache;

	FOnModifiedShaderStages OnModifiedShaderStagesDelegate;

	UPROPERTY()
	UNiagaraStackShaderStagePropertiesItem* ShaderStageProperties;
};
