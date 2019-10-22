// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraCommon.h"
#include "AssetRegistry/Public/AssetData.h"
#include "NiagaraStackScriptItemGroup.generated.h"

class FNiagaraScriptViewModel;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
class FScriptItemGroupAddUtilities;
class UEdGraph;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackScriptItemGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	void Initialize(
		FRequiredEntryData InRequiredEntryData,
		FText InDisplayName,
		FText InToolTip,
		TSharedRef<FNiagaraScriptViewModel> InScriptViewModel,
		ENiagaraScriptUsage InScriptUsage,
		FGuid InScriptUsageId = FGuid());

	ENiagaraScriptUsage GetScriptUsage() const { return ScriptUsage; }
	FGuid GetScriptUsageId() const { return ScriptUsageId; }
	UNiagaraNodeOutput* GetScriptOutputNode() const;

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void FinalizeInternal() override;

	virtual TOptional<FDropRequestResponse> ChildRequestCanDropInternal(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest) override;

	virtual TOptional<FDropRequestResponse> ChildRequestDropInternal(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest) override;

private:
	void ItemAdded(UNiagaraNodeFunctionCall* AddedModule);

	void ChildModifiedGroupItems();

	void OnScriptGraphChanged(const struct FEdGraphEditAction& InAction);

	TOptional<FDropRequestResponse> ChildRequestCanDropEntries(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> ChildRequestCanDropAssets(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> ChildRequestCanDropParameter(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> ChildRequestDropEntries(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> ChildRequestDropAssets(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> ChildRequestDropParameter(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

protected:
	TWeakPtr<FNiagaraScriptViewModel> ScriptViewModel;
	void RefreshIssues(TArray<FStackIssue>& NewIssues);

private:
	TSharedPtr<FScriptItemGroupAddUtilities> AddUtilities;

	ENiagaraScriptUsage ScriptUsage;

	FGuid ScriptUsageId;
	bool bIsValidForOutput;

	TWeakObjectPtr<UEdGraph> ScriptGraph;

	FDelegateHandle OnGraphChangedHandle;
};
