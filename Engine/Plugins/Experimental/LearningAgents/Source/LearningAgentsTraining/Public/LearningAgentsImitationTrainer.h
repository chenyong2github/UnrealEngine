// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsTrainer.h" // Included for ELearningAgentsTrainerDevice
#include "LearningArray.h"

#include "Templates/SharedPointer.h"
#include "UObject/ObjectPtr.h"
#include "Tasks/Task.h"

#include "LearningAgentsImitationTrainer.generated.h"

namespace UE::Learning
{
	struct FSharedMemoryImitationTrainer;
}

class ULearningAgentsType;
class ULearningAgentsPolicy;

/** The configurable settings for the training process. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsImitationTrainerTrainingSettings
{
	GENERATED_BODY()

public:

	/** The number of iterations to run before training is complete. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfIterations = 1000000;

	/** Learning rate of the policy network. Typical values are between 0.001f and 0.0001f. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LearningRate = 0.0001f;

	/** Ratio by which to decay the learning rate every 1000 iterations. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LearningRateDecay = 0.99f;

	/**
	* Amount of weight decay to apply to the network. Larger values encourage network weights to be smaller but too
	* large a value can cause the network weights to collapse to all zeros.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WeightDecay = 0.001f;

	/**
	* Batch size to use for training. Smaller values tend to produce better results at the cost of slowing down
	* training.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	uint32 BatchSize = 128;

	/** The seed used for any random sampling the trainer will perform, e.g. for weight initialization. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 RandomSeed = 1234;

	/** The device to train on. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsTrainerDevice Device = ELearningAgentsTrainerDevice::CPU;

	/** If true, TensorBoard logs will be emitted to Intermediate\TensorBoard. Otherwise, no logs will be emitted. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseTensorboard = false;
};

/**
* The ULearningAgentsImitationTrainer enable imitation learning, i.e. learning from human/AI demonstrations.
* Imitation training is typically much faster than reinforcement learning, but requires gathering large amounts of
* data in order to generalize. This can be used to initialize a reinforcement learning policy to speed up initial
* exploration.
* @see ULearningAgentsType to understand how observations and actions work.
* @see ULearningAgentsRecorder to understand how to make new recordings.
* @see ULearningAgentsDataStorage to understand how to retrieve previous recordings.
*/
UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsImitationTrainer : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsImitationTrainer();
	ULearningAgentsImitationTrainer(FVTableHelper& Helper);
	virtual ~ULearningAgentsImitationTrainer();

	/** Will automatically call EndTraining if training is still in-progress when play is ending. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	* Begins the training process with the provided settings.
	* @param InPolicy The policy to train.
	* @param Records The data to train on.
	* @param TrainingSettings The settings for this training run.
	* @param bReinitializePolicyNetwork If true, reinitialize the policy. Set this to false if your policy is pre-trained.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BeginTraining(
		ULearningAgentsPolicy* InPolicy,
		const TArray<ULearningAgentsRecord*>& Records,
		const FLearningAgentsImitationTrainerTrainingSettings& TrainingSettings = FLearningAgentsImitationTrainerTrainingSettings(),
		const bool bReinitializePolicyNetwork = true);

	/** Stops the training process. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EndTraining();

	/** Returns true if the trainer is currently training; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsTraining() const;

	/** Returns true if the previously launched training has completed; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsTrainingComplete() const;

// ----- Private Data -----
private:

	/** The policy being trained. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsPolicy> Policy;

	/** True if training is currently in-progress. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsTraining = false;

	/** True if training is completed. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsTrainingComplete = false;

	TLearningArray<2, float> RecordedObservations;
	TLearningArray<2, float> RecordedActions;

	TUniquePtr<UE::Learning::FSharedMemoryImitationTrainer> ImitationTrainer;
	UE::Tasks::FTask ImitationTrainingTask;
	FRWLock NetworkLock;

	TAtomic<bool> bRequestImitationTrainingStop = false;
};
