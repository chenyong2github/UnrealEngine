// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Misc/FrameNumber.h"
#include "ActorForWorldTransforms.h"
#include "ControlRigSnapper.generated.h"

class AActor;
class UControlRig;
class ISequencer;
class UMovieScene;

//Specification containing a Control Rig and a list of selected Controls we use to get World Transforms for Snapping.
USTRUCT(BlueprintType)
struct FControlRigForWorldTransforms
{
	GENERATED_BODY()

	FControlRigForWorldTransforms() : ControlRig(nullptr) {};
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Control Rig")
	TWeakObjectPtr<UControlRig> ControlRig;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Control Rig")
	TArray<FName> ControlNames;
};

//Selection from the UI to Snap To. Contains a set of Actors and/or Control Rigs to snap onto or from.
USTRUCT(BlueprintType)
struct FControlRigSnapperSelection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Selection")
	TArray<FActorForWorldTransforms> Actors;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Selection")
	TArray<FControlRigForWorldTransforms>  ControlRigs;

	bool IsValid() const { return (NumSelected() > 0); }
	FText GetName() const;
	int32 NumSelected() const;
	void Clear() { Actors.SetNum(0); ControlRigs.SetNum(0); }
};



struct FControlRigSnapper 
{
	/**
	*  Get Current Active Sequencer in the Level.
	*/
	TWeakPtr<ISequencer> GetSequencer();

	/**
	*  Snap the passed in children over the start and end frames.
	* @param StartFrame Start of the snap interval.
	* @param EndFrame End of the snap interval.
	* @param ChildrenToSnap The children to snap over the interval. Will set a key per frame on them.
	* @param ParentToSnap  The parent to snap to.
	*/
	bool SnapIt(FFrameNumber StartFrame, FFrameNumber EndFrame, const FControlRigSnapperSelection& ChildrenToSnap,
		const FControlRigSnapperSelection& ParentToSnap);

};


