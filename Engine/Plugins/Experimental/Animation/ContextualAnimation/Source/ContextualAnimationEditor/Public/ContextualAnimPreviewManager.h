// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MovieSceneFwd.h"
#include "ContextualAnimPreviewManager.generated.h"

class UWorld;
class FSceneView;
class FPrimitiveDrawInterface;
class ACharacter;
class UAnimMontage;
class UContextualAnimSceneAsset;
struct FContextualAnimData;

USTRUCT()
struct FContextualAnimPreviewActorData
{
	GENERATED_BODY()

	/** Preview actor */
	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;

	/** Role this actor is representing */
	UPROPERTY()
	FName Role = NAME_None;

	/** MovieScene Object Binding's identifier  */
	UPROPERTY()
	FGuid Guid;

	/** Animation this actor is playing */
	UPROPERTY()
	TWeakObjectPtr<UAnimMontage> Animation;

	FORCEINLINE AActor* GetActor() const { return Actor.Get(); }
	FORCEINLINE UAnimMontage* GetAnimation() const { return Animation.Get(); }

	void ResetActorTransform(float Time);
};

UCLASS()
class UContextualAnimPreviewManager : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY()
	TArray<FContextualAnimPreviewActorData> PreviewActorsData;

	UContextualAnimPreviewManager(const FObjectInitializer& ObjectInitializer);

	void Initialize(UWorld& World, const UContextualAnimSceneAsset& SceneAsset);

	virtual UWorld* GetWorld() const override;
	
	void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI);

	const UContextualAnimSceneAsset* GetSceneAsset() const;

	void DisableCollisionBetweenActors();

	void PreviewTimeChanged(EMovieScenePlayerStatus::Type PreviousStatus, float PreviousTime, EMovieScenePlayerStatus::Type CurrentStatus, float CurrentTime, float PlaybackSpeed);

	void MoveForward(float Value);
	
	void MoveRight(float Value);
	
	void MoveToLocation(const FVector& GoalLocation);

	AActor* SpawnPreviewActor(const FName& Role, const FContextualAnimData& Data);

	void AddPreviewActor(AActor& Actor, const FName& Role, const FGuid& Guid, UAnimMontage& Animation);

	FName FindRoleByGuid(const FGuid& Guid) const;

	UAnimMontage* FindAnimationByRole(const FName& Role) const;

	UAnimMontage* FindAnimationByGuid(const FGuid& Guid) const;

	void Reset();

private:

	UPROPERTY()
	TWeakObjectPtr<const UContextualAnimSceneAsset> SceneAssetPtr;

	UPROPERTY()
	TWeakObjectPtr<UWorld> WorldPtr;

	UPROPERTY()
	TWeakObjectPtr<ACharacter> ControlledCharacter;
};