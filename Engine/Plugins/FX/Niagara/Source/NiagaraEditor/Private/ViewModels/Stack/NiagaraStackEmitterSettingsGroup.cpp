// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackEmitterSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitterDetailsCustomization.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackEmitterItemGroup"

void UNiagaraStackEmitterPropertiesItem::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, TEXT("EmitterProperties"));
	Emitter = GetEmitterViewModel()->GetEmitter();
	Emitter->OnPropertiesChanged().AddUObject(this, &UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged);
}

void UNiagaraStackEmitterPropertiesItem::FinalizeInternal()
{
	if (Emitter.IsValid())
	{
		Emitter->OnPropertiesChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

FText UNiagaraStackEmitterPropertiesItem::GetDisplayName() const
{
	return LOCTEXT("EmitterPropertiesName", "Emitter Properties");
}

FText UNiagaraStackEmitterPropertiesItem::GetTooltipText() const
{
	return LOCTEXT("EmitterPropertiesTooltip", "Properties that are handled per Emitter. These cannot change at runtime.");
}

bool UNiagaraStackEmitterPropertiesItem::CanResetToBase() const
{
	if (bCanResetToBaseCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		if (BaseEmitter != nullptr && Emitter != BaseEmitter)
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			bCanResetToBaseCache = MergeManager->IsEmitterEditablePropertySetDifferentFromBase(*Emitter.Get(), *BaseEmitter);
		}
		else
		{
			bCanResetToBaseCache = false;
		}
	}
	return bCanResetToBaseCache.GetValue();
}

void UNiagaraStackEmitterPropertiesItem::ResetToBase()
{
	if (CanResetToBase())
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
		MergeManager->ResetEmitterEditablePropertySetToBase(*Emitter, *BaseEmitter);
	}
}

bool UNiagaraStackEmitterPropertiesItem::IsExpandedByDefault() const
{
	return false;
}

void UNiagaraStackEmitterPropertiesItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (EmitterObject == nullptr)
	{
		EmitterObject = NewObject<UNiagaraStackObject>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), FExecutionCategoryNames::Emitter, NAME_None, GetStackEditorData());
		EmitterObject->Initialize(RequiredEntryData, Emitter.Get(), GetStackEditorDataKey());
		EmitterObject->RegisterInstancedCustomPropertyLayout(UNiagaraEmitter::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraEmitterDetails::MakeInstance));
	}

	NewChildren.Add(EmitterObject);
	bCanResetToBaseCache.Reset();
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged()
{
	bCanResetToBaseCache.Reset();
}

UNiagaraStackEmitterSettingsGroup::UNiagaraStackEmitterSettingsGroup()
	: PropertiesItem(nullptr)
{
}

void UNiagaraStackEmitterSettingsGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (PropertiesItem == nullptr)
	{
		PropertiesItem = NewObject<UNiagaraStackEmitterPropertiesItem>(this);
		PropertiesItem->Initialize(CreateDefaultChildRequiredData());
	}

	NewChildren.Add(PropertiesItem);

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

#undef LOCTEXT_NAMESPACE
