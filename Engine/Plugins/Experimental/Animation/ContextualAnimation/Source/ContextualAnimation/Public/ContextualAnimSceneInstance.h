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

/** Represent an actor bound to a role in the scene */
USTRUCT(BlueprintType)
struct CONTEXTUALANIMATION_API FContextualAnimSceneActorData
{
	GENERATED_BODY()

	FContextualAnimSceneActorData(){}
	FContextualAnimSceneActorData(const FName& InRole, int32 InVariantIdx, AActor& InActor, const FContextualAnimTrack& InAnimTrack, float InAnimStartTime = 0.f)
		: Role(InRole), VariantIdx(InVariantIdx), Actor(&InActor), AnimTrackPtr(&InAnimTrack), AnimStartTime(InAnimStartTime)
	{}

	/** Return a pointer to the actual actor in the world */
	FORCEINLINE AActor* GetActor() const { return Actor.Get(); }
	FORCEINLINE FName GetRole() const { return Role; }
	FORCEINLINE int32 GetVariantIdx() const { return VariantIdx; }
	FORCEINLINE float GetAnimStartTime() const { return AnimStartTime; }
	FORCEINLINE const FContextualAnimTrack& GetAnimTrack() const { return *AnimTrackPtr; }
	FORCEINLINE const UContextualAnimSceneInstance& GetSceneInstance() const { return *SceneInstancePtr.Get(); }

	const FContextualAnimIKTargetDefContainer& GetIKTargetDefs() const;

	/** Return the transform used for alignment for this scene actor */
	FTransform GetTransform() const;

	/** Return the current playback time of the animation this actor is playing */
	float GetAnimTime() const;

	FName GetCurrentSection() const;

	int32 GetCurrentSectionIndex() const;

	/** Returns the ActiveMontageInstance or null in the case of static actors */
	FAnimMontageInstance* GetAnimMontageInstance() const;

	UAnimInstance* GetAnimInstance() const;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const;

	UContextualAnimSceneActorComponent* GetSceneActorComponent() const;

private:

	friend UContextualAnimSceneInstance;

	/** Role this actor is representing */
	FName Role = NAME_None;

	int32 VariantIdx = INDEX_NONE;

	/** The actual actor in the world */
	TWeakObjectPtr<AActor> Actor = nullptr;

	/** Ptr to the animation data in the scene asset used by this actor */
	const FContextualAnimTrack* AnimTrackPtr = nullptr;

	/** Desired time to start the animation */
	float AnimStartTime = 0.f;

	/** Ptr back to the scene instance we belong to */
	TWeakObjectPtr<const UContextualAnimSceneInstance> SceneInstancePtr = nullptr;

#if WITH_EDITOR
public:
	/** Guid only used in editor to bind this actor to sequencer */
	FGuid Guid;
#endif

};

USTRUCT(BlueprintType)
struct CONTEXTUALANIMATION_API FContextualAnimSceneBindings
{
	GENERATED_BODY()
	
	const FContextualAnimSceneActorData* FindSceneActorDataByActor(const AActor* Actor) const
	{
		return Actor ? Data.FindByPredicate([Actor](const auto& Item) { return Item.GetActor() == Actor; }) : nullptr;
	}

	const FContextualAnimSceneActorData* FindSceneActorDataByRole(const FName& Role) const
	{
		return Role != NAME_None ? Data.FindByPredicate([&Role](const auto& Item) { return Item.GetRole() == Role; }) : nullptr;
	}

#if WITH_EDITOR
	const FContextualAnimSceneActorData* FindSceneActorDataByGuid(const FGuid& Guid) const
	{
		return Guid.IsValid() ? Data.FindByPredicate([&Guid](const auto& Item) { return Item.Guid == Guid; }) : nullptr;
	}
#endif

	FORCEINLINE int32 Num() const { return Data.Num(); }
	FORCEINLINE int32 Add(const FContextualAnimSceneActorData& NewData) { return Data.Add(NewData); }
	FORCEINLINE void Reset() { return Data.Reset(); }

	FORCEINLINE TArray<FContextualAnimSceneActorData>::RangedForIteratorType      begin() { return Data.begin(); }
	FORCEINLINE TArray<FContextualAnimSceneActorData>::RangedForConstIteratorType begin() const { return Data.begin(); }
	FORCEINLINE TArray<FContextualAnimSceneActorData>::RangedForIteratorType      end() { return Data.end(); }
	FORCEINLINE TArray<FContextualAnimSceneActorData>::RangedForConstIteratorType end() const { return Data.end(); }

private:

	UPROPERTY()
	TArray<FContextualAnimSceneActorData> Data;
};

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
	const FContextualAnimSceneActorData* FindSceneActorDataByActor(const AActor* Actor) const { return Bindings.FindSceneActorDataByActor(Actor); }
	const FContextualAnimSceneActorData* FindSceneActorDataByRole(const FName& Role) const { return Bindings.FindSceneActorDataByRole(Role); }

#if WITH_EDITOR
	const FContextualAnimSceneActorData* FindSceneActorDataByGuid(const FGuid& Guid) const { return Bindings.FindSceneActorDataByGuid(Guid); }
#endif

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Instance")
	AActor* GetActorByRole(FName Role) const;

	/** Extracts data from a ContextualAnimSceneActorData */
	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Scene Actor Data", meta = (NativeBreakFunc))
	static void BreakContextualAnimSceneActorData(const FContextualAnimSceneActorData& SceneActorData, AActor*& Actor, UAnimMontage*& Montage, float& AnimTime, int32& CurrentSectionIndex, FName& CurrentSectionName);

protected:

	/** Tells the scene actor to join the scene (play animation) */
	void Join(FContextualAnimSceneActorData& SceneActorData);

	/** Tells the scene actor to leave the scene (stop animation) */
	void Leave(FContextualAnimSceneActorData& SceneActorData);

	bool TransitionTo(FContextualAnimSceneActorData& SceneActorData, const FName& ToSectionName);

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