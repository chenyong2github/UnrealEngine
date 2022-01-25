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
class NIAGARAEDITOR_API UNiagaraStackFunctionInputCollectionBase : public UNiagaraStackItemContent
{
	GENERATED_BODY()

public:
	UNiagaraStackFunctionInputCollectionBase();

	static FText UncategorizedName;

protected:

	struct FInputData
	{
		const UEdGraphPin* Pin;
		FNiagaraTypeDefinition Type;
		int32 SortKey;
		TOptional<FText> DisplayName;
		FText Category;
		bool bIsStatic;
		bool bIsHidden;
		bool bShouldShowInSummary;

		UNiagaraNodeFunctionCall* ModuleNode;
		UNiagaraNodeFunctionCall* InputFunctionCallNode;
		
		TArray<FInputData*> Children;
		bool bIsChild = false;

	};

	struct FNiagaraParentData
	{
		const UEdGraphPin* ParentPin;
		TArray<int32> ChildIndices;
	};

	struct FFunctionCallNodesState
	{		
		TArray<FInputData> InputDataCollection;
		TMap<FName, FNiagaraParentData> ParentMapping;
	};
	
	void RefreshChildrenForFunctionCall(UNiagaraNodeFunctionCall* ModuleNode, UNiagaraNodeFunctionCall* InputFunctionCallNode, const TArray<UNiagaraStackEntry*>& CurrentChildren, 
		TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues, bool bShouldApplySummaryFilter);
	
	void AppendInputsForFunctionCall(FFunctionCallNodesState& State, UNiagaraNodeFunctionCall* ModuleNode, UNiagaraNodeFunctionCall* InputFunctionCallNode, TArray<FStackIssue>& NewIssues, bool bShouldApplySummaryFilter);
	
	void ApplyAllFunctionInputsToChildren(FFunctionCallNodesState& State, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues, bool bShouldApplySummaryFilter);

	void RefreshIssues(UNiagaraNodeFunctionCall* InputFunctionCallNode, const TArray<FName>& DuplicateInputNames, const TArray<FName>& ValidAliasedInputNames, const TArray<const UEdGraphPin*>& PinsWithInvalidTypes, const TMap<FName, UEdGraphPin*>& StaticSwitchInputs, TArray<FStackIssue>& NewIssues);

	void OnFunctionInputsChanged();

	FStackIssueFix GetNodeRemovalFix(UEdGraphPin* PinToRemove, FText FixDescription);

	FStackIssueFix GetResetPinFix(UEdGraphPin* PinToReset, FText FixDescription);

	FStackIssueFix GetUpgradeVersionFix(FText FixDescription);

	void AddInvalidChildStackIssue(FName PinName, TArray<FStackIssue>& OutIssues);


	void AddInputToCategory(const FInputData& InputData, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren);

};



UCLASS()
class NIAGARAEDITOR_API UNiagaraStackFunctionInputCollection : public UNiagaraStackFunctionInputCollectionBase
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

	void GetCustomFilteredChildInputs(TArray<UNiagaraStackFunctionInput*>& OutResult, const TArray<FOnFilterChild>& CustomFilters) const;

	TArray<UNiagaraStackFunctionInput*> GetInlineParameterInputs() const;

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	
private:
	UNiagaraNodeFunctionCall* ModuleNode;
	UNiagaraNodeFunctionCall* InputFunctionCallNode;
	bool bShouldShowInStack;
};
