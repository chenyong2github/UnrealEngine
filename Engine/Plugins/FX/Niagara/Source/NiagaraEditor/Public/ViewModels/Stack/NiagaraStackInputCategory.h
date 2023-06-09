// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItem.h"
#include "NiagaraTypes.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "ViewModels/HierarchyEditor/NiagaraSummaryViewViewModel.h"
#include "NiagaraStackInputCategory.generated.h"

class UNiagaraStackFunctionInput;
class UNiagaraNodeFunctionCall;
class UNiagaraClipboardFunctionInput;

UENUM()
enum class EStackParameterBehavior
{
	Dynamic, Static
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackCategory : public UNiagaraStackItemContent
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData, FString InOwningStackItemEditorDataKey, FString InStackEditorDataKey);

	//~ UNiagaraStackEntry interface
	virtual FText GetDisplayName() const override;
	virtual bool GetShouldShowInStack() const override;
	virtual EStackRowStyle GetStackRowStyle() const override;
	virtual void GetSearchItems(TArray<FStackSearchItem>& SearchItems) const override;

	virtual bool IsTopLevelCategory() const { return false; }
	virtual int32 GetChildIndentLevel() const override;

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
private:
private:
	bool FilterForVisibleCondition(const UNiagaraStackEntry& Child) const;
	bool FilterForIsInlineEditConditionToggle(const UNiagaraStackEntry& Child) const;
protected:
	bool bShouldShowInStack;

	UPROPERTY()
	TObjectPtr<UNiagaraStackSpacer> CategorySpacer;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackInputCategory : public UNiagaraStackCategory
{
	GENERATED_BODY() 

public:
	void Initialize(
		FRequiredEntryData InRequiredEntryData,
		FString InputCategoryStackEditorDataKey,
		FText InCategoryName,
		bool bInIsTopLevelCategory,
		FString InOwnerStackItemEditorDataKey);
	
	bool GetIsEnabled() const;

	virtual FText GetDisplayName() const override;

	void ResetInputs();

	void AddInput(UNiagaraNodeFunctionCall* InModuleNode, UNiagaraNodeFunctionCall* InInputFunctionCallNode, FName InInputParameterHandle, FNiagaraTypeDefinition InInputType, EStackParameterBehavior InParameterBehavior, TOptional<FText> InOptionalDisplayName, bool bIsHidden, bool bIsChildInput);

	void SetShouldShowInStack(bool bInShouldShowInStack);

	void ToClipboardFunctionInputs(UObject* InOuter, TArray<const UNiagaraClipboardFunctionInput*>& OutClipboardFunctionInputs) const;

	// We pass in the owning input collection to refresh the inputs of a category after setting a static switch value.
	// This is done to support copy paste for chained static switches, as otherwise the stack entry for the 2nd static switch won't exist for pasting data
	void SetStaticSwitchValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs, class UNiagaraStackFunctionInputCollection& OwningFunctionCollection);

	void SetStandardValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs);

	void GetFilteredChildInputs(TArray<UNiagaraStackFunctionInput*>& OutFilteredChildInputs) const;

protected:
	//~ UNiagaraStackEntry interface
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	virtual bool IsTopLevelCategory() const override { return bIsTopLevelCategory; }
private:
	struct FInputParameterHandleAndType
	{
		UNiagaraNodeFunctionCall* ModuleNode;
		UNiagaraNodeFunctionCall* InputFunctionCallNode;
		FName ParameterHandle;
		FNiagaraTypeDefinition Type;
		EStackParameterBehavior ParameterBehavior;
		TOptional<FText> DisplayName;
		bool bIsHidden;
		bool bIsChildInput;
	};

	FText CategoryName;
	TOptional<FText> DisplayName;
	bool bIsTopLevelCategory;

	TArray<FInputParameterHandleAndType> Inputs;
};

void AddSummaryItem(UNiagaraHierarchyItemBase* HierarchyItem, UNiagaraStackEntry* Parent);

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackSummaryCategory : public UNiagaraStackCategory
{
	GENERATED_BODY()

public:
	UNiagaraStackSummaryCategory() {}

	void Initialize(
		FRequiredEntryData InRequiredEntryData,
		TSharedPtr<FNiagaraHierarchyCategoryViewModel> InCategory,
		FString InOwnerStackItemEditorDataKey);

	//~ UNiagaraStackEntry interface
	virtual FText GetDisplayName() const override;
	virtual bool GetIsEnabled() const override { return true; }
	
	TWeakPtr<FNiagaraHierarchyCategoryViewModel> GetHierarchyCategory() const { return CategoryViewModelWeakPtr; }

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	virtual bool IsTopLevelCategory() const override;
	virtual FText GetTooltipText() const override;
	virtual int32 GetChildIndentLevel() const override;

private:
	TWeakPtr<FNiagaraHierarchyCategoryViewModel> CategoryViewModelWeakPtr;
};
