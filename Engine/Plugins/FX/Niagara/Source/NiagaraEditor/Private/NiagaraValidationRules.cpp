// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraValidationRules.h"

#include "NiagaraComponentRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackEmitterSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "ViewModels/Stack/NiagaraStackSystemPropertiesItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraValidationRules"

template<typename T>
TArray<T*> GetStackEntries(UNiagaraStackViewModel* StackViewModel, bool bRefresh = false)
{
	TArray<T*> Results;
	TArray<UNiagaraStackEntry*> EntriesToCheck;
	UNiagaraStackEntry* RootEntry = StackViewModel->GetRootEntry();
	if (bRefresh)
	{
		RootEntry->RefreshChildren();
	}
	RootEntry->GetUnfilteredChildren(EntriesToCheck);
	while (EntriesToCheck.Num() > 0)
	{
		UNiagaraStackEntry* Entry = EntriesToCheck.Pop();
		if (T* ItemToCheck = Cast<T>(Entry))
		{
			Results.Add(ItemToCheck);
		}
		Entry->GetUnfilteredChildren(EntriesToCheck);
	}
	return Results;
}

// helper function to retrieve a single stack entry from the system or emitter view model
template<typename T>
T* GetStackEntry(UNiagaraStackViewModel* StackViewModel, bool bRefresh = false)
{
	TArray<T*> StackEntries = GetStackEntries<T>(StackViewModel, bRefresh);
	if (StackEntries.Num() > 0)
	{
		return StackEntries[0];
	}
	return nullptr;
}

// --------------------------------------------------------------------------------------------------------------------------------------------

TArray<FNiagaraValidationResult> UNiagara_NoWarmupTime::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const
{
	TArray<FNiagaraValidationResult> Results;
	UNiagaraSystem& System = ViewModel->GetSystem();
	if (System.NeedsWarmup())
	{
		UNiagaraStackSystemPropertiesItem* SystemProperties = GetStackEntry<UNiagaraStackSystemPropertiesItem>(ViewModel->GetSystemStackViewModel());
		FNiagaraValidationResult Result(ENiagaraValidationSeverity::Error, LOCTEXT("WarumupSummary", "Warmuptime > 0 is not allowed"), LOCTEXT("WarmupDescription", "Systems with the chosen effect type do not allow warmup time, as it costs too much performance.\nPlease set the warmup time to 0 in the system properties."), SystemProperties);
		Results.Add(Result);
	}
	return Results;
}

TArray<FNiagaraValidationResult> UNiagara_FixedGPUBoundsSet::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const
{
	TArray<FNiagaraValidationResult> Results;

	// if the system has fixed bounds set then it overrides the emitter settings
	if (ViewModel->GetSystem().bFixedBounds)
	{
		return Results;
	}

	// check that all the gpu emitters have fixed bounds set
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels = ViewModel->GetEmitterHandleViewModels();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel : EmitterHandleViewModels)
	{
		UNiagaraEmitter* NiagaraEmitter = EmitterHandleModel.Get().GetEmitterHandle()->GetInstance();
		if (NiagaraEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && NiagaraEmitter->bFixedBounds == false)
		{
			UNiagaraStackEmitterPropertiesItem* EmitterProperties = GetStackEntry<UNiagaraStackEmitterPropertiesItem>(EmitterHandleModel.Get().GetEmitterStackViewModel());
			FNiagaraValidationResult Result(ENiagaraValidationSeverity::Error, LOCTEXT("FixedBoundsSummary", "GPU emitters require fixed bounds"), LOCTEXT("FixedBoundsDescription", "Please set fixed bounds for gpu emitters or the system as a whole."), EmitterProperties);
			Results.Add(Result);
		}
	}
	return Results;
}

bool IsEnabledForMaxQualityLevel(FNiagaraPlatformSet Platforms, int32 MaxQualityLevel)
{
	for (int i = 0; i < MaxQualityLevel; i++)
	{
		if (Platforms.IsEnabledForQualityLevel(i))
		{
			return true;
		}
	}
	return false;
}

TArray<FNiagaraValidationResult> UNiagara_NoComponentRendererOnLowSettings::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const
{
	TArray<FNiagaraValidationResult> Results;

	// check that component renderers are only active for high and cinematic settings 
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels = ViewModel->GetEmitterHandleViewModels();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel : EmitterHandleViewModels)
	{
		int32 MaxQualityLevel = 3;
		UNiagaraEmitter* NiagaraEmitter = EmitterHandleModel.Get().GetEmitterHandle()->GetInstance();
		NiagaraEmitter->ForEachRenderer([&Results, EmitterHandleModel, MaxQualityLevel](UNiagaraRendererProperties* RendererProperties)
		{
			if (Cast<UNiagaraComponentRendererProperties>(RendererProperties) && RendererProperties->GetIsEnabled() && IsEnabledForMaxQualityLevel(RendererProperties->Platforms, MaxQualityLevel))
			{
				TArray<UNiagaraStackRendererItem*> RendererItems = GetStackEntries<UNiagaraStackRendererItem>(EmitterHandleModel.Get().GetEmitterStackViewModel());
				for (UNiagaraStackRendererItem* Item : RendererItems)
				{
					if (Item->GetRendererProperties() != RendererProperties)
					{
						continue;
					}
					FNiagaraValidationResult Result(ENiagaraValidationSeverity::Warning, LOCTEXT("ComponentRenderSummary", "Component renderers should not be used for low-level platforms"), LOCTEXT("ComponentRenderDescription", "Please disable low level platforms in the renderer's scalability settings."), Item);
					Results.Add(Result);
				}
			}
		});
	}
	return Results;
}

TArray<FNiagaraValidationResult> UNiagara_InvalidEffectType::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const
{
	TArray<FNiagaraValidationResult> Results;
	UNiagaraStackSystemPropertiesItem* SystemProperties = GetStackEntry<UNiagaraStackSystemPropertiesItem>(ViewModel->GetSystemStackViewModel());
	FNiagaraValidationResult Result(ENiagaraValidationSeverity::Error, LOCTEXT("InvalidEffectSummary", "Invalid Effect Type"), LOCTEXT("InvalidEffectDescription", "The effect type on this system was marked as invalid for production content and should only be used as placeholder."), SystemProperties);
	Results.Add(Result);
	return Results;
}

#undef LOCTEXT_NAMESPACE
