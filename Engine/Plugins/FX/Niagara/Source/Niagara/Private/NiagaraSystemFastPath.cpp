// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemFastPath.h"
#include "NiagaraEmitter.h"

namespace FNiagaraSystemFastPath
{
	FORCEINLINE float SafeReciprocal(float v)
	{
		return FMath::Abs(v) > SMALL_NUMBER ? 1.0f / v : 0.0f;
	}
}

FName FNiagaraSystemFastPath::FParameterNames::ExecutionState = "ExecutionState";
FName FNiagaraSystemFastPath::FParameterNames::ExecutionStateSource = "ExecutionStateSource";

void FNiagaraSystemFastPath::SetSpawnMapDefaults(FParamMap0& Map)
{
	Map.System.ExecutionState = (ENiagaraExecutionState)0;
	Map.System.ExecutionStateSource = (ENiagaraExecutionStateSource)0;
}

void FNiagaraSystemFastPath::SetUpdateMapDefaults(FParamMap0& Map)
{
	Map.Scalability.ExecutionState = (ENiagaraExecutionState)0;
}

void FNiagaraSystemFastPath::Module_SystemScalability(const FNiagaraFastPath_Module_SystemScalability& Context_Map_SystemScalability, FParamMap0& Context_Map)
{
// 	bool Result9 = Context_Map.Engine.Owner.TimeSinceRendered > Context_Map_SystemScalability.VisibilityCullDelay;
// 	bool Result10 = Context_Map_SystemScalability.bEnableVisibilityCulling && Result9;
// 	ENiagaraExecutionState ENiagaraExecutionState_IfResult3;
// 	if (Result10)
// 	{
// 		ENiagaraExecutionState_IfResult3 = Context_Map_SystemScalability.CulledState;
// 	}
// 	else
// 	{
// 		ENiagaraExecutionState_IfResult3 = Context_Map.Scalability.ExecutionState;
// 	}
// 	Context_Map.Scalability.ExecutionState = ENiagaraExecutionState_IfResult3;
}

void FNiagaraSystemFastPath::Function_SystemChangeState(ENiagaraExecutionState In_NewState, bool In_Condition, ENiagaraExecutionStateSource In_NewStateSource, FParamMap0& Context_Map)
{
	bool Result4 = Context_Map.System.ExecutionStateSource <= In_NewStateSource;
	bool Result5 = In_Condition && Result4;
	ENiagaraExecutionState ENiagaraExecutionState_IfResult1;
	ENiagaraExecutionStateSource ENiagaraExecutionStateSource_IfResult1;
	if (Result5)
	{
		ENiagaraExecutionState_IfResult1 = In_NewState;
		ENiagaraExecutionStateSource_IfResult1 = In_NewStateSource;
	}
	else
	{
		ENiagaraExecutionState_IfResult1 = Context_Map.System.ExecutionState;
		ENiagaraExecutionStateSource_IfResult1 = Context_Map.System.ExecutionStateSource;
	}
	Context_Map.System.ExecutionState = ENiagaraExecutionState_IfResult1;
	Context_Map.System.ExecutionStateSource = ENiagaraExecutionStateSource_IfResult1;
}

void FNiagaraSystemFastPath::Module_SystemLifeCycle(const FNiagaraFastPath_Module_SystemLifeCycle& Context_Map_SystemLifeCycle, FParamMap0& Context_Map)
{
	bool Constant2 = true;
	ENiagaraExecutionStateSource Constant3 = ENiagaraExecutionStateSource::Scalability;
	Function_SystemChangeState(Context_Map.Scalability.ExecutionState, Constant2, Constant3, Context_Map);
	ENiagaraExecutionState Constant4 = ENiagaraExecutionState::Complete;
	ENiagaraExecutionState Constant5 = ENiagaraExecutionState::Active;
	bool Result2 = Context_Map.System.ExecutionState != Constant5;
	bool Result3 = Context_Map_SystemLifeCycle.bCompleteOnInactive && Result2;
	ENiagaraExecutionStateSource Constant6 = ENiagaraExecutionStateSource::InternalCompletion;
	Function_SystemChangeState(Constant4, Result3, Constant6, Context_Map);
	ENiagaraExecutionState Constant7 = ENiagaraExecutionState::Active;
	bool Result6 = Context_Map.Engine.Owner.ExecutionState != Constant7;
	ENiagaraExecutionStateSource Constant8 = ENiagaraExecutionStateSource::Owner;
	Function_SystemChangeState(Context_Map.Engine.Owner.ExecutionState, Result6, Constant8, Context_Map);
}

FName FNiagaraEmitterFastPath::FAttributeNames::Age = "Age";
FName FNiagaraEmitterFastPath::FAttributeNames::CurrentLoopDelay = "CurrentLoopDelay";
FName FNiagaraEmitterFastPath::FAttributeNames::CurrentLoopDuration = "CurrentLoopDuration";
FName FNiagaraEmitterFastPath::FAttributeNames::ExecutionState = "ExecutionState";
FName FNiagaraEmitterFastPath::FAttributeNames::ExecutionStateSource = "ExecutionStateSource";
FName FNiagaraEmitterFastPath::FAttributeNames::LoopCount = "LoopCount";
FName FNiagaraEmitterFastPath::FAttributeNames::LoopedAge = "LoopedAge";
FName FNiagaraEmitterFastPath::FAttributeNames::NormalizedLoopAge = "NormalizedLoopAge";
FName FNiagaraEmitterFastPath::FAttributeNames::DistanceTravelled = "DistanceTraveled";
FName FNiagaraEmitterFastPath::FAttributeNames::Scalability_DistanceFraction = "Scalability.DistanceFraction";

template<typename TSourceInput, typename TTargetInput>
void InitInput(
	const TSourceInput& SourceInput,
	TTargetInput& TargetInput,
	FNiagaraTypeDefinition InputType,
	FNiagaraParameterStore& InstanceParameters,
	TArray<TNiagaraFastPathUserParameterInputBinding<TTargetInput>>& UserParameterInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<TTargetInput>>& UpdateRangedInputBindings)
{
	switch (SourceInput.Type)
	{
	case ENiagaraFastPathInputType::Local:
		TargetInput = SourceInput.Local;
		break;
	case ENiagaraFastPathInputType::RangedSpawn:
		TargetInput = FMath::RandRange(SourceInput.RangeMin, SourceInput.RangeMax);
		break;
	case ENiagaraFastPathInputType::RangedUpdate:
		UpdateRangedInputBindings.Add(TNiagaraFastPathRangedInputBinding<TTargetInput>(SourceInput.RangeMin, SourceInput.RangeMax, &TargetInput));
		break;
	case ENiagaraFastPathInputType::User:
		FNiagaraVariable UserParameterVariable = FNiagaraVariable(InputType, SourceInput.UserParameterName);
		TNiagaraFastPathUserParameterInputBinding<TTargetInput> UserParameterBinding;
		UserParameterBinding.ParameterBinding.Init(InstanceParameters, UserParameterVariable);
		if (UserParameterBinding.ParameterBinding.ValuePtr != nullptr)
		{
			UserParameterBinding.InputValue = &TargetInput;
			UserParameterInputBindings.Add(UserParameterBinding);
		}
		else
		{
			TargetInput = SourceInput.Local;
		}
		break;
	}
}

void FNiagaraEmitterFastPath::FParamMap0_SpawnRate::Init(
	const UNiagaraEmitter* Emitter,
	int32 ModuleIndex,
	FNiagaraParameterStore& InstanceParameters,
	TArray<TNiagaraFastPathUserParameterInputBinding<int32>>& IntUserParameterInputBindings,
	TArray<TNiagaraFastPathUserParameterInputBinding<float>>& FloatUserParameterInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<int32>>& IntUpdateRangedInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<float>>& FloatUpdateRangedInputBindings)
{
	const FNiagaraFastPath_Module_SpawnRate& SpawnRateInputs = Emitter->SpawnRate[ModuleIndex];
	InitInput(SpawnRateInputs.SpawnRate, SpawnRate, FNiagaraTypeDefinition::GetFloatDef(), InstanceParameters, FloatUserParameterInputBindings, FloatUpdateRangedInputBindings);
	SpawnGroup = SpawnRateInputs.SpawnGroup;
}

void FNiagaraEmitterFastPath::FParamMap0_SpawnPerUnit::Init(
	const UNiagaraEmitter* Emitter,
	int32 ModuleIndex,
	FNiagaraParameterStore& InstanceParameters,
	TArray<TNiagaraFastPathUserParameterInputBinding<int32>>& IntUserParameterInputBindings,
	TArray<TNiagaraFastPathUserParameterInputBinding<float>>& FloatUserParameterInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<int32>>& IntUpdateRangedInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<float>>& FloatUpdateRangedInputBindings)
{
	const FNiagaraFastPath_Module_SpawnPerUnit& SpawnPerUnitInputs = Emitter->SpawnPerUnit[ModuleIndex];
	InitInput(SpawnPerUnitInputs.SpawnPerUnit, SpawnPerUnit, FNiagaraTypeDefinition::GetFloatDef(), InstanceParameters, FloatUserParameterInputBindings, FloatUpdateRangedInputBindings);
	bUseMovementTolerance = SpawnPerUnitInputs.bUseMovementTolerance;
	MovementTolerance = SpawnPerUnitInputs.MovementTolerance;
	bUseMaxMovementThreshold = SpawnPerUnitInputs.bUseMaxMovementThreshold;
	MaxMovementThreshold = SpawnPerUnitInputs.MaxMovementThreshold;
	SpawnGroup = SpawnPerUnitInputs.SpawnGroup;
}

void FNiagaraEmitterFastPath::FParamMap0_SpawnBurst_Instantaneous::Init(
	const UNiagaraEmitter* Emitter,
	int32 ModuleIndex,
	FNiagaraParameterStore& InstanceParameters,
	TArray<TNiagaraFastPathUserParameterInputBinding<int32>>& IntUserParameterInputBindings,
	TArray<TNiagaraFastPathUserParameterInputBinding<float>>& FloatUserParameterInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<int32>>& IntUpdateRangedInputBindings,
	TArray<TNiagaraFastPathRangedInputBinding<float>>& FloatUpdateRangedInputBindings)
{
	const FNiagaraFastPath_Module_SpawnBurstInstantaneous& SpawnBurstInstantaneousInputs = Emitter->SpawnBurstInstantaneous[ModuleIndex];
	InitInput(SpawnBurstInstantaneousInputs.SpawnCount, SpawnCount, FNiagaraTypeDefinition::GetIntDef(), InstanceParameters, IntUserParameterInputBindings, IntUpdateRangedInputBindings);
	InitInput(SpawnBurstInstantaneousInputs.SpawnTime, SpawnTime, FNiagaraTypeDefinition::GetFloatDef(), InstanceParameters, FloatUserParameterInputBindings, FloatUpdateRangedInputBindings);
	SpawnGroup = SpawnBurstInstantaneousInputs.SpawnGroup;
}

void FNiagaraEmitterFastPath::InitFastPathAttributeBindings(
	const FNiagaraFastPathAttributeNames& SourceAttributeNames,
	FNiagaraParameterStore& TargetParameterStore,
	FNiagaraSystemFastPath::FParamMap0& SystemMap,
	FNiagaraEmitterFastPath::FParamMap0& EmitterMap,
	TArray<TNiagaraFastPathAttributeBinding<int32>>& FastPathIntAttributeBindings,
	TArray<TNiagaraFastPathAttributeBinding<float>>& FastPathFloatAttributeBindings)
{
	for (int32 SystemParameterNameIndex = 0; SystemParameterNameIndex < SourceAttributeNames.System.Num(); ++SystemParameterNameIndex)
	{
		FName SystemParameterName = SourceAttributeNames.System[SystemParameterNameIndex];
		FName SystemParameterFullName = SourceAttributeNames.SystemFullNames[SystemParameterNameIndex];
		if (SystemParameterName == FNiagaraSystemFastPath::FParameterNames::ExecutionState)
		{
			AddBinding(SystemParameterFullName, FNiagaraTypeDefinition::GetExecutionStateEnum(), (int32*)&(SystemMap.System.ExecutionState), TargetParameterStore, FastPathIntAttributeBindings);
		}
		else if (SystemParameterName == FNiagaraSystemFastPath::FParameterNames::ExecutionStateSource)
		{
			AddBinding(SystemParameterFullName, FNiagaraTypeDefinition::GetExecutionStateSouceEnum(), (int32*)&(SystemMap.System.ExecutionStateSource), TargetParameterStore, FastPathIntAttributeBindings);
		}
	}

	for (int32 EmitterParameterNameIndex = 0; EmitterParameterNameIndex < SourceAttributeNames.Emitter.Num(); ++EmitterParameterNameIndex)
	{
		FName EmitterParameterName = SourceAttributeNames.Emitter[EmitterParameterNameIndex];
		FName EmitterParameterFullName = SourceAttributeNames.EmitterFullNames[EmitterParameterNameIndex];
		if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::Age)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.Age), TargetParameterStore, FastPathFloatAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::CurrentLoopDelay)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.CurrentLoopDelay), TargetParameterStore, FastPathFloatAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::CurrentLoopDuration)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.CurrentLoopDuration), TargetParameterStore, FastPathFloatAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::ExecutionState)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetExecutionStateEnum(), (int32*)&(EmitterMap.Emitter.ExecutionState), TargetParameterStore, FastPathIntAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::ExecutionStateSource)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetExecutionStateSouceEnum(), (int32*)&(EmitterMap.Emitter.ExecutionStateSource), TargetParameterStore, FastPathIntAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::LoopCount)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetIntDef(), &(EmitterMap.Emitter.LoopCount), TargetParameterStore, FastPathIntAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::LoopedAge)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.LoopedAge), TargetParameterStore, FastPathFloatAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::NormalizedLoopAge)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.NormalizedLoopAge), TargetParameterStore, FastPathFloatAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::DistanceTravelled)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.DistanceTraveled), TargetParameterStore, FastPathFloatAttributeBindings);
		}
		else if (EmitterParameterName == FNiagaraEmitterFastPath::FAttributeNames::Scalability_DistanceFraction)
		{
			AddBinding(EmitterParameterFullName, FNiagaraTypeDefinition::GetFloatDef(), &(EmitterMap.Emitter.Scalability.DistanceFraction), TargetParameterStore, FastPathFloatAttributeBindings);
		}
	}
}

void FNiagaraEmitterFastPath::SetSpawnMapDefaults(const UNiagaraEmitter& Emitter, FParamMap0& Map)
{
	Map.Emitter.Age = 0;
	Map.Emitter.CurrentLoopDelay = 0;
	Map.Emitter.CurrentLoopDuration = 0;
	Map.Emitter.ExecutionState = (ENiagaraExecutionState)0;
	Map.Emitter.ExecutionStateSource = (ENiagaraExecutionStateSource)0;
	Map.Emitter.LoopCount = 0;
	Map.Emitter.LoopedAge = 0.0f;
	Map.Emitter.NormalizedLoopAge = 0.0f;
	Map.Emitter.DistanceTraveled = 0;
	//Map.Emitter.SpawnCountScale = 1.0f; // TODO: This seems to always set the value to 1.0 due to the order of operations in FNiagaraSystemSimulation::TickFastPath
	Map.Emitter.Scalability.DistanceFraction = 1;
	Map.Scalability.Emitter.ExecutionState = (ENiagaraExecutionState)0;
	Map.Scalability.Emitter.SpawnCountScale = 1;
	Map.Emitter.SpawnRate.Empty();
	Map.Emitter.SpawnRate.AddDefaulted(Emitter.SpawnRate.Num());
	Map.Emitter.SpawnPerUnit.Empty();
	Map.Emitter.SpawnPerUnit.AddDefaulted(Emitter.SpawnPerUnit.Num());
	Map.Emitter.SpawnBurst_Instantaneous.Empty();
	Map.Emitter.SpawnBurst_Instantaneous.AddDefaulted(Emitter.SpawnBurstInstantaneous.Num());
}

void FNiagaraEmitterFastPath::SetUpdateMapDefaults(FParamMap0& Map)
{
	/* TODO?
	Map.Emitter.SpawnRate.SpawnOutputInfo.Count = 0;
	Map.Emitter.SpawnRate.SpawnOutputInfo.InterpStartDt = 0.0f;
	Map.Emitter.SpawnRate.SpawnOutputInfo.IntervalDt = 0.0f;
	Map.Emitter.SpawnRate.SpawnOutputInfo.SpawnGroup = 0;
	*/
}

void FNiagaraEmitterFastPath::Function_SampleCurve_SpawnCountScaleByDistanceFraction(const FNiagaraFastPath_Module_EmitterScalability& Context_Map_EmitterScalability, float InFraction, float& OutScale)
{
	OutScale = FMath::Lerp(Context_Map_EmitterScalability.ScaleForMinFraction, Context_Map_EmitterScalability.ScaleForMaxFraction, InFraction);
}

void FNiagaraEmitterFastPath::Module_EmitterScalability(const FNiagaraFastPath_Module_EmitterScalability& Context_Map_EmitterScalability, FParamMap0& Context_Map)
{
	float Constant13 = 0;
	float float_IfResult;
	if (Context_Map_EmitterScalability.bUseMinDistance)
	{
		float_IfResult = Context_Map_EmitterScalability.MinDistance;
	}
	else
	{
		float_IfResult = Constant13;
	}
	float Result9 = Context_Map.Engine.Owner.LODDistance - float_IfResult;
	float Constant14 = Context_Map.Engine.Owner.MaxLODDistance;
	float float_IfResult1;
	if (Context_Map_EmitterScalability.bUseMaxDistance)
	{
		float_IfResult1 = Context_Map_EmitterScalability.MaxDistance;
	}
	else
	{
		float_IfResult1 = Constant14;
	}
	float Result10 = float_IfResult1 - float_IfResult;
	float Result11 = Result9 / Result10;
	Context_Map.Emitter.Scalability.DistanceFraction = Result11;
	// Value is initialized in input struct constructor now so ignore this to avoid overwritting a value set on the emitter.
	// ENiagaraExecutionState Constant15 = ENiagaraExecutionState::Inactive;
	// Context_Map_EmitterScalability.MinCulledState = Constant15;
	ENiagaraExecutionState Constant16 = ENiagaraExecutionState::Active;
	Context_Map.Scalability.Emitter.ExecutionState = Constant16;
	float Constant17 = 0;
	bool Result12 = Context_Map.Emitter.Scalability.DistanceFraction < Constant17;
	bool Result13 = Result12 && Context_Map_EmitterScalability.bUseMinDistance;
	ENiagaraExecutionState ENiagaraExecutionState_IfResult3;
	if (Result13)
	{
		ENiagaraExecutionState_IfResult3 = Context_Map_EmitterScalability.MinCulledState;
	}
	else
	{
		ENiagaraExecutionState_IfResult3 = Context_Map.Scalability.Emitter.ExecutionState;
	}
	Context_Map.Scalability.Emitter.ExecutionState = ENiagaraExecutionState_IfResult3;
	// Value is initialized in input struct constructor now so ignore this to avoid overwriting a value set on the emitter.
	// ENiagaraExecutionState Constant18 = ENiagaraExecutionState::Inactive;
	// Context_Map_EmitterScalability.MaxCulledState = Constant18;
	float Constant19 = 1;
	bool Result14 = Context_Map.Emitter.Scalability.DistanceFraction > Constant19;
	bool Result15 = Result14 && Context_Map_EmitterScalability.bUseMaxDistance;
	ENiagaraExecutionState ENiagaraExecutionState_IfResult4;
	if (Result15)
	{
		ENiagaraExecutionState_IfResult4 = Context_Map_EmitterScalability.MaxCulledState;
	}
	else
	{
		ENiagaraExecutionState_IfResult4 = Context_Map.Scalability.Emitter.ExecutionState;
	}
	Context_Map.Scalability.Emitter.ExecutionState = ENiagaraExecutionState_IfResult4;
	float Constant20 = 1;
	float float_IfResult2;
	if (Context_Map_EmitterScalability.bApplySpawnCountScale)
	{
		float_IfResult2 = Context_Map_EmitterScalability.SpawnCountScale;
	}
	else
	{
		float_IfResult2 = Constant20;
	}
	float SampleCurve_SpawnCountScaleByDistanceFractionOutput_Value;
	Function_SampleCurve_SpawnCountScaleByDistanceFraction(Context_Map_EmitterScalability, Context_Map.Emitter.Scalability.DistanceFraction, SampleCurve_SpawnCountScaleByDistanceFractionOutput_Value);
	float Constant21 = 1;
	float float_IfResult3;
	if (Context_Map_EmitterScalability.bApplySpawnCountScaleByDistanceFraction)
	{
		float_IfResult3 = SampleCurve_SpawnCountScaleByDistanceFractionOutput_Value;
	}
	else
	{
		float_IfResult3 = Constant21;
	}
	float Result16 = float_IfResult2 * float_IfResult3;
	Context_Map.Scalability.Emitter.SpawnCountScale = Result16;
}

void FNiagaraEmitterFastPath::Function_EmitterLifeCycle_EmitterChangeState(ENiagaraExecutionState In_NewState, bool In_Condition, ENiagaraExecutionStateSource In_NewStateSource, FParamMap0& Context_Map)
{
	ENiagaraExecutionState Constant33 = ENiagaraExecutionState::Complete;
	bool Result32 = Context_Map.Emitter.ExecutionState != Constant33;
	ENiagaraExecutionState Constant34 = ENiagaraExecutionState::Disabled;
	bool Result33 = Context_Map.Emitter.ExecutionState != Constant34;
	bool Result34 = Result32 && Result33;
	bool Context_Map_Local_Emitter_CanChangeState = Result34;
	bool Result35 = In_Condition && Context_Map_Local_Emitter_CanChangeState;
	bool Result36 = Context_Map.Emitter.ExecutionStateSource <= In_NewStateSource;
	bool Result37 = Result35 && Result36;
	ENiagaraExecutionState ENiagaraExecutionState_IfResult6;
	ENiagaraExecutionStateSource ENiagaraExecutionStateSource_IfResult3;
	if (Result37)
	{
		ENiagaraExecutionState_IfResult6 = In_NewState;
		ENiagaraExecutionStateSource_IfResult3 = In_NewStateSource;
	}
	else
	{
		ENiagaraExecutionState_IfResult6 = Context_Map.Emitter.ExecutionState;
		ENiagaraExecutionStateSource_IfResult3 = Context_Map.Emitter.ExecutionStateSource;
	}
	Context_Map.Emitter.ExecutionState = ENiagaraExecutionState_IfResult6;
	Context_Map.Emitter.ExecutionStateSource = ENiagaraExecutionStateSource_IfResult3;
}

void FNiagaraEmitterFastPath::Module_EmitterLifeCycle(const FNiagaraFastPath_Module_EmitterLifeCycle& Context_Map_EmitterLifeCycle, FParamMap0& Context_Map)
{
	float Constant24 = 0;
	bool Result19 = Context_Map.Emitter.Age == Constant24;
	float Result20 = -(Context_Map_EmitterLifeCycle.NextLoopDelay);
	float Constant25 = 0;
	float Constant26 = 0;
	float float_IfResult4;
	float float001_IfResult;
	float float002_IfResult;
	float float003_IfResult;
	if (Result19)
	{
		float_IfResult4 = Result20;
		float001_IfResult = Context_Map_EmitterLifeCycle.NextLoopDuration;
		float002_IfResult = Context_Map_EmitterLifeCycle.NextLoopDelay;
		float003_IfResult = Constant25;
	}
	else
	{
		float_IfResult4 = Context_Map.Emitter.LoopedAge;
		float001_IfResult = Context_Map.Emitter.CurrentLoopDuration;
		float002_IfResult = Context_Map.Emitter.CurrentLoopDelay;
		float003_IfResult = Constant26;
	}
	Context_Map.Emitter.LoopedAge = float_IfResult4;
	Context_Map.Emitter.CurrentLoopDuration = float001_IfResult;
	Context_Map.Emitter.CurrentLoopDelay = float002_IfResult;
	float Result21 = Context_Map.Emitter.Age + Context_Map.Engine.DeltaTime;
	float Result22 = Context_Map.Engine.DeltaTime + Context_Map.Emitter.LoopedAge;
	float Result23 = Result22 / Context_Map.Emitter.CurrentLoopDuration;
	int32 Count;
	Count = Result23;
	int32 Constant27 = 0;
	int32 Result24 = FMath::Max(Count, Constant27);
	float Result25 = Result24 * Context_Map.Emitter.CurrentLoopDuration;
	float Result26 = Result22 - Result25;
	int32 Result27 = Result24 + Context_Map.Emitter.LoopCount;
	Context_Map.Emitter.Age = Result21;
	Context_Map.Emitter.LoopedAge = Result26;
	Context_Map.Emitter.LoopCount = Result27;
	int32 Constant28 = 0;
	bool Result28 = Result24 > Constant28;
	float float_IfResult5;
	if (Context_Map_EmitterLifeCycle.bDurationRecalcEachLoop)
	{
		float_IfResult5 = Context_Map_EmitterLifeCycle.NextLoopDuration;
	}
	else
	{
		float_IfResult5 = Context_Map.Emitter.CurrentLoopDuration;
	}
	float Constant31 = 0;
	float float_IfResult6;
	if (Context_Map_EmitterLifeCycle.bDelayFirstLoopOnly)
	{
		float_IfResult6 = Constant31;
	}
	else
	{
		float_IfResult6 = Context_Map_EmitterLifeCycle.NextLoopDelay;
	}
	float Result29 = Context_Map.Emitter.LoopedAge - float_IfResult6;
	float float_IfResult7;
	float float001_IfResult1;
	float float002_IfResult1;
	if (Result28)
	{
		float_IfResult7 = float_IfResult5;
		float001_IfResult1 = float_IfResult6;
		float002_IfResult1 = Result29;
	}
	else
	{
		float_IfResult7 = Context_Map.Emitter.CurrentLoopDuration;
		float001_IfResult1 = Context_Map.Emitter.CurrentLoopDelay;
		float002_IfResult1 = Context_Map.Emitter.LoopedAge;
	}
	float Result30 = float002_IfResult1 / float_IfResult7;
	Context_Map.Emitter.CurrentLoopDuration = float_IfResult7;
	Context_Map.Emitter.CurrentLoopDelay = float001_IfResult1;
	Context_Map.Emitter.LoopedAge = float002_IfResult1;
	Context_Map.Emitter.NormalizedLoopAge = Result30;
	ENiagaraExecutionStateSource Constant32 = ENiagaraExecutionStateSource::Scalability;
	bool Result31 = Context_Map.System->ExecutionStateSource != Constant32;
	Function_EmitterLifeCycle_EmitterChangeState(Context_Map.System->ExecutionState, Result31, Context_Map.System->ExecutionStateSource, Context_Map);

	//System level scalability is now handled in higher level C++ code.
// 	ENiagaraExecutionStateSource Constant35 = ENiagaraExecutionStateSource::Scalability;
// 	bool Result38 = Context_Map.System->ExecutionStateSource == Constant35;
// 	ENiagaraExecutionState Constant36 = (ENiagaraExecutionState::Active);
// 	bool Result39 = Context_Map.System->ExecutionState != Constant36;
// 	bool Result40 = Result38 && Result39;
// 	ENiagaraExecutionState ENiagaraExecutionState_IfResult7;
// 	if (Result40)
// 	{
// 		ENiagaraExecutionState_IfResult7 = Context_Map.System->ExecutionState;
// 	}
// 	else
// 	{
// 		ENiagaraExecutionState_IfResult7 = Context_Map.Scalability.Emitter.ExecutionState;
// 	}
// 	Context_Map.Scalability.Emitter.ExecutionState = ENiagaraExecutionState_IfResult7;

	bool Constant37 = true;
	ENiagaraExecutionStateSource Constant38 = ENiagaraExecutionStateSource::Scalability;
	Function_EmitterLifeCycle_EmitterChangeState(Context_Map.Scalability.Emitter.ExecutionState, Constant37, Constant38, Context_Map);
	ENiagaraExecutionState Constant39 = ENiagaraExecutionState::Inactive;
	bool Constant40 = true;
	Context_Map.Spawning.bCanEverSpawn = Constant40;
	int32 Constant41 = 0;
	bool Result44 = Context_Map_EmitterLifeCycle.MaxLoopCount > Constant41;
	bool Result45 = Context_Map.Emitter.LoopCount >= Context_Map_EmitterLifeCycle.MaxLoopCount;
	bool Result46 = Result44 && Result45;
	ENiagaraExecutionStateSource Constant42 = ENiagaraExecutionStateSource::Internal;
	Function_EmitterLifeCycle_EmitterChangeState(Constant39, Result46, Constant42, Context_Map);
	ENiagaraExecutionState Constant43 = ENiagaraExecutionState::Complete;
	// Values are initialized in input struct constructor now so ignore this to avoid overwriting a value set on the emitter.
	// bool Constant42 = true;
	// Context_Map_EmitterLifeCycle.AutoComplete = Constant42;
	// bool Constant43 = false;
	// Context_Map_EmitterLifeCycle.CompleteOnInactive = Constant43;
	int32 Constant46 = 0;
	bool Result50 = Context_Map.Engine.Emitter.NumParticles == Constant46;
	ENiagaraExecutionState Constant47 = ENiagaraExecutionState::Active;

	//Manual edit to stop auto complete when we become inactive due to scalability.
	bool Result51 = Context_Map.Emitter.ExecutionState != Constant47 && Context_Map.Emitter.ExecutionStateSource != ENiagaraExecutionStateSource::Scalability;

	bool Result52 = Result50 && Result51;
	bool Result53 = Result52 && Context_Map_EmitterLifeCycle.bAutoComplete;
	bool Result54 = Context_Map_EmitterLifeCycle.bCompleteOnInactive && Result51;
	bool Result55 = Result53 || Result54;
	ENiagaraExecutionStateSource Constant48 = ENiagaraExecutionStateSource::InternalCompletion;
	Function_EmitterLifeCycle_EmitterChangeState(Constant43, Result55, Constant48, Context_Map);
}

void FNiagaraEmitterFastPath::Module_SpawnRate(FParamMap0& Context_Map)
{
	for (int32 SpawnRateIndex = 0; SpawnRateIndex < Context_Map.SpawnRate.Num(); ++SpawnRateIndex)
	{
		const FParamMap0_SpawnRate& Context_Map_SpawnRate = Context_Map.SpawnRate[SpawnRateIndex];
		FParamMap0_Emitter_SpawnRate& Context_Map_Emitter_SpawnRate = Context_Map.Emitter.SpawnRate[SpawnRateIndex];

		float Result57 = Context_Map_SpawnRate.SpawnRate * Context_Map.Scalability.Emitter.SpawnCountScale * Context_Map.Emitter.SpawnCountScale;
		float Result58 = FNiagaraSystemFastPath::SafeReciprocal(Result57);
		float Result59 = 1 - Context_Map_Emitter_SpawnRate.SpawnRemainder;
		float Result60 = Result58 * Result59;
		float Context_Map_Local_SpawnRate_SpawnRate = Result57;
		float Context_Map_Local_SpawnRate_IntervalDT = Result58;
		float Context_Map_Local_SpawnRate_InterpStartDT = Result60;
		float Constant47 = 0;
		bool Result61 = Context_Map.Emitter.LoopedAge >= Constant47;
		float Constant48 = 1;
		float Constant49 = 0;
		float float_IfResult8;
		if (Result61)
		{
			float_IfResult8 = Constant48;
		}
		else
		{
			float_IfResult8 = Constant49;
		}
		float Result62 = Context_Map_Local_SpawnRate_SpawnRate * float_IfResult8;
		float Result63 = Result62 * Context_Map.Engine.DeltaTime + Context_Map_Emitter_SpawnRate.SpawnRemainder;
		int32 Result64 = floor(Result63);
		float Result65 = Result63 - Result64;
		int32 Context_Map_Local_SpawnRate_SpawnCount = Result64;
		Context_Map_Emitter_SpawnRate.SpawnRemainder = Result65;
		FNiagaraSpawnInfo Output1;
		Output1.Count = Context_Map_Local_SpawnRate_SpawnCount;
		Output1.InterpStartDt = Context_Map_Local_SpawnRate_InterpStartDT;
		Output1.IntervalDt = Context_Map_Local_SpawnRate_IntervalDT;
		Output1.SpawnGroup = Context_Map_SpawnRate.SpawnGroup;
		bool Constant50 = true;
		Context_Map_Emitter_SpawnRate.SpawnOutputInfo = Output1;
		Context_Map.Spawning.bCanEverSpawn = Constant50;
	}
}

void FNiagaraEmitterFastPath::Module_SpawnPerUnit(FParamMap0& Context_Map)
{
	for (int32 SpawnPerUnitIndex = 0; SpawnPerUnitIndex < Context_Map.SpawnPerUnit.Num(); ++SpawnPerUnitIndex)
	{
		const FParamMap0_SpawnPerUnit& Context_Map_SpawnPerUnit = Context_Map.SpawnPerUnit[SpawnPerUnitIndex];
		FParamMap0_Emitter_SpawnPerUnit& Context_Map_Emitter_SpawnPerUnit = Context_Map.Emitter.SpawnPerUnit[SpawnPerUnitIndex];

		// Not exposed on the fast path module.
		FVector Context_Map_SpawnPerUnit_VelocityVector = Context_Map.Engine.Owner.Velocity;
		float Context_Map_SpawnPerUnit_DeltaTime = Context_Map.Engine.DeltaTime;

		// Value is initialized in input struct constructor now so ignore this to avoid overwriting a value set on the emitter.
		// bool Constant51 = false;
		// Context_Map_SpawnPerUnit.UseMovementTolerance = Constant51;
		float Result66 = Context_Map_SpawnPerUnit_VelocityVector.Size();
		float Context_Map_Local_SpawnPerUnit_VelocityVectorLength = Result66;
		float Result67 = Result66 * Context_Map_SpawnPerUnit_DeltaTime;
		bool Result68 = Result67 > Context_Map_SpawnPerUnit.MovementTolerance;
		float Constant52 = 0;
		float float_IfResult9;
		if (Result68)
		{
			float_IfResult9 = Result66;
		}
		else
		{
			float_IfResult9 = Constant52;
		}
		float float_IfResult10;
		if (Context_Map_SpawnPerUnit.bUseMovementTolerance)
		{
			float_IfResult10 = float_IfResult9;
		}
		else
		{
			float_IfResult10 = Result66;
		}
		float Context_Map_Local_SpawnPerUnit_MovementThresholdVectorLength = float_IfResult10;
		float Constant53 = 100;
		float Result69 = Context_Map_Local_SpawnPerUnit_VelocityVectorLength / Constant53;
		float Constant54 = 500000;
		float Result70 = FMath::Fmod(Context_Map.Emitter.DistanceTraveled, Constant54);
		float Result71 = Result69 + Result70;
		float Result72 = FNiagaraSystemFastPath::SafeReciprocal(Context_Map_SpawnPerUnit.SpawnPerUnit);
		float Result73 = Context_Map_Local_SpawnPerUnit_MovementThresholdVectorLength * Result72;
		Context_Map.Emitter.DistanceTraveled = Result71;
		float Context_Map_Local_SpawnPerUnit_SpawnSpacing = Result73;
		float Result74 = Context_Map.Scalability.Emitter.SpawnCountScale * Context_Map_Local_SpawnPerUnit_SpawnSpacing;
		float Result75 = Result74 * Context_Map.Engine.DeltaTime + Context_Map_Emitter_SpawnPerUnit.SpawnRemainder;
		int32 Result76 = FMath::FloorToInt(Result75);
		float Result77 = Result75 - Result76;
		float Result78 = FNiagaraSystemFastPath::SafeReciprocal(Context_Map_Local_SpawnPerUnit_SpawnSpacing);
		float Result79 = 1 - Context_Map_Emitter_SpawnPerUnit.SpawnRemainder;
		float Result80 = Result79 * Result78;
		int32 Context_Map_Local_SpawnPerUnit_SpawnCountInt = Result76;
		Context_Map_Emitter_SpawnPerUnit.SpawnRemainder = Result77;
		float Context_Map_Local_SpawnPerUnit_IntervalDt = Result78;
		float Context_Map_Local_SpawnPerUnit_InterpStartDt = Result80;
		// Value is initialized in input struct constructor now so ignore this to avoid overwriting a value set on the emitter.
		//bool Constant55 = true;
		//Context_Map_SpawnPerUnit.bUseMaxMovementThreshold = Constant55;
		bool Result81 = Context_Map_Local_SpawnPerUnit_VelocityVectorLength > Context_Map_SpawnPerUnit.MaxMovementThreshold;
		int32 Constant56 = 0;
		int32 int32_IfResult;
		if (Result81)
		{
			int32_IfResult = Constant56;
		}
		else
		{
			int32_IfResult = Context_Map_Local_SpawnPerUnit_SpawnCountInt;
		}
		int32 int32_IfResult1;
		if (Context_Map_SpawnPerUnit.bUseMaxMovementThreshold)
		{
			int32_IfResult1 = int32_IfResult;
		}
		else
		{
			int32_IfResult1 = Context_Map_Local_SpawnPerUnit_SpawnCountInt;
		}
		FNiagaraSpawnInfo Output11;
		Output11.Count = int32_IfResult1;
		Output11.InterpStartDt = Context_Map_Local_SpawnPerUnit_InterpStartDt;
		Output11.IntervalDt = Context_Map_Local_SpawnPerUnit_IntervalDt;
		Output11.SpawnGroup = Context_Map_SpawnPerUnit.SpawnGroup;
		bool Constant57 = true;
		int32 Constant58 = 0;
		bool Result82 = int32_IfResult1 > Constant58;
		Context_Map_Emitter_SpawnPerUnit.SpawnOutputInfo = Output11;
		Context_Map.Spawning.bCanEverSpawn = Constant57;
		//Context.Map.OUTPUT_VAR.SpawnPerUnit.HasSpawnedThisFrame = Result82;
	}
}

void FNiagaraEmitterFastPath::Module_SpawnBurstInstantaneous(FParamMap0& Context_Map)
{
	for (int32 SpawnBurstInstantaneousIndex = 0; SpawnBurstInstantaneousIndex < Context_Map.SpawnBurst_Instantaneous.Num(); ++SpawnBurstInstantaneousIndex)
	{
		const FParamMap0_SpawnBurst_Instantaneous& Context_Map_SpawnBurst_Instantaneous = Context_Map.SpawnBurst_Instantaneous[SpawnBurstInstantaneousIndex];
		FParamMap0_Emitter_SpawnBurst_Instantaneous& Context_Map_Emitter_SpawnBurst_Instantaneous = Context_Map.Emitter.SpawnBurst_Instantaneous[SpawnBurstInstantaneousIndex];

		// Not exposed on the fast path module
		float Context_Map_SpawnBurst_Instantaneous_Age = Context_Map.Emitter.LoopedAge;

		float Result83 = Context_Map_SpawnBurst_Instantaneous_Age - Context_Map.Engine.DeltaTime;
		float Result84 = Context_Map_SpawnBurst_Instantaneous.SpawnTime - Result83;
		float Constant59 = 0;
		float Output12;
		Output12 = Constant59;
		bool Result85 = Result84 >= Output12;
		float Result86 = Context_Map_SpawnBurst_Instantaneous.SpawnTime - Context_Map_SpawnBurst_Instantaneous_Age;
		bool Result87 = Result86 < Output12;
		bool Result88 = Result85 && Result87;
		int32 Constant60 = 0;
		bool Result89 = Context_Map_SpawnBurst_Instantaneous.SpawnCount == Constant60;
		float Constant61 = 0;
		float Result90 = Context_Map_SpawnBurst_Instantaneous.SpawnCount * Context_Map.Scalability.Emitter.SpawnCountScale * Context_Map.Emitter.SpawnCountScale; 
		float Constant62 = 1;
		float Result91 = FMath::Max(Result90, Constant62);
		float float_IfResult11;
		if (Result89)
		{
			float_IfResult11 = Constant61;
		}
		else
		{
			float_IfResult11 = Result91;
		}
		float Constant63 = 0;
		FNiagaraSpawnInfo Output13;
		Output13.Count = float_IfResult11;
		Output13.InterpStartDt = Result84;
		Output13.IntervalDt = Constant63;
		Output13.SpawnGroup = Context_Map_SpawnBurst_Instantaneous.SpawnGroup;
		int32 Constant64 = 0;
		float Constant65 = 0;
		float Constant66 = 0;
		FNiagaraSpawnInfo Output14;
		Output14.Count = Constant64;
		Output14.InterpStartDt = Constant65;
		Output14.SpawnGroup = Context_Map_SpawnBurst_Instantaneous.SpawnGroup;
		Output14.IntervalDt = Constant66;
		FNiagaraSpawnInfo SpawnInfo_IfResult;
		if (Result88)
		{
			SpawnInfo_IfResult = Output13;
		}
		else
		{
			SpawnInfo_IfResult = Output14;
		}
		Context_Map_Emitter_SpawnBurst_Instantaneous.SpawnBurst = SpawnInfo_IfResult;
		bool Result92 = Context_Map.Emitter.LoopedAge <= Context_Map_SpawnBurst_Instantaneous.SpawnTime;
		bool Result93 = Context_Map.Spawning.bCanEverSpawn || Result92;
		Context_Map.Spawning.bCanEverSpawn = Result93;
	}
}