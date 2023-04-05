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

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsType : public UActorComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsType();
	ULearningAgentsType(FVTableHelper& Helper);
	virtual ~ULearningAgentsType();

	virtual void PostInitProperties() override;

// ----- Setup -----
public:

	/**
	 * Initializes this object and runs the setup functions for observations and actions
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupAgentType();

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const bool IsSetupPerformed() const;

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const int32 GetMaxInstanceNum() const;

// ----- Agent Management -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	int32 AddAgent(UObject* Agent);
	
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgentById(int32 AgentId);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(UObject* Agent);

	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	UObject* GetAgent(int32 AgentId, TSubclassOf<UObject> AgentClass);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(UObject* Agent) const;

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgentById(int32 AgentId) const;

public:

	const UObject* GetAgent(int32 AgentId) const;
	UObject* GetAgent(int32 AgentId);

// ----- Observations -----
public:

	/**
	 * Event where all observations should be set up for this agent type.
	 * @param AgentType This AgentType. This is a convenience for blueprints so you don't have to find the Self pin.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupObservations(ULearningAgentsType* AgentType);

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetObservations(const TArray<int32>& AgentIds);

	void AddObservation(TObjectPtr<ULearningAgentsObservation> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature);

// ----- Actions -----
public:

	/**
	 * Event where all actions should be set up for this agent type.
	 * @param AgentType This AgentType. This is a convenience for blueprints so you don't have to find the Self pin.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupActions(ULearningAgentsType* AgentType);

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void GetActions(const TArray<int32>& AgentIds);

	void AddAction(TObjectPtr<ULearningAgentsAction> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature);

// ----- Encoding / Decoding -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EncodeObservations();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void DecodeActions();

// ----- Non-blueprint public interface -----
public:

	const TSharedPtr<UE::Learning::FArrayMap>& GetInstanceData() const;

	UE::Learning::FFeatureObject& GetObservationFeature() const;
	UE::Learning::FFeatureObject& GetActionFeature() const;
	TConstArrayView<ULearningAgentsObservation*> GetObservationObjects() const;
	TConstArrayView<ULearningAgentsAction*> GetActionObjects() const;

	const TConstArrayView<TObjectPtr<UObject>> GetAgents() const;
	const UE::Learning::FIndexSet GetOccupiedAgentSet() const;
	const UE::Learning::FIndexSet GetVacantAgentSet() const;

// ----- Private Data -----
private:

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

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bSetupPerformed = false;

	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<UObject>> Agents;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsObservation>> ObservationObjects;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsAction>> ActionObjects;
};
