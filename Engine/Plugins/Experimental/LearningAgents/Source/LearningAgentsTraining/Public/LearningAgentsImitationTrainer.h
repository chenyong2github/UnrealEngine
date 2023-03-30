// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "LearningAgentsTypeComponent.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "UObject/NameTypes.h"
#include "Tasks/Task.h"
#include "LearningArray.h"

#include "LearningAgentsImitationTrainer.generated.h"

namespace UE::Learning
{
	struct FArrayMap;
	struct FNeuralNetwork;
	struct FNeuralNetworkPolicyFunction;
	struct FSharedMemoryImitationTrainer;
}

class ULearningAgentsType;

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsImitationTrainer : public ULearningAgentsTypeComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsImitationTrainer();
	ULearningAgentsImitationTrainer(FVTableHelper& Helper);
	virtual ~ULearningAgentsImitationTrainer();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const bool IsTraining() const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BeginTraining(TArray<UObject*> Records);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EndTraining();

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const bool IsTrainingComplete() const;

//** ----- Private ----- */
private:

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

	float TrainerTimeout = 10.0f;
};
