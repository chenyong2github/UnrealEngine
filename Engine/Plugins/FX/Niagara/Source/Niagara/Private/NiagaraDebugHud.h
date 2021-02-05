// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Class used help debugging Niagara simulations
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RHICommandList.h"
#include "NiagaraDebuggerCommon.h"

class FNiagaraDebugHud
{
	typedef TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> FGpuDataSetPtr;

	struct FSystemDebugInfo
	{
		FString		SystemName;
		bool		bShowInWorld = false;
		int32		TotalSystems = 0;
		int32		TotalScalability = 0;
		int32		TotalEmitters = 0;
		int32		TotalParticles = 0;
	};

	struct FGpuEmitterCache
	{
		uint64					LastAccessedCycles;
		TArray<FGpuDataSetPtr>	CurrentEmitterData;
		TArray<FGpuDataSetPtr>	PendingEmitterData;
	};

public:
	FNiagaraDebugHud(class UWorld* World);
	~FNiagaraDebugHud();

	void GatherSystemInfo();

	void UpdateSettings(const FNiagaraDebugHUDSettingsData& NewSettings);

private:
	class FNiagaraDataSet* GetParticleDataSet(class FNiagaraSystemInstance* SystemInstance, class FNiagaraEmitterInstance* EmitterInstance, int32 iEmitter);

	static void DebugDrawCallback(class UCanvas* Canvas, class APlayerController* PC);

	void Draw(class FNiagaraWorldManager* WorldManager, class UCanvas* Canvas, class APlayerController* PC);
	void DrawOverview(class FNiagaraWorldManager* WorldManager, class FCanvas* DrawCanvas, class UFont* Font);
	void DrawComponents(class FNiagaraWorldManager* WorldManager, class UCanvas* Canvas, class UFont* Font);

private:
	TWeakObjectPtr<class UWorld>	WeakWorld;

	int32							GlobalTotalSystems = 0;
	int32							GlobalTotalScalability = 0;
	int32							GlobalTotalEmitters = 0;
	int32							GlobalTotalParticles = 0;
	TMap<FName, FSystemDebugInfo>	PerSystemDebugInfo;

	TArray<TWeakObjectPtr<class UNiagaraComponent>>	InWorldComponents;

	TMap<FNiagaraSystemInstanceID, FGpuEmitterCache> GpuEmitterData;
};
