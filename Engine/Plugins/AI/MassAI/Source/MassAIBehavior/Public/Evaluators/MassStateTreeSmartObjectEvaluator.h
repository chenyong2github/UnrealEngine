// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSmartObjectProcessor.h"
#include "MassStateTreeTypes.h"
#include "MassSmartObjectTypes.h"
#include "MassStateTreeSmartObjectEvaluator.generated.h"

struct FStateTreeExecutionContext;
struct FDataFragment_Transform;
struct FMassZoneGraphLaneLocationFragment;

/**
 * Evaluator that will keep track if there is one or more smart object(s) that can be used.
 */
USTRUCT(meta = (DisplayName = "Mass SmartObject Eval"))
struct MASSAIBEHAVIOR_API FMassStateTreeSmartObjectEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override;
	void Reset();

	TStateTreeItemHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
	TStateTreeItemHandle<UMassSignalSubsystem> MassSignalSubsystemHandle;
	TStateTreeItemHandle<FDataFragment_Transform> EntityTransformHandle;
	TStateTreeItemHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeItemHandle<FMassZoneGraphLaneLocationFragment, EStateTreeItemRequirement::Optional> LocationHandle;

	/** The identifier of the search request send by the evaluator to find candidates */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	FMassSmartObjectRequestResult SearchRequestResult;

	/** The identifier of the search request send by the evaluator to find candidates */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	FMassSmartObjectRequestID SearchRequestID;

	/** The delay that the evaluator should wait before trying to find a smart object after a failed attempt */
	UPROPERTY(EditAnywhere, Category = SmartObject)
	float RetryCooldown = 0.f;

	/** The default delay that the evaluator should wait before evaluating again */
	UPROPERTY(EditAnywhere, Category = SmartObject)
	float TickInterval = 0.f;

	/** Next update time; evaluator will not do anything when Evaluate gets called before that time */
	float NextUpdate = 0.f;

	/** Indicates that the result of the candidates search is ready and contains some candidates */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	bool bCandidatesFound = false;
	
	/** Indicates that an object has been claimed */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	bool bClaimed = false;

	/** Indicates that the query was able to use annotations on zone graph lanes instead of a spatial query. */
	bool bUsingZoneGraphAnnotations = false;
};
