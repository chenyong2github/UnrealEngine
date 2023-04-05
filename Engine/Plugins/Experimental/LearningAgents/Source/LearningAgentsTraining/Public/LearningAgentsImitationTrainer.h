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

//------------------------------------------------------------------

USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsImitationTrainerTrainingSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfIterations = 1000000;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LearningRate = 0.0001f;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LearningRateDecay = 0.99f;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WeightDecay = 0.001f;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	uint32 BatchSize = 128;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 RandomSeed = 1234;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsTrainerDevice Device = ELearningAgentsTrainerDevice::CPU;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseTensorboard = false;
};

//------------------------------------------------------------------

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

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BeginTraining(
		ULearningAgentsPolicy* InPolicy,
		const TArray<ULearningAgentsRecord*>& Records,
		const FLearningAgentsImitationTrainerTrainingSettings& TrainingSettings = FLearningAgentsImitationTrainerTrainingSettings(),
		const bool bReinitializePolicyNetwork = true);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EndTraining();

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsTraining() const;

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsTrainingComplete() const;

// ----- Private Data -----
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsPolicy> Policy;

	/** True if training is currently in-progress. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsTraining = false;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsTrainingComplete = false;

	TLearningArray<2, float> RecordedObservations;
	TLearningArray<2, float> RecordedActions;

	TUniquePtr<UE::Learning::FSharedMemoryImitationTrainer> ImitationTrainer;
	UE::Tasks::FTask ImitationTrainingTask;
	FRWLock NetworkLock;

	TAtomic<bool> bRequestImitationTrainingStop = false;
};
