// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneActorComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class AActor;
class UContextualAnimSceneInstance;
struct FContextualAnimSceneActorData;

USTRUCT(BlueprintType)
struct FContextualAnimDebugParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	TWeakObjectPtr<AActor> TestActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (UIMin = 0, ClampMin = 0))
	float DrawAlignmentTransformAtTime = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FContextualAnimSceneActorCompDelegate, class UContextualAnimSceneActorComponent*, SceneActorComponent);

UCLASS(meta = (BlueprintSpawnableComponent))
class CONTEXTUALANIMATION_API UContextualAnimSceneActorComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:

	/** Event that happens when the actor owner of this component joins an scene */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FContextualAnimSceneActorCompDelegate OnJoinedSceneDelegate;

	/** Event that happens when the actor owner of this component leave an scene */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FContextualAnimSceneActorCompDelegate OnLeftSceneDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	class UContextualAnimSceneAsset* SceneAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebug;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ShowOnlyInnerProperties, EditCondition = "bEnableDebug"))
	FContextualAnimDebugParams DebugParams;

	UContextualAnimSceneActorComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	/** Called from the scene instance when the actor owner of this component joins an scene */
	void OnJoinedScene(const FContextualAnimSceneActorData* SceneActorData);
	
	/** Called from the scene instance when the actor owner of this component leave an scene */
	void OnLeftScene(const FContextualAnimSceneActorData* SceneActorData);

	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Scene Actor Component")
	const TArray<FContextualAnimIKTarget>& GetIKTargets() const { return IKTargets; }

	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Scene Actor Component")
	const FContextualAnimIKTarget& GetIKTargetByGoalName(FName GoalName) const;

protected:

	/** Ptr back to the scene actor data that represent us in the scene instance we are part of */
	const FContextualAnimSceneActorData* SceneActorDataPtr = nullptr;

	/** List of IKTarget for this frame */
	UPROPERTY()
	TArray<FContextualAnimIKTarget> IKTargets;

	void UpdateIKTargets();

	/** 
	 * Event called right before owner's mesh ticks the pose when we are in a scene instance and IK Targets are required. 
	 * Used to update IK Targets before animation need them 
	 */
	UFUNCTION()
	void OnTickPose(class USkinnedMeshComponent* SkinnedMeshComponent, float DeltaTime, bool bNeedsValidRootMotion);

private:

	bool bRegistered = false;
};
