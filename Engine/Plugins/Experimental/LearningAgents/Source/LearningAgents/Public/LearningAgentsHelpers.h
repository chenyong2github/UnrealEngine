// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsDebug.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "Components/SplineComponent.h" // Required for ESplineCoordinateSpace::World

#include "LearningAgentsHelpers.generated.h"

class ULearningAgentsManagerComponent;
class UMeshComponent;

//------------------------------------------------------------------

/**
* The base class for all helpers. Helpers are additional objects that can be used in getting or setting observations,
* actions, rewards, and completions.
*/
UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTS_API ULearningAgentsHelper : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsManagerComponent> ManagerComponent;

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Color used to draw this helper in the visual log */
	FLinearColor VisualLogColor = FColor::Magenta;
#endif
};

//------------------------------------------------------------------

/** A helper for computing various properties from a SplineComponent. */
UCLASS()
class LEARNINGAGENTS_API USplineComponentHelper : public ULearningAgentsHelper
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static USplineComponentHelper* AddSplineComponentHelper(ULearningAgentsManagerComponent* InManagerComponent, const FName Name = NAME_None);

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	float GetProportionAlongSpline(const int32 AgentId, const USplineComponent* SplineComponent, const float DistanceAlongSpline) const;

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	float GetProportionAlongSplineAsAngle(const int32 AgentId, const USplineComponent* SplineComponent, const float DistanceAlongSpline) const;

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetPositionsAlongSpline(
		TArray<FVector>& OutPositions,
		const int32 AgentId,
		const USplineComponent* SplineComponent,
		const int32 PositionNum,
		const float StartDistanceAlongSpline,
		const float StopDistanceAlongSpline,
		const ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World) const;

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	float GetVelocityAlongSpline(
		const int32 AgentId,
		const USplineComponent* SplineComponent,
		const FVector Position,
		const FVector Velocity,
		const float FiniteDifferenceDelta = 10.0f,
		const ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World) const;
};

//------------------------------------------------------------------

/** A helper for projecting onto surfaces. */
UCLASS()
class LEARNINGAGENTS_API UProjectionHelper : public ULearningAgentsHelper
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UProjectionHelper* AddProjectionHelper(ULearningAgentsManagerComponent* InManagerComponent, const FName Name = NAME_None);

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	FTransform ProjectTransformOntoGroundPlane(const int32 AgentId, const FTransform Transform, const FVector LocalForwardVector = FVector::ForwardVector) const;

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void ProjectPositionRotationOntoGroundPlane(
		FVector& OutPosition, 
		FRotator& OutRotation, 
		const int32 AgentId, 
		const FVector InPosition, 
		const FRotator InRotation, 
		const FVector LocalForwardVector = FVector::ForwardVector) const;
};

//------------------------------------------------------------------

/** A helper for getting various properties from a MeshComponent. */
UCLASS()
class LEARNINGAGENTS_API UMeshComponentHelper : public ULearningAgentsHelper
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UMeshComponentHelper* AddMeshComponentHelper(ULearningAgentsManagerComponent* InManagerComponent, const FName Name = NAME_None);

	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetMeshBonePositions(TArray<FVector>& OutBonePositions, const int32 AgentId, const UMeshComponent* MeshComponent, const TArray<FName>& BoneNames) const;
};

//------------------------------------------------------------------

/** A helper for performing various kinds of ray cast. */
UCLASS()
class LEARNINGAGENTS_API URayCastHelper : public ULearningAgentsHelper
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static URayCastHelper* AddRayCastHelper(ULearningAgentsManagerComponent* InManagerComponent, const FName Name = NAME_None);

	UFUNCTION(BlueprintPure=false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void RayCastGridHeights(
		TArray<float>& OutHeights,
		const int32 AgentId,
		const FVector Position,
		const FRotator Rotation,
		const int32 RowNum = 5,
		const int32 ColNum = 5,
		const float RowWidth = 1000.0f,
		const float ColWidth = 1000.0f,
		const float MaxHeight = 10000.0f,
		const float MinHeight = -10000.0f,
		const ECollisionChannel CollisionChannel = ECollisionChannel::ECC_WorldStatic) const;
};
