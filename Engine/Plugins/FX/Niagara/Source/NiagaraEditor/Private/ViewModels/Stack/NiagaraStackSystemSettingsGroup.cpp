// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSystemSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "ViewModels/Stack/NiagaraStackParameterStoreEntry.h"
#include "Internationalization/Internationalization.h"
#include "ViewModels/Stack/NiagaraStackSpacer.h"
#include "NiagaraStackEditorData.h"
#include "ScopedTransaction.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackSystemPropertiesItem.h"


#define LOCTEXT_NAMESPACE "UNiagaraStackParameterStoreGroup"

class FParameterStoreGroupAddAction : public INiagaraStackItemGroupAddAction
{
public:
	FParameterStoreGroupAddAction(FNiagaraVariable InNewParameterVariable)
		: NewParameterVariable(InNewParameterVariable)
	{
	}

	FNiagaraVariable GetNewParameterVariable() const
	{
		return NewParameterVariable;
	}

	virtual FText GetCategory() const override
	{
		return LOCTEXT("CreateNewParameterCategory", "Parameter Types");
	}

	virtual FText GetDisplayName() const override
	{
		return NewParameterVariable.GetType().GetNameText();
	}

	virtual FText GetDescription() const override
	{
		return FText::Format(LOCTEXT("AddParameterActionDescriptionFormat", "Create a new {0} parameter."), GetDisplayName());
	}

	virtual FText GetKeywords() const override
	{
		return FText();
	}

private:
	FNiagaraVariable NewParameterVariable;
};

class FParameterStoreGroupAddUtiliites : public TNiagaraStackItemGroupAddUtilities<FNiagaraVariable>
{
public:
	FParameterStoreGroupAddUtiliites(UObject& InParameterStoreOwner, FNiagaraParameterStore& InParameterStore, UNiagaraStackEditorData& InStackEditorData, FOnItemAdded InOnItemAdded)
		: TNiagaraStackItemGroupAddUtilities(LOCTEXT("ScriptGroupAddItemName", "Parameter"), EAddMode::AddFromAction, true, InOnItemAdded)
		, ParameterStoreOwner(InParameterStoreOwner)
		, ParameterStore(InParameterStore)
		, StackEditorData(InStackEditorData)
	{
	}

	virtual void AddItemDirectly() override { unimplemented(); }

	virtual void GenerateAddActions(TArray<TSharedRef<INiagaraStackItemGroupAddAction>>& OutAddActions, const FNiagaraStackItemGroupAddOptions& AddProperties) const override
	{
		TArray<FNiagaraTypeDefinition> AvailableTypes;
		FNiagaraStackGraphUtilities::GetNewParameterAvailableTypes(AvailableTypes, FNiagaraParameterHandle::UserNamespace);
		for (const FNiagaraTypeDefinition& AvailableType : AvailableTypes)
		{
			FNiagaraParameterHandle NewParameterHandle(FNiagaraParameterHandle::UserNamespace, *(TEXT("New") + AvailableType.GetName()));
			FNiagaraVariable NewParameterVariable(AvailableType, NewParameterHandle.GetParameterHandleString());
			OutAddActions.Add(MakeShared<FParameterStoreGroupAddAction>(NewParameterVariable));
		}
	}

	virtual void ExecuteAddAction(TSharedRef<INiagaraStackItemGroupAddAction> AddAction, int32 TargetIndex) override
	{
		TSharedRef<FParameterStoreGroupAddAction> ParameterAddAction = StaticCastSharedRef<FParameterStoreGroupAddAction>(AddAction);

		FScopedTransaction AddTransaction(LOCTEXT("AddParameter", "Add Parameter"));
		ParameterStoreOwner.Modify();

		FNiagaraVariable ParameterVariable = ParameterAddAction->GetNewParameterVariable();
		FNiagaraEditorUtilities::ResetVariableToDefaultValue(ParameterVariable);

		ParameterStore.AddParameter(ParameterVariable);
		StackEditorData.SetModuleInputIsRenamePending(ParameterVariable.GetName().ToString(), true);

		OnItemAdded.ExecuteIfBound(ParameterVariable);
	}

private:
	UObject& ParameterStoreOwner;
	FNiagaraParameterStore& ParameterStore;
	UNiagaraStackEditorData& StackEditorData;
};

void UNiagaraStackSystemSettingsGroup::Initialize(
	FRequiredEntryData InRequiredEntryData,
	UObject* InOwner,
	FNiagaraParameterStore* InParameterStore)
{
	AddUtilities = MakeShared<FParameterStoreGroupAddUtiliites>(*InOwner, *InParameterStore, *InRequiredEntryData.StackEditorData,
		FParameterStoreGroupAddUtiliites::FOnItemAdded::CreateUObject(this, &UNiagaraStackSystemSettingsGroup::ParameterAdded));
	FText DisplayName = LOCTEXT("SystemSettingsGroupName", "System Settings");
	FText Tooltip = LOCTEXT("SystemSettingsTooltip", "Settings of the System.");
	Super::Initialize(InRequiredEntryData, DisplayName, Tooltip, AddUtilities.Get());

	Owner = InOwner;
	UserParameterStore = InParameterStore;
}

void UNiagaraStackSystemSettingsGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (UserParameterStore != nullptr)
	{
		FName UserParameterStoreItemSpacerKey = "UserParameterStoreSpacer";
		UNiagaraStackSpacer* UserParameterStoreItemSpacer = FindCurrentChildOfTypeByPredicate<UNiagaraStackSpacer>(CurrentChildren,
			[=](UNiagaraStackSpacer* CurrentParameterStoreItemSpacer) { return CurrentParameterStoreItemSpacer->GetSpacerKey() == UserParameterStoreItemSpacerKey; });

		if (UserParameterStoreItemSpacer == nullptr)
		{
			UserParameterStoreItemSpacer = NewObject<UNiagaraStackSpacer>(this);
			UserParameterStoreItemSpacer->Initialize(CreateDefaultChildRequiredData(), UserParameterStoreItemSpacerKey, 1.4f);
		}

		NewChildren.Add(UserParameterStoreItemSpacer);

		UNiagaraStackParameterStoreItem* UserParameterStoreItem = FindCurrentChildOfTypeByPredicate<UNiagaraStackParameterStoreItem>(CurrentChildren,
			[=](UNiagaraStackParameterStoreItem* CurrentItem) { return true; });

		if (UserParameterStoreItem == nullptr)
		{
			UserParameterStoreItem = NewObject<UNiagaraStackParameterStoreItem>(this);
			UserParameterStoreItem->Initialize(CreateDefaultChildRequiredData(), Owner.Get(), UserParameterStore);
		}

		NewChildren.Add(UserParameterStoreItem);
	}

	FName SystemPropertiesItemSpacerKey = "SystemPropertiesSpacer";
	UNiagaraStackSpacer* SystemPropertiesItemSpacer = FindCurrentChildOfTypeByPredicate<UNiagaraStackSpacer>(CurrentChildren,
		[=](UNiagaraStackSpacer* CurrentSystemPropertiesItemSpacer) { return CurrentSystemPropertiesItemSpacer->GetSpacerKey() == SystemPropertiesItemSpacerKey; });

	if (SystemPropertiesItemSpacer == nullptr)
	{
		SystemPropertiesItemSpacer = NewObject<UNiagaraStackSpacer>(this);
		SystemPropertiesItemSpacer->Initialize(CreateDefaultChildRequiredData(), SystemPropertiesItemSpacerKey, 1.4f);
	}
	NewChildren.Add(SystemPropertiesItemSpacer);

	UNiagaraStackSystemPropertiesItem* SystemPropertiesItem = FindCurrentChildOfTypeByPredicate<UNiagaraStackSystemPropertiesItem>(CurrentChildren,
		[=](UNiagaraStackSystemPropertiesItem* CurrentItem) { return true; });

	if (SystemPropertiesItem == nullptr)
	{
		SystemPropertiesItem = NewObject<UNiagaraStackSystemPropertiesItem>(this);
		SystemPropertiesItem->Initialize(CreateDefaultChildRequiredData());
	}
	NewChildren.Add(SystemPropertiesItem);

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackSystemSettingsGroup::ParameterAdded(FNiagaraVariable AddedParameter)
{
	RefreshChildren();
}

void UNiagaraStackParameterStoreItem::Initialize(
	FRequiredEntryData InRequiredEntryData,
	UObject* InOwner,
	FNiagaraParameterStore* InParameterStore)
{
	Super::Initialize(InRequiredEntryData, TEXT("ParameterStoreItem"));

	Owner = InOwner;
	ParameterStore = InParameterStore;
}

FText UNiagaraStackParameterStoreItem::GetDisplayName() const
{
	return LOCTEXT("ParameterItemDisplayName", "User Parameters");
}

FText UNiagaraStackParameterStoreItem::GetTooltipText() const
{
	return LOCTEXT("ParameterItemTooltip", "Displays the variables created in the User namespace. These variables are exposed to owning UComponents, blueprints, etc.");
}

void UNiagaraStackParameterStoreItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (ParameterStore != nullptr && ParameterStore->GetNumParameters() > 0)
	{
		TArray<FNiagaraVariable> Variables;
		ParameterStore->GetParameters(Variables);

		for (FNiagaraVariable& Var : Variables)
		{
			UNiagaraStackParameterStoreEntry* ValueObjectEntry = FindCurrentChildOfTypeByPredicate<UNiagaraStackParameterStoreEntry>(CurrentChildren,
				[=](UNiagaraStackParameterStoreEntry* CurrentEntry) { return CurrentEntry->GetDisplayName().ToString() == Var.GetName().ToString(); });

			if (ValueObjectEntry == nullptr)
			{
				ValueObjectEntry = NewObject<UNiagaraStackParameterStoreEntry>(this);
				ValueObjectEntry->Initialize(CreateDefaultChildRequiredData(), Owner.Get(), ParameterStore, Var.GetName().ToString(), Var.GetType(), GetStackEditorDataKey());
				ValueObjectEntry->OnParameterDeleted().AddUObject(this, &UNiagaraStackParameterStoreItem::ParameterDeleted);
			}

			NewChildren.Add(ValueObjectEntry);
		}
	}
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackParameterStoreItem::ParameterDeleted()
{
	RefreshChildren();
}

#undef LOCTEXT_NAMESPACE
