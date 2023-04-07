// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Components/ActorComponent.h"

#include "LearningAgentsController.generated.h"

/**
* A controller provides a method for injecting actions into the learning agents system from some other existing
* behavior, e.g. we may want to gather demonstrations from a human or AI behavior tree controlling our agent(s)
* for imitation learning purposes.
*/
UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsController : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsController();
	ULearningAgentsController(FVTableHelper& Helper);
	virtual ~ULearningAgentsController();

	/** Initializes this object to be used with the given agent type. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupController(ULearningAgentsType* InAgentType);

	/** Returns true if SetupController has been run successfully; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsControllerSetupPerformed() const;

// ----- Agent Management -----
public:

	/**
	 * Adds an agent to this controller.
	 * @param AgentId The id of the agent to be added.
	 * @warning The agent id must exist for the agent type.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(const int32 AgentId);

	/**
	* Removes an agent from this controller.
	* @param AgentId The id of the agent to be removed.
	* @warning The agent id must exist for this controller already.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(const int32 AgentId);

	/** Returns true if the given id has been previously added to this controller; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(const int32 AgentId) const;

	/**
	* Gets the agent type this controller is associated with.
	* @param AgentClass The class to cast the agent type to (in blueprint).
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	ULearningAgentsType* GetAgentType(const TSubclassOf<ULearningAgentsType> AgentClass);

// ----- Actions -----
public:

	/**
	* During this event, you should set the actions of your agents.
	* @param AgentIds The list of agent ids to set actions for.
	* @see LearningAgentsActions.h for the list of available actions.
	* @see ULearningAgentsType::GetAgent to get the agent corresponding to each id.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetActions(const TArray<int32>& AgentIds);

	/**
	* Call this function when it is time to gather all the actions for your agents. This should be called roughly 
	* whenever you are calling ULearningAgentsType::EncodeObservations. This will call this controller's SetActions event.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EncodeActions();

// ----- Private Data -----
private:

	/** The agent type this controller is associated with. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The agent ids this controller is managing. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

private:

	UE::Learning::FIndexSet SelectedAgentsSet;
};
