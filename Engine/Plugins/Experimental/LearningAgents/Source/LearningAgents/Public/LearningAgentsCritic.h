// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsNeuralNetwork.h" // Included for ELearningAgentsActivationFunction
#include "LearningArray.h"

#include "Components/ActorComponent.h"
#include "UObject/ObjectPtr.h"
#include "EngineDefines.h"

#include "LearningAgentsCritic.generated.h"

namespace UE::Learning
{
	struct FNeuralNetwork;
	struct FNeuralNetworkCriticFunction;
}

struct FDirectoryPath;
class ULearningAgentsNeuralNetwork;

/** The configurable settings for a ULearningAgentsCritic. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct LEARNINGAGENTS_API FLearningAgentsCriticSettings
{
	GENERATED_BODY()

public:

	/** Total layers for critic network including input, hidden, and output layers */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "2", UIMin = "2"))
	int32 LayerNum = 3;

	/** Number of neurons in each hidden layer of the critic network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 HiddenLayerSize = 128;

	/** Activation function to use on hidden layers of the critic network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsActivationFunction ActivationFunction = ELearningAgentsActivationFunction::ELU;
};

/** A critic used by some algorithms for training the managed agents. */
UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsCritic : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsCritic();
	ULearningAgentsCritic(FVTableHelper& Helper);
	virtual ~ULearningAgentsCritic();

	/** Initializes this object to be used with the given agent type and critic settings. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupCritic(ULearningAgentsType* InAgentType, const FLearningAgentsCriticSettings& CriticSettings = FLearningAgentsCriticSettings());

	/** Returns true if SetupCritic has been run successfully; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsCriticSetupPerformed() const;

// ----- Agent Management -----
public:

	/**
	 * Adds an agent to this critic.
	 * @param AgentId The id of the agent to be added.
	 * @warning The agent id must exist for the agent type.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(const int32 AgentId);

	/**
	* Removes an agent from this critic.
	* @param AgentId The id of the agent to be removed.
	* @warning The agent id must exist for this critic already.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(const int32 AgentId);

	/** Returns true if the given id has been previously added to this critic; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(const int32 AgentId) const;

	/**
	* Gets the agent type this critic is associated with.
	* @param AgentClass The class to cast the agent type to (in blueprint).
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	ULearningAgentsType* GetAgentType(const TSubclassOf<ULearningAgentsType> AgentClass);

// ----- Load / Save -----
public:

	/**
	* Load a snapshot's weights into this critic.
	* @param Directory The directory the snapshot file is in.
	* @param Filename The filename of the snapshot, including the file extension.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadCriticFromSnapshot(const FDirectoryPath& Directory, const FString Filename);

	/**
	* Save this critic's weights into a snapshot.
	* @param Directory The directory to save the snapshot file in.
	* @param Filename The filename of the snapshot, including the file extension.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SaveCriticToSnapshot(const FDirectoryPath& Directory, const FString Filename) const;

	/**
	* Load a ULearningAgentsNeuralNetwork asset's weights into this critic.
	* @param NeuralNetworkAsset The asset to load from.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadCriticFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset);

	/**
	* Save this critic's weights to a ULearningAgentsNeuralNetwork asset.
	* @param NeuralNetworkAsset The asset to save to.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", Meta=(DevelopmentOnly))
	void SaveCriticToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const;

// ----- Evaluation -----
public:

	/**
	* Calling this function will run the underlying neural network on the previously buffered observations to populate
	* the output value buffer. This should be called after the corresponding agent type's EncodeObservations.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateCritic();

// ----- Non-blueprint public interface -----
public:

	/** Get a reference to this critic's neural network. */
	UE::Learning::FNeuralNetwork& GetCriticNetwork();
	
	/** Get a reference to this critic's critic function object. */
	UE::Learning::FNeuralNetworkCriticFunction& GetCriticObject();

// ----- Private Data -----
private:

	/** The agent type this critic is associated with. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The agent ids this critic is managing. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

	/** True if this critic's SetupCritic has been run; Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bCriticSetupPerformed = false;

	/** The underlying neural network. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsNeuralNetwork> Network;

private:

	TSharedPtr<UE::Learning::FNeuralNetworkCriticFunction> CriticObject;

	UE::Learning::FIndexSet SelectedAgentsSet;

private:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Orange;

	/** Describes this critic to the visual logger for debugging purposes. */
	void VisualLog(const UE::Learning::FIndexSet Instances) const;
#endif
};
