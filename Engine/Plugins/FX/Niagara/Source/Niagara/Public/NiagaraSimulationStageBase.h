// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraMergeable.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#include "NiagaraSimulationStageBase.generated.h"

class UNiagaraScript;

/**
* A base class for niagara simulation stages.  This class should be derived to add stage specific information.
*/
UCLASS()
class NIAGARA_API UNiagaraSimulationStageBase : public UNiagaraMergeable
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UNiagaraScript* Script;

	UPROPERTY(EditAnywhere, Category = "Simulation Stage")
	FName SimulationStageName;

	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const { return true; }

};

UCLASS(meta = (DisplayName = "Generic Simulation Stage"))
class NIAGARA_API UNiagaraSimulationStageGeneric : public UNiagaraSimulationStageBase
{
	GENERATED_BODY()

public:
	UNiagaraSimulationStageGeneric()
		: Iterations(1)
	{
	}

	/** Determine which elements this script is iterating over. You are not allowed to */
	UPROPERTY(EditAnywhere, Category = "Simulation Stage")
	ENiagaraIterationSource IterationSource;

	UPROPERTY(EditAnywhere, Category = "Simulation Stage")
	int32 Iterations;

	UPROPERTY(EditAnywhere, Category = "Simulation Stage", meta = (DisplayName = "Emitter Reset Only", Tooltip = "When enabled the stage will only run on the first tick after the emitter is reset, only valid for data interface iteration stages", EditCondition = "IterationSource == ENiagaraIterationSource::DataInterface"))
	uint32 bSpawnOnly : 1;

	UPROPERTY(EditAnywhere, Category = "Simulation Stage", meta = (editcondition = "IterationSource == ENiagaraIterationSource::DataInterface"))
	FNiagaraVariableDataInterfaceBinding DataInterface;

	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};