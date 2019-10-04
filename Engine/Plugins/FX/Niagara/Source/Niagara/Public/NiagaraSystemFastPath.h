// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystemFastPath.generated.h"

class UNiagaraEmitter;

UENUM()
enum class ENiagaraFastPathMode
{
	ScriptVMOnly,
	FastPathOnly,
	ScrptVMAndFastPath
};

UENUM()
enum class ENiagaraFastPathInputType
{
	Local,
	RangedSpawn,
	RangedUpdate,
	User
};

USTRUCT()
struct FNiagaraFastPathFloatInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Input")
	ENiagaraFastPathInputType Type;

	UPROPERTY(EditAnywhere, Category = "Input")
	float Local;

	UPROPERTY(EditAnywhere, Category = "Input")
	float RangeMin;

	UPROPERTY(EditAnywhere, Category = "Input")
	float RangeMax;

	UPROPERTY(EditAnywhere, Category = "Input")
	FName UserParameterName;

	FNiagaraFastPathFloatInput()
		: Type(ENiagaraFastPathInputType::Local)
		, Local(0.0f)
		, RangeMin(0.0f)
		, RangeMax(0.0f)
	{
	}
};

USTRUCT()
struct FNiagaraFastPathIntInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Input")
	ENiagaraFastPathInputType Type;

	UPROPERTY(EditAnywhere, Category = "Input")
	int32 Local;

	UPROPERTY(EditAnywhere, Category = "Input")
	int32 RangeMin;

	UPROPERTY(EditAnywhere, Category = "Input")
	int32 RangeMax;

	UPROPERTY(EditAnywhere, Category = "Input")
	FName UserParameterName;

	FNiagaraFastPathIntInput()
		: Type(ENiagaraFastPathInputType::Local)
		, Local(0)
		, RangeMin(0)
		, RangeMax(0)
	{
	}
};

USTRUCT()
struct FNiagaraFastPathAttributeNames
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FName> System;

	UPROPERTY()
	TArray<FName> SystemFullNames;

	UPROPERTY()
	TArray<FName> Emitter;

	UPROPERTY()
	TArray<FName> EmitterFullNames;
};

template<typename T>
struct TNiagaraFastPathUserParameterInputBinding
{
	T* InputValue;
	FNiagaraParameterDirectBinding<T> ParameterBinding;

	void Tick()
	{
		*InputValue = ParameterBinding.GetValue();
	}
};

template<typename T>
struct TNiagaraFastPathRangedInputBinding
{
	const T Min;
	const T Max;
	T* const InputValue;

	TNiagaraFastPathRangedInputBinding(T SourceMin, T SourceMax, T* TargetInputValue)
		: Min(SourceMin)
		, Max(SourceMax)
		, InputValue(TargetInputValue)
	{
	}

	void Tick()
	{
		*InputValue = FMath::RandRange(Min, Max);
	}
};

template<typename T>
struct TNiagaraFastPathAttributeBinding
{
	T* ParameterValue;
	FNiagaraParameterDirectBinding<T> ParameterBinding;

	void Tick()
	{
		ParameterBinding.SetValue(*ParameterValue);
	}
};

USTRUCT()
struct FNiagaraFastPath_Module_SystemScalability
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "System Scalability")
	bool bUseSystemScalability;

	UPROPERTY(EditAnywhere, Category = "System Scalability")
	ENiagaraExecutionState CulledState;

	UPROPERTY(EditAnywhere, Category = "System Scalability")
	bool bEnableVisibilityCulling;

	UPROPERTY(EditAnywhere, Category = "System Scalability")
	float VisibilityCullDelay;

	FNiagaraFastPath_Module_SystemScalability()
		: bUseSystemScalability(true)
		, CulledState(ENiagaraExecutionState::Inactive)
		, bEnableVisibilityCulling(true)
		, VisibilityCullDelay(1.0f)
	{
	}
};

USTRUCT()
struct FNiagaraFastPath_Module_SystemLifeCycle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "System Life Cycle")
	bool bCompleteOnInactive;

	FNiagaraFastPath_Module_SystemLifeCycle()
	: bCompleteOnInactive(false)
	{
	}
};

namespace FNiagaraSystemFastPath
{
	struct FParamMap0_Engine_Owner
	{
		float TimeSinceRendered;
		ENiagaraExecutionState ExecutionState;
	};

	struct FParamMap0_Engine
	{
		FParamMap0_Engine_Owner Owner;
	};

	struct FParamMap0_System
	{
		ENiagaraExecutionState ExecutionState;
		ENiagaraExecutionStateSource ExecutionStateSource;
	};

	struct FParamMap0_Scalability
	{
		ENiagaraExecutionState ExecutionState;
	};

	struct FParamMap0
	{
		FParamMap0_Engine Engine;
		FParamMap0_System System;
		FParamMap0_Scalability Scalability;
	};

	struct FParameterNames
	{
		static FName ExecutionState;
		static FName ExecutionStateSource;
	};

	void SetSpawnMapDefaults(FParamMap0& Map);
	void SetUpdateMapDefaults(FParamMap0& Map);

	void Module_SystemScalability(const FNiagaraFastPath_Module_SystemScalability& Context_Map_SystemScalability, FParamMap0& Context_Map);

	void Function_SystemChangeState(ENiagaraExecutionState In_NewState, bool In_Condition, ENiagaraExecutionStateSource In_NewStateSource, FParamMap0& Context_Map);

	void Module_SystemLifeCycle(const FNiagaraFastPath_Module_SystemLifeCycle& Context_Map_SystemLifeCycle, FParamMap0& Context_Map);
};

USTRUCT()
struct FNiagaraFastPath_Module_EmitterScalability
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	bool bUseEmitterScalability;

	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	bool bUseMaxDistance;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	ENiagaraExecutionState MaxCulledState;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	float MaxDistance;

	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	bool bUseMinDistance;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	ENiagaraExecutionState MinCulledState;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	float MinDistance;

	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	bool bApplySpawnCountScale;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	float SpawnCountScale;

	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	bool bApplySpawnCountScaleByDistanceFraction;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	float ScaleForMinFraction;
	UPROPERTY(EditAnywhere, Category = "Emitter Scalability")
	float ScaleForMaxFraction;

	FNiagaraFastPath_Module_EmitterScalability()
		: bUseEmitterScalability(true)
		, bUseMaxDistance(false)
		, MaxCulledState(ENiagaraExecutionState::Inactive)
		, MaxDistance(5000.f)
		, bUseMinDistance(false)
		, MinCulledState(ENiagaraExecutionState::Inactive)
		, MinDistance(0.0f)
		, bApplySpawnCountScale(false)
		, SpawnCountScale(1.0f)
		, bApplySpawnCountScaleByDistanceFraction(false)
		, ScaleForMinFraction(1.0f)
		, ScaleForMaxFraction(1.0f)
	{
	}
};

USTRUCT()
struct FNiagaraFastPath_Module_EmitterLifeCycle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	bool bAutoComplete;
	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	bool bCompleteOnInactive;
	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	bool bDelayFirstLoopOnly;
	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	bool bDurationRecalcEachLoop;

	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	int32 MaxLoopCount;

	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	float NextLoopDelay;

	UPROPERTY(EditAnywhere, Category = "Emitter Life Cycle")
	float NextLoopDuration;

	FNiagaraFastPath_Module_EmitterLifeCycle()
		: bAutoComplete(true)
		, bCompleteOnInactive(false)
		, bDelayFirstLoopOnly(false)
		, bDurationRecalcEachLoop(false)
		, MaxLoopCount(0)
		, NextLoopDelay(0.0f)
		, NextLoopDuration(5.0f)
	{
	}
};

USTRUCT()
struct FNiagaraFastPath_Module_SpawnRate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Spawn Rate")
	FNiagaraFastPathFloatInput SpawnRate;

	UPROPERTY(EditAnywhere, Category = "Spawn Rate")
	int32 SpawnGroup;

	FNiagaraFastPath_Module_SpawnRate()
		: SpawnGroup(0)
	{
	}
};

USTRUCT()
struct FNiagaraFastPath_Module_SpawnPerUnit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Spawn Per Unit")
	FNiagaraFastPathFloatInput SpawnPerUnit;

	UPROPERTY(EditAnywhere, Category = "Spawn Per Unit")
	bool bUseMovementTolerance;
	UPROPERTY(EditAnywhere, Category = "Spawn Per Unit")
	float MovementTolerance;

	UPROPERTY(EditAnywhere, Category = "Spawn Per Unit")
	bool bUseMaxMovementThreshold;
	UPROPERTY(EditAnywhere, Category = "Spawn Per Unit")
	float MaxMovementThreshold;

	UPROPERTY(EditAnywhere, Category = "Spawn Per Unit")
	int32 SpawnGroup;

	FNiagaraFastPath_Module_SpawnPerUnit()
		: bUseMovementTolerance(false)
		, MovementTolerance(0.1f)
		, bUseMaxMovementThreshold(true)
		, MaxMovementThreshold(5000.0f)
		, SpawnGroup(0)
	{
	}
};

USTRUCT()
struct FNiagaraFastPath_Module_SpawnBurstInstantaneous
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Spawn Burst Instantaneous")
	FNiagaraFastPathIntInput SpawnCount;

	UPROPERTY(EditAnywhere, Category = "Spawn Burst Instantaneous")
	FNiagaraFastPathFloatInput SpawnTime;

	UPROPERTY(EditAnywhere, Category = "Spawn Burst Instantaneous")
	int32 SpawnGroup;

	FNiagaraFastPath_Module_SpawnBurstInstantaneous()
		: SpawnGroup(0)
	{
	}
};

namespace FNiagaraEmitterFastPath
{
	struct FParamMap0_Engine_Emitter
	{
		int NumParticles;
	};

	struct FParamMap0_Engine_Owner
	{
		float LODDistance;
		FVector Velocity;
	};

	struct FParamMap0_Engine
	{
		float DeltaTime;
		float GlobalSpawnCountScale;
		FParamMap0_Engine_Emitter Emitter;
		FParamMap0_Engine_Owner Owner;
	};

	struct FParamMap0_Scalability_Emitter
	{
		ENiagaraExecutionState ExecutionState;
		float SpawnCountScale;
	};

	struct FParamMap0_Scalability
	{
		FParamMap0_Scalability_Emitter Emitter;
	};

	struct FParamMap0_Spawning
	{
		bool bCanEverSpawn;
	};

	struct FParamMap0_SpawnRate
	{
		int32 SpawnGroup;
		float SpawnRate;

		void Init(
			const UNiagaraEmitter* Emitter,
			int32 ModuleIndex,
			FNiagaraParameterStore& InstanceParameters,
			TArray<TNiagaraFastPathUserParameterInputBinding<int32>>& IntUserParameterInputBindings,
			TArray<TNiagaraFastPathUserParameterInputBinding<float>>& FloatUserParameterInputBindings,
			TArray<TNiagaraFastPathRangedInputBinding<int32>>& IntUpdateRangedInputBindings,
			TArray<TNiagaraFastPathRangedInputBinding<float>>& FloatUpdateRangedInputBindings);
	};

	struct FParamMap0_SpawnPerUnit
	{
		float SpawnPerUnit;
		bool bUseMovementTolerance;
		float MovementTolerance;
		bool bUseMaxMovementThreshold;
		float MaxMovementThreshold;
		int32 SpawnGroup;

		void Init(
			const UNiagaraEmitter* Emitter,
			int32 ModuleIndex,
			FNiagaraParameterStore& InstanceParameters,
			TArray<TNiagaraFastPathUserParameterInputBinding<int32>>& IntUserParameterInputBindings,
			TArray<TNiagaraFastPathUserParameterInputBinding<float>>& FloatUserParameterInputBindings,
			TArray<TNiagaraFastPathRangedInputBinding<int32>>& IntUpdateRangedInputBindings,
			TArray<TNiagaraFastPathRangedInputBinding<float>>& FloatUpdateRangedInputBindings);
	};

	struct FParamMap0_SpawnBurst_Instantaneous
	{
		int32 SpawnCount;
		int32 SpawnGroup;
		float SpawnTime;

		void Init(
			const UNiagaraEmitter* Emitter,
			int32 ModuleIndex,
			FNiagaraParameterStore& InstanceParameters,
			TArray<TNiagaraFastPathUserParameterInputBinding<int32>>& IntUserParameterInputBindings,
			TArray<TNiagaraFastPathUserParameterInputBinding<float>>& FloatUserParameterInputBindings,
			TArray<TNiagaraFastPathRangedInputBinding<int32>>& IntUpdateRangedInputBindings,
			TArray<TNiagaraFastPathRangedInputBinding<float>>& FloatUpdateRangedInputBindings);
	};

	struct FParamMap0_Emitter_Scalability
	{
		float DistanceFraction;
	};

	struct FParamMap0_Emitter_SpawnRate
	{
		FNiagaraSpawnInfo SpawnOutputInfo;
		float SpawnRemainder;

		FParamMap0_Emitter_SpawnRate()
			: SpawnRemainder(0.0f)
		{
		}
	};

	struct FParamMap0_Emitter_SpawnPerUnit
	{
		FNiagaraSpawnInfo SpawnOutputInfo;
		float SpawnRemainder;

		FParamMap0_Emitter_SpawnPerUnit()
			: SpawnRemainder(0.0f)
		{
		}
	};

	struct FParamMap0_Emitter_SpawnBurst_Instantaneous
	{
		FNiagaraSpawnInfo SpawnBurst;
	};

	struct FParamMap0_Emitter
	{
		float Age;
		float CurrentLoopDelay;
		float CurrentLoopDuration;
		ENiagaraExecutionState ExecutionState;
		ENiagaraExecutionStateSource ExecutionStateSource;
		int32 LoopCount;
		float LoopedAge;
		float NormalizedLoopAge;
		float DistanceTraveled;
		float SpawnCountScale;
		FParamMap0_Emitter_Scalability Scalability;
		TArray<FParamMap0_Emitter_SpawnRate> SpawnRate;
		TArray<FParamMap0_Emitter_SpawnPerUnit> SpawnPerUnit;
		TArray<FParamMap0_Emitter_SpawnBurst_Instantaneous> SpawnBurst_Instantaneous;
	};

	struct FParamMap0
	{
		TArray<FParamMap0_SpawnRate> SpawnRate;
		TArray<FParamMap0_SpawnPerUnit> SpawnPerUnit;
		TArray<FParamMap0_SpawnBurst_Instantaneous> SpawnBurst_Instantaneous;
		FParamMap0_Engine Engine;
		FParamMap0_Emitter Emitter;
		FParamMap0_Scalability Scalability;
		FParamMap0_Spawning Spawning;
		const FNiagaraSystemFastPath::FParamMap0_System* System;
	};

	struct FAttributeNames
	{
		static FName Age;
		static FName CurrentLoopDelay;
		static FName CurrentLoopDuration;
		static FName ExecutionState;
		static FName ExecutionStateSource;
		static FName LoopCount;
		static FName LoopedAge;
		static FName NormalizedLoopAge;
		static FName DistanceTravelled;
		static FName Scalability_DistanceFraction;
	};

	void InitFastPathAttributeBindings(
		const FNiagaraFastPathAttributeNames& SourceAttributeNames,
		FNiagaraParameterStore& TargetParameterStore,
		FNiagaraSystemFastPath::FParamMap0& SystemMap,
		FNiagaraEmitterFastPath::FParamMap0& EmitterMap,
		TArray<TNiagaraFastPathAttributeBinding<int32>>& FastPathIntAttributeBindings,
		TArray<TNiagaraFastPathAttributeBinding<float>>& FastPathFloatAttributeBindings);

	template<typename TBindingType, typename TVariableType>
	static void AddBinding(FName ParameterName, TVariableType ParameterType, TBindingType* SourceValuePtr, FNiagaraParameterStore& TargetParameterStore, TArray<TNiagaraFastPathAttributeBinding<TBindingType>>& TargetBindings)
	{
		TNiagaraFastPathAttributeBinding<TBindingType> Binding;
		FNiagaraVariable ParameterVariable = FNiagaraVariable(ParameterType, ParameterName);
		Binding.ParameterBinding.Init(TargetParameterStore, ParameterVariable);
		if (Binding.ParameterBinding.ValuePtr != nullptr)
		{
			Binding.ParameterValue = SourceValuePtr;
			TargetBindings.Add(Binding);
		}
	}

	void SetSpawnMapDefaults(const UNiagaraEmitter& Emitter, FParamMap0& Map);
	void SetUpdateMapDefaults(FParamMap0& Map);

	void Function_SampleCurve_SpawnCountScaleByDistanceFraction(const FNiagaraFastPath_Module_EmitterScalability& Context_Map_EmitterScalability, float InFraction, float& OutScale);
	void Module_EmitterScalability(const FNiagaraFastPath_Module_EmitterScalability& Context_Map_EmitterScalability, FParamMap0& Context_Map);

	void Function_EmitterLifeCycle_EmitterChangeState(ENiagaraExecutionState In_NewState, bool In_Condition, ENiagaraExecutionStateSource In_NewStateSource, FParamMap0& Context_Map);
	void Module_EmitterLifeCycle(const FNiagaraFastPath_Module_EmitterLifeCycle& Context_Map_EmitterLifeCycle, FParamMap0& Context_Map);

	void Module_SpawnRate(FParamMap0& Context_Map);

	void Module_SpawnPerUnit(FParamMap0& ContextMap);

	void Module_SpawnBurstInstantaneous(FParamMap0& ContextMap);
}