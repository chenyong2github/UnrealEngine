// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Class used help debugging Niagara simulations
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RHICommandList.h"
#include "NiagaraDebugHud.generated.h"

UENUM()
enum class ENiagaraDebugHudSystemVerbosity
{
	/** Display no text with the system. */
	None = 0,
	/** Display minimal information with the system, i.e. component / system name. */
	Minimal = 1,
	/** Display basic information with the system, i.e. Minimal + counts. */
	Basic = 2,
	/** Display basic information with the system, i.e. Basic + per emitter information. */
	Verbose = 3,
};

class FNiagaraDebugHud
{
	struct FSystemDebugInfo
	{
		FString		SystemName;
		bool		bShowInWorld = false;
		int32		TotalSystems = 0;
		int32		TotalScalability = 0;
		int32		TotalEmitters = 0;
		int32		TotalParticles = 0;
	};

public:
	FNiagaraDebugHud(class UWorld* World);
	~FNiagaraDebugHud();

	void GatherSystemInfo();

private:
	void DebugDrawNiagara(class UCanvas* Canvas, class APlayerController* PC);

private:
	TWeakObjectPtr<class UWorld>	WeakWorld;

	FDelegateHandle					DebugDrawHandle;

	int32							GlobalTotalSystems = 0;
	int32							GlobalTotalScalability = 0;
	int32							GlobalTotalEmitters = 0;
	int32							GlobalTotalParticles = 0;
	TMap<FName, FSystemDebugInfo>	PerSystemDebugInfo;

	TArray<TWeakObjectPtr<class UNiagaraComponent>>	InWorldComponents;
};
