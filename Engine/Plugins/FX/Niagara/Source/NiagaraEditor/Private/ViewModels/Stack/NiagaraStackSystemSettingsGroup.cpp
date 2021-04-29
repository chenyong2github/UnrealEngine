// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSystemSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "ViewModels/Stack/NiagaraStackParameterStoreEntry.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackSystemPropertiesItem.h"
#include "NiagaraConstants.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraEditorUtilities.h"


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

	virtual TArray<FString> GetCategories() const override
	{
		return {FNiagaraEditorUtilities::GetVariableTypeCategory(GetNewParameterVariable()).ToString()};
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
		FNiagaraStackGraphUtilities::GetNewParameterAvailableTypes(AvailableTypes, FNiagaraConstants::UserNamespace);
		for (const FNiagaraTypeDefinition& AvailableType : AvailableTypes)
		{
			FNiagaraParameterHandle NewParameterHandle(FNiagaraConstants::UserNamespace, *(TEXT("New") + AvailableType.GetName()));
			FNiagaraVariable NewParameterVariable(AvailableType, NewParameterHandle.GetParameterHandleString());
			OutAddActions.Add(MakeShared<FParameterStoreGroupAddAction>(NewParameterVariable));
		}
	}

	virtual void ExecuteAddAction(TSharedRef<INiagaraStackItemGroupAddAction> AddAction, int32 TargetIndex) override
	{
		TSharedRef<FParameterStoreGroupAddAction> ParameterAddAction = StaticCastSharedRef<FParameterStoreGroupAddAction>(AddAction);
		FNiagaraVariable NewParameterVariable = ParameterAddAction->GetNewParameterVariable();
		bool bSuccess = FNiagaraEditorUtilities::AddParameter(NewParameterVariable, ParameterStore, ParameterStoreOwner, &StackEditorData);
		if (bSuccess)
		{
			OnItemAdded.ExecuteIfBound(NewParameterVariable);
		}
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
		UNiagaraStackParameterStoreItem* UserParameterStoreItem = FindCurrentChildOfTypeByPredicate<UNiagaraStackParameterStoreItem>(CurrentChildren,
			[=](UNiagaraStackParameterStoreItem* CurrentItem) { return true; });

		if (UserParameterStoreItem == nullptr)
		{
			UserParameterStoreItem = NewObject<UNiagaraStackParameterStoreItem>(this);
			UserParameterStoreItem->Initialize(CreateDefaultChildRequiredData(), Owner.Get(), UserParameterStore);
		}

		NewChildren.Add(UserParameterStoreItem);
	}

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
	if (AddedParameter.GetType().IsDataInterface())
	{
		UNiagaraDataInterface* DataInterfaceParameter = UserParameterStore->GetDataInterface(AddedParameter);
		if (DataInterfaceParameter != nullptr)
		{
			TArray<UObject*> ChangedObjects;
			ChangedObjects.Add(DataInterfaceParameter);
			OnDataObjectModified().Broadcast(ChangedObjects, ENiagaraDataObjectChange::Added);
		}
	}
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
	ParameterStoreChangedHandle = ParameterStore->AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraStackParameterStoreItem::ParameterStoreChanged));
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
	if (ParameterStore != nullptr && ParameterStore->ReadParameterVariables().Num() > 0)
	{
		TArray<FNiagaraVariable> Variables;
		ParameterStore->GetParameters(Variables);

		for (FNiagaraVariable& Var : Variables)
		{
			UNiagaraStackParameterStoreEntry* ValueObjectEntry = FindCurrentChildOfTypeByPredicate<UNiagaraStackParameterStoreEntry>(CurrentChildren,
				[=](UNiagaraStackParameterStoreEntry* CurrentEntry) { 
				bool bSameName = CurrentEntry->GetDisplayName().ToString() == Var.GetName().ToString();
				bool bSameType = CurrentEntry->GetInputType() == Var.GetType();
				return bSameName && bSameType;
			});

			if (ValueObjectEntry == nullptr)
			{
				ValueObjectEntry = NewObject<UNiagaraStackParameterStoreEntry>(this);
				ValueObjectEntry->Initialize(CreateDefaultChildRequiredData(), Owner.Get(), ParameterStore, Var.GetName().ToString(), Var.GetType(), GetStackEditorDataKey());
			}

			NewChildren.Add(ValueObjectEntry);
		}
	}
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackParameterStoreItem::FinalizeInternal()
{
	if (Owner.IsValid() && ParameterStore != nullptr)
	{
		ParameterStore->RemoveOnChangedHandler(ParameterStoreChangedHandle);
		Owner.Reset();
		ParameterStore = nullptr;
	}
	Super::FinalizeInternal();
}

void UNiagaraStackParameterStoreItem::ParameterStoreChanged()
{
	if (IsFinalized() == false && Owner.IsValid())
	{
		RefreshChildren();
	}
}

#undef LOCTEXT_NAMESPACE
