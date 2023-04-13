// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsManagerComponent.h"

#include "LearningAgentsNeuralNetwork.h" // Included for ELearningAgentsActivationFunction
#include "LearningAgentsDebug.h"
#include "LearningArray.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsPolicy.generated.h"

namespace UE::Learning
{
	struct FNeuralNetwork;
	struct FNeuralNetworkPolicyFunction;
}

struct FDirectoryPath;
class ULearningAgentsNeuralNetwork;

/** The configurable settings for a ULearningAgentsPolicy. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct LEARNINGAGENTS_API FLearningAgentsPolicySettings
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

	/** Initial scale of the action noise used by the policy. Should be 1.0 for agents participating in training. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float InitialActionNoiseScale = 1.0f;

	/** Total layers for policy network including input, hidden, and output layers */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "2", UIMin = "2"))
	int32 LayerNum = 3;

	/** Number of neurons in each hidden layer of the policy network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 HiddenLayerSize = 128;

	/** Activation function to use on hidden layers of the policy network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsActivationFunction ActivationFunction = ELearningAgentsActivationFunction::ELU;
};

/** A policy that maps from observations to actions for the managed agents. */
UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsPolicy : public ULearningAgentsManagerComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsPolicy();
	ULearningAgentsPolicy(FVTableHelper& Helper);
	virtual ~ULearningAgentsPolicy();

	/** Initializes this object to be used with the given agent type and policy settings. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupPolicy(ALearningAgentsManager* InAgentManager, ULearningAgentsType* InAgentType, const FLearningAgentsPolicySettings& PolicySettings = FLearningAgentsPolicySettings());

public:

	//~ Begin ULearningAgentsManagerComponent Interface
	virtual bool AddAgent(const int32 AgentId) override;
	//~ End ULearningAgentsManagerComponent Interface

// ----- Load / Save -----
public:

	/**
	* Load a snapshot's weights into this policy.
	* @param Directory The directory the snapshot file is in.
	* @param Filename The filename of the snapshot, including the file extension.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadPolicyFromSnapshot(const FDirectoryPath& Directory, const FString Filename);

	/**
	* Save this policy's weights into a snapshot.
	* @param Directory The directory to save the snapshot file in.
	* @param Filename The filename of the snapshot, including the file extension.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SavePolicyToSnapshot(const FDirectoryPath& Directory, const FString Filename) const;

	/**
	* Load a ULearningAgentsNeuralNetwork asset's weights into this policy.
	* @param NeuralNetworkAsset The asset to load from.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadPolicyFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset);

	/**
	* Save this policy's weights to a ULearningAgentsNeuralNetwork asset.
	* @param NeuralNetworkAsset The asset to save to.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", Meta = (DevelopmentOnly))
	void SavePolicyToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const;

// ----- Evaluation -----
public:

	/**
	* Calling this function will run the underlying neural network on the previously buffered observations to populate
	* the output action buffer. This should be called after the associated agent type's EncodeObservations and
	* before its DecodeActions.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluatePolicy();

	/**
	* Calls EncodeObservations, followed by EvaluatePolicy, followed by DecodeActions
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RunInference();

	/**
	* Get the action noise scale used by an agent.
	*
	* @param AgentId			The AgentId to get the action noise scale for.
	* @return					Action noise scale for that agent.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (AgentId = "-1"))
	float GetAgentActionNoiseScale(const int32 AgentId) const;

	/**
	* Set the action noise scale used by an agent. This can be useful if you have certain
	* agents that are participating in training (and so should have an action noise scale of 1.0)
	* and certain agents which you are testing the inference for (and so will want action noise
	* scale of 0.0)
	*
	* @param AgentId			The AgentId to set the action noise scale for.
	* @param ActionNoiseScale	Action noise scale for that agent.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	void SetAgentActionNoiseScale(const int32 AgentId, const float ActionNoiseScale);

	/**
	* Set the action noise scale used by all agents.
	*
	* @param ActionNoiseScale	Action noise scale to use for all agents
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetAllAgentsActionNoiseScale(const float ActionNoiseScale);

// ----- Non-blueprint public interface -----
public:

	/** Get a reference to this policy's neural network. */
	UE::Learning::FNeuralNetwork& GetPolicyNetwork();

	/** Get a reference to this policy's policy function object. */
	UE::Learning::FNeuralNetworkPolicyFunction& GetPolicyObject();

// ----- Private Data -----
private:

	/** The agent type this policy is associated with. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The underlying neural network. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsNeuralNetwork> Network;

private:

	TSharedPtr<UE::Learning::FNeuralNetworkPolicyFunction> PolicyObject;
	
	float InitialActionNoiseScale = 1.0f;

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Purple;

	/** Describes this policy to the visual logger for debugging purposes. */
	void VisualLog(const UE::Learning::FIndexSet Instances) const;
#endif
};
