// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ContextualAnimSceneInstance.generated.h"

struct FContextualAnimData;
class UContextualAnimSceneAssetBase;
class UAnimMontage;
class UWorld;

/** Represent an actor bound to a role in the scene */
USTRUCT()
struct CONTEXTUALANIMATION_API FContextualAnimSceneActorData
{
	GENERATED_BODY()

	/** The actual actor in the world */
	TWeakObjectPtr<AActor> Actor = nullptr;

	/** Ptr to the animation data in the scene asset used by this actor */
	const FContextualAnimData* AnimDataPtr = nullptr;

	/** Desired time to start the animation */
	float AnimStartTime = 0.f;

	FContextualAnimSceneActorData() : Actor(nullptr), AnimDataPtr(nullptr), AnimStartTime(0.f) {}

	FContextualAnimSceneActorData(AActor* InActor, const FContextualAnimData* InAnimData, float InAnimStartTime = 0.f)
		: Actor(InActor), AnimDataPtr(InAnimData), AnimStartTime(InAnimStartTime)
	{}

	/** Return a pointer to the actual actor in the world */
	FORCEINLINE AActor* GetActor() const { return Actor.Get(); }

	/** Return the transform used for alignment for this scene actor */
	FTransform GetTransform() const;

	/** Return the current playback time of the animation this actor is playing */
	float GetAnimTime() const;
};

/** Delegate to notify external objects when this is scene is completed */
DECLARE_DELEGATE_OneParam(FOnContextualAnimSceneEnded, class UContextualAnimSceneInstance*)

/** Delegate to notify external objects when an actor join this scene */
DECLARE_DELEGATE_TwoParams(FOnContextualAnimSceneActorJoined, class UContextualAnimSceneInstance*, AActor* Actor)

/** Delegate to notify external objects when an actor left this scene */
DECLARE_DELEGATE_TwoParams(FOnContextualAnimSceneActorLeft, class UContextualAnimSceneInstance*, AActor* Actor)

/** Instance of a contextual animation scene */
UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimSceneInstance : public UObject
{
	GENERATED_BODY()

public:

	/** Scene asset this instance was created from */
	UPROPERTY()
	TObjectPtr<const UContextualAnimSceneAssetBase> SceneAsset;

	/** Map of roles to scene actor */
	UPROPERTY()
	TMap<FName, FContextualAnimSceneActorData> SceneActorMap;

	/** List of alignment section to scene pivot */
	TArray<TTuple<FName, FTransform>> AlignmentSectionToScenePivotList;

	/** Delegate to notify external objects when this is scene is completed */
	FOnContextualAnimSceneEnded OnSceneEnded;

	/** Delegate to notify external objects when an actor join */
	FOnContextualAnimSceneActorJoined OnActorJoined;

	/** Delegate to notify external objects when an actor leave */
	FOnContextualAnimSceneActorLeft OnActorLeft;

	UContextualAnimSceneInstance(const FObjectInitializer& ObjectInitializer);

	virtual UWorld* GetWorld() const override;

	void Tick(float DeltaTime);

	/** Resolve initial alignment and start playing animation for all actors */
	void Start();

	/** Force all the actors to leave the scene */
	void Stop();

	/** Whether the supplied actor is part of this scene */
	bool IsActorInThisScene(const AActor* Actor) const;

protected:

	/** Tells the scene actor to join the scene (play animation) */
	void Join(FContextualAnimSceneActorData& Data);

	/** Tells the scene actor to leave the scene (stop animation) */
	void Leave(FContextualAnimSceneActorData& Data);

	/** Helper function to set ignore collision between the supplied actor and all the other actors in this scene */
	void SetIgnoreCollisionWithOtherActors(AActor* Actor, bool bValue) const;

	UFUNCTION()
	void OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnNotifyBeginReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload);

	UFUNCTION()
	void OnNotifyEndReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload);
};