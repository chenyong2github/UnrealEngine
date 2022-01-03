// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraStackRoot.generated.h"

class FNiagaraEmitterViewModel;
class UNiagaraStackSystemSettingsGroup;
class UNiagaraStackEmitterSettingsGroup;
class UNiagaraStackScriptItemGroup;
class UNiagaraStackEventHandlerGroup;
class UNiagaraStackSimulationStagesGroup;
class UNiagaraStackRenderItemGroup;
class UNiagaraStackEmitterSummaryGroup;
class UNiagaraStackSummaryViewCollapseButton;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackRoot : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	UNiagaraStackRoot();
	
	void Initialize(FRequiredEntryData InRequiredEntryData, bool bInIncludeSystemInformation, bool bInIncludeEmitterInformation);
	void FinalizeInternal() override;

	virtual bool GetCanExpand() const override;
	virtual bool GetShouldShowInStack() const override;
	UNiagaraStackRenderItemGroup* GetRenderGroup() const
	{
		return RenderGroup;
	}
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void EmitterArraysChanged();
	void OnSummaryViewStateChanged();

private:
	UPROPERTY()
	TObjectPtr<UNiagaraStackSystemSettingsGroup> SystemSettingsGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackScriptItemGroup> SystemSpawnGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackScriptItemGroup> SystemUpdateGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackEmitterSettingsGroup> EmitterSettingsGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackEmitterSummaryGroup> EmitterSummaryGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackScriptItemGroup> EmitterSpawnGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackScriptItemGroup> EmitterUpdateGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackScriptItemGroup> ParticleSpawnGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackScriptItemGroup> ParticleUpdateGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackEventHandlerGroup> AddEventHandlerGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackSimulationStagesGroup> AddSimulationStageGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackRenderItemGroup> RenderGroup;

	UPROPERTY()
	TObjectPtr<UNiagaraStackSummaryViewCollapseButton> SummaryCollapseButton;

	bool bIncludeSystemInformation;
	bool bIncludeEmitterInformation;
};
