// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraTypes.h"
#include "NiagaraStackFunctionInputCollection.generated.h"

class UNiagaraNodeFunctionCall;
class UNiagaraStackFunctionInput;
class UNiagaraClipboardFunctionInput;
class UEdGraphPin;

struct NIAGARAEDITOR_API FNiagaraLocalInputValueData
{
	FNiagaraLocalInputValueData(TSharedPtr<const FStructOnScope> InLocalData, TMulticastDelegate<void()>& InOnValueChanged) :
	LocalData(InLocalData), OnValueChanged(InOnValueChanged)
	{}
	TSharedPtr<const FStructOnScope> LocalData;
	TMulticastDelegate<void()>& OnValueChanged;
	TArray<UNiagaraStackEntry::FStackSearchItem> StackSearchItems;
};

struct NIAGARAEDITOR_API FNiagaraDataInterfaceInput
{
	FNiagaraDataInterfaceInput(TWeakObjectPtr<const UNiagaraDataInterface> InDataInterface, TMulticastDelegate<void()>& InOnValueChanged) :
	DataInterface(InDataInterface), OnValueChanged(InOnValueChanged)
	{}
	TWeakObjectPtr<const UNiagaraDataInterface> DataInterface;
	TMulticastDelegate<void()>& OnValueChanged;
	TArray<UNiagaraStackEntry::FStackSearchItem> StackSearchItems;
};



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

		TArray<FInputData*> Children;
		bool bIsChild = false;
	};

	struct FNiagaraParentData
	{
		const UEdGraphPin* ParentPin;
		TArray<int32> ChildIndices;
	};
	
	void RefreshChildrenForFunctionCall(UNiagaraNodeFunctionCall* ModuleNode, UNiagaraNodeFunctionCall* InputFunctionCallNode, const TArray<UNiagaraStackEntry*>& CurrentChildren, 
		TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues, bool bShouldApplySummaryFilter, const FText& BaseCategory);

	void RefreshIssues(UNiagaraNodeFunctionCall* InputFunctionCallNode, const TArray<FName>& DuplicateInputNames, const TArray<FName>& ValidAliasedInputNames, const TArray<const UEdGraphPin*>& PinsWithInvalidTypes, const TMap<FName, UEdGraphPin*>& StaticSwitchInputs, TArray<FStackIssue>& NewIssues);

	void OnFunctionInputsChanged();

	FStackIssueFix GetNodeRemovalFix(UEdGraphPin* PinToRemove, FText FixDescription);

	FStackIssueFix GetResetPinFix(UEdGraphPin* PinToReset, FText FixDescription);

	FStackIssueFix GetUpgradeVersionFix(FText FixDescription);

	void AddInvalidChildStackIssue(FName PinName, TArray<FStackIssue>& OutIssues);


	void AddInputToCategory(UNiagaraNodeFunctionCall* ModuleNode, UNiagaraNodeFunctionCall* InputFunctionCallNode, const FInputData& InputData, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren);

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

	void GetFilteredChildInputs(TArray<UNiagaraStackFunctionInput*>& OutResult) const;

	TOptional<FNiagaraLocalInputValueData> GetLocalInput(FNiagaraVariable InputParameter, bool bFiltered = false) const;

	TOptional<FNiagaraDataInterfaceInput> GetDataInterfaceForInput(FNiagaraVariable InputParameter, bool bFiltered = false) const;

protected:
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	
private:
	UNiagaraNodeFunctionCall* ModuleNode;
	UNiagaraNodeFunctionCall* InputFunctionCallNode;
	bool bShouldShowInStack;
};
