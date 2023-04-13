// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsManagerComponent.h"

#include "LearningArray.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsType.generated.h"

namespace UE::Learning
{
	struct FArrayMap;
	struct FConcatenateFeature;
	struct FFeatureObject;
}

class ALearningAgentsManager;
class ULearningAgentsAction;
class ULearningAgentsObservation;

/**
* The agent type defines how agents' observations and actions are implemented and provides methods to be called during
* the inference process of those agents.
*
* To use this class, you need to implement the SetupObservations and SetupActions functions (as well as their
* corresponding SetObservations and SetActions functions), which will define the size of inputs and outputs to your
* policy. Before you can do inference, you need to call SetupAgentType, which will initialize the underlying data
* structure, and you need to call AddAgent for each object you want controlled by this agent type.
*/
UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsType : public ULearningAgentsManagerComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsType();
	ULearningAgentsType(FVTableHelper& Helper);
	virtual ~ULearningAgentsType();

	/** Initializes this object and runs the setup events for observations and actions. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupAgentType(ALearningAgentsManager* InAgentManager);

// ----- Observations -----
public:

	/**
	 * During this event, all observations should be added to this agent type.
	 * @param AgentType This agent type. This is a convenience for blueprints so you don't have to find the Self pin.
	 * @see LearningAgentsObservations.h for the list of available observations.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupObservations(ULearningAgentsType* AgentType);

	/**
	* During this event, all observations should be set for each agent.
	* @param AgentIds The list of agent ids to set observations for.
	* @see LearningAgentsObservations.h for the list of available observations.
	* @see GetAgent to get the agent corresponding to each id.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetObservations(const TArray<int32>& AgentIds);

	/**
	* Used by objects derived from ULearningAgentsObservation to add themselves to this agent type during their
	* creation. You shouldn't need to call this directly.
	*/
	void AddObservation(TObjectPtr<ULearningAgentsObservation> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature);

// ----- Actions -----
public:

	/**
	 * During this event, all actions should be added to this agent type.
	 * @param AgentType This AgentType. This is a convenience for blueprints so you don't have to find the Self pin.
	 * @see LearningAgentsActions.h for the list of available actions.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupActions(ULearningAgentsType* AgentType);

	/**
	* During this event, you should retrieve the actions and apply them to your agents.
	* @param AgentIds The list of agent ids to get actions for.
	* @see LearningAgentsActions.h for the list of available actions.
	* @see GetAgent to get the agent corresponding to each id.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void GetActions(const TArray<int32>& AgentIds);

	/**
	* Used by objects derived from ULearningAgentsAction to add themselves to this agent type during their creation.
	* You shouldn't need to call this directly.
	*/
	void AddAction(TObjectPtr<ULearningAgentsAction> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature);

// ----- Encoding / Decoding -----
public:

	/**
	* Call this function when it is time to gather all the observations for your agents. This can be done each frame or
	* you can consider wiring it up to some kind of meaningful event, e.g. a hypothetical "OnAITurnStarted" if you have
	* a turn-based game. This will call this agent type's SetObservations event.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EncodeObservations();

	/**
	* Call this function when it is time for your agents to take their actions. You most likely want to call this after
	* your policy's EvaluatePolicy function to ensure that you are receiving the latest actions. This will call this
	* agent type's GetActions event.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void DecodeActions();

// ----- Non-blueprint public interface -----
public:

	/** Get a reference to this agent type's observation feature. */
	UE::Learning::FFeatureObject& GetObservationFeature() const;

	/** Get a reference to this agent type's action feature. */
	UE::Learning::FFeatureObject& GetActionFeature() const;

	/** Get a const array view of this agent type's observation objects. */
	TConstArrayView<ULearningAgentsObservation*> GetObservationObjects() const;

	/** Get a const array view of this agent type's action objects. */
	TConstArrayView<ULearningAgentsAction*> GetActionObjects() const;

private:

	/** The list of current observation objects. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsObservation>> ObservationObjects;

	/** The list of current action objects. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsAction>> ActionObjects;

// ----- Private Data -----
private:

	TArray<TSharedRef<UE::Learning::FFeatureObject>, TInlineAllocator<16>> ObservationFeatures;
	TArray<TSharedRef<UE::Learning::FFeatureObject>, TInlineAllocator<16>> ActionFeatures;

	TSharedPtr<UE::Learning::FConcatenateFeature> Observations;
	TSharedPtr<UE::Learning::FConcatenateFeature> Actions;
};
