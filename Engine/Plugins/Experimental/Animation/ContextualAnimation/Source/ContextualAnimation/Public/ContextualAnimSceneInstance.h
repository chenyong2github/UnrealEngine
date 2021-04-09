// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameFramework/Actor.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneInstance.generated.h"

struct FAnimMontageInstance;
struct FContextualAnimData;
struct FContextualAnimTrackSettings;
class UContextualAnimSceneInstance;
class UContextualAnimSceneActorComponent;
class UContextualAnimSceneAsset;
class UAnimInstance;
class USkeletalMeshComponent;
class UAnimMontage;
class UWorld;

/** Represent an actor bound to a role in the scene */
USTRUCT(BlueprintType)
struct CONTEXTUALANIMATION_API FContextualAnimSceneActorData
{
	GENERATED_BODY()

		FContextualAnimSceneActorData()
		: Actor(nullptr), AnimDataPtr(nullptr), SettingsPtr(nullptr), AnimStartTime(0.f)
	{}

	FContextualAnimSceneActorData(AActor* InActor, const FContextualAnimData* InAnimData, const FContextualAnimTrackSettings* InSettings, float InAnimStartTime = 0.f)
		: Actor(InActor), AnimDataPtr(InAnimData), SettingsPtr(InSettings), AnimStartTime(InAnimStartTime)
	{}

	/** Return a pointer to the actual actor in the world */
	FORCEINLINE AActor* GetActor() const { return Actor.Get(); }

	FORCEINLINE float GetAnimStartTime() const { return AnimStartTime; }

	FORCEINLINE const FContextualAnimData* GetAnimData() const { return AnimDataPtr; }

	FORCEINLINE const FContextualAnimTrackSettings* GetSettings() const { return SettingsPtr; }

	FORCEINLINE const UContextualAnimSceneInstance* GetSceneInstance() const { return SceneInstancePtr.Get(); }

	/** Return the transform used for alignment for this scene actor */
	FTransform GetTransform() const;

	/** Return the current playback time of the animation this actor is playing */
	float GetAnimTime() const;

	FName GetCurrentSection() const;

	int32 GetCurrentSectionIndex() const;

	FAnimMontageInstance* GetAnimMontageInstance() const;

	const UAnimMontage* GetAnimMontage() const;

	UAnimInstance* GetAnimInstance() const;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const;

	UContextualAnimSceneActorComponent* GetSceneActorComponent() const;

private:

	friend UContextualAnimSceneInstance;

	/** The actual actor in the world */
	TWeakObjectPtr<AActor> Actor = nullptr;

	/** Ptr to the animation data in the scene asset used by this actor */
	const FContextualAnimData* AnimDataPtr = nullptr;

	const FContextualAnimTrackSettings* SettingsPtr = nullptr;

	/** Desired time to start the animation */
	float AnimStartTime = 0.f;

	/** Ptr back to the scene instance we belong to */
	TWeakObjectPtr<const UContextualAnimSceneInstance> SceneInstancePtr = nullptr;
};

/** Delegate to notify external objects when this is scene is completed */
DECLARE_DELEGATE_OneParam(FOnContextualAnimSceneEnded, class UContextualAnimSceneInstance*)

/** Delegate to notify external objects when an actor join this scene */
DECLARE_DELEGATE_TwoParams(FOnContextualAnimSceneActorJoined, class UContextualAnimSceneInstance*, AActor* Actor)

/** Delegate to notify external objects when an actor left this scene */
DECLARE_DELEGATE_TwoParams(FOnContextualAnimSceneActorLeft, class UContextualAnimSceneInstance*, AActor* Actor)

/** Instance of a contextual animation scene */
UCLASS(BlueprintType, Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneInstance : public UObject
{
	GENERATED_BODY()

public:

	/** Scene asset this instance was created from */
	UPROPERTY()
	TObjectPtr<const UContextualAnimSceneAsset> SceneAsset;

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

	const FContextualAnimSceneActorData* FindSceneActorDataForActor(const AActor* Actor) const;

	const FContextualAnimSceneActorData* FindSceneActorDataForRole(const FName& Role) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Instance")
	float GetCurrentSectionTimeLeft() const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Instance")
	bool DidCurrentSectionLoop() const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Instance")
	float GetPositionInCurrentSection() const;

	UFUNCTION(BlueprintNativeEvent, Category = "Contextual Anim|Scene Instance")
	float GetResumePositionForSceneActor(const FContextualAnimSceneActorData& SceneActorData, int32 DesiredSectionIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Instance")
	AActor* GetActorByRole(FName Role) const;

protected:

	/** Tells the scene actor to join the scene (play animation) */
	void Join(FContextualAnimSceneActorData& SceneActorData);

	/** Tells the scene actor to leave the scene (stop animation) */
	void Leave(FContextualAnimSceneActorData& SceneActorData);

	bool TransitionTo(FContextualAnimSceneActorData& SceneActorData, const FName& ToSectionName);

	/** Helper function to set ignore collision between the supplied actor and all the other actors in this scene */
	void SetIgnoreCollisionWithOtherActors(AActor* Actor, bool bValue) const;

	void UpdateTransitions(float DeltaTime);

	UFUNCTION()
	void OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnNotifyBeginReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload);

	UFUNCTION()
	void OnNotifyEndReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload);

public:

	/** Extracts data from a ContextualAnimSceneActorData */
	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Scene Actor Data", meta = (NativeBreakFunc))
	static void BreakContextualAnimSceneActorData(const FContextualAnimSceneActorData& SceneActorData, AActor*& Actor, UAnimMontage*& Montage, float& AnimTime, int32& CurrentSectionIndex, FName& CurrentSectionName);
};