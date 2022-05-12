// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameFramework/Actor.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneInstance.generated.h"

struct FAnimMontageInstance;
struct FContextualAnimTrack;
class UContextualAnimSceneInstance;
class UContextualAnimSceneActorComponent;
class UContextualAnimSceneAsset;
class UAnimInstance;
class USkeletalMeshComponent;
class UAnimMontage;
class UWorld;

/** Delegate to notify external objects when this is scene is completed */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextualAnimSceneEnded, class UContextualAnimSceneInstance*, SceneInstance);

/** Delegate to notify external objects when an actor join this scene */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnContextualAnimSceneActorJoined, class UContextualAnimSceneInstance*, SceneInstance, AActor*, Actor);

/** Delegate to notify external objects when an actor left this scene */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnContextualAnimSceneActorLeft, class UContextualAnimSceneInstance*, SceneInstance, AActor*, Actor);

/** Delegate to notify external objects about anim notify events */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnContextualAnimSceneNotify, class UContextualAnimSceneInstance*, SceneInstance, AActor*, Actor, FName, NotifyName);

/** Instance of a contextual animation scene */
UCLASS(BlueprintType, Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneInstance : public UObject
{
	GENERATED_BODY()

public:

	friend class UContextualAnimManager;
	friend class FContextualAnimViewModel;

	/** Delegate to notify external objects when this is scene is completed */
	UPROPERTY(BlueprintAssignable)
	FOnContextualAnimSceneEnded OnSceneEnded;

	/** Delegate to notify external objects when an actor join */
	UPROPERTY(BlueprintAssignable)
	FOnContextualAnimSceneActorJoined OnActorJoined;

	/** Delegate to notify external objects when an actor leave */
	UPROPERTY(BlueprintAssignable)
	FOnContextualAnimSceneActorLeft OnActorLeft;

	/** Delegate to notify external objects when an animation hits a 'PlayMontageNotify' or 'PlayMontageNotifyWindow' begin */
	UPROPERTY(BlueprintAssignable)
	FOnContextualAnimSceneNotify OnNotifyBegin;

	/** Delegate to notify external objects when an animation hits a 'PlayMontageNotify' or 'PlayMontageNotifyWindow' end */
	UPROPERTY(BlueprintAssignable)
	FOnContextualAnimSceneNotify OnNotifyEnd;

	UContextualAnimSceneInstance(const FObjectInitializer& ObjectInitializer);

	virtual UWorld* GetWorld() const override;

	void Tick(float DeltaTime);

	/** Resolve initial alignment and start playing animation for all actors */
	void Start();

	/** Force all the actors to leave the scene */
	void Stop();

	/** Whether the supplied actor is part of this scene */
	bool IsActorInThisScene(const AActor* Actor) const;

	const UContextualAnimSceneAsset& GetSceneAsset() const { return *SceneAsset; }
	const FContextualAnimSceneBindings& GetBindings() const { return Bindings; };
	FContextualAnimSceneBindings& GetBindings() { return Bindings; };
	const FContextualAnimSceneBinding* FindBindingByActor(const AActor* Actor) const { return Bindings.FindBindingByActor(Actor); }
	const FContextualAnimSceneBinding* FindBindingByRole(const FName& Role) const { return Bindings.FindBindingByRole(Role); }

#if WITH_EDITOR
	const FContextualAnimSceneBinding* FindBindingByGuid(const FGuid& Guid) const { return Bindings.FindBindingByGuid(Guid); }
#endif

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Instance")
	AActor* GetActorByRole(FName Role) const;

protected:

	/** Tells the scene actor to join the scene (play animation) */
	void Join(FContextualAnimSceneBinding& Binding);

	/** Tells the scene actor to leave the scene (stop animation) */
	void Leave(FContextualAnimSceneBinding& Binding);

	bool TransitionTo(FContextualAnimSceneBinding& Binding, const FName& ToSectionName);

	/** Helper function to set ignore collision between the supplied actor and all the other actors in this scene */
	void SetIgnoreCollisionWithOtherActors(AActor* Actor, bool bValue) const;

	UFUNCTION()
	void OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnNotifyBeginReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload);

	UFUNCTION()
	void OnNotifyEndReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload);

private:

	/** Scene asset this instance was created from */
	UPROPERTY()
	TObjectPtr<const UContextualAnimSceneAsset> SceneAsset;

	UPROPERTY()
	FContextualAnimSceneBindings Bindings;

	TArray<TTuple<FName, FTransform>> AlignmentSectionToScenePivotList;
};