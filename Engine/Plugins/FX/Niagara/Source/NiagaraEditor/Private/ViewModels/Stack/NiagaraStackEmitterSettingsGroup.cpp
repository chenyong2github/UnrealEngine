// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackEmitterSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitterDetailsCustomization.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraSystem.h"
#include "NiagaraEditorStyle.h"

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

bool UNiagaraStackEmitterPropertiesItem::TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const
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
	if (bCanResetToBaseCache.GetValue())
	{
		OutCanResetToBaseMessage = LOCTEXT("CanResetToBase", "Reset the emitter properties to the state defined by the parent emitter.");
		return true;
	}
	else
	{
		OutCanResetToBaseMessage = LOCTEXT("CanNotResetToBase", "No parent to reset to, or not different from parent.");
		return false;
	}
}

void UNiagaraStackEmitterPropertiesItem::ResetToBase()
{
	FText Unused;
	if (TestCanResetToBaseWithMessage(Unused))
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

const FSlateBrush* UNiagaraStackEmitterPropertiesItem::GetIconBrush() const
{
	if (Emitter->SimTarget == ENiagaraSimTarget::CPUSim)
	{
		return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.CPUIcon");
	}
	if (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.GPUIcon");
	}
	return FEditorStyle::GetBrush("NoBrush");
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
	RefreshIssues(NewIssues);
}


void UNiagaraStackEmitterPropertiesItem::RefreshIssues(TArray<FStackIssue>& NewIssues)
{
	UNiagaraEmitter* ActualEmitter = GetEmitterViewModel()->GetEmitter();
	if (ActualEmitter && ActualEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && ActualEmitter->bFixedBounds == false)
	{
		bool bAddError = true;
		
		UNiagaraSystem& Sys = GetSystemViewModel()->GetSystem();
		if (Sys.bFixedBounds)
		{
			bAddError = false;
		}			
		

		if (bAddError)
		{
			FStackIssue MissingRequiredFixedBoundsModuleError(
				EStackIssueSeverity::Warning,
				LOCTEXT("RequiredFixedBoundsWarningFormat", "The emitter is GPU but the fixed bounds checkbox is not set.\r\nPlease update the Emitter or System properties, otherwise existing value for fixed bounds will be used."),
				LOCTEXT("MissingFixedBounds", "Missing fixed bounds."),
				GetStackEditorDataKey(),
				false);

			NewIssues.Add(MissingRequiredFixedBoundsModuleError);
		}
	}
}

void UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged()
{
	if (IsFinalized() == false)
	{
		// Undo/redo can cause objects to disappear and reappear which can prevent safe removal of delegates
		// so guard against receiving an event when finalized here.
		bCanResetToBaseCache.Reset();
		RefreshChildren();
	}
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
