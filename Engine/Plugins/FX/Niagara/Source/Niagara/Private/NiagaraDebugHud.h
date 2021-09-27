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
#include "Particles/ParticlePerfStatsManager.h"

#if WITH_PARTICLE_PERF_STATS

class FNiagaraDebugHud;

struct FNiagaraDebugHudFrameStat
{
	double Time_GT;
	double Time_RT;
};

/** Ring buffer history of stats. */
struct FNiagaraDebugHudStatHistory
{
	TArray<double> GTFrames;
	TArray<double> RTFrames;

	int32 CurrFrame = 0;
	int32 CurrFrameRT = 0;

	void AddFrame_GT(double Time);
	void AddFrame_RT(double Time);

	void GetHistoryFrames_GT(TArray<double>& OutHistoryGT);
	void GetHistoryFrames_RT(TArray<double>& OutHistoryRT);
};

struct FNiagaraDebugHUDPerfStats
{
	FNiagaraDebugHudFrameStat Avg;
	FNiagaraDebugHudFrameStat Max;
	FNiagaraDebugHudStatHistory History;
};

/**
Listener that accumulates short runs of stats and reports then to the debug hud.
*/
class FNiagaraDebugHUDStatsListener : public FParticlePerfStatsListener_GatherAll
{
public:
	FNiagaraDebugHud& Owner;
	FNiagaraDebugHUDStatsListener(FNiagaraDebugHud& InOwner) 
	: FParticlePerfStatsListener_GatherAll(true, true, false)//TODO: Also gather component stats and display that info in world.
	, Owner(InOwner)
	{
	}

	int32 NumFrames = 0;
	int32 NumFramesRT = 0;

	FNiagaraDebugHUDPerfStats GlobalStats;

	TMap<TWeakObjectPtr<const UFXSystemAsset>, TSharedPtr<FNiagaraDebugHUDPerfStats>> SystemStats;
	FCriticalSection SystemStatsGuard;

	virtual bool Tick()override;
	virtual void TickRT()override;
	TSharedPtr<FNiagaraDebugHUDPerfStats> GetSystemStats(UNiagaraSystem* System);
	FNiagaraDebugHUDPerfStats& GetGlobalStats();

	virtual void OnAddSystem(const TWeakObjectPtr<const UFXSystemAsset>& NewSystem)override;
	virtual void OnRemoveSystem(const TWeakObjectPtr<const UFXSystemAsset>& System)override;
};

#endif

class FNiagaraDebugHud
{
	typedef TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> FGpuDataSetPtr;

	struct FSystemDebugInfo
	{
		FString		SystemName;

		#if WITH_PARTICLE_PERF_STATS
		TSharedPtr<FNiagaraDebugHUDPerfStats> PerfStats = nullptr;
		#endif
		FLinearColor UniqueColor = FLinearColor::Red;
		int32		FramesSinceVisible = 0;

		bool		bShowInWorld = false;
		bool		bPassesSystemFilter = true;
		int32		TotalSystems = 0;
		int32		TotalScalability = 0;
		int32		TotalEmitters = 0;
		int32		TotalParticles = 0;
		int64		TotalBytes = 0;

		int32		TotalCulled = 0;
		int32		TotalCulledByDistance = 0;
		int32		TotalCulledByVisibility = 0;
		int32		TotalCulledByInstanceCount = 0;
		int32		TotalCulledByBudget = 0;
		
		int32		TotalPlayerSystems = 0;

		void Reset()
		{
			bShowInWorld = false;
			bPassesSystemFilter = true;
			TotalSystems = 0;
			TotalScalability = 0;
			TotalEmitters = 0;
			TotalParticles = 0;
			TotalBytes = 0;

			TotalCulled = 0;
			TotalCulledByDistance = 0;
			TotalCulledByVisibility = 0;
			TotalCulledByInstanceCount = 0;
			TotalCulledByBudget = 0;

			TotalPlayerSystems = 0;
		}
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

	int32 GlobalTotalSystems = 0;
	int32 GlobalTotalScalability = 0;
	int32 GlobalTotalEmitters = 0;
	int32 GlobalTotalParticles = 0;
	int64 GlobalTotalBytes = 0;

	int32 GlobalTotalCulled = 0;
	int32 GlobalTotalCulledByDistance = 0;
	int32 GlobalTotalCulledByVisibility = 0;
	int32 GlobalTotalCulledByInstanceCount = 0;
	int32 GlobalTotalCulledByBudget = 0;

	int32 GlobalTotalPlayerSystems = 0;

	TMap<FName, FSystemDebugInfo>	PerSystemDebugInfo;

	TArray<TWeakObjectPtr<class UNiagaraComponent>>	InWorldComponents;

	TMap<TWeakObjectPtr<class UNiagaraComponent>, FValidationErrorInfo> ValidationErrors;

	TMap<FNiagaraSystemInstanceID, FGpuEmitterCache> GpuEmitterData;

#if WITH_PARTICLE_PERF_STATS
	TSharedPtr<FNiagaraDebugHUDStatsListener, ESPMode::ThreadSafe> StatsListener;
#endif

	float LastDrawTime = 0.0f;
	float DeltaSeconds = 0.0f;

	/** Generic messages that the debugger or other parts of Niagara can post to the HUD. */
	TMap<FName, FNiagaraDebugMessage> Messages;
};
