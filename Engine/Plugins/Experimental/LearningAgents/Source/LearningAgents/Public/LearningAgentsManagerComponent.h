// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "LearningArray.h"
#include "Containers/Array.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsManagerComponent.generated.h"

class ALearningAgentsManager;

/**
 * Base class for components which can be attached to an ALearningAgentsManager.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsManagerComponent();
	ULearningAgentsManagerComponent(FVTableHelper& Helper);
	virtual ~ULearningAgentsManagerComponent();

	/**
	 * Adds an agent to this component.
	 * @param AgentId The id of the agent to be added.
	 * @return True if the agent was added successfully. Otherwise, false.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	virtual bool AddAgent(const int32 AgentId);

	/**
	* Removes an agent from this component.
	* @param AgentId The id of the agent to be removed.
	* @return True if the agent was removed successfully. Otherwise, false.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	virtual bool RemoveAgent(const int32 AgentId);

	/** Returns true if the given id has been previously added to this component; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (AgentId = "-1"))
	bool HasAgent(const int32 AgentId) const;

	/** Returns true if this component has been setup. Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsSetup() const;

// ----- Blueprint Convenience Functions -----
protected:

	/**
	* Gets the agent with the given id from the manager. Calling this from blueprint with the appropriate AgentClass
	* will automatically cast the object to the given type. If not in a blueprint, you should call the manager's
	* GetAgent methods directly.
	* @param AgentId The id of the agent to get.
	* @param AgentClass The class to cast the agent object to (in blueprint).
	* @return The agent object.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (AgentId = "-1", DeterminesOutputType = "AgentClass"))
	UObject* GetAgent(const int32 AgentId, const TSubclassOf<UObject> AgentClass) const;

	/**
	* Gets the agent manager associated with this component.
	* @param AgentClass The class to cast the agent manager to (in blueprint).
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentManagerClass"))
	ALearningAgentsManager* GetAgentManager(const TSubclassOf<ALearningAgentsManager> AgentManagerClass) const;

// ----- Non-blueprint public interface -----
public:

	/** Gets the agent corresponding to the given id. */
	const UObject* GetAgent(const int32 AgentId) const;

	/** Gets the agent corresponding to the given id. */
	UObject* GetAgent(const int32 AgentId);

	ALearningAgentsManager* GetAgentManager() const;

protected:

	/** True if this component has been setup. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	bool bIsSetup = false;

	/** The agent manager associated with this component. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ALearningAgentsManager> AgentManager;

	/** The agent ids added to this component. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<int32> AddedAgentIds;

	UE::Learning::FIndexSet AddedAgentSet;
};
