// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackInputCategory.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraClipboard.h"

void UNiagaraStackInputCategory::Initialize(
	FRequiredEntryData InRequiredEntryData,
	FString InputCategoryStackEditorDataKey,
	FText InCategoryName,
	FString InOwnerStackItemEditorDataKey)
{
	bool bCategoryIsAdvanced = false;
	Super::Initialize(InRequiredEntryData, InOwnerStackItemEditorDataKey, InputCategoryStackEditorDataKey);
	CategoryName = InCategoryName;
	bShouldShowInStack = true;
	
	AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackInputCategory::FilterForVisibleCondition));
	AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackInputCategory::FilterForIsInlineEditConditionToggle));
}

const FText& UNiagaraStackInputCategory::GetCategoryName() const
{
	return CategoryName;
}

void UNiagaraStackInputCategory::ResetInputs()
{
	Inputs.Empty();
}

void UNiagaraStackInputCategory::AddInput(UNiagaraNodeFunctionCall* InModuleNode, UNiagaraNodeFunctionCall* InInputFunctionCallNode, FName InInputParameterHandle, FNiagaraTypeDefinition InInputType, EStackParameterBehavior InParameterBehavior, TOptional<FText> InOptionalDisplayName, bool bIsInputHidden, bool bIsChildInput)
{
	Inputs.Add({ InModuleNode, InInputFunctionCallNode, InInputParameterHandle, InInputType, InParameterBehavior, InOptionalDisplayName, bIsInputHidden, bIsChildInput });
}

void UNiagaraStackInputCategory::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	for (FInputParameterHandleAndType& Input : Inputs)
	{
		UNiagaraStackFunctionInput* InputChild = FindCurrentChildOfTypeByPredicate<UNiagaraStackFunctionInput>(CurrentChildren, [&](UNiagaraStackFunctionInput* CurrentInput) 
		{ 
			return CurrentInput->GetInputParameterHandle() == Input.ParameterHandle && CurrentInput->GetInputType() == Input.Type && CurrentInput->GetInputFunctionCallInitialScript() == Input.InputFunctionCallNode->FunctionScript;
		});

		if (InputChild == nullptr)
		{
			InputChild = NewObject<UNiagaraStackFunctionInput>(this);
			InputChild->Initialize(CreateDefaultChildRequiredData(), *Input.ModuleNode, *Input.InputFunctionCallNode,
				Input.ParameterHandle, Input.Type, Input.ParameterBehavior, GetOwnerStackItemEditorDataKey());
		}
		InputChild->SetIsHidden(Input.bIsHidden);
		InputChild->SetSemanticChild(Input.bIsChildInput);
		InputChild->SetSummaryViewDisiplayName(Input.DisplayName);
		NewChildren.Add(InputChild);
	}
}

FText UNiagaraStackInputCategory::GetDisplayName() const
{
	return CategoryName;
}

bool UNiagaraStackInputCategory::GetShouldShowInStack() const
{
	// Categories may be empty if their children have all been hidden due to visible filters or advanced display.
	// in the case where all children have been hidden, don't show the category in the stack.
	TArray<UNiagaraStackEntry*> CurrentFilteredChildren;
	GetFilteredChildren(CurrentFilteredChildren);
	return bShouldShowInStack && CurrentFilteredChildren.Num() > 0;
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackInputCategory::GetStackRowStyle() const
{
	return EStackRowStyle::ItemCategory;
}

bool UNiagaraStackInputCategory::GetIsEnabled() const
{
	for (const auto& Input : Inputs)
	{
		if (Input.InputFunctionCallNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled)
		{
			return true;
		}
	}

	return false;
}

void UNiagaraStackInputCategory::GetSearchItems(TArray<FStackSearchItem>& SearchItems) const
{
	// Don't return search results if we're not being shown in the stack, otherwise we'll generate search results which can't be navigated to.
	if (GetShouldShowInStack())
	{
		Super::GetSearchItems(SearchItems);
	}
}

void UNiagaraStackInputCategory::SetShouldShowInStack(bool bInShouldShowInStack)
{
	bShouldShowInStack = bInShouldShowInStack;
}

void UNiagaraStackInputCategory::ToClipboardFunctionInputs(UObject* InOuter, TArray<const UNiagaraClipboardFunctionInput*>& OutClipboardFunctionInputs) const
{
	TArray<UNiagaraStackFunctionInput*> ChildInputs;
	GetUnfilteredChildrenOfType(ChildInputs);
	for (UNiagaraStackFunctionInput* ChildInput : ChildInputs)
	{
		const UNiagaraClipboardFunctionInput* FunctionInput = ChildInput->ToClipboardFunctionInput(InOuter);
		if (FunctionInput != nullptr)
		{
			OutClipboardFunctionInputs.Add(FunctionInput);
		}
	}
}

template<typename Predicate>
void SetValuesFromFunctionInputsInternal(UNiagaraStackInputCategory* Category, const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs, Predicate InputMatchesFilter)
{
	TArray<UNiagaraStackFunctionInput*> ChildInputs;
	Category->GetUnfilteredChildrenOfType(ChildInputs);
	for (UNiagaraStackFunctionInput* ChildInput : ChildInputs)
	{
		for (const UNiagaraClipboardFunctionInput* ClipboardFunctionInput : ClipboardFunctionInputs)
		{
			if (InputMatchesFilter(ChildInput) && ChildInput->GetInputParameterHandle().GetName() == ClipboardFunctionInput->InputName && ChildInput->GetInputType() == ClipboardFunctionInput->InputType)
			{
				ChildInput->SetValueFromClipboardFunctionInput(*ClipboardFunctionInput);
			}
		}
	}
}

void  UNiagaraStackInputCategory::SetStaticSwitchValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs)
{
	SetValuesFromFunctionInputsInternal(this, ClipboardFunctionInputs, [](UNiagaraStackFunctionInput* ChildInput) { return ChildInput->IsStaticParameter(); });
}

void  UNiagaraStackInputCategory::SetStandardValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs)
{
	SetValuesFromFunctionInputsInternal(this, ClipboardFunctionInputs, [](UNiagaraStackFunctionInput* ChildInput) { return ChildInput->IsStaticParameter() == false; });
}

bool UNiagaraStackInputCategory::FilterForVisibleCondition(const UNiagaraStackEntry& Child) const
{
	const UNiagaraStackFunctionInput* StackFunctionInputChild = Cast<UNiagaraStackFunctionInput>(&Child);
	return StackFunctionInputChild == nullptr || StackFunctionInputChild->GetShouldPassFilterForVisibleCondition();
}

bool UNiagaraStackInputCategory::FilterForIsInlineEditConditionToggle(const UNiagaraStackEntry& Child) const
{
	const UNiagaraStackFunctionInput* StackFunctionInputChild = Cast<UNiagaraStackFunctionInput>(&Child);
	return StackFunctionInputChild == nullptr || StackFunctionInputChild->GetIsInlineEditConditionToggle() == false;
}

