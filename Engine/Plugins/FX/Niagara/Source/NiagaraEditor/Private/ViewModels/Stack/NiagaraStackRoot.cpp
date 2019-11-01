// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackRoot.h"
#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackEmitterSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackRenderItemGroup.h"
#include "ViewModels/Stack/NiagaraStackEventHandlerGroup.h"
#include "ViewModels/Stack/NiagaraStackEventScriptItemGroup.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraSystemScriptViewModel.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraEmitterEditorData.h"
#include "ViewModels/Stack/NiagaraStackSystemSettingsGroup.h"

#define LOCTEXT_NAMESPACE "NiagaraStackViewModel"

UNiagaraStackRoot::UNiagaraStackRoot()
	: SystemSettingsGroup(nullptr)
	, EmitterSettingsGroup(nullptr)
	, EmitterSpawnGroup(nullptr)
	, EmitterUpdateGroup(nullptr)
	, ParticleSpawnGroup(nullptr)
	, ParticleUpdateGroup(nullptr)
	, AddEventHandlerGroup(nullptr)
	, RenderGroup(nullptr)
{
}

void UNiagaraStackRoot::Initialize(FRequiredEntryData InRequiredEntryData, bool bInIncludeSystemInformation, bool bInIncludeEmitterInformation)
{
	Super::Initialize(InRequiredEntryData, TEXT("Root"));
	bIncludeSystemInformation = bInIncludeSystemInformation;
	bIncludeEmitterInformation = bInIncludeEmitterInformation;
	SystemSettingsGroup = nullptr;
	EmitterSettingsGroup = nullptr;
	EmitterSpawnGroup = nullptr;
	EmitterUpdateGroup = nullptr;
	ParticleSpawnGroup = nullptr;
	ParticleUpdateGroup = nullptr;
	AddEventHandlerGroup = nullptr;
	RenderGroup = nullptr;
}

bool UNiagaraStackRoot::GetCanExpand() const
{
	return false;
}

bool UNiagaraStackRoot::GetShouldShowInStack() const
{
	return false;
}

void UNiagaraStackRoot::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	// Create static entries as needed.
	if (bIncludeSystemInformation && SystemSettingsGroup == nullptr)
	{
		SystemSettingsGroup = NewObject<UNiagaraStackSystemSettingsGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::System, FExecutionSubcategoryNames::Settings,
			GetSystemViewModel()->GetEditorData().GetStackEditorData());
		SystemSettingsGroup->Initialize(RequiredEntryData, &GetSystemViewModel()->GetSystem(), &GetSystemViewModel()->GetSystem().GetExposedParameters());
	}

	if (bIncludeSystemInformation && SystemSpawnGroup == nullptr)
	{
		SystemSpawnGroup = NewObject<UNiagaraStackScriptItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::System, FExecutionSubcategoryNames::Spawn,
			GetSystemViewModel()->GetEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("SystemSpawnGroupName", "System Spawn");
		FText ToolTip = LOCTEXT("SystemSpawnGroupToolTip", "Occurs once at System creation on the CPU. Modules in this section should initialize defaults and/or do initial setup.\r\nModules are executed in order from top to bottom of the stack.");
		SystemSpawnGroup->Initialize(RequiredEntryData, DisplayName, ToolTip, GetSystemViewModel()->GetSystemScriptViewModel().ToSharedRef(), ENiagaraScriptUsage::SystemSpawnScript);
	}

	if (bIncludeSystemInformation && SystemUpdateGroup == nullptr)
	{
		SystemUpdateGroup = NewObject<UNiagaraStackScriptItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::System, FExecutionSubcategoryNames::Update,
			GetSystemViewModel()->GetEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("SystemUpdateGroupName", "System Update");
		FText ToolTip = LOCTEXT("SystemUpdateGroupToolTip", "Occurs every Emitter tick on the CPU.Modules in this section should compute values for parameters for emitter or particle update or spawning this frame.\r\nModules are executed in order from top to bottom of the stack.");
		SystemUpdateGroup->Initialize(RequiredEntryData, DisplayName, ToolTip, GetSystemViewModel()->GetSystemScriptViewModel().ToSharedRef(), ENiagaraScriptUsage::SystemUpdateScript);
	}

	if (bIncludeEmitterInformation && EmitterSettingsGroup == nullptr)
	{
		EmitterSettingsGroup = NewObject<UNiagaraStackEmitterSettingsGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Emitter, FExecutionSubcategoryNames::Settings,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("EmitterSettingsGroupName", "Emitter Settings");
		FText Tooltip = LOCTEXT("EmitterSettingsTooltip", "Settings that are handled per Emitter.");
		EmitterSettingsGroup->Initialize(RequiredEntryData, DisplayName, Tooltip, nullptr);
	}

	if (bIncludeEmitterInformation && EmitterSpawnGroup == nullptr)
	{
		EmitterSpawnGroup = NewObject<UNiagaraStackScriptItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Emitter, FExecutionSubcategoryNames::Spawn,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("EmitterSpawnGroupName", "Emitter Spawn");
		FText ToolTip = LOCTEXT("EmitterSpawnGroupTooltip", "Occurs once at Emitter creation on the CPU. Modules in this section should initialize defaults and/or do initial setup.\r\nModules are executed in order from top to bottom of the stack.");
		EmitterSpawnGroup->Initialize(RequiredEntryData, DisplayName, ToolTip, GetEmitterViewModel()->GetSharedScriptViewModel(), ENiagaraScriptUsage::EmitterSpawnScript);
	}

	if (bIncludeEmitterInformation && EmitterUpdateGroup == nullptr)
	{
		EmitterUpdateGroup = NewObject<UNiagaraStackScriptItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Emitter, FExecutionSubcategoryNames::Update,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("EmitterUpdateGroupName", "Emitter Update");
		FText ToolTip = LOCTEXT("EmitterUpdateGroupTooltip", "Occurs every Emitter tick on the CPU. Modules in this section should compute values for parameters for Particle Update or Spawning this frame.\r\nModules are executed in order from top to bottom of the stack.");
		EmitterUpdateGroup->Initialize(RequiredEntryData, DisplayName, ToolTip, GetEmitterViewModel()->GetSharedScriptViewModel(), ENiagaraScriptUsage::EmitterUpdateScript);
	}

	if (bIncludeEmitterInformation && ParticleSpawnGroup == nullptr)
	{
		ParticleSpawnGroup = NewObject<UNiagaraStackScriptItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Particle, FExecutionSubcategoryNames::Spawn,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("ParticleSpawnGroupName", "Particle Spawn");
		FText ToolTip = LOCTEXT("ParticleSpawnGroupTooltip", "Called once per created particle. Modules in this section should set up initial values for each particle.\r\nIf \"Use Interpolated Spawning\" is set, we will also run the Particle Update script after the Particle Spawn script.\r\nModules are executed in order from top to bottom of the stack.");
		ParticleSpawnGroup->Initialize(RequiredEntryData, DisplayName, ToolTip, GetEmitterViewModel()->GetSharedScriptViewModel(), ENiagaraScriptUsage::ParticleSpawnScript);
	}

	if (bIncludeEmitterInformation && ParticleUpdateGroup == nullptr)
	{
		ParticleUpdateGroup = NewObject<UNiagaraStackScriptItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Particle, FExecutionSubcategoryNames::Update,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		FText DisplayName = LOCTEXT("ParticleUpdateGroupName", "Particle Update");
		FText ToolTip = LOCTEXT("ParticleUpdateGroupTooltip", "Called every frame per particle. Modules in this section should update new values for this frame.\r\nModules are executed in order from top to bottom of the stack.");
		ParticleUpdateGroup->Initialize(RequiredEntryData, DisplayName, ToolTip, GetEmitterViewModel()->GetSharedScriptViewModel(), ENiagaraScriptUsage::ParticleUpdateScript);
	}

	if (bIncludeEmitterInformation && AddEventHandlerGroup == nullptr)
	{
		AddEventHandlerGroup = NewObject<UNiagaraStackEventHandlerGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Particle, FExecutionSubcategoryNames::Event,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		AddEventHandlerGroup->Initialize(RequiredEntryData);
		AddEventHandlerGroup->SetOnItemAdded(UNiagaraStackEventHandlerGroup::FOnItemAdded::CreateUObject(this, &UNiagaraStackRoot::EmitterEventArraysChanged));
	}

	if (bIncludeEmitterInformation && RenderGroup == nullptr)
	{
		RenderGroup = NewObject<UNiagaraStackRenderItemGroup>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
			FExecutionCategoryNames::Render, FExecutionSubcategoryNames::Render,
			GetEmitterViewModel()->GetOrCreateEditorData().GetStackEditorData());
		RenderGroup->Initialize(RequiredEntryData);
	}

	// Populate new children
	if (bIncludeSystemInformation)
	{
		NewChildren.Add(SystemSettingsGroup);
		NewChildren.Add(SystemSpawnGroup);
		NewChildren.Add(SystemUpdateGroup);
	}

	if (bIncludeEmitterInformation)
	{
		NewChildren.Add(EmitterSettingsGroup);
		NewChildren.Add(EmitterSpawnGroup);
		NewChildren.Add(EmitterUpdateGroup);

		NewChildren.Add(ParticleSpawnGroup);
		NewChildren.Add(ParticleUpdateGroup);

		for (const FNiagaraEventScriptProperties& EventScriptProperties : GetEmitterViewModel()->GetEmitter()->GetEventHandlers())
		{
			UNiagaraStackEventScriptItemGroup* EventHandlerGroup = FindCurrentChildOfTypeByPredicate<UNiagaraStackEventScriptItemGroup>(CurrentChildren,
				[&](UNiagaraStackEventScriptItemGroup* CurrentEventHandlerGroup) { return CurrentEventHandlerGroup->GetScriptUsageId() == EventScriptProperties.Script->GetUsageId(); });

			if (EventHandlerGroup == nullptr)
			{
				EventHandlerGroup = NewObject<UNiagaraStackEventScriptItemGroup>(this, NAME_None, RF_Transactional);
				FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
					FExecutionCategoryNames::Particle, FExecutionSubcategoryNames::Event,
					GetEmitterViewModel()->GetEditorData().GetStackEditorData());
				EventHandlerGroup->Initialize(RequiredEntryData, GetEmitterViewModel()->GetSharedScriptViewModel(), ENiagaraScriptUsage::ParticleEventScript, EventScriptProperties.Script->GetUsageId());
				EventHandlerGroup->SetOnModifiedEventHandlers(UNiagaraStackEventScriptItemGroup::FOnModifiedEventHandlers::CreateUObject(this, &UNiagaraStackRoot::EmitterEventArraysChanged));
			}

			NewChildren.Add(EventHandlerGroup);
		}

		NewChildren.Add(AddEventHandlerGroup);

		NewChildren.Add(RenderGroup);
	}
}

void UNiagaraStackRoot::EmitterEventArraysChanged()
{
	RefreshChildren();
}

#undef LOCTEXT_NAMESPACE
