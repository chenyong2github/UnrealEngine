// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSystemPropertiesItem.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ScopedTransaction.h"

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
	}
	NewChildren.Add(SystemObject);
	bCanResetToBase.Reset();

	//Check if we're trying to override scalability settings without an EffectType. Ideally we can allow this but it's somewhat awkward so for now we just post a warning and ignore this.
	UNiagaraSystem* SystemPtr = System.Get();
	TWeakPtr<FNiagaraSystemViewModel> WeakSysViewModel = GetSystemViewModel();
	if (SystemPtr && SystemPtr->GetOverrideScalabilitySettings() && SystemPtr->GetEffectType() == nullptr)
	{
		FText FixDescription = LOCTEXT("FixOverridesWithNoEffectType", "Disable Overrides");
		FStackIssueFix FixIssue(
			FixDescription,
			FStackIssueFixDelegate::CreateLambda([=]()
				{
					if (auto Pinned = WeakSysViewModel.Pin())
					{
						FScopedTransaction ScopedTransaction(FixDescription);
						Pinned->GetSystem().Modify();
						Pinned->GetSystem().SetOverrideScalabilitySettings(false);
						Pinned->RefreshAll();
					}
				}));

		FStackIssue OverridesWithNoEffectTypeWarning(
			EStackIssueSeverity::Warning,
			LOCTEXT("FixOverridesWithNoEffectTypeSummaryText", "Scalability overrides with no Effect Type."),
			LOCTEXT("FixOverridesWithNoEffectTypeErrorText", "Scalability settings cannot be overriden if the System has no Effect Type."),
			GetStackEditorDataKey(),
			false,
			FixIssue);

		NewIssues.Add(OverridesWithNoEffectTypeWarning);
	}

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackSystemPropertiesItem::SystemPropertiesChanged()
{
	bCanResetToBase.Reset();
}

#undef LOCTEXT_NAMESPACE
