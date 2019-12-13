// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraStackRoot.generated.h"

class FNiagaraEmitterViewModel;
class UNiagaraStackSystemSettingsGroup;
class UNiagaraStackEmitterSettingsGroup;
class UNiagaraStackScriptItemGroup;
class UNiagaraStackEventHandlerGroup;
class UNiagaraStackRenderItemGroup;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackRoot : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	UNiagaraStackRoot();
	
	void Initialize(FRequiredEntryData InRequiredEntryData, bool bInIncludeSystemInformation, bool bInIncludeEmitterInformation);

	virtual bool GetCanExpand() const override;
	virtual bool GetShouldShowInStack() const override;
	UNiagaraStackRenderItemGroup* GetRenderGroup() const
	{
		return RenderGroup;
	}
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

private:
	void EmitterEventArraysChanged();

private:
	UPROPERTY()
	UNiagaraStackSystemSettingsGroup* SystemSettingsGroup;

	UPROPERTY()
	UNiagaraStackScriptItemGroup* SystemSpawnGroup;

	UPROPERTY()
	UNiagaraStackScriptItemGroup* SystemUpdateGroup;

	UPROPERTY()
	UNiagaraStackEmitterSettingsGroup* EmitterSettingsGroup;

	UPROPERTY()
	UNiagaraStackScriptItemGroup* EmitterSpawnGroup;

	UPROPERTY()
	UNiagaraStackScriptItemGroup* EmitterUpdateGroup;

	UPROPERTY()
	UNiagaraStackScriptItemGroup* ParticleSpawnGroup;

	UPROPERTY()
	UNiagaraStackScriptItemGroup* ParticleUpdateGroup;

	UPROPERTY()
	UNiagaraStackEventHandlerGroup* AddEventHandlerGroup;

	UPROPERTY()
	UNiagaraStackRenderItemGroup* RenderGroup;

	bool bIncludeSystemInformation;
	bool bIncludeEmitterInformation;
};
