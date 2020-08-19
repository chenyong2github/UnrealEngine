// Copyright Epic Games, Inc. All Rights Reserved

#pragma once
#include "BaseMovementComponent.h"
#include "MockRootMotionSimulation.h"

#include "MockRootMotionComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UMockRootMotionSourceDataAsset;
class UCurveVector;

// This component acts as the Driver for the FMockRootMotionSimulation
// It is essentially a standin for the movement component, and would be replaced by "new movement system" component.
// If we support "root motion without movement component" then this could either be that component, or possibly
// built into or inherit from a USkeletalMeshComponent.
//
// The main thing this provides is:
//		-Interface for initiating root motions through the NP system (via client Input and via server "OOB" writes)
//		-FinalizeFrame: take the output of the NP simulation and push it to the movement/animation components
//		=Place holder for UMockRootMotionSourceDataAsset (the temp thing that maps our RootMotionSourceIDs -> actual sources)

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UMockRootMotionComponent : public UBaseMovementComponent
{
public:

	GENERATED_BODY()

	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	void InitializeSimulationState(FMockRootMotionSyncState* SyncState, FMockRootMotionAuxState* AuxState);
	void ProduceInput(const int32 SimTimeMS, FMockRootMotionInputCmd* Cmd);
	void RestoreFrame(const FMockRootMotionSyncState* SyncState, const FMockRootMotionAuxState* AuxState);
	void FinalizeFrame(const FMockRootMotionSyncState* SyncState, const FMockRootMotionAuxState* AuxState);
	
	// Callable by controlling client only. Queues root motion source to be played. By UAnimMontage in RootMotionSourceDataAsset.
	UFUNCTION(BlueprintCallable, Category=Input)
	void Input_PlayRootMotionByMontage(UAnimMontage* Montage);

	// Callable by controlling client only. Queues root motion source to be played. By UCurveVector in RootMotionSourceDataAsset.
	// Scale is an optional scaler for the curve
	UFUNCTION(BlueprintCallable, Category=Input)
	void Input_PlayRootMotionByCurve(UCurveVector* CurveVector, FVector Scale = FVector(1.f));

	// Callable by authority. Plays "out of band" animation: e.g, directly sets the RootMotionSourceID on the sync state, rather than the pending InputCmd.
	// This is analogous to outside code teleporting the actor (outside of the core simulation function)
	UFUNCTION(BlueprintCallable, Category=Animation)
	void PlayRootMotionMontage(UAnimMontage* Montage, float PlayRate);

protected:

	void FindAndCacheAnimInstance();

	// Data asset that holds all RootMotinoSource mappings. This could have also been a global thing that all actors share.
	// (though that would create the problem of not all sources are compatible with all meshes)
	// This should be viewed as a holdover until we settle on a place for how we want to manage root motion sources.
	UPROPERTY(EditDefaultsOnly, Category="Root Motion")
	UMockRootMotionSourceDataAsset* RootMotionSourceDataAsset;
	
	// Next local InputCmd that will be submitted. This is a simple way of getting input to the system
	FMockRootMotionInputCmd PendingInputCmd;

	void InitializeNetworkPredictionProxy() override;
	TUniquePtr<FMockRootMotionSimulation> OwnedMockRootMotionSimulation;

	UAnimInstance* AnimInstance = nullptr;

private:

	template<typename AssetType>
	int32 PlayRootMotionByAssetType(AssetType* Asset);
};