// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/ContainersFwd.h"
#include "LearningArray.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsType.generated.h"

namespace UE::Learning
{
	struct FArrayMap;
	struct FConcatenateFeature;
	struct FFeatureObject;
	struct FNeuralNetwork;
	struct FNeuralNetworkPolicyFunction;
}

struct FDirectoryPath;
class ULearningAgentsAction;
class ULearningAgentsObservation;

USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsTypeSettings
{
	GENERATED_BODY()

public:

	/** Seed for the action noise used by the policy  */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 ActionNoiseSeed = 1234;

	/** Minimum action noise used by the policy */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionNoiseMin = 0.25f;

	/** Maximum action noise used by the policy */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionNoiseMax = 0.25f;
};

UENUM(BlueprintType, Category = "LearningAgents")
enum class ELearningAgentsActivationFunction : uint8
{
	/** ReLU Activation - Fast to train and evaluate but occasionally causes gradient collapse and untrainable networks. */
	ReLU	UMETA(DisplayName = "ReLU"),

	/** ELU Activation - Generally performs better than ReLU and is not prone to gradient collapse but slower to evaluate. */
	ELU		UMETA(DisplayName = "ELU"),

	/** TanH Activation - Smooth activation function that is slower to train and evaluate but sometimes more stable for certain tasks. */
	TanH	UMETA(DisplayName = "TanH"),
};

USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsNetworkSettings
{
	GENERATED_BODY()

public:

	/** Total layers for neural network including input, hidden, and output layers */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "2", UIMin = "2"))
	int32 LayerNum = 3;

	/** Number of neurons in each hidden layer */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 HiddenLayerSize = 128;

	/** Activation function to use on hidden layers */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsActivationFunction ActivationFunction = ELearningAgentsActivationFunction::ELU;
};

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsType : public USceneComponent
{
	GENERATED_BODY()

public:

	DECLARE_MULTICAST_DELEGATE(FOnSetupComplete);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAgentEvent, int32 /*AgentId*/, UObject* /*Agent*/);

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsType();
	ULearningAgentsType(FVTableHelper& Helper);
	virtual ~ULearningAgentsType();

	/** Sets up the agent ids so that we can add/remove agents at any time. */
	virtual void PostInitProperties() override;

// ----- Setup -----
public:

	/**
	 * Initializes this object and runs the setup functions for observations and actions
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupAgentType(
		const FLearningAgentsTypeSettings& Settings = FLearningAgentsTypeSettings(),
		const FLearningAgentsNetworkSettings& NetworkSettings = FLearningAgentsNetworkSettings());

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

	const UObject* GetAgent(int32 AgentId) const;

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

// ----- Inference Process -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadNetwork(const FDirectoryPath& Directory, const FString Filename);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EncodeObservations();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluatePolicy();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void DecodeActions();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BroadcastSetupComplete();

// ----- Non-blueprint public interface -----
public:
	const TSharedPtr<UE::Learning::FArrayMap>& GetInstanceData() const;
	UE::Learning::FFeatureObject& GetObservationFeature() const;
	UE::Learning::FFeatureObject& GetActionFeature() const;
	UE::Learning::FNeuralNetwork& GetNeuralNetwork() const;
	const UE::Learning::FNeuralNetworkPolicyFunction& GetPolicy() const;
	const TConstArrayView<TObjectPtr<UObject>> GetAgents() const;
	const UE::Learning::FIndexSet GetOccupiedAgentSet() const;
	const UE::Learning::FIndexSet GetVacantAgentSet() const;
	FOnSetupComplete& GetOnSetupComplete() { return OnSetupComplete; }
	FOnAgentEvent& GetOnAgentAdded() { return OnAgentAdded; }
	FOnAgentEvent& GetOnAgentRemoved() { return OnAgentRemoved; }
	TConstArrayView<ULearningAgentsObservation*> GetObservationObjects() const { return ObservationObjects; }
	TConstArrayView<ULearningAgentsAction*> GetActionObjects() const { return ActionObjects; }

// ----- Private Data -----
private:

	void UpdateAgentSets();

	TArray<int32> OccupiedAgentIds;
	TArray<int32> VacantAgentIds;
	UE::Learning::FIndexSet OccupiedAgentSet;
	UE::Learning::FIndexSet VacantAgentSet;

	TSharedPtr<UE::Learning::FArrayMap> InstanceData;

	TArray<TSharedRef<UE::Learning::FFeatureObject>, TInlineAllocator<16>> ObservationFeatures;
	TSharedPtr<UE::Learning::FConcatenateFeature> Observations;
	
	TArray<TSharedRef<UE::Learning::FFeatureObject>, TInlineAllocator<16>> ActionFeatures;
	TSharedPtr<UE::Learning::FConcatenateFeature> Actions;

	TSharedPtr<UE::Learning::FNeuralNetwork> NeuralNetwork;
	TUniquePtr<UE::Learning::FNeuralNetworkPolicyFunction> Policy;

	FOnSetupComplete OnSetupComplete;
	FOnAgentEvent OnAgentAdded;
	FOnAgentEvent OnAgentRemoved;

	/** Maximum number of agent instances. Used to preallocate internal buffers. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxInstanceNum = 1;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bSetupPerformed = false;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bNetworkLoaded = false;

	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TArray<TObjectPtr<UObject>> Agents;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsObservation>> ObservationObjects;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsAction>> ActionObjects;
};
