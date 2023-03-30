// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "EngineDefines.h"

#include "LearningAgentsActions.generated.h"

namespace UE::Learning
{
	struct FFeatureObject;
	struct FFloatFeature;
	struct FRotationVectorFeature;
}

class ULearningAgentsType;

// For functions in this file, we are favoring having more verbose names such as "AddFloatAction" vs simply "Add" in 
// order to keep it easy to find the correct function in blueprints.

UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTS_API ULearningAgentsAction : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Blue;

	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UFloatAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatAction* AddFloatAction(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 1.0f);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	float GetFloatAction(int32 AgentId);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetFloatAction(int32 AgentId, float Value);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UVectorAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVectorAction* AddVectorAction(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 1.0f);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	FVector GetVectorAction(int32 AgentId);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetVectorAction(int32 AgentId, FVector Action);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API URotationVectorArrayAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static URotationVectorArrayAction* AddRotationVectorArrayAction(ULearningAgentsType* AgentType, FName Name = NAME_None, int32 RotationVectorNum = 0, float Scale = 180.0f);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	void GetRotationVectorArrayAction(int32 AgentId, TArray<FVector>& OutRotationVectors);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	void GetRotationVectorArrayActionAsQuats(int32 AgentId, TArray<FQuat>& OutRotations);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	void GetRotationVectorArrayActionAsRotators(int32 AgentId, TArray<FRotator>& OutRotations);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FRotationVectorFeature> FeatureObject;
};

//------------------------------------------------------------------