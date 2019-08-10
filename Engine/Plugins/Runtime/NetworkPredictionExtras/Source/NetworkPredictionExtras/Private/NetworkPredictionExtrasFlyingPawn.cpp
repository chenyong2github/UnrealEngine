// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionExtrasFlyingPawn.h"
#include "Components/InputComponent.h"
#include "Movement/FlyingMovement.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "DrawDebugHelpers.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"

namespace FlyingPawnCVars
{

int32 CameraStyle = 0;
FAutoConsoleVariableRef CVarPenetrationPullbackDistance(TEXT("NetworkPredictionExtras.FlyingPawn.CameraSyle"),
	CameraStyle,
	TEXT("Sets camera mode style in ANetworkPredictionExtrasFlyingPawn \n")
	TEXT("0=camera fixed behind pawn. 1-3 are variations of a free camera system (Gamepad recommended)."),
	ECVF_Default);

static int32 BindAutomatically = 1;
static FAutoConsoleVariableRef CVarBindAutomatically(TEXT("NetworkPredictionExtras.FlyingPawn.BindAutomatically"),
	BindAutomatically, TEXT("Binds local input and mispredict commands to 5 and 6 respectively"), ECVF_Default);
}

ANetworkPredictionExtrasFlyingPawn::ANetworkPredictionExtrasFlyingPawn()
{
	FlyingMovementComponent = CreateDefaultSubobject<UFlyingMovementComponent>(TEXT("FlyingMovementComponent"));
	
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// Setup callback for generating local input. There is some nuance here. This is the order we want things to happen for a locally controlled actor:
		//	1. Create local input (the lambda below)
		//	2. Tick the network sim (called by UFlyingMovementComponent::TickComponent). This wants the latest input (important the user cmd has the same DeltaTime as the current frame!)
		//  3. Tick this actor (ANetworkPredictionExtrasFlyingPawn::Tick). This wants the latest movement state (computed in step #2).
		//
		// The ::SetLocallyControlledPreTick isn't strictly necessary. A player controller or some other object could tick first, submit the input, then let the sim tick and this actor tick.
		// But for this simple example, its clearer to contain everything here. The delegate is just a helper to save you from dealing with tick graph prereq headaches.

		FlyingMovementComponent->SetLocallyControlledPreTick(UNetworkPredictionComponent::FPreSimTickDelegate::CreateUObject(this, &ThisClass::GenerateLocalInput));

		// Binds 0 and 9 to the debug hud commands. This is just a convenience for the extras plugin. Real projects should bind this themselves
		if (FlyingPawnCVars::BindAutomatically > 0)
		{
			if (ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController())
			{
				LocalPlayer->Exec(GetWorld(), TEXT("setbind Nine nms.Debug.LocallyControlledPawn"), *GLog);
				LocalPlayer->Exec(GetWorld(), TEXT("setbind Zero nms.Debug.ToggleContinous"), *GLog);
			}
		}
	}


}

void ANetworkPredictionExtrasFlyingPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Setup some bindings.
	//
	// This also is not necessary. This would usually be defined in DefaultInput.ini.
	// But the nature of NetworkPredictionExtras is to not assume or require anything of the project its being run in.
	// So in order to make this nice and self contained, manually do input bindings here.
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (PC->PlayerInput)
		{
			//Gamepad
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("MoveRight"), EKeys::Gamepad_LeftX));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("MoveForward"), EKeys::Gamepad_LeftY));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("LookYaw"), EKeys::Gamepad_RightX));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("LookPitch"), EKeys::Gamepad_RightY));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("LeftTriggerAxis"), EKeys::Gamepad_LeftTriggerAxis));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("RightTriggerAxis"), EKeys::Gamepad_RightTriggerAxis));
			PC->PlayerInput->AddActionMapping(FInputActionKeyMapping(TEXT("LeftShoulder"), EKeys::Gamepad_LeftShoulder));
			PC->PlayerInput->AddActionMapping(FInputActionKeyMapping(TEXT("RightShoulder"), EKeys::Gamepad_RightShoulder));

			// Keyboard
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("MoveRight"), EKeys::D, 1.f));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("MoveRight"), EKeys::A, -1.f));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("MoveForward"), EKeys::W, 1.f));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("MoveForward"), EKeys::S, -1.f));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("LeftTriggerAxis"), EKeys::LeftControl, 1.f));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("RightTriggerAxis"), EKeys::LeftShift, 1.f));

			// Mouse
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("LookYaw"), EKeys::MouseX));
			PC->PlayerInput->AddAxisMapping(FInputAxisKeyMapping(TEXT("LookPitch"), EKeys::MouseY));
		}
	}

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this,	&ThisClass::InputAxis_MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this,		&ThisClass::InputAxis_MoveRight);
	PlayerInputComponent->BindAxis(TEXT("LookYaw"), this,		&ThisClass::InputAxis_LookYaw);
	PlayerInputComponent->BindAxis(TEXT("LookPitch"), this,		&ThisClass::InputAxis_LookPitch);
	PlayerInputComponent->BindAxis(TEXT("RightTriggerAxis"), this,		&ThisClass::InputAxis_MoveUp);
	PlayerInputComponent->BindAxis(TEXT("LeftTriggerAxis"), this,		&ThisClass::InputAxis_MoveDown);

	PlayerInputComponent->BindAction(TEXT("LeftShoulder"), IE_Pressed, this, &ThisClass::Action_LeftShoulder_Pressed);
	PlayerInputComponent->BindAction(TEXT("LeftShoulder"), IE_Released, this, &ThisClass::Action_LeftShoulder_Released);
	PlayerInputComponent->BindAction(TEXT("RightShoulder"), IE_Pressed, this, &ThisClass::Action_RightShoulder_Pressed);
	PlayerInputComponent->BindAction(TEXT("RightShoulder"), IE_Released, this, &ThisClass::Action_RightShoulder_Released);
}

void ANetworkPredictionExtrasFlyingPawn::InputAxis_MoveForward(float Value)
{
	CachedMoveInput.X = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ANetworkPredictionExtrasFlyingPawn::InputAxis_MoveRight(float Value)
{
	CachedMoveInput.Y = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ANetworkPredictionExtrasFlyingPawn::InputAxis_LookYaw(float Value)
{
	CachedLookInput.X = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ANetworkPredictionExtrasFlyingPawn::InputAxis_LookPitch(float Value)
{
	CachedLookInput.Y = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ANetworkPredictionExtrasFlyingPawn::InputAxis_MoveUp(float Value)
{
	CachedMoveInput.Z = FMath::Clamp(CachedMoveInput.Z + Value, -1.0f, 1.0f);
}

void ANetworkPredictionExtrasFlyingPawn::InputAxis_MoveDown(float Value)
{
	CachedMoveInput.Z = FMath::Clamp(CachedMoveInput.Z - Value, -1.0f, 1.0f);
}

void ANetworkPredictionExtrasFlyingPawn::Tick( float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Do whatever you want here. By now we have the latest movement state and latest input processed.
}

void ANetworkPredictionExtrasFlyingPawn::GenerateLocalInput(float DeltaSeconds)
{
	// Generate user commands. Called right before the flying movement simulation will tick (for a locally controlled pawn)
	// This isn't meant to be the best way of doing a camera system. It is just meant to show a couple of ways it may be done
	// and to make sure we can keep distinct the movement, rotation, and view angles.
	// Change with CVar NetworkPredictionExtras.FlyingPawn.CameraSyle. Styles 1-3 are really meant to be used with a gamepad.
	//
	// Its worth calling out: the code that happens here is happening *outside* of the flying movement simulation. All we are doing
	// is generating the input being fed into that simulation. That said, this means that A) the code below does not run on the server
	// (and non controlling clients) and B) the code is not rerun during reconcile/resimulates. Use this information guide any
	// decisions about where something should go (such as aim assist, lock on targeting systems, etc): it is hard to give absolute
	// answers and will depend on the game and its specific needs. In general, at this time, I'd recommend aim assist and lock on 
	// targeting systems to happen /outside/ of the system, i.e, here. But I can think of scenarios where that may not be ideal too.

	check(Controller);
	if (FlyingMovement::FInputCmd* NextCmd = FlyingMovementComponent->GetNextClientInputCmdForWrite(DeltaSeconds))
	{
		if (USpringArmComponent* SpringComp = FindComponentByClass<USpringArmComponent>())
		{
			// This is not best practice: do not search for component every frame
			SpringComp->bUsePawnControlRotation = true;
		}

		// Simple input scaling. A real game will probably map this to an acceleration curve
		static float LookRateYaw = 150.f;
		static float LookRatePitch = 150.f;

		static float ControllerLookRateYaw = 1.5f;
		static float ControllerLookRatePitch = 1.5f;

		// Zero out input structs in case each path doesnt set each member. This is really all we are filling out here.
		NextCmd->MovementInput = FVector::ZeroVector;
		NextCmd->RotationInput = FRotator::ZeroRotator;

		switch (FlyingPawnCVars::CameraStyle)
		{
			case 0:
			{
				// Fixed camera
				if (USpringArmComponent* SpringComp = FindComponentByClass<USpringArmComponent>())
				{
					// Only this camera mode has to set this to false
					SpringComp->bUsePawnControlRotation = false;
				}

				NextCmd->RotationInput.Yaw = CachedLookInput.X * LookRateYaw;
				NextCmd->RotationInput.Pitch = CachedLookInput.Y * LookRatePitch;
				NextCmd->RotationInput.Roll = 0;
					
				NextCmd->MovementInput = CachedMoveInput;
				break;
			}
			case 1:
			{
				// Free camera Restricted 2D movement on XY plane.
				APlayerController* PC = Cast<APlayerController>(Controller);
				if (ensure(PC)) // Requires player controller for now
				{
					// Camera yaw rotation
					PC->AddYawInput(CachedLookInput.X * ControllerLookRateYaw );
					PC->AddPitchInput(CachedLookInput.Y * ControllerLookRatePitch );

					static float RotationMagMin = (1e-3);
						
					float RotationInputYaw = 0.f;
					if (CachedMoveInput.Size() >= RotationMagMin)
					{
						FVector DesiredMovementDir = PC->GetControlRotation().RotateVector(CachedMoveInput);

						// 2D xy movement, relative to camera
						{
							FVector DesiredMoveDir2D = FVector(DesiredMovementDir.X, DesiredMovementDir.Y, 0.f);
							DesiredMoveDir2D.Normalize();

							const float DesiredYaw = DesiredMoveDir2D.Rotation().Yaw;
							const float DeltaYaw = DesiredYaw - GetActorRotation().Yaw;
							NextCmd->RotationInput.Yaw = DeltaYaw / NextCmd->FrameDeltaTime;
						}
					}

					NextCmd->MovementInput = FVector(FMath::Clamp<float>( CachedMoveInput.Size2D(), -1.f, 1.f ), 0.0f, CachedMoveInput.Z);
				}
				break;
			}
			case 2:
			{
				// Free camera on yaw and pitch, camera-relative movement.
				APlayerController* PC = Cast<APlayerController>(Controller);
				if (ensure(PC)) // Requires player controller for now
				{
					// Camera yaw rotation
					PC->AddYawInput(CachedLookInput.X * ControllerLookRateYaw );
					PC->AddPitchInput(CachedLookInput.Y * ControllerLookRatePitch );

					// Rotational movement: orientate us towards our camera-relative desired velocity (unless we are upside down, then flip it)
					static float RotationMagMin = (1e-3);
					const float MoveInputMag = CachedMoveInput.Size();
						
					float RotationInputYaw = 0.f;
					float RotationInputPitch = 0.f;
					float RotationInputRoll = 0.f;

					if (MoveInputMag >= RotationMagMin)
					{								
						FVector DesiredMovementDir = PC->GetControlRotation().RotateVector(CachedMoveInput);

						const float DesiredYaw = DesiredMovementDir.Rotation().Yaw;
						const float DeltaYaw = DesiredYaw - GetActorRotation().Yaw;

						const float DesiredPitch = DesiredMovementDir.Rotation().Pitch;
						const float DeltaPitch = DesiredPitch - GetActorRotation().Pitch;

						const float DesiredRoll = DesiredMovementDir.Rotation().Roll;
						const float DeltaRoll = DesiredRoll - GetActorRotation().Roll;

						// Kind of gross but because we want "instant" turning we must factor in delta time so that it gets factored out inside the simulation
						RotationInputYaw = DeltaYaw / NextCmd->FrameDeltaTime;
						RotationInputPitch = DeltaPitch / NextCmd->FrameDeltaTime;
						RotationInputRoll = DeltaRoll / NextCmd->FrameDeltaTime;
								
					}

					NextCmd->RotationInput.Yaw = RotationInputYaw;
					NextCmd->RotationInput.Pitch = RotationInputPitch;
					NextCmd->RotationInput.Roll = RotationInputRoll;
						
					NextCmd->MovementInput = FVector(FMath::Clamp<float>( MoveInputMag, -1.f, 1.f ), 0.0f, 0.0f); // Not the best way but simple
				}
				break;
			}
			case 3:
			{
				// Free camera on the yaw, camera-relative motion
				APlayerController* PC = Cast<APlayerController>(Controller);
				if (ensure(PC)) // Requires player controller for now
				{
					// Camera yaw rotation
					PC->AddYawInput(CachedLookInput.X * ControllerLookRateYaw );

					// Rotational movement: orientate us towards our camera-relative desired velocity (unless we are upside down, then flip it)
					static float RotationMagMin = (1e-3);
					const float MoveInputMag = CachedMoveInput.Size2D();
						
					float RotationInputYaw = 0.f;
					if (MoveInputMag >= RotationMagMin)
					{
						FVector DesiredMovementDir = PC->GetControlRotation().RotateVector(CachedMoveInput);

						const bool bIsUpsideDown = FVector::DotProduct(FVector(0.f, 0.f, 1.f), GetActorQuat().GetUpVector() ) < 0.f;
						if (bIsUpsideDown)
						{
							DesiredMovementDir *= -1.f;
						}							

						const float DesiredYaw = DesiredMovementDir.Rotation().Yaw;
						const float DeltaYaw = DesiredYaw - GetActorRotation().Yaw;

						// Kind of gross but because we want "instant" turning we must factor in delta time so that it gets factored out inside the simulation
						RotationInputYaw = DeltaYaw / NextCmd->FrameDeltaTime;
					}

					NextCmd->RotationInput.Yaw = RotationInputYaw;
					NextCmd->RotationInput.Pitch = CachedLookInput.Y * LookRatePitch; // Just pitch like normal
					NextCmd->RotationInput.Roll = 0;
						
					NextCmd->MovementInput = FVector(FMath::Clamp<float>( MoveInputMag, -1.f, 1.f ), 0.0f, CachedMoveInput.Z); // Not the best way but simple
				}
				break;
			}
		}
				

		CachedMoveInput = FVector::ZeroVector;
		CachedLookInput = FVector2D::ZeroVector;
	}
}