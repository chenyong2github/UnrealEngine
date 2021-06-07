// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraMergeable.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#include "NiagaraConstants.h"
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
	UNiagaraSimulationStageBase()
	{
		bEnabled = true;
	}

	UPROPERTY()
	UNiagaraScript* Script;

	UPROPERTY(EditAnywhere, Category = "Simulation Stage")
	FName SimulationStageName;

	UPROPERTY()
	uint32 bEnabled : 1;

	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;
#if WITH_EDITOR
	/** Return the FName to use in place of the default for the location in the stack context. If this would be the default, return NAME_None.*/
	virtual FName GetStackContextReplacementName() const { return NAME_None; }
	void SetEnabled(bool bEnabled);
	void RequestRecompile();
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
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

	UPROPERTY(EditAnywhere, Category = "Simulation Stage", meta = (NoSpinbox = "true", ClampMin = 1, Tooltip = "The number of times we run this simulation stage before moving to the next stage."))
	int32 Iterations;

	UPROPERTY(EditAnywhere, Category = "Simulation Stage", meta = (DisplayName = "Emitter Reset Only", Tooltip = "When enabled the stage will only run on the first tick after the emitter is reset, only valid for data interface iteration stages", EditCondition = "IterationSource == ENiagaraIterationSource::DataInterface"))
	uint32 bSpawnOnly : 1;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Simulation Stage", meta = (Tooltip = "Disables the ability to read / write from the same particle buffer, i.e. only update position and no other attributes.  By default this should not be changed and is a debugging tool.", EditCondition = "IterationSource == ENiagaraIterationSource::Particles"))
	uint32 bDisablePartialParticleUpdate : 1;

	/** Source data interface to use for the simulation stage. The data interface needs to be a subclass of UNiagaraDataInterfaceRWBase, for example the Grid2D and Grid3D data interfaces. */
	UPROPERTY(EditAnywhere, Category = "Simulation Stage", meta = (editcondition = "IterationSource == ENiagaraIterationSource::DataInterface"))
	FNiagaraVariableDataInterfaceBinding DataInterface;

	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
#if WITH_EDITOR
	virtual FName GetStackContextReplacementName() const override; 

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};