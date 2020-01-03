// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSystemPropertiesItem.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Customizations/NiagaraSystemDetails.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackSystemItemGroup"

void UNiagaraStackSystemPropertiesItem::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, TEXT("SystemProperties"));
	System = &GetSystemViewModel()->GetSystem();
}

FText UNiagaraStackSystemPropertiesItem::GetDisplayName() const
{
	return LOCTEXT("SystemPropertiesName", "System Properties");
}

FText UNiagaraStackSystemPropertiesItem::GetTooltipText() const
{
	return LOCTEXT("SystemPropertiesTooltip", "Properties of the System. These cannot change at runtime.");
}

bool UNiagaraStackSystemPropertiesItem::IsExpandedByDefault() const
{
	return false;
}

void UNiagaraStackSystemPropertiesItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (SystemObject == nullptr)
	{
		SystemObject = NewObject<UNiagaraStackObject>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), FExecutionCategoryNames::System, NAME_None, GetStackEditorData());
		SystemObject->Initialize(RequiredEntryData, System.Get(), GetStackEditorDataKey());
		SystemObject->RegisterInstancedCustomPropertyLayout(UNiagaraSystem::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraSystemDetails::MakeInstance));
	}

	NewChildren.Add(SystemObject);
	bCanResetToBase.Reset();
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackSystemPropertiesItem::SystemPropertiesChanged()
{
	bCanResetToBase.Reset();
}

#undef LOCTEXT_NAMESPACE
