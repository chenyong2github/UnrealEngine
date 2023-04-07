// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Components/ActorComponent.h"
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

class ULearningAgentsAction;
class ULearningAgentsObservation;

/**
* The agent type is the core class around which the rest of Learning Agents is built. It has a few responsibilities:
*   1) It keeps track of which objects are agents.
*   2) It defines how those agents' observations and actions are implemented.
*   3) It provides methods that need to be called during the inference process of those agents.
*
* To use this class, you need to implement the SetupObservations and SetupActions functions (as well as their
* corresponding SetObservations and SetActions functions), which will define the size of inputs and outputs to your
* policy. Before you can do inference, you need to call SetupAgentType, which will initialize the underlying data
* structure, and you need to call AddAgent for each object you want controlled by this agent type.
*
* If you have multiple different types of objects you want controlled by Learning Agents, you should create
* one agent type per object type, rather than trying to share an agent type.
*
* @see ULearningAgentsTrainer to understand how training works.
*/
UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsType : public UActorComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsType();
	ULearningAgentsType(FVTableHelper& Helper);
	virtual ~ULearningAgentsType();

	/** Sets up the agent ids so that agents can be added prior to calling SetupAgentType. */
	virtual void PostInitProperties() override;

// ----- Setup -----
public:

	/** Initializes this object and runs the setup events for observations and actions. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupAgentType();

	/** Returns true if SetupAgentType has been run successfully; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsSetupPerformed() const;

	/** Returns the maximum number of agents that this agent type is configured to handle. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	int32 GetMaxInstanceNum() const;

// ----- Agent Management -----
public:

	/**
	 * Adds the given object as an agent to this agent type. This can be called before or after SetupAgentType.
	 * @param Agent The object to be added.
	 * @return The agent's newly assigned id.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	int32 AddAgent(UObject* Agent);
	
	/**
	 * Removes the agent with the given id from this agent type.
	 * @param AgentId The id of the agent to remove.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgentById(const int32 AgentId);

	/**
	* Removes the given agent from this agent type. Use RemoveAgentById if you have the id available as this function
	* is slower.
	* @param Agent The agent to be removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(UObject* Agent);

	/**
	 * Gets the agent with the given id. Calling this from blueprint with the appropriate AgentClass will automatically
	 * cast the object to the given type. If not in a blueprint, you should use one of the other GetAgent overloads.
	 * @param AgentId The id of the agent to get.
	 * @param AgentClass The class to cast the agent object to (in blueprint).
	 * @return The agent object.
	 */
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	UObject* GetAgent(const int32 AgentId, const TSubclassOf<UObject> AgentClass);

	/** Returns true if the given object is an agent in this agent type; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(UObject* Agent) const;

	/** Returns true if the given id is used by an agent in this agent type; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgentById(const int32 AgentId) const;

public:

	/** Gets the agent corresponding to the given id. */
	const UObject* GetAgent(const int32 AgentId) const;

	/** Gets the agent corresponding to the given id. */
	UObject* GetAgent(const int32 AgentId);

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

	/** Get a const reference to this agent type's underlying instance data. */
	const TSharedPtr<UE::Learning::FArrayMap>& GetInstanceData() const;

	/** Get a reference to this agent type's observation feature. */
	UE::Learning::FFeatureObject& GetObservationFeature() const;

	/** Get a reference to this agent type's action feature. */
	UE::Learning::FFeatureObject& GetActionFeature() const;

	/** Get a const array view of this agent type's observation objects. */
	TConstArrayView<ULearningAgentsObservation*> GetObservationObjects() const;

	/** Get a const array view of this agent type's action objects. */
	TConstArrayView<ULearningAgentsAction*> GetActionObjects() const;

	/** Get a const array view of this agent type's agent objects. */
	TConstArrayView<TObjectPtr<UObject>> GetAgents() const;

	/** Get an FIndexSet with this agent type's occupied agent ids. */
	UE::Learning::FIndexSet GetOccupiedAgentSet() const;

	/** Get an FIndexSet with this agent type's vacant agent ids. */
	UE::Learning::FIndexSet GetVacantAgentSet() const;

// ----- Private Data -----
private:

	/** Update the agent sets to keep them in sync with the id lists. */
	void UpdateAgentSets();

	TArray<int32> OccupiedAgentIds;
	TArray<int32> VacantAgentIds;
	UE::Learning::FIndexSet OccupiedAgentSet;
	UE::Learning::FIndexSet VacantAgentSet;

	TSharedPtr<UE::Learning::FArrayMap> InstanceData;

	TArray<TSharedRef<UE::Learning::FFeatureObject>, TInlineAllocator<16>> ObservationFeatures;
	TArray<TSharedRef<UE::Learning::FFeatureObject>, TInlineAllocator<16>> ActionFeatures;

	TSharedPtr<UE::Learning::FConcatenateFeature> Observations;	
	TSharedPtr<UE::Learning::FConcatenateFeature> Actions;
	
private:

	/** Maximum number of agent instances. Used to preallocate internal buffers. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxInstanceNum = 1;

	/** True if SetupAgentType has been performed; Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bSetupPerformed = false;

	/** The list of current agents. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<UObject>> Agents;

	/** The list of current observation objects. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsObservation>> ObservationObjects;

	/** The list of current action objects. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsAction>> ActionObjects;
};
