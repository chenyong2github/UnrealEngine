// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Class used help debugging Niagara simulations
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RHICommandList.h"

class UWorld;

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
	FNiagaraDebugHud(UWorld* World);
	~FNiagaraDebugHud();

	void GatherSystemInfo();

private:
	void DebugDrawNiagara(UCanvas* Canvas, APlayerController* PC);

private:
	TWeakObjectPtr<UWorld>			WeakWorld;

	FDelegateHandle					DebugDrawHandle;

	int32							GlobalTotalSystems = 0;
	int32							GlobalTotalScalability = 0;
	int32							GlobalTotalEmitters = 0;
	int32							GlobalTotalParticles = 0;
	TMap<FName, FSystemDebugInfo>	PerSystemDebugInfo;

	TArray<TWeakObjectPtr<class UNiagaraComponent>>	InWorldComponents;
};
