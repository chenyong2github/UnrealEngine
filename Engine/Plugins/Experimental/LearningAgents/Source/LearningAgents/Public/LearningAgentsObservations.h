// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "EngineDefines.h"

#include "LearningAgentsObservations.generated.h"

namespace UE::Learning
{
	struct FFeatureObject;
	struct FFloatFeature;
	struct FAngleFeature;
	struct FPlanarDirectionFeature;
	struct FDirectionFeature;
	struct FPlanarPositionFeature;
	struct FPositionFeature;
	struct FPlanarVelocityFeature;
	struct FVelocityFeature;
}

class ULearningAgentsType;

// For functions in this file, we are favoring having more verbose names such as "AddFloatObservation" vs simply "Add" in 
// order to keep it easy to find the correct function in blueprints.

UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTS_API ULearningAgentsObservation : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this observation in the visual log */
	FLinearColor VisualLogColor = FColor::Red;

	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UFloatObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatObservation* AddFloatObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetFloatObservation(int32 AgentId, float Observation);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UVectorObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVectorObservation* AddVectorObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetVectorObservation(int32 AgentId, FVector Observation);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UAngleObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UAngleObservation* AddAngleObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetAngleObservation(int32 AgentId, float Angle, float RelativeAngle = 0.0f);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FAngleFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UPlanarDirectionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarDirectionObservation* AddPlanarDirectionObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 1.0f, FVector Axis0 = FVector::ForwardVector, FVector Axis1 = FVector::RightVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarDirectionObservation(int32 AgentId, FVector Direction, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarDirectionFeature> FeatureObject;
};

UCLASS()
class LEARNINGAGENTS_API UDirectionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UDirectionObservation* AddDirectionObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 100.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetDirectionObservation(int32 AgentId, FVector Direction, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FDirectionFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UPlanarPositionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionObservation* AddPlanarPositionObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 100.0f, FVector Axis0 = FVector::ForwardVector, FVector Axis1 = FVector::RightVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionObservation(int32 AgentId, FVector Position, FVector RelativePosition = FVector::ZeroVector, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionFeature> FeatureObject;
};

UCLASS()
class LEARNINGAGENTS_API UPositionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPositionObservation* AddPositionObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 100.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPositionObservation(int32 AgentId, FVector Position, FVector RelativePosition = FVector::ZeroVector, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPositionFeature> FeatureObject;
};

UCLASS()
class LEARNINGAGENTS_API UPlanarPositionArrayObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionArrayObservation* AddPlanarPositionArrayObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, int32 PositionNum = 0, float Scale = 100.0f, FVector Axis0 = FVector::ForwardVector, FVector Axis1 = FVector::RightVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionArrayObservation(int32 AgentId, const TArray<FVector>& Positions, FVector RelativePosition = FVector::ZeroVector, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionFeature> FeatureObject;
};

UCLASS()
class LEARNINGAGENTS_API UPositionArrayObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPositionArrayObservation* AddPositionArrayObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, int32 PositionNum = 0, float Scale = 100.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPositionArrayObservation(int32 AgentId, const TArray<FVector>& Positions, FVector RelativePosition = FVector::ZeroVector, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPositionFeature> FeatureObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTS_API UPlanarVelocityObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarVelocityObservation* AddPlanarVelocityObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 200.0f, FVector Axis0 = FVector::ForwardVector, FVector Axis1 = FVector::RightVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarVelocityObservation(int32 AgentId, FVector Velocity, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarVelocityFeature> FeatureObject;
};

UCLASS()
class LEARNINGAGENTS_API UVelocityObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVelocityObservation* AddVelocityObservation(ULearningAgentsType* AgentType, FName Name = NAME_None, float Scale = 200.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetVelocityObservation(int32 AgentId, FVector Velocity, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FVelocityFeature> FeatureObject;
};

//------------------------------------------------------------------