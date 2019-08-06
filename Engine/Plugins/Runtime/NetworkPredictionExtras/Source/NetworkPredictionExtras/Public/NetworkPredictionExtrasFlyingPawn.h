// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Pawn.h"
#include "NetworkPredictionExtrasFlyingPawn.generated.h"

class UInputComponent;
class UFlyingMovementComponent;

// -------------------------------------------------------------------------------------------------------------------------------
//	ANetworkPredictionExtrasFlyingPawn
//
//	This provides a minimal pawn class that uses UFlyingMovementCompnent. This isn't really intended to be used in shipping games,
//	rather just to serve as standalone example of using the system, contained completely in the NetworkPredictionExtras plugin.
//	Apart from the most basic glue/setup, this class provides an example of turning UE4 input event callbacks into the input commands
//	that are used by the flying movement simulation. This includes some basic camera/aiming code.
//
//	Highlights:
//		FlyingMovement::FMovementSystem::Update						The "core update" function of the flying movement simulation.
//		ANetworkPredictionExtrasFlyingPawn::GenerateLocalInput		Function that generates local input commands that are fed into the movement system.
//
//	Usage:
//		You should be able to just use this pawn like you would any other pawn. You can specify it as your pawn class in your game mode, or manually override in world settings, etc.
//		Alternatively, you can just load the NetworkPredictionExtras/Content/TestMap.umap which will have everything setup.
//
//	Once spawned, there are some useful console commands:
//		NetworkPredictionExtras.FlyingPawn.CameraSyle [0-3]			Changes camera mode style.
//		nms.Debug.LocallyControlledPawn 1							Enables debug hud. binds to '9' by default, see ANetworkPredictionExtrasFlyingPawn()
//		nms.Debug.ToggleContinous 1									Toggles continuous updates of the debug hud. binds to '0' by default, see ANetworkPredictionExtrasFlyingPawn()
//
// -------------------------------------------------------------------------------------------------------------------------------


/** Sample pawn that uses UFlyingMovementComponent. The main thing this provides is actually producing user input for the component/simulation to consume. */
UCLASS(config = Game)
class NETWORKPREDICTIONEXTRAS_API ANetworkPredictionExtrasFlyingPawn : public APawn
{
	GENERATED_BODY()

	ANetworkPredictionExtrasFlyingPawn();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick( float DeltaSeconds) override;

private:

	void GenerateLocalInput(float DeltaSeconds);

	FVector CachedMoveInput;
	FVector2D CachedLookInput;

	void InputAxis_MoveForward(float Value);
	void InputAxis_MoveRight(float Value);
	void InputAxis_LookYaw(float Value);
	void InputAxis_LookPitch(float Value);
	void InputAxis_MoveUp(float Value);
	void InputAxis_MoveDown(float Value);

	void Action_LeftShoulder_Pressed() { }
	void Action_LeftShoulder_Released() { }
	void Action_RightShoulder_Pressed() { }
	void Action_RightShoulder_Released() { }

	UPROPERTY()
	UFlyingMovementComponent* FlyingMovementComponent;
};