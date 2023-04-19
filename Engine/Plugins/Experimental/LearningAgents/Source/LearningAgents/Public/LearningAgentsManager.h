// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsManager.generated.h"

namespace UE::Learning
{
	struct FArrayMap;
}

class ULearningAgentsManagerComponent;

/**
* The agent manager is responsible for tracking which game objects are agents. It's the central class around which
* most of Learning Agents is built.
*
* If you have multiple different types of objects you want controlled by Learning Agents, you should consider creating
* one agent manager per object type, rather than trying to share an agent manager.
*/
UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ALearningAgentsManager : public AActor
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ALearningAgentsManager();
	ALearningAgentsManager(FVTableHelper& Helper);
	virtual ~ALearningAgentsManager();

	/** Sets up the agent ids so that agents can be added prior to calling SetupManager. */
	virtual void PostInitProperties() override;

// ----- Setup -----
public:

	/** Initializes this object and runs the setup events for observations and actions. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupManager();

	/** Returns true if SetupManager has been run successfully; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsManagerSetup() const;

	/** Returns the maximum number of agents that this manager is configured to handle. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	int32 GetMaxInstanceNum() const;

// ----- Agent Management -----
public:

	/**
	 * Adds the given object as an agent to this manager. This can be called before or after SetupManager.
	 * @param Agent The object to be added.
	 * @return The agent's newly assigned id.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	int32 AddAgent(UObject* Agent);

	/**
	 * Called whenever a new agent is added to this manager. By default, this will add the agent to each of this
	 * manager's agent components, but you can override this event if you have custom logic. For example, maybe some of
	 * the agents are inference agents only and should not be added to the trainer component. 
	 * @param AgentId The agent's newly assigned id.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void OnAgentAdded(const int32 AgentId);
	
	/**
	 * Removes the agent with the given id from this manager.
	 * @param AgentId The id of the agent to remove.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	void RemoveAgentById(const int32 AgentId);

	/**
	* Removes the given agent from this manager. Use RemoveAgentById if you have the id available as this function
	* must do a linear search to find the agent.
	* @param Agent The agent to be removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(UObject* Agent);

	/**
	 * Called whenever an agent is removed from this manager. By default, this will remove the agent from each of
	 * this manager's agent components, but you can override this event if you have additional logic. 
	 * @param AgentId The removed agent's id.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void OnAgentRemoved(const int32 AgentId);

	/**
	 * Gets the agent with the given id. Calling this from blueprint with the appropriate AgentClass will automatically
	 * cast the object to the given type. If not in a blueprint, you should use one of the other GetAgent overloads.
	 * @param AgentId The id of the agent to get.
	 * @param AgentClass The class to cast the agent object to (in blueprint).
	 * @return The agent object.
	 */
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (AgentId = "-1", DeterminesOutputType = "AgentClass"))
	UObject* GetAgent(const int32 AgentId, const TSubclassOf<UObject> AgentClass) const;

	/**
	 * Gets the agents associated with a set of ids. Calling this from blueprint with the appropriate AgentClass will 
	 * automatically cast the object to the given type.
	 * @param AgentIds The ids of the agents to get.
	 * @param AgentClass The class to cast the agent objects to (in blueprint).
	 * @param OutAgents The output array of agent objects.
	 */
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass", DynamicOutputParam = "OutAgents"))
	void GetAgents(const TArray<int32>& AgentIds, const TSubclassOf<UObject> AgentClass, TArray<UObject*>& OutAgents) const;

	/**
	 * Gets the current added agents. Calling this from blueprint with the appropriate AgentClass will automatically
	 * cast the object to the given type.
	 * @param AgentClass The class to cast the agent objects to (in blueprint).
	 * @param OutAgents The output array of agent objects.
	 */
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass", DynamicOutputParam = "OutAgents"))
	void GetAddedAgents(const TSubclassOf<UObject> AgentClass, TArray<UObject*>& OutAgents) const;

	/**
	* Returns true if the given object is an agent in this manager; Otherwise, false.
	* Use HasAgentById if you have the id available as this function must do a linear search to find the agent.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(UObject* Agent) const;

	/** Returns true if the given id is used by an agent in this manager; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (AgentId = "-1"))
	bool HasAgentById(const int32 AgentId) const;

	/** Get the current added agent ids. */
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents")
	void GetAgentIds(TArray<int32>& OutAgentIds) const;

// ----- Non-blueprint public interface -----
public:

	/** Gets the agent corresponding to the given id. */
	const UObject* GetAgent(const int32 AgentId) const;

	/** Gets the agent corresponding to the given id. */
	UObject* GetAgent(const int32 AgentId);

	/** Get a const reference to this manager's underlying instance data. */
	const TSharedPtr<UE::Learning::FArrayMap>& GetInstanceData() const;

	/** Get a const array view of this manager's agent objects. */
	TConstArrayView<TObjectPtr<UObject>> GetAgents() const;

private:

	/** Maximum number of agents. Used to preallocate internal buffers. Setting this higher will allow more agents but use up more memory. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxInstanceNum = 1;

	/** True if SetupManager has been performed; Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	bool bIsSetup = false;

	/** The list of current agents. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<UObject>> Agents;

	/** Update the agent sets to keep them in sync with the id lists. */
	void UpdateAgentSets();

	TArray<int32> OccupiedAgentIds;
	TArray<int32> VacantAgentIds;
	UE::Learning::FIndexSet OccupiedAgentSet;
	UE::Learning::FIndexSet VacantAgentSet;

	TSharedPtr<UE::Learning::FArrayMap> InstanceData;

	/** The manager components that were found during setup. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsManagerComponent>> CachedManagerComponents;
};
