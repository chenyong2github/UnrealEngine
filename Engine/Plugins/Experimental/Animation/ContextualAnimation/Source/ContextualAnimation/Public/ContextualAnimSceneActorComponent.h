// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContextualAnimTypes.h"
#include "ActorComponents/IKRigInterface.h"
#include "Components/PrimitiveComponent.h"
#include "ContextualAnimSceneActorComponent.generated.h"

class AActor;
class FPrimitiveSceneProxy;
class UAnimInstance;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FContextualAnimSceneActorCompDelegate, class UContextualAnimSceneActorComponent*, SceneActorComponent);

UCLASS(meta = (BlueprintSpawnableComponent))
class CONTEXTUALANIMATION_API UContextualAnimSceneActorComponent : public UPrimitiveComponent, public IIKGoalCreatorInterface
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
	TObjectPtr<class UContextualAnimSceneAsset> SceneAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebug;

	UContextualAnimSceneActorComponent(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual void AddIKGoals_Implementation(TMap<FName, FIKRigGoal>& OutGoals) override;

	/** Called when the actor owner of this component joins an scene */
	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Actor Component")
	void OnJoinedScene(const FContextualAnimSceneBindings& InBindings);
	
	/** Called from the scene instance when the actor owner of this component leave an scene */
	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Actor Component")
	void OnLeftScene();

	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Scene Actor Component")
	const TArray<FContextualAnimIKTarget>& GetIKTargets() const { return IKTargets; }

	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Scene Actor Component")
	const FContextualAnimIKTarget& GetIKTargetByGoalName(FName GoalName) const;

protected:

	/** 
	 * Replicated version of the bindings for the interaction we are currently playing.
	 * @TODO: Right now we are replicating the same set of bindings from each actor in the interaction. 
	 * This is not a big structure so it might be ok, but as an optimization we could replicate it only from the leader of the interaction and set it on the other actors in the OnRep notify
	 */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_Bindings)
	FContextualAnimSceneBindings RepBindings;

	/** 
	 * Local copy of the bindings for the interaction we are currently playing.
	 * Used to update IK, keep montage in sync and disable/enable collision between actors on simulated proxies too 
	 */
	UPROPERTY(Transient)
	FContextualAnimSceneBindings LocalBindings;

	/** List of IKTarget for this frame */
	UPROPERTY(Transient)
	TArray<FContextualAnimIKTarget> IKTargets;

	/** Value of AllowPhysicsRotationDuringAnimRootMotion property for this actor before we join an scene, so we can restore it once the interaction ends */
	UPROPERTY(Transient)
	bool bAllowPhysicsRotationDuringAnimRootMotionBackup = false;

	void UpdateIKTargets();

	/** 
	 * Event called right before owner's mesh ticks the pose when we are in a scene instance and IK Targets are required. 
	 * Used to update IK Targets before animation need them 
	 */
	UFUNCTION()
	void OnTickPose(class USkinnedMeshComponent* SkinnedMeshComponent, float DeltaTime, bool bNeedsValidRootMotion);

	UFUNCTION()
	void OnRep_Bindings();

	void SetIgnoreCollisionWithOtherActors(bool bValue) const;

private:

	bool bRegistered = false;
};
