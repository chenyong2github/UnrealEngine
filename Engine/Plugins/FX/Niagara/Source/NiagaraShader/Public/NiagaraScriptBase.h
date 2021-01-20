// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NiagaraScriptBase.generated.h"

USTRUCT()
struct NIAGARASHADER_API FSimulationStageMetaData
{
	GENERATED_USTRUCT_BODY()
public:
	FSimulationStageMetaData();

	/** User simulation stage name. */
	UPROPERTY()
	FName SimulationStageName;

	/** The Data Interface that we iterate over for this stage. If None, then use particles.*/
	UPROPERTY()
	FName IterationSource;

	/** Is this stage a spawn-only stage? */
	UPROPERTY()
	uint32 bSpawnOnly : 1;

	/** Do we write to particles this stage? */
	UPROPERTY()
	uint32 bWritesParticles : 1;

	/** When enabled the simulation stage does not write all variables out, so we are reading / writing to the same buffer. */
	UPROPERTY()
	uint32 bPartialParticleUpdate : 1;

	/** DataInterfaces that we write to in this stage.*/
	UPROPERTY()
	TArray<FName> OutputDestinations;

	/** Index of the simulation stage where we begin iterating. This is meant to encompass iteration count without having an entry for each iteration.*/
	UPROPERTY()
	int32 MinStage = 0;

	/** Index of the simulation stage where we end iterating. This is meant to encompass iteration count without having an entry for each iteration.*/
	UPROPERTY()
	int32 MaxStage = 0;
};

UCLASS(MinimalAPI, abstract)
class UNiagaraScriptBase : public UObject
{
	GENERATED_UCLASS_BODY()

	virtual void ModifyCompilationEnvironment(struct FShaderCompilerEnvironment& OutEnvironment) const PURE_VIRTUAL(UNiagaraScriptBase::ModifyCompilationEnvironment, );
	virtual TConstArrayView<FSimulationStageMetaData> GetSimulationStageMetaData() const PURE_VIRTUAL(UNiagaraScriptBase::GetSimulationStageMetaData(), return MakeArrayView<FSimulationStageMetaData>(nullptr, 0); )
};
