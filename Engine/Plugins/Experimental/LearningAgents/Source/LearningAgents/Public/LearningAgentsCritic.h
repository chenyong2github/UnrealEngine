// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsManagerComponent.h"

#include "LearningAgentsNeuralNetwork.h" // Included for ELearningAgentsActivationFunction
#include "LearningAgentsDebug.h"
#include "LearningArray.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsCritic.generated.h"

namespace UE::Learning
{
	struct FNeuralNetwork;
	struct FNeuralNetworkCriticFunction;
}

struct FFilePath;
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
class LEARNINGAGENTS_API ULearningAgentsCritic : public ULearningAgentsManagerComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsCritic();
	ULearningAgentsCritic(FVTableHelper& Helper);
	virtual ~ULearningAgentsCritic();

	/** Initializes this object to be used with the given agent interactor and critic settings. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupCritic(
		ALearningAgentsManager* InAgentManager,
		ULearningAgentsInteractor* InInteractor,
		const FLearningAgentsCriticSettings& CriticSettings = FLearningAgentsCriticSettings());

// ----- Load / Save -----
public:

	/**
	* Load a snapshot's weights into this critic.
	* @param File The snapshot file.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (RelativePath))
	void LoadCriticFromSnapshot(const FFilePath& File);

	/**
	* Save this critic's weights into a snapshot.
	* @param File The snapshot file.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (RelativePath))
	void SaveCriticToSnapshot(const FFilePath& File) const;

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
	* the output value buffer. This should be called after the corresponding agent interactor's EncodeObservations.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateCritic();

	/**
	* Gets an estimate of the average discounted return expected by an agent according to the critic. I.E. the total
	* sum of future rewards, scaled by the discount factor that was used during training. This value can be useful if 
	* you want to make some decision based on how well the agent thinks they are doing at achieving their task. This 
	* should be called only after EvaluateCritic.
	* 
	* @param AgentId	The AgentId to look-up the estimated discounted return for
	* @returns			The estimated average discounted return according to the critic
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	float GetEstimatedDiscountedReturn(const int32 AgentId) const;

// ----- Non-blueprint public interface -----
public:

	/** Get a reference to this critic's neural network. */
	UE::Learning::FNeuralNetwork& GetCriticNetwork();
	
	/** Get a reference to this critic's critic function object. */
	UE::Learning::FNeuralNetworkCriticFunction& GetCriticObject();

// ----- Private Data -----
private:

	/** The agent interactor this critic is associated with. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsInteractor> Interactor;

	/** The underlying neural network. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsNeuralNetwork> Network;

private:

	TSharedPtr<UE::Learning::FNeuralNetworkCriticFunction> CriticObject;

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Orange;

	/** Describes this critic to the visual logger for debugging purposes. */
	void VisualLog(const UE::Learning::FIndexSet Instances) const;
#endif
};
