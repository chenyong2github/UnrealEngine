// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponentSettings.h"

int32 UNiagaraComponentSettings::bAllowSuppressActivation = 0;
int32 UNiagaraComponentSettings::bAllowForceAutoPooling = 0;

static FAutoConsoleVariableRef CVarNiagaraUseSupressActivateList(
	TEXT("fx.Niagara.UseSupressActivateList"),
	UNiagaraComponentSettings::bAllowSuppressActivation,
	TEXT("When a component is activated we will check the surpession list."),
	ECVF_Default
);

static FAutoConsoleVariableRef CVarNiagaraForceAutoPooling(
	TEXT("fx.Niagara.ForceAutoPooling"),
	UNiagaraComponentSettings::bAllowForceAutoPooling,
	TEXT("Forces auto pooling to be enabled on spawned components."),
	ECVF_Default
);

UNiagaraComponentSettings::UNiagaraComponentSettings(FObjectInitializer const& ObjectInitializer)
{
}
