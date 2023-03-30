// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"
#include "LearningAgentsTrainer.h" // Required for ELearningAgentsCompletion::Termination

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "EngineDefines.h"

#include "LearningAgentsCompletions.generated.h"

namespace UE::Learning
{
	struct FCompletionObject;
	struct FConditionalCompletion;
	struct FPlanarPositionDifferenceCompletion;
}

// For functions in this file, we are favoring having more verbose names such as "AddConditionalCompletion" vs simply "Add" in 
// order to keep it easy to find the correct function in blueprints.

UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTSTRAINING_API ULearningAgentsCompletion : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this completion in the visual log */
	FLinearColor VisualLogColor = FColor::Yellow;

	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTSTRAINING_API UConditionalCompletion : public ULearningAgentsCompletion
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UConditionalCompletion* AddConditionalCompletion(ULearningAgentsTrainer* AgentTrainer, FName Name = NAME_None, ELearningAgentsCompletion InCompletionMode = ELearningAgentsCompletion::Termination);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetConditionalCompletion(int32 AgentId, bool bIsCompleted);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FConditionalCompletion> CompletionObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTSTRAINING_API UPlanarPositionDifferenceCompletion : public ULearningAgentsCompletion
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionDifferenceCompletion* AddPlanarPositionDifferenceCompletion(
		ULearningAgentsTrainer* AgentTrainer, 
		FName Name = NAME_None,
		float Threshold = 100.0f, 
		ELearningAgentsCompletion InCompletionMode = ELearningAgentsCompletion::Termination,
		FVector Axis0 = FVector::ForwardVector,
		FVector Axis1 = FVector::RightVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionDifferenceCompletion(int32 AgentId, FVector Position0, FVector Position1);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionDifferenceCompletion> CompletionObject;
};
