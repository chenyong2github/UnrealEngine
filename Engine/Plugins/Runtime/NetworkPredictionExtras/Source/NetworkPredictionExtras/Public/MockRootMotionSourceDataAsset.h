// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "Engine/EngineTypes.h"
#include "Engine/DataAsset.h"

#include "MockRootMotionSimulation.h"
#include "MockRootMotionSourceDataAsset.generated.h"

class UAnimMontage;
class UCurveVector;

USTRUCT(meta=(ShowOnlyInnerProperties))
struct FMockRootMotionSourceContainer_Montage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = RootMotion)
	TArray<UAnimMontage*> Montages;
};

USTRUCT(meta=(ShowOnlyInnerProperties))
struct FMockRootMotionSourceContainer_Curves
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = RootMotion)
	TArray<UCurveVector*> Curves;
};

// DataAsset that implements IMockRootMotionSourceMap. Doing this as a Dataasset is not how we would want the final 
//	version to look but will serve to quickly stand things up.
//
// The advantages here are:
//	-We can populate the data asset with all root motion montages at editor time.
//	-Everyone (all clients, all actors) can agree on a RootMotionSourceID -> montage mapping
//		-We want to avoid NetSerializing UAnimMontage* right now, because this limits bunch sharing between clients.
//		-The layer of indirection gives us flexibility to map RootMotionSourceIDs to non montages like curves.
//
// The disadvanges are that its a data asset that we need to manually manage right now. It also won't scale well
// - we don't want to require a binary asset that has to track every root motion source in the game.
//
// But it will probably make sense to have some centralized thing/subsystem that can map the RootMotionSourceID
// to the actual root motion logic. Perhaps a global RootMotion mapping object that can hold global / anim graph
// independent root motion sources + a path into the actual anim instance where the anim graph itself can define
// root motion sources?
//
// To Consider:
//	-How much of 'advance PlayPosition/PlayRate and do clamp/loop logic' can be shared between the various source types?
//	-Consider parameterization: everything right now is very hard coded but we would like to be able to:
//		-Statically parameterize: take a single Montage/Curve and have statically scaled versions of them
//			-this would look like multiple entries in the data asse with the same backing asset, but with some new scaling proeprties
//			-In this case, you would need some kind of tag/name to identify root motion sources (E.g, cant 'play montage by asset' but instead 'play montage by tag/name')
//		-Dynamic parameterization: the code that is starting the root motion, whether its sim code or outside OOB code, may want to add some
//			parameterization on top fo the static data. In a send PlayRate works like this.
//		-Finding a uniform way of doing this across all source types, in a way that is effecient and not too boiler-platey would
//			be very powerful!
//			
//

UCLASS()
class UMockRootMotionSourceDataAsset : public UDataAsset, public IMockRootMotionSourceMap
{
public:
	GENERATED_BODY()

	// ---------------------------------------------------------------------
	//	Montage based RootMotionSource 
	// ---------------------------------------------------------------------

	// All simple montage based RootMotion sources
	UPROPERTY(EditAnywhere, Category = RootMotion, meta=(ShowOnlyInnerProperties))
	FMockRootMotionSourceContainer_Montage Montages;

	int32 FindRootMotionSourceID(UAnimMontage* Montage);

	// ---------------------------------------------------------------------
	//	Curves
	// ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = RootMotion, meta=(ShowOnlyInnerProperties))
	FMockRootMotionSourceContainer_Curves Curves;

	int32 FindRootMotionSourceID(UCurveVector* Curve);

	// ---------------------------------------------------------------------
	// Utility
	// ---------------------------------------------------------------------
	bool IsValidSourceID(int32 RootMotionSourceID) const;

	// ---------------------------------------------------------------------
	// IMockRootMotionSourceMap
	// ---------------------------------------------------------------------
	FTransform StepRootMotion(const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux) final override;
	void FinalizePose(const FMockRootMotionSyncState* Sync, UAnimInstance* AnimInstance) final override;

private:

	FTransform StepRootMotion_Curve(UCurveVector* Curve, const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux);
	FTransform StepRootMotion_Montage(UAnimMontage* Montage, const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux);
};