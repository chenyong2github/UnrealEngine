// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBuildingPawn.h"
#include "WorldBuildingCharacterMovement.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/Controller.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"

FName AWorldBuildingPawn::MeshComponentName(TEXT("MeshComponent0"));

AWorldBuildingPawn::AWorldBuildingPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UWorldBuildingCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;
	RunningSpeedModifier = 2.5f;

	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh;
		FConstructorStatics()
			: SphereMesh(TEXT("/Engine/BasicShapes/Sphere")) {}
	};

	static FConstructorStatics ConstructorStatics;

	// Visuals only
	MeshComponent = CreateOptionalDefaultSubobject<UStaticMeshComponent>(AWorldBuildingPawn::MeshComponentName);
	if (MeshComponent)
	{
		MeshComponent->SetStaticMesh(ConstructorStatics.SphereMesh.Object);
		MeshComponent->AlwaysLoadOnClient = true;
		MeshComponent->AlwaysLoadOnServer = true;
		MeshComponent->bOwnerNoSee = true;
		MeshComponent->bCastDynamicShadow = true;
		MeshComponent->bAffectDynamicIndirectLighting = false;
		MeshComponent->bAffectDistanceFieldLighting = false;
		MeshComponent->bVisibleInRayTracing = false;
		MeshComponent->PrimaryComponentTick.TickGroup = TG_PrePhysics;
		MeshComponent->SetupAttachment(RootComponent);
		// No need for Collision because base class ACharacter as a Capsule component
		MeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		MeshComponent->SetGenerateOverlapEvents(false);
		MeshComponent->SetCanEverAffectNavigation(false);
	}
}

void AWorldBuildingPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AWorldBuildingPawn, bWantsToRun, COND_SkipOwner);
}

void AWorldBuildingPawn::SetupPlayerInputComponent(UInputComponent* InInputComponent)
{
	static bool bBindingsAdded = false;
	if (!bBindingsAdded)
	{
		bBindingsAdded = true;

		// Do our own mapping to not depend on DefaultInput.ini
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveForward", EKeys::W, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveForward", EKeys::S, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveForward", EKeys::Up, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveForward", EKeys::Down, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveForward", EKeys::Gamepad_LeftY, 1.f));

		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveRight", EKeys::A, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveRight", EKeys::D, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveRight", EKeys::Gamepad_LeftX, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::Gamepad_LeftThumbstick, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::Gamepad_RightThumbstick, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::Gamepad_FaceButton_Bottom, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::LeftControl, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::SpaceBar, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::C, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::E, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_MoveUp", EKeys::Q, -1.f));

		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_TurnRate", EKeys::Gamepad_RightX, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_TurnRate", EKeys::Left, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_TurnRate", EKeys::Right, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_Turn", EKeys::MouseX, 1.f));

		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_LookUpRate", EKeys::Gamepad_RightY, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("WorldBuildingPawn_LookUp", EKeys::MouseY, -1.f));

		UPlayerInput::AddEngineDefinedActionMapping(FInputActionKeyMapping("WorldBuildingPawn_Run", EKeys::LeftShift));
		UPlayerInput::AddEngineDefinedActionMapping(FInputActionKeyMapping("WorldBuildingPawn_Run", EKeys::Gamepad_LeftShoulder));
	}
	
	check(InInputComponent);

	InInputComponent->BindAxis("WorldBuildingPawn_MoveForward", this, &AWorldBuildingPawn::MoveForward);
	InInputComponent->BindAxis("WorldBuildingPawn_MoveRight", this, &AWorldBuildingPawn::MoveRight);
	InInputComponent->BindAxis("WorldBuildingPawn_MoveUp", this, &AWorldBuildingPawn::MoveUp_World);
	InInputComponent->BindAxis("WorldBuildingPawn_Turn", this, &AWorldBuildingPawn::AddControllerYawInput);
	InInputComponent->BindAxis("WorldBuildingPawn_TurnRate", this, &AWorldBuildingPawn::TurnAtRate);
	InInputComponent->BindAxis("WorldBuildingPawn_LookUp", this, &AWorldBuildingPawn::AddControllerPitchInput);
	InInputComponent->BindAxis("WorldBuildingPawn_LookUpRate", this, &AWorldBuildingPawn::LookUpAtRate);
	InInputComponent->BindAction("WorldBuildingPawn_Run", IE_Pressed, this, &AWorldBuildingPawn::OnStartRunning);
	InInputComponent->BindAction("WorldBuildingPawn_Run", IE_Released, this, &AWorldBuildingPawn::OnStopRunning);
}

bool AWorldBuildingPawn::IsRunning() const
{
	if (!GetCharacterMovement())
	{
		return false;
	}

	return bWantsToRun && !GetVelocity().IsZero() && (GetVelocity().GetSafeNormal2D() | GetActorForwardVector()) > -0.1;
}

void AWorldBuildingPawn::MoveRight(float Val)
{
	if (Val != 0.f)
	{
		if (Controller)
		{
			FRotator const ControlSpaceRot = Controller->GetControlRotation();

			// transform to world space and add it
			AddMovementInput(FRotationMatrix(ControlSpaceRot).GetScaledAxis(EAxis::Y), Val);
		}
	}
}

void AWorldBuildingPawn::MoveForward(float Val)
{
	if (Val != 0.f)
	{
		if (Controller)
		{
			FRotator const ControlSpaceRot = Controller->GetControlRotation();

			// transform to world space and add it
			AddMovementInput(FRotationMatrix(ControlSpaceRot).GetScaledAxis(EAxis::X), Val);
		}
	}
}

void AWorldBuildingPawn::MoveUp_World(float Val)
{
	if (Val != 0.f)
	{
		AddMovementInput(FVector::UpVector, Val);
	}
}

void AWorldBuildingPawn::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds() * CustomTimeDilation);
}

void AWorldBuildingPawn::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds() * CustomTimeDilation);
}

void AWorldBuildingPawn::OnStartRunning()
{
	SetRunning(true);
}
void AWorldBuildingPawn::OnStopRunning()
{
	SetRunning(false);
}

void AWorldBuildingPawn::SetRunning(bool bNewRunning)
{
	bWantsToRun = bNewRunning;

	if (GetLocalRole() < ROLE_Authority)
	{
		ServerSetRunning(bNewRunning);
	}
}

void AWorldBuildingPawn::ServerSetRunning_Implementation(bool bNewRunning)
{
	SetRunning(bNewRunning);
}