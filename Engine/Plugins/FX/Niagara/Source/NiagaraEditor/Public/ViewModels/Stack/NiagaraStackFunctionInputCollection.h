// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraTypes.h"
#include "NiagaraStackFunctionInputCollection.generated.h"

class UNiagaraNodeFunctionCall;
class UNiagaraStackFunctionInput;
class UNiagaraClipboardFunctionInput;
class UEdGraphPin;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackFunctionInputCollection : public UNiagaraStackItemContent
{
	GENERATED_BODY()

public:
	UNiagaraStackFunctionInputCollection();

	UNiagaraNodeFunctionCall* GetModuleNode() const;

	UNiagaraNodeFunctionCall* GetInputFunctionCallNode() const;

	void Initialize(
		FRequiredEntryData InRequiredEntryData,
		UNiagaraNodeFunctionCall& InModuleNode,
		UNiagaraNodeFunctionCall& InInputFunctionCallNode,
		FString InOwnerStackItemEditorDataKey);

	//~ UNiagaraStackEntry interface
	virtual FText GetDisplayName() const override;
	virtual bool GetShouldShowInStack() const override;
	virtual bool GetIsEnabled() const;

	void SetShouldShowInStack(bool bInShouldShowInStack);

	void ToClipboardFunctionInputs(UObject* InOuter, TArray<const UNiagaraClipboardFunctionInput*>& OutClipboardFunctionInputs) const;

	void SetValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs);

	void GetChildInputs(TArray<UNiagaraStackFunctionInput*>& OutResult) const;

	static FText UncategorizedName;

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	
private:
	void RefreshIssues(const TArray<FName>& DuplicateInputNames, const TArray<FName>& ValidAliasedInputNames, const TArray<const UEdGraphPin*>& PinsWithInvalidTypes, const TMap<FName, UEdGraphPin*>& StaticSwitchInputs, TArray<FStackIssue>& NewIssues);

	void OnFunctionInputsChanged();

	FStackIssueFix GetNodeRemovalFix(UEdGraphPin* PinToRemove, FText FixDescription);

	FStackIssueFix GetResetPinFix(UEdGraphPin* PinToReset, FText FixDescription);

	FStackIssueFix GetUpgradeVersionFix(FText FixDescription);

	void AddInvalidChildStackIssue(FName PinName, TArray<FStackIssue>& OutIssues);

	struct FInputData
	{
		const UEdGraphPin* Pin;
		FNiagaraTypeDefinition Type;
		int32 SortKey;
		FText Category;
		bool bIsStatic;
		bool bIsHidden;

		TArray<FInputData*> Children;
		bool bIsChild = false;
	};

	void AddInputToCategory(const FInputData& InputData, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren);

	UNiagaraNodeFunctionCall* ModuleNode;
	UNiagaraNodeFunctionCall* InputFunctionCallNode;
	bool bShouldShowInStack;
};
