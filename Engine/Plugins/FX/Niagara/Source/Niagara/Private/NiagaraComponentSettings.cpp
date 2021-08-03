// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponentSettings.h"

int32 UNiagaraComponentSettings::bAllowSuppressActivation = 0;
int32 UNiagaraComponentSettings::bAllowForceAutoPooling = 0;
int32 UNiagaraComponentSettings::bUseSuppressEmitterList = 0;
int32 UNiagaraComponentSettings::bUseGpuEmitterWhitelist = 0;
int32 UNiagaraComponentSettings::bUseGpuDataInterfaceBlacklist = 0;

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

static FAutoConsoleVariableRef CVarNiagaraUseEmitterSupressList(
	TEXT("fx.Niagara.UseEmitterSuppressList"),
	UNiagaraComponentSettings::bUseSuppressEmitterList,
	TEXT("When an emitter is activated we will check the surpession list."),
	ECVF_Default
);

static FAutoConsoleVariableRef CVarNiagaraUseGpuEmitterWhitelist(
	TEXT("fx.Niagara.UseGpuEmitterWhitelist"),
	UNiagaraComponentSettings::bUseGpuEmitterWhitelist,
	TEXT("When enabled only whitelisted GPU emitters are allowed to run."),
	ECVF_Default
);

static FAutoConsoleVariableRef CVarNiagaraUseGpuDataInterfaceBlacklist(
	TEXT("fx.Niagara.UseGpuDataInterfaceBlacklist"),
	UNiagaraComponentSettings::bUseGpuDataInterfaceBlacklist,
	TEXT("When enabled GPU emitters will be disabled if they use the blacklisted data interface."),
	ECVF_Default
);

UNiagaraComponentSettings::UNiagaraComponentSettings(FObjectInitializer const& ObjectInitializer)
{
}
