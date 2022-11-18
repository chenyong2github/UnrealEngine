// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UNiagaraSystem;
class FNiagaraEmitterInstance;

namespace FNiagaraComponentSettings
{
	extern NIAGARA_API bool IsSystemAllowedToRun(const UNiagaraSystem* System);
	extern NIAGARA_API bool IsEmitterAllowedToRun(const FNiagaraEmitterInstance* EmitterInstance);
};
