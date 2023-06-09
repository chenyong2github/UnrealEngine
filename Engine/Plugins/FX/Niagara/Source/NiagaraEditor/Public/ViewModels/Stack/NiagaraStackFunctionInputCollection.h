// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraTypes.h"
#include "NiagaraStackSection.h"
#include "NiagaraStackFunctionInputCollection.generated.h"

class UNiagaraNodeFunctionCall;
class UNiagaraStackFunctionInput;
class UNiagaraClipboardFunctionInput;
class UEdGraphPin;

/** A base class for value collections. Values can be all kinds of input data, such as module inputs, object properties etc.
 * This has base functionality for sections.
 */
UCLASS()
class NIAGARAEDITOR_API UNiagaraStackValueCollection : public UNiagaraStackItemContent
{
	GENERATED_BODY()
public:
	void Initialize(FRequiredEntryData InRequiredEntryData, FString InOwningStackItemEditorDataKey, FString InStackEditorDataKey);
	
	bool GetShouldDisplayLabel() const { return bShouldDisplayLabel; }
	void SetShouldDisplayLabel(bool bInShouldShowLabel);

	const TArray<FText>& GetSections() const;

	FText GetActiveSection() const;

	void SetActiveSection(FText InActiveSection);

	FText GetTooltipForSection(FString Section) const;
public:
	static FText UncategorizedName;

	static FText AllSectionName;
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	virtual void GetSectionsInternal(TArray<FNiagaraStackSection>& OutStackSections) const { }
	void UpdateCachedSectionData() const;
	virtual bool FilterByActiveSection(const UNiagaraStackEntry& Child) const;
private:
	virtual bool GetCanExpand() const override;
	virtual bool GetShouldShowInStack() const override;
	virtual int32 GetChildIndentLevel() const override;
private:
	mutable TOptional<TArray<FText>> SectionsCache;
	mutable TOptional<TMap<FString, TArray<FText>>> SectionToCategoryMapCache;
	mutable TOptional<TMap<FString, FText>> SectionToTooltipMapCache;
	mutable TOptional<FText> ActiveSectionCache;
	FText LastActiveSection;

	bool bShouldDisplayLabel;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackFunctionInputCollection : public UNiagaraStackValueCollection
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
	virtual bool GetIsEnabled() const;

	void ToClipboardFunctionInputs(UObject* InOuter, TArray<const UNiagaraClipboardFunctionInput*>& OutClipboardFunctionInputs) const;

	void SetValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs);

	void GetChildInputs(TArray<UNiagaraStackFunctionInput*>& OutResult) const;

	void GetFilteredChildInputs(TArray<UNiagaraStackFunctionInput*>& OutFilteredChildInputs) const;

	void GetCustomFilteredChildInputs(TArray<UNiagaraStackFunctionInput*>& OutResult, const TArray<FOnFilterChild>& CustomFilters) const;

	TArray<UNiagaraStackFunctionInput*> GetInlineParameterInputs() const;

private:

	struct FInputData
	{
		FNiagaraVariable InputVariable;
		int32 SortKey;
		TOptional<FText> DisplayName;
		FText Category;
		bool bIsStatic;
		bool bIsHidden;

		UNiagaraNodeFunctionCall* ModuleNode;
		UNiagaraNodeFunctionCall* InputFunctionCallNode;
		
		TArray<FInputData*> Children;
		bool bIsChild = false;
	};

	struct FNiagaraParentData
	{
		FNiagaraVariable ParentVariable;
		TArray<int32> ChildIndices;
	};

	struct FFunctionCallNodesState
	{		
		TArray<FInputData> InputDataCollection;
		TMap<FName, FNiagaraParentData> ParentMapping;
	};
	
	void OnScriptApplied(UNiagaraScript* NiagaraScript, FGuid Guid);
	
	virtual void FinalizeInternal() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void GetSectionsInternal(TArray<FNiagaraStackSection>& OutStackSections) const override;
	
	void RefreshChildrenForFunctionCall(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues);
	
	void AppendInputsForFunctionCall(FFunctionCallNodesState& State, TArray<FStackIssue>& NewIssues);
	
	void ApplyAllFunctionInputsToChildren(FFunctionCallNodesState& State, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues);

	void RefreshIssues(const TArray<FName>& DuplicateInputNames, const TArray<FName>& ValidAliasedInputNames, const TArray<FNiagaraVariable>& InputsWithInvalidTypes, const TMap<FName, UEdGraphPin*>& StaticSwitchInputs, TArray<FStackIssue>& NewIssues);

	void OnFunctionInputsChanged();

	FStackIssueFix GetNodeRemovalFix(UEdGraphPin* PinToRemove, FText FixDescription);

	FStackIssueFix GetResetPinFix(UEdGraphPin* PinToReset, FText FixDescription);

	FStackIssueFix GetUpgradeVersionFix(FText FixDescription);

	void AddInvalidChildStackIssue(FName PinName, TArray<FStackIssue>& OutIssues);
	
	void AddInputToCategory(const FInputData& InputData, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren);
	
private:
	UNiagaraNodeFunctionCall* ModuleNode;
	UNiagaraNodeFunctionCall* InputFunctionCallNode;

	/** If this is set to true, no children will be resued when RefreshChildren is called */
	bool bForceCompleteRebuild = false;
};
