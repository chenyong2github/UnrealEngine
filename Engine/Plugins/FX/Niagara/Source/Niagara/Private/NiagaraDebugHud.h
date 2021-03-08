// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Class used help debugging Niagara simulations
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
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
		int64		TotalBytes = 0;
	};

	struct FGpuEmitterCache
	{
		uint64					LastAccessedCycles;
		TArray<FGpuDataSetPtr>	CurrentEmitterData;
		TArray<FGpuDataSetPtr>	PendingEmitterData;
	};

	struct FValidationErrorInfo
	{
		double						LastWarningTime = 0.0;
		FString						DisplayName;
		TArray<FName>				SystemVariablesWithErrors;
		TMap<FName, TArray<FName>>	ParticleVariablesWithErrors;
	};

public:
	FNiagaraDebugHud(class UWorld* World);
	~FNiagaraDebugHud();

	void GatherSystemInfo();

	void UpdateSettings(const FNiagaraDebugHUDSettingsData& NewSettings);

	void AddMessage(FName Key, const FNiagaraDebugMessage& Message);
	void RemoveMessage(FName Key);

private:
	class FNiagaraDataSet* GetParticleDataSet(class FNiagaraSystemInstance* SystemInstance, class FNiagaraEmitterInstance* EmitterInstance, int32 iEmitter);
	FValidationErrorInfo& GetValidationErrorInfo(class UNiagaraComponent* NiagaraComponent);

	static void DebugDrawCallback(class UCanvas* Canvas, class APlayerController* PC);

	void Draw(class FNiagaraWorldManager* WorldManager, class UCanvas* Canvas, class APlayerController* PC);
	void DrawOverview(class FNiagaraWorldManager* WorldManager, class FCanvas* DrawCanvas);
	void DrawValidation(class FNiagaraWorldManager* WorldManager, class FCanvas* DrawCanvas, FVector2D& TextLocation);
	void DrawComponents(class FNiagaraWorldManager* WorldManager, class UCanvas* Canvas);
	void DrawMessages(class FNiagaraWorldManager* WorldManager, class FCanvas* DrawCanvas, FVector2D& TextLocation);

private:
	TWeakObjectPtr<class UWorld>	WeakWorld;

	int32							GlobalTotalSystems = 0;
	int32							GlobalTotalScalability = 0;
	int32							GlobalTotalEmitters = 0;
	int32							GlobalTotalParticles = 0;
	int64							GlobalTotalBytes = 0;
	TMap<FName, FSystemDebugInfo>	PerSystemDebugInfo;

	TArray<TWeakObjectPtr<class UNiagaraComponent>>	InWorldComponents;

	TMap<TWeakObjectPtr<class UNiagaraComponent>, FValidationErrorInfo> ValidationErrors;

	TMap<FNiagaraSystemInstanceID, FGpuEmitterCache> GpuEmitterData;

	float LastDrawTime = 0.0f;
	float DeltaSeconds = 0.0f;

	/** Generic messages that the debugger or other parts of Niagara can post to the HUD. */
	TMap<FName, FNiagaraDebugMessage> Messages;
};
