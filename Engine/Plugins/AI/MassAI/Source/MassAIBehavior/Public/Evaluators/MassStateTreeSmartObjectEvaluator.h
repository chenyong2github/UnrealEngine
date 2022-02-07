// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSignalSubsystem.h"
#include "MassSmartObjectRequest.h"
#include "MassStateTreeTypes.h"
#include "SmartObjectSubsystem.h"
#include "MassStateTreeSmartObjectEvaluator.generated.h"

struct FStateTreeExecutionContext;
struct FTransformFragment;
struct FMassZoneGraphLaneLocationFragment;
struct FMassSmartObjectUserFragment;

/**
 * Evaluator that will keep track if there is one or more smart object(s) that can be used.
 */
USTRUCT()
struct MASSAIBEHAVIOR_API FMassStateTreeSmartObjectEvaluatorInstanceData
{
	GENERATED_BODY()

	/** The identifier of the search request send by the evaluator to find candidates */
	UPROPERTY(EditAnywhere, Category = Output)
	FMassSmartObjectRequestResult SearchRequestResult;

	/** The identifier of the search request send by the evaluator to find candidates */
	UPROPERTY(EditAnywhere, Category = Output)
	FMassSmartObjectRequestID SearchRequestID;

	/** Indicates that the result of the candidates search is ready and contains some candidates */
	UPROPERTY(EditAnywhere, Category = Output)
	bool bCandidatesFound = false;
	
	/** Indicates that an object has been claimed */
	UPROPERTY(EditAnywhere, Category = Output)
	bool bClaimed = false;

	/** Next update time; evaluator will not do anything when Evaluate gets called before that time */
	UPROPERTY()
	float NextUpdate = 0.f;

	/** Indicates that the query was able to use annotations on zone graph lanes instead of a spatial query. */
	UPROPERTY()
	bool bUsingZoneGraphAnnotations = false;
};

USTRUCT(meta = (DisplayName = "Mass SmartObject Eval"))
struct MASSAIBEHAVIOR_API FMassStateTreeSmartObjectEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FMassStateTreeSmartObjectEvaluatorInstanceData::StaticStruct(); }
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const override;
	void Reset(FStateTreeExecutionContext& Context) const;

	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
	TStateTreeExternalDataHandle<UMassSignalSubsystem> MassSignalSubsystemHandle;
	TStateTreeExternalDataHandle<FTransformFragment> EntityTransformHandle;
	TStateTreeExternalDataHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeExternalDataHandle<FMassZoneGraphLaneLocationFragment, EStateTreeExternalDataRequirement::Optional> LocationHandle;

	TStateTreeInstanceDataPropertyHandle<FMassSmartObjectRequestResult> SearchRequestResultHandle;
	TStateTreeInstanceDataPropertyHandle<FMassSmartObjectRequestID> SearchRequestIDHandle;
	TStateTreeInstanceDataPropertyHandle<bool> CandidatesFoundHandle;
	TStateTreeInstanceDataPropertyHandle<bool> ClaimedHandle;
	TStateTreeInstanceDataPropertyHandle<float> NextUpdateHandle;
	TStateTreeInstanceDataPropertyHandle<bool> UsingZoneGraphAnnotationsHandle;

	/** The delay that the evaluator should wait before trying to find a smart object after a failed attempt */
	UPROPERTY(EditAnywhere, Category = Parameter)
	float RetryCooldown = 0.f;

	/** The default delay that the evaluator should wait before evaluating again */
	UPROPERTY(EditAnywhere, Category = Parameter)
	float TickInterval = 0.f;
};
