// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVehicleMovementComponent.h"
#include "EngineGlobals.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "DrawDebugHelpers.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Net/UnrealNetwork.h"
#include "VehicleAnimationInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Physics/PhysicsFiltering.h"
#include "Physics/PhysicsInterfaceUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Logging/MessageLog.h"
#include "ChaosTireConfig.h"
#include "DisplayDebugHelpers.h"

#include "ChaosVehicleManager.h"
#include "SimpleVehicle.h"

#include "AI/Navigation/AvoidanceManager.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "GameFramework/HUD.h"

#define LOCTEXT_NAMESPACE "UVehicleMovementComponent"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

DEFINE_LOG_CATEGORY(LogVehicle);


#if WITH_CHAOS

FVehicleDebugParams GVehicleDebugParams;

FAutoConsoleVariableRef CVarChaosVehiclesShowCOM(TEXT("p.Vehicles.ShowCOM"), GVehicleDebugParams.ShowCOM, TEXT("Enable/Disable Center Of Mass Debug Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowModelAxis(TEXT("p.Vehicles.ShowModelOrigin"), GVehicleDebugParams.ShowModelOrigin, TEXT("Enable/Disable Model Origin Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableAirControl(TEXT("p.Vehicles.DisableAirControl"), GVehicleDebugParams.DisableAirControl, TEXT("Enable/Disable In Air Control."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableGroundControl(TEXT("p.Vehicles.DisableGroundControl"), GVehicleDebugParams.DisableGroundControl, TEXT("Enable/Disable In Ground Control."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableAerodynamics(TEXT("p.Vehicles.DisableAerodynamics"), GVehicleDebugParams.DisableAerodynamics, TEXT("Enable/Disable Aerodynamic forces drag/downforce."));
FAutoConsoleVariableRef CVarChaosVehiclesBatchQueries(TEXT("p.Vehicles.BatchQueries"), GVehicleDebugParams.BatchQueries, TEXT("Enable/Disable Batching of suspension raycasts."));
FAutoConsoleVariableRef CVarChaosVehiclesAerofoilForces(TEXT("p.Vehicles.ShowAerofoilForces"), GVehicleDebugParams.ShowAerofoilForces, TEXT("Enable/Disable Aerofoil Force Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesAerofoilSurface(TEXT("p.Vehicles.ShowAerofoilSurface"), GVehicleDebugParams.ShowAerofoilSurface, TEXT("Enable/Disable Aerofoil Surface Visualisation."));

void FVehicleState::CaptureState(FBodyInstance* TargetInstance, float DeltaTime)
{
	if (TargetInstance)
	{
		VehicleWorldTransform = TargetInstance->GetUnrealWorldTransform();
		VehicleWorldVelocity = TargetInstance->GetUnrealWorldVelocity();
		VehicleWorldAngularVelocity = TargetInstance->GetUnrealWorldAngularVelocityInRadians();

		VehicleUpAxis = VehicleWorldTransform.GetUnitAxis(EAxis::Z);
		VehicleForwardAxis = VehicleWorldTransform.GetUnitAxis(EAxis::X);
		VehicleRightAxis = VehicleWorldTransform.GetUnitAxis(EAxis::Y);

		ForwardSpeed = FVector::DotProduct(VehicleWorldVelocity, VehicleForwardAxis);
		ForwardsAcceleration = (ForwardSpeed - PrevForwardSpeed) / DeltaTime;
		PrevForwardSpeed = ForwardSpeed;

		// bVehicleInAir is determined elsewhere
	}
}

UChaosVehicleMovementComponent::UChaosVehicleMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Mass = 1500.0f;
	DragCoefficient = 0.3f;
	ChassisWidth = 180.f;
	ChassisHeight = 140.f;
	InertiaTensorScale = FVector( 1.0f, 1.0f, 1.0f );
	AngErrorAccumulator = 0.0f;

	AirControl.Enabled = true;
	AirControl.RollTorqueScaling = 5.0f;
	AirControl.PitchTorqueScaling = 5.0f; 
	AirControl.YawTorqueScaling = 5.0f;
	AirControl.RotationDamping = 0.02f;

	IdleBrakeInput = 0.0f;
	StopThreshold = 10.0f; 
	WrongDirectionThreshold = 100.f;
	ThrottleInputRate.RiseRate = 6.0f;
	ThrottleInputRate.FallRate = 10.0f;
	BrakeInputRate.RiseRate = 6.0f;
	BrakeInputRate.FallRate = 10.0f;
	PitchInputRate.RiseRate = 6.0f;
	PitchInputRate.FallRate = 10.0f;
	RollInputRate.RiseRate = 6.0f;
	RollInputRate.FallRate = 10.0f;
	YawInputRate.RiseRate = 6.0f;
	YawInputRate.FallRate = 10.0f;
	HandbrakeInputRate.RiseRate = 12.0f;
	HandbrakeInputRate.FallRate = 12.0f;
	SteeringInputRate.RiseRate = 2.5f;
	SteeringInputRate.FallRate = 5.0f;

	//bDeprecatedSpringOffsetMode = false;	//This is just for backwards compat. Newly tuned content should never need to use this

#if WANT_RVO
	bUseRVOAvoidance = false;
	AvoidanceVelocity = FVector::ZeroVector;
	AvoidanceLockVelocity = FVector::ZeroVector;
	AvoidanceLockTimer = 0.0f;
	AvoidanceGroup.bGroup0 = true;
	GroupsToAvoid.Packed = 0xFFFFFFFF;
	GroupsToIgnore.Packed = 0;
	RVOAvoidanceRadius = 400.0f;
	RVOAvoidanceHeight = 200.0f;
	AvoidanceConsiderationRadius = 2000.0f;
	RVOSteeringStep = 0.5f;
	RVOThrottleStep = 0.25f;
#endif

	SetIsReplicatedByDefault(true);

	AHUD::OnShowDebugInfo.AddUObject(this, &UChaosVehicleMovementComponent::ShowDebugInfo);
}

// public

void UChaosVehicleMovementComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// Custom serialization goes here...
}

#if WITH_EDITOR
void UChaosVehicleMovementComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	CopyToSolverSafeContactStaticData();

	// Trigger a runtime rebuild of the Chaos vehicle
	FChaosVehicleManager::VehicleSetupTag++;
}
#endif // WITH_EDITOR

void UChaosVehicleMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	//Skip PawnMovementComponent and simply set PawnOwner to null if we don't have a PawnActor as owner
	UNavMovementComponent::SetUpdatedComponent(NewUpdatedComponent);
	PawnOwner = NewUpdatedComponent ? Cast<APawn>(NewUpdatedComponent->GetOwner()) : nullptr;

	if(USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(NewUpdatedComponent))
	{
		SKC->bLocalSpaceKinematics = true;
	}
}

void UChaosVehicleMovementComponent::SetOverrideController(AController* InOverrideController)
{
	OverrideController = InOverrideController;
}


bool UChaosVehicleMovementComponent::ShouldCreatePhysicsState() const
{
	if (!IsRegistered() || IsBeingDestroyed())
	{
		return false;
	}

	// only create 'Physics' vehicle in game
	UWorld* World = GetWorld();
	if (World->IsGameWorld())
	{
		FPhysScene* PhysScene = World->GetPhysicsScene();

		if (PhysScene && FChaosVehicleManager::GetVehicleManagerFromScene(PhysScene))
		{
			if (CanCreateVehicle())
			{
				return true;
			}
		}
	}

	return false;
}

bool UChaosVehicleMovementComponent::HasValidPhysicsState() const
{
	return PVehicle.IsValid();
}

bool UChaosVehicleMovementComponent::CanCreateVehicle() const
{
	check(GetOwner());
	FString ActorName = GetOwner()->GetName();

	if (UpdatedComponent == NULL)
	{
		UE_LOG(LogVehicle, Warning, TEXT("Ca't create vehicle %s (%s). UpdatedComponent is not set."), *ActorName, *GetPathName());
		return false;
	}

	if (UpdatedPrimitive == NULL)
	{
		UE_LOG(LogVehicle, Warning, TEXT("Ca't create vehicle %s (%s). UpdatedComponent is not a PrimitiveComponent."), *ActorName, *GetPathName());
		return false;
	}

	if (UpdatedPrimitive->GetBodyInstance() == NULL)
	{
		UE_LOG(LogVehicle, Warning, TEXT("Cannot create vehicle %s (%s). UpdatedComponent has not initialized its rigid body actor."), *ActorName, *GetPathName());
		return false;
	}

	return true;
}


void UChaosVehicleMovementComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	VehicleSetupTag = FChaosVehicleManager::VehicleSetupTag;

	// only create Physics vehicle in game
	UWorld* World = GetWorld();
	if (World->IsGameWorld())
	{
		FPhysScene* PhysScene = World->GetPhysicsScene();

		if (PhysScene && FChaosVehicleManager::GetVehicleManagerFromScene(PhysScene))
		{
			//FixupSkeletalMesh();
			CreateVehicle();

			if (PVehicle)
			{
				FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(PhysScene);
				VehicleManager->AddVehicle(this);
			}
		}
	}

	if (USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(GetMesh()))
	{
		SkeletalMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		SkeletalMesh->SetNotifyRigidBodyCollision(true);
		SkeletalMesh->SetEnablePhysicsBlending(false); //??

		//SkeletalMesh->BodyInstance.bStartAwake = false;
		//SkeletalMesh->BodyInstance.bGenerateWakeEvents = true;
		//SkeletalMesh->BodyInstance.bContactModification = true; // #todo: put this on a param - expose implementation
		//SkeletalMesh->BodyInstance.bUseCCD = true; // #todo: put this on a param
		//SkeletalMesh->BodyInstance.bEnableGravity = false;  // #todo: param to say use own gravity or not
	}

}

void UChaosVehicleMovementComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();

	if (PVehicle.IsValid())
	{
		FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(GetWorld()->GetPhysicsScene());
		VehicleManager->RemoveVehicle(this);
		PVehicle.Reset(nullptr);

		if (UpdatedComponent)
		{
			UpdatedComponent->RecreatePhysicsState();
		}
	}
}

void UChaosVehicleMovementComponent::PreTick(float DeltaTime)
{
	// movement updates and replication
	if (PVehicle && UpdatedComponent)
	{
		APawn* MyOwner = Cast<APawn>(UpdatedComponent->GetOwner());
		if (MyOwner)
		{
			UpdateState(DeltaTime);
		}
	}

	if (VehicleSetupTag != FChaosVehicleManager::VehicleSetupTag)
	{
		RecreatePhysicsState();
	}
}

void UChaosVehicleMovementComponent::TickVehicle(float DeltaTime)
{
	//if (AvoidanceLockTimer > 0.0f)
	//{
	//	AvoidanceLockTimer -= DeltaTime;
	//}

	// movement updates and replication
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (PVehicle && UpdatedComponent && TargetInstance)
	{
		APawn* MyOwner = Cast<APawn>(UpdatedComponent->GetOwner());
		if (MyOwner)
		{
			// Wake if control input pressed
			if (VehicleState.bSleeping && RawThrottleInput > SMALL_NUMBER)
			{
				VehicleState.bSleeping = false;
				TargetInstance->WakeInstance();
			}

			if (!VehicleState.bSleeping)
			{
				UpdateSimulation(DeltaTime);
			}

			// Sleep - possibly more agressive than the standard physics one
			bool bAutoSleepEnabled = true;
			float LinearThreshold = 0.001f;
			float AngularThreshold = 0.001f;
			if (bAutoSleepEnabled && !VehicleState.bSleeping && RawBrakeInput == 1.f && !VehicleState.bVehicleInAir)
			{
				if (TargetInstance
					&& TargetInstance->GetUnrealWorldVelocity().SizeSquared() < LinearThreshold
					&& TargetInstance->GetUnrealWorldAngularVelocityInRadians().SizeSquared() < AngularThreshold)
				{
					TargetInstance->PutInstanceToSleep();
					VehicleState.bSleeping = true;
				}
			}
		}
	}

	DrawDebug3D();
}

void UChaosVehicleMovementComponent::StopMovementImmediately()
{
	Super::StopMovementImmediately();
	ClearAllInput();
}

// Input

void UChaosVehicleMovementComponent::SetThrottleInput(float Throttle)
{
	RawThrottleInput = FMath::Clamp(Throttle, -1.0f, 1.0f);
}

void UChaosVehicleMovementComponent::IncreaseThrottleInput(float ThrottleDelta)
{
	RawThrottleInput = FMath::Clamp(RawThrottleInput+ThrottleDelta, 0.f, 1.0f);
}

void UChaosVehicleMovementComponent::DecreaseThrottleInput(float ThrottleDelta)
{
	RawThrottleInput = FMath::Clamp(RawThrottleInput - ThrottleDelta, 0.f, 1.0f);
}

void UChaosVehicleMovementComponent::SetBrakeInput(float Brake)
{
	RawBrakeInput = FMath::Clamp(Brake, -1.0f, 1.0f);
}

void UChaosVehicleMovementComponent::SetSteeringInput(float Steering)
{
	RawSteeringInput = FMath::Clamp(Steering, -1.0f, 1.0f);
}

void UChaosVehicleMovementComponent::SetPitchInput(float Pitch)
{
	RawPitchInput = FMath::Clamp(Pitch, -1.0f, 1.0f);
}

void UChaosVehicleMovementComponent::SetRollInput(float Roll)
{
	RawRollInput = FMath::Clamp(Roll, -1.0f, 1.0f);
}

void UChaosVehicleMovementComponent::SetYawInput(float Yaw)
{
	RawYawInput = FMath::Clamp(Yaw, -1.0f, 1.0f);
}

void UChaosVehicleMovementComponent::SetHandbrakeInput(bool bNewHandbrake)
{
	bRawHandbrakeInput = bNewHandbrake;
}

void UChaosVehicleMovementComponent::SetChangeUpInput(bool bNewGearUp)
{
	bRawGearUpInput = bNewGearUp;
}

void UChaosVehicleMovementComponent::SetChangeDownInput(bool bNewGearDown)
{
	bRawGearDownInput = bNewGearDown;
}

void UChaosVehicleMovementComponent::SetTargetGear(int32 GearNum, bool bImmediate)
{
	if (PVehicle.IsValid() && PVehicle->HasTransmission() && GearNum != PVehicle->GetTransmission().GetTargetGear())
	{
		// #todo: do we need this translation - what values are comming through in GearNum?
		//const uint32 TargetGearNum = GearToChaosGear(GearNum);
		PVehicle->GetTransmission().SetGear(GearNum, bImmediate);
	}
}

void UChaosVehicleMovementComponent::SetUseAutomaticGears(bool bUseAuto)
{
	if (PVehicle.IsValid())
	{
		check(false); // fix
		Chaos::ETransmissionType TransmissionType = bUseAuto ? Chaos::ETransmissionType::Automatic : Chaos::ETransmissionType::Manual;
		//PVehicle->GetTransmission().AccessSetup().TransmissionType = TransmissionType;
	}
}

// Data access

float UChaosVehicleMovementComponent::GetForwardSpeed() const
{
	return VehicleState.ForwardSpeed;
}

float UChaosVehicleMovementComponent::GetForwardSpeedMPH() const
{
	return CmSToMPH(GetForwardSpeed());
}


// input related
float UChaosVehicleMovementComponent::CalcSteeringInput()
{
#if WANT_RVO
	if (bUseRVOAvoidance)
	{
		const float AngleDiff = AvoidanceVelocity.HeadingAngle() - GetVelocityForRVOConsideration().HeadingAngle();
		if (AngleDiff > 0.0f)
		{
			RawSteeringInput = FMath::Clamp(RawSteeringInput + RVOSteeringStep, 0.0f, 1.0f);
		}
		else if (AngleDiff < 0.0f)
		{
			RawSteeringInput = FMath::Clamp(RawSteeringInput - RVOSteeringStep, -1.0f, 0.0f);
		}
	}
#endif

	return RawSteeringInput;
}

float UChaosVehicleMovementComponent::CalcBrakeInput()
{
	if (bReverseAsBrake)
	{
		float NewBrakeInput = 0.0f;

		// if player wants to move forwards...
		if (RawThrottleInput > 0.f)
		{
			// if vehicle is moving backwards, then press brake
			if (VehicleState.ForwardSpeed < -WrongDirectionThreshold)
			{
				NewBrakeInput = 1.0f;
			}

		}

		// if player wants to move backwards...
		else if (RawBrakeInput > 0.f)
		{
			// if vehicle is moving forwards, then press brake
			if (VehicleState.ForwardSpeed > WrongDirectionThreshold)
			{
				NewBrakeInput = 1.0f;			// Seems a bit severe to have 0 or 1 braking. Better control can be had by allowing continuous brake input values
			}
		}
		// if player isn't pressing forward or backwards...
		else
		{
			if (VehicleState.ForwardSpeed < StopThreshold && VehicleState.ForwardSpeed > -StopThreshold)	//auto break 
			{
				NewBrakeInput = 1.f;
			}
			else
			{
				NewBrakeInput = IdleBrakeInput;
			}
		}

		return FMath::Clamp<float>(NewBrakeInput, 0.0, 1.0);
	}
	else
	{
		float NewBrakeInput = FMath::Abs(RawBrakeInput);

		// if player isn't pressing forward or backwards...
		if (RawBrakeInput < SMALL_NUMBER && RawThrottleInput < SMALL_NUMBER)
		{
			if (VehicleState.ForwardSpeed < StopThreshold && VehicleState.ForwardSpeed > -StopThreshold)	//auto break 
			{
				NewBrakeInput = 1.f;
				FBodyInstance* TargetInstance = GetBodyInstance();
			}
		}

		return NewBrakeInput;
	}

}

float UChaosVehicleMovementComponent::CalcHandbrakeInput()
{
	return (bRawHandbrakeInput == true) ? 1.0f : 0.0f;
}

float UChaosVehicleMovementComponent::CalcPitchInput()
{
	return RawPitchInput;
}

float UChaosVehicleMovementComponent::CalcRollInput()
{
	return RawRollInput;
}

float UChaosVehicleMovementComponent::CalcYawInput()
{
	return RawYawInput;
}

float UChaosVehicleMovementComponent::CalcThrottleInput()
{
#if WANT_RVO
	if (bUseRVOAvoidance)
	{
		const float AvoidanceSpeedSq = AvoidanceVelocity.SizeSquared();
		const float DesiredSpeedSq = GetVelocityForRVOConsideration().SizeSquared();

		if (AvoidanceSpeedSq > DesiredSpeedSq)
		{
			RawThrottleInput = FMath::Clamp(RawThrottleInput + RVOThrottleStep, -1.0f, 1.0f);
		}
		else if (AvoidanceSpeedSq < DesiredSpeedSq)
		{
			RawThrottleInput = FMath::Clamp(RawThrottleInput - RVOThrottleStep, -1.0f, 1.0f);
		}
	}
#endif

	float NewThrottleInput = RawThrottleInput;
	if (bReverseAsBrake)
	{
		if (RawBrakeInput > 0.f && PVehicle->GetTransmission().GetTargetGear() < 0/*ForwardSpeed < -WrongDirectionThreshold*/)
		{
			NewThrottleInput = RawBrakeInput;
		}
		else
			//If the user is changing direction we should really be braking first and not applying any gas, so wait until they've changed gears
			if ((RawThrottleInput > 0.f && PVehicle->GetTransmission().GetTargetGear() < 0) || (RawBrakeInput > 0.f && PVehicle->GetTransmission().GetTargetGear() > 0))
			{
				NewThrottleInput = 0.f;
			}
	}

	return FMath::Abs(NewThrottleInput);
}

void UChaosVehicleMovementComponent::ClearInput()
{
	SteeringInput = 0.0f;
	ThrottleInput = 0.0f;
	BrakeInput = 0.0f;
	HandbrakeInput = 0.0f;
	PitchInput = 0.0f;
	RollInput = 0.0f;
	YawInput = 0.0f;

	// Send this immediately.
	int32 CurrentGear = 0;
	if (PVehicle && PVehicle->HasTransmission())
	{
		CurrentGear = PVehicle->GetTransmission().GetCurrentGear();
	}

	ServerUpdateState(SteeringInput, ThrottleInput, BrakeInput, HandbrakeInput, CurrentGear/*GetCurrentGear()*/);
}

void UChaosVehicleMovementComponent::ClearRawInput()
{
	RawBrakeInput = 0.0f;
	RawSteeringInput = 0.0f;
	RawThrottleInput = 0.0f;
	RawPitchInput = 0.0f;
	RawRollInput = 0.0f;
	RawYawInput = 0.0f;
	bRawGearDownInput = false;
	bRawGearUpInput = false;
	bRawHandbrakeInput = false;
}

// Update

void UChaosVehicleMovementComponent::UpdateState(float DeltaTime)
{
	// update input values
	AController* Controller = GetController();

	// TODO: IsLocallyControlled will fail if the owner is unpossessed (i.e. Controller == nullptr);
	// Should we remove input instead of relying on replicated state in that case?
	if (Controller && Controller->IsLocalController() && PVehicle)
	{
		if (bReverseAsBrake)
		{
			//for reverse as state we want to automatically shift between reverse and first gear
			if (FMath::Abs(GetForwardSpeed()) < WrongDirectionThreshold)	//we only shift between reverse and first if the car is slow enough.
			{
				if (RawBrakeInput > KINDA_SMALL_NUMBER && PVehicle->GetTransmission().GetCurrentGear() >= 0 && PVehicle->GetTransmission().GetTargetGear() >= 0)
				{
					SetTargetGear(-1, false);
				}
				else if (RawThrottleInput > KINDA_SMALL_NUMBER && PVehicle->GetTransmission().GetCurrentGear() <= 0 && PVehicle->GetTransmission().GetTargetGear() <= 0)
				{
					SetTargetGear(1, false);
				}
			}
		}

#if WANT_RVO
		if (bUseRVOAvoidance)
		{
			CalculateAvoidanceVelocity(DeltaTime);
			UpdateAvoidance(DeltaTime);
		}
#endif
		SteeringInput = SteeringInputRate.InterpInputValue(DeltaTime, SteeringInput, CalcSteeringInput());
		ThrottleInput = ThrottleInputRate.InterpInputValue(DeltaTime, ThrottleInput, CalcThrottleInput());
		BrakeInput = BrakeInputRate.InterpInputValue(DeltaTime, BrakeInput, CalcBrakeInput());
		PitchInput = PitchInputRate.InterpInputValue(DeltaTime, PitchInput, CalcPitchInput());
		RollInput = RollInputRate.InterpInputValue(DeltaTime, RollInput, CalcRollInput());
		YawInput = YawInputRate.InterpInputValue(DeltaTime, YawInput, CalcYawInput());
		HandbrakeInput = HandbrakeInputRate.InterpInputValue(DeltaTime, HandbrakeInput, CalcHandbrakeInput());

		// and send to server - (ServerUpdateState_Implementation below)
		int32 TargetGear = 0;
		if (PVehicle->HasTransmission())
		{
			TargetGear = PVehicle->GetTransmission().GetTargetGear();
		}
		ServerUpdateState(SteeringInput, ThrottleInput, BrakeInput, HandbrakeInput, TargetGear);

		if (PawnOwner && PawnOwner->IsNetMode(NM_Client))
		{
			MarkForClientCameraUpdate();
		}
	}
	else
	{
		// use replicated values for remote pawns
		SteeringInput = ReplicatedState.SteeringInput;
		ThrottleInput = ReplicatedState.ThrottleInput;
		BrakeInput = ReplicatedState.BrakeInput;
		PitchInput = ReplicatedState.PitchInput;
		RollInput = ReplicatedState.RollInput;
		YawInput = ReplicatedState.YawInput;
		HandbrakeInput = ReplicatedState.HandbrakeInput;
		SetTargetGear(ReplicatedState.TargetGear, true);
	}
}

void UChaosVehicleMovementComponent::UpdateSimulation(float DeltaTime)
{
	FBodyInstance* TargetInstance = GetBodyInstance();

	if (CanSimulate() && TargetInstance)
	{
		VehicleState.CaptureState(TargetInstance, DeltaTime);

		// #todo: param to say use own gravity or not
		//TargetInstance->AddForce(GetGravity(), true, true);

		ApplyAerodynamics(DeltaTime);
		ApplyAerofoilForces(DeltaTime);
		ApplyAirControl(DeltaTime);

	}
}

/** Pass control Input to the vehicle systems */
void UChaosVehicleMovementComponent::ApplyInput(float DeltaTime)
{
	for (int AerofoilIdx = 0; AerofoilIdx < Aerofoils.Num(); AerofoilIdx++)
	{
		FAerofoil& Aerofoil = PVehicle->GetAerofoil(AerofoilIdx);

		switch (Aerofoil.Setup().Type)
		{
		case Chaos::FAerofoilType::Rudder:
			Aerofoil.SetControlSurface(-YawInput);
			break;

		case Chaos::FAerofoilType::Elevator:
			Aerofoil.SetControlSurface(PitchInput);
			break;

		case Chaos::FAerofoilType::Wing:
			if (Aerofoil.Setup().Offset.Y < 0.0f)
			{
				Aerofoil.SetControlSurface(RollInput);
			}
			else
			{
				Aerofoil.SetControlSurface(-RollInput);
			}
			break;
		}
	}
}


void UChaosVehicleMovementComponent::ApplyAerodynamics(float DeltaTime)
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (!GVehicleDebugParams.DisableAerodynamics && TargetInstance)
	{
		Chaos::FSimpleAerodynamicsSim& PAerodynamics = PVehicle->GetAerodynamics();
		FVector LocalDragLiftForce = /*MToCm*/(PAerodynamics.GetCombinedForces(CmToM(VehicleState.ForwardSpeed))) * 100.0f;
		FVector WorldLiftDragForce = VehicleState.VehicleWorldTransform.TransformVector(LocalDragLiftForce);
		TargetInstance->AddForce(WorldLiftDragForce); // applied whether the vehicle is on the ground or not
	}
}

void UChaosVehicleMovementComponent::ApplyAerofoilForces(float DeltaTime)
{
	TArray<FVector> VelocityLocal;
	TArray<FVector> VelocityWorld;
	VelocityLocal.SetNum(PVehicle->Aerofoils.Num());
	VelocityWorld.SetNum(PVehicle->Aerofoils.Num());

	float Altitude = VehicleState.VehicleWorldTransform.GetLocation().Z;

	// Work out velocity at each aerofoil before applying any forces
	for (int AerofoilIdx = 0; AerofoilIdx < PVehicle->Aerofoils.Num(); AerofoilIdx++)
	{
		FVector WorldLocation = VehicleState.VehicleWorldTransform.TransformPosition(PVehicle->GetAerofoil(AerofoilIdx).Setup().Offset * 100.0f);
		VelocityWorld[AerofoilIdx] = GetBodyInstance()->GetUnrealWorldVelocityAtPoint(WorldLocation);
		VelocityLocal[AerofoilIdx] = VehicleState.VehicleWorldTransform.InverseTransformVector(VelocityWorld[AerofoilIdx]);
	}

	for (int AerofoilIdx = 0; AerofoilIdx < PVehicle->Aerofoils.Num(); AerofoilIdx++)
	{
		Chaos::FAerofoil& Aerofoil = PVehicle->GetAerofoil(AerofoilIdx);

		FVector Force = Aerofoil.GetForce(VehicleState.VehicleWorldTransform, VelocityLocal[AerofoilIdx] * 0.01f, CmToM(Altitude), DeltaTime);

		FVector WorldForce = VehicleState.VehicleWorldTransform.TransformVector(Force);
		FVector WorldLocation = VehicleState.VehicleWorldTransform.TransformPosition(Aerofoil.GetCenterOfLiftOffset() * 100.0f);
		GetBodyInstance()->AddForceAtPosition(WorldForce * 100.0f, WorldLocation);

		FVector WorldAxis = VehicleState.VehicleWorldTransform.TransformVector(FVector::CrossProduct(FVector(1,0,0), Aerofoil.Setup().UpAxis));
		if (GVehicleDebugParams.ShowAerofoilSurface)
		{
			DrawDebugLine(GetWorld(), WorldLocation - WorldAxis * 150.0f, WorldLocation + WorldAxis * 150.0f, FColor::Blue, false, -1.f, 0, 10.f);
		}
		if (GVehicleDebugParams.ShowAerofoilForces)
		{
			DrawDebugLine(GetWorld(), WorldLocation, WorldLocation + WorldForce * 0.05f, FColor::Green, false, -1.f, 0, 16.f);
		}
	}

}

void UChaosVehicleMovementComponent::ApplyAirControl(float DeltaTime)
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (!GVehicleDebugParams.DisableAirControl && AirControl.Enabled && VehicleState.bVehicleInAir && TargetInstance)
	{
		// user control
		FVector LocalTorque(-RollInput * AirControl.RollTorqueScaling
			, PitchInput * AirControl.PitchTorqueScaling
			, YawInput * AirControl.YawTorqueScaling);

		// slowing rotation effect
		FVector DampingTorque = (VehicleState.VehicleWorldAngularVelocity / DeltaTime) * AirControl.RotationDamping;

		// combined world torque
		FVector TotalWorldTorque = VehicleState.VehicleWorldTransform.TransformVector(LocalTorque) - DampingTorque;

		// apply torque to physics body
		TargetInstance->AddTorqueInRadians(TotalWorldTorque, /*bAllowSubstepping=*/false, /*bAccelChange=*/true);
	}
}


void UChaosVehicleMovementComponent::CopyToSolverSafeContactStaticData()
{
	//if (GetPhysicsVehicleConfigs())
	//{
	//	SolverSafeContactData.ContactModificationOffset = GetPhysicsVehicleConfigs()->ContactModificationOffset;
	//	SolverSafeContactData.VehicleFloorFriction = GetPhysicsVehicleConfigs()->VehicleFloorFriction;
	//	SolverSafeContactData.VehicleSideScrapeFriction = GetPhysicsVehicleConfigs()->VehicleSideScrapeFriction;
	//	SolverSafeContactData.VehicleSideScrapeMaxCosAngle = GetPhysicsVehicleConfigs()->VehicleSideScrapeMaxCosAngle;
	//}
	//else
	{
		SolverSafeContactData.ContactModificationOffset = 10.f;
		SolverSafeContactData.VehicleFloorFriction = 0.f;
		SolverSafeContactData.VehicleSideScrapeFriction = 0.1f;
	}
}


/// @cond DOXYGEN_WARNINGS

bool UChaosVehicleMovementComponent::ServerUpdateState_Validate(float InSteeringInput, float InThrottleInput, float InBrakeInput, float InHandbrakeInput, int32 InCurrentGear)
{
	return true;
}

void UChaosVehicleMovementComponent::ServerUpdateState_Implementation(float InSteeringInput, float InThrottleInput, float InBrakeInput, float InHandbrakeInput, int32 InCurrentGear)
{
	SteeringInput = InSteeringInput;
	ThrottleInput = InThrottleInput;
	BrakeInput = InBrakeInput;
	HandbrakeInput = InHandbrakeInput;

	if (false/*!GetUseAutoGears()*/)
	{
		SetTargetGear(InCurrentGear, true);
	}

	// update state of inputs
	ReplicatedState.SteeringInput = InSteeringInput;
	ReplicatedState.ThrottleInput = InThrottleInput;
	ReplicatedState.BrakeInput = InBrakeInput;
	ReplicatedState.HandbrakeInput = InHandbrakeInput;
	ReplicatedState.TargetGear = InCurrentGear;
}

/// @endcond


// Setup
AController* UChaosVehicleMovementComponent::GetController() const
{
	if (OverrideController)
	{
		return OverrideController;
	}

	if (UpdatedComponent)
	{
		if (APawn* Pawn = Cast<APawn>(UpdatedComponent->GetOwner()))
		{
			return Pawn->Controller;
		}
	}

	return nullptr;
}


FBodyInstance* UChaosVehicleMovementComponent::GetBodyInstance()
{
	return UpdatedPrimitive ? UpdatedPrimitive->GetBodyInstance() : nullptr;
}


USkinnedMeshComponent* UChaosVehicleMovementComponent::GetMesh()
{
	return Cast<USkinnedMeshComponent>(UpdatedComponent);
}

void UChaosVehicleMovementComponent::CreateVehicle()
{
	ComputeConstants();

	if (PVehicle == nullptr)
	{
		if (CanCreateVehicle())
		{
			check(UpdatedComponent);
			if (ensure(UpdatedPrimitive != nullptr))
			{
				check(UpdatedPrimitive->GetBodyInstance()->IsDynamic());

				// Low level physics representation
				PVehicle = MakeUnique<Chaos::FSimpleWheeledVehicle>();

				SetupVehicle();

				if (PVehicle != nullptr)
				{
					PostSetupVehicle();
				}
			}
		}
	}
}

void UChaosVehicleMovementComponent::SetupVehicle()
{
	Chaos::FSimpleAerodynamicsSim AerodynamicsSim(&GetAerodynamicsConfig());
	PVehicle->Aerodynamics.Add(AerodynamicsSim);

	for (FVehicleAerofoilConfig& AerofoilSetup : Aerofoils)
	{
		Chaos::FAerofoil AerofoilSim(&AerofoilSetup.GetPhysicsAerofoilConfig());
		PVehicle->Aerofoils.Add(AerofoilSim);
	}
}

void UChaosVehicleMovementComponent::PostSetupVehicle()
{
#if WANT_RVO
	if (bUseRVOAvoidance)
	{
		UAvoidanceManager* AvoidanceManager = GetWorld()->GetAvoidanceManager();
		if (AvoidanceManager)
		{
			AvoidanceManager->RegisterMovementComponent(this, AvoidanceWeight);
		}
	}
#endif
}

void UChaosVehicleMovementComponent::SetupVehicleMass()
{
	if (!UpdatedPrimitive)
	{
		return;
	}

	//Ensure that if mass properties ever change we set them back to our override
	UpdatedPrimitive->GetBodyInstance()->OnRecalculatedMassProperties().AddUObject(this, &UChaosVehicleMovementComponent::UpdateMassProperties);

	UpdateMassProperties(UpdatedPrimitive->GetBodyInstance());
}

void UChaosVehicleMovementComponent::UpdateMassProperties(FBodyInstance* BI)
{
	FBodyInstance* TargetInstance = GetBodyInstance();

	//FPhysicsCommand::ExecuteWrite(BI->ActorHandle, [&](const FPhysicsActorHandle& Actor)
	//{
	//	PxRigidActor* PActor = FPhysicsInterface::GetPxRigidActor_AssumesLocked(Actor);
	//	if (!PActor)
	//	{
	//		return;
	//	}

	//	if (PxRigidDynamic* PVehicleActor = PActor->is<PxRigidDynamic>())
	//	{
	//		// Override mass
	//		const float MassRatio = Mass > 0.0f ? Mass / PVehicleActor->getMass() : 1.0f;

	//		PxVec3 PInertiaTensor = PVehicleActor->getMassSpaceInertiaTensor();

	//		PInertiaTensor.x *= InertiaTensorScale.X * MassRatio;
	//		PInertiaTensor.y *= InertiaTensorScale.Y * MassRatio;
	//		PInertiaTensor.z *= InertiaTensorScale.Z * MassRatio;

	//		PVehicleActor->setMassSpaceInertiaTensor(PInertiaTensor);
	//		PVehicleActor->setMass(Mass);

	//		const PxVec3 PCOMOffset = U2PVector(GetLocalCOM());
	//		PVehicleActor->setCMassLocalPose(PxTransform(PCOMOffset, PxQuat(physx::PxIdentity)));	//ignore the mass reference frame. TODO: expose this to the user

	//		//if (PVehicle)
	//		//{
	//		//	PxVehicleWheelsSimData& WheelData = PVehicle->mWheelsSimData;
	//		//	SetupWheelMassProperties_AssumesLocked(WheelData.getNbWheels(), &WheelData, PVehicleActor);
	//		//}
	//	}
	//});
}

void UChaosVehicleMovementComponent::ComputeConstants()
{
	DragArea = ChassisWidth * ChassisHeight;
}


// Debug
void UChaosVehicleMovementComponent::ShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	static FName NAME_Vehicle = FName(TEXT("Vehicle"));

	if (Canvas && HUD->ShouldDisplayDebug(NAME_Vehicle))
	{
		if (APlayerController* Controller = Cast<APlayerController>(GetController()))
		{
			if (Controller->IsLocalController())
			{
				DrawDebug(Canvas, YL, YPos);
			}
		}
	}
}

void UChaosVehicleMovementComponent::DrawDebug(UCanvas* Canvas, float& YL, float& YPos)
{
}

void UChaosVehicleMovementComponent::DrawDebug3D()
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (TargetInstance == nullptr)
	{
		return;
	}
		
	const FTransform BodyTransform = VehicleState.VehicleWorldTransform;

	if (GVehicleDebugParams.ShowCOM && TargetInstance)
	{
		FVector COMWorld = TargetInstance->GetCOMPosition();
		DrawDebugCoordinateSystem(GetWorld(), COMWorld, FRotator(BodyTransform.GetRotation()), 200.f, false, -1.f, 0, 2.f);
	}

	if (GVehicleDebugParams.ShowModelOrigin && TargetInstance)
	{
		DrawDebugCoordinateSystem(GetWorld(), BodyTransform.GetLocation(), FRotator(BodyTransform.GetRotation()), 200.f, false, -1.f, 0, 2.f);
	}

}


//void UVehicleMovementComponent::CopyToSolverSafeContactDynamicData()
//{
//	// Copy to solver inputs
//	SolverSafeContactData.IgnoredBuildingActors = IgnoredBuildingActors;
//	SolverSafeContactData.LocallyIgnoredBuildingActors = LocallyIgnoredBuildingActors;
//	SolverSafeContactData.bSkipRotations = ShouldSkipContactRotations();
//}


/// @cond DOXYGEN_WARNINGS

void UChaosVehicleMovementComponent::GetLifetimeReplicatedProps( TArray< FLifetimeProperty > & OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	DOREPLIFETIME( UChaosVehicleMovementComponent, ReplicatedState );
	DOREPLIFETIME(UChaosVehicleMovementComponent, OverrideController);
}

/// @endcond


void UChaosVehicleMovementComponent::DrawLine2D(UCanvas* Canvas, const FVector2D& StartPos, const FVector2D& EndPos, FColor Color, float Thickness)
{
	if (Canvas)
	{
		FCanvasLineItem LineItem(StartPos, EndPos);
		LineItem.SetColor(Color);
		LineItem.LineThickness = Thickness;
		Canvas->DrawItem(LineItem);
	}
}

#if WANT_RVO

void UChaosVehicleMovementComponent::UpdateAvoidance(float DeltaTime)
{
	UpdateDefaultAvoidance();
}

void UChaosVehicleMovementComponent::UpdateDefaultAvoidance()
{
	if (!bUseRVOAvoidance)
	{
		return;
	}

	UAvoidanceManager* AvoidanceManager = GetWorld()->GetAvoidanceManager();
	if (AvoidanceManager && !bWasAvoidanceUpdated)
	{
		AvoidanceManager->UpdateRVO(this);

		//Consider this a clean move because we didn't even try to avoid.
		SetAvoidanceVelocityLock(AvoidanceManager, AvoidanceManager->LockTimeAfterClean);
	}

	bWasAvoidanceUpdated = false;		//Reset for next frame
}

void UChaosVehicleMovementComponent::SetAvoidanceVelocityLock(class UAvoidanceManager* Avoidance, float Duration)
{
	Avoidance->OverrideToMaxWeight(AvoidanceUID, Duration);
	AvoidanceLockVelocity = AvoidanceVelocity;
	AvoidanceLockTimer = Duration;
}

void UChaosVehicleMovementComponent::CalculateAvoidanceVelocity(float DeltaTime)
{
	if (!bUseRVOAvoidance)
	{
		return;
	}
	
	UAvoidanceManager* AvoidanceManager = GetWorld()->GetAvoidanceManager();
	APawn* MyOwner = UpdatedComponent ? Cast<APawn>(UpdatedComponent->GetOwner()) : NULL;

	// since we don't assign the avoidance velocity but instead use it to adjust steering and throttle,
	// always reset the avoidance velocity to the current velocity
	AvoidanceVelocity = GetVelocityForRVOConsideration();

	if (AvoidanceWeight >= 1.0f || AvoidanceManager == NULL || MyOwner == NULL)
	{
		return;
	}
	
	if (MyOwner->GetLocalRole() != ROLE_Authority)
	{	
		return;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const bool bShowDebug = AvoidanceManager->IsDebugEnabled(AvoidanceUID);
#endif

	if (!AvoidanceVelocity.IsZero())
	{
		//See if we're doing a locked avoidance move already, and if so, skip the testing and just do the move.
		if (AvoidanceLockTimer > 0.0f)
		{
			AvoidanceVelocity = AvoidanceLockVelocity;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (bShowDebug)
			{
				DrawDebugLine(GetWorld(), GetRVOAvoidanceOrigin(), GetRVOAvoidanceOrigin() + AvoidanceVelocity, FColor::Blue, true, 0.5f, SDPG_MAX);
			}
#endif
		}
		else
		{
			FVector NewVelocity = AvoidanceManager->GetAvoidanceVelocityForComponent(this);
			if (!NewVelocity.Equals(AvoidanceVelocity))		//Really want to branch hint that this will probably not pass
			{
				//Had to divert course, lock this avoidance move in for a short time. This will make us a VO, so unlocked others will know to avoid us.
				AvoidanceVelocity = NewVelocity;
				SetAvoidanceVelocityLock(AvoidanceManager, AvoidanceManager->LockTimeAfterAvoid);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (bShowDebug)
				{
					DrawDebugLine(GetWorld(), GetRVOAvoidanceOrigin(), GetRVOAvoidanceOrigin() + AvoidanceVelocity, FColor::Red, true, 20.0f, SDPG_MAX, 10.0f);
				}
#endif
			}
			else
			{
				//Although we didn't divert course, our velocity for this frame is decided. We will not reciprocate anything further, so treat as a VO for the remainder of this frame.
				SetAvoidanceVelocityLock(AvoidanceManager, AvoidanceManager->LockTimeAfterClean);	//10 ms of lock time should be adequate.
			}
		}

		AvoidanceManager->UpdateRVO(this);
		bWasAvoidanceUpdated = true;
	}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	else if (bShowDebug)
	{
		DrawDebugLine(GetWorld(), GetRVOAvoidanceOrigin(), GetRVOAvoidanceOrigin() + GetVelocityForRVOConsideration(), FColor::Yellow, true, 0.05f, SDPG_MAX);
	}

	if (bShowDebug)
	{
		FVector UpLine(0, 0, 500);
		DrawDebugLine(GetWorld(), GetRVOAvoidanceOrigin(), GetRVOAvoidanceOrigin() + UpLine, (AvoidanceLockTimer > 0.01f) ? FColor::Red : FColor::Blue, true, 0.05f, SDPG_MAX, 5.0f);
	}
#endif
}

void UChaosVehicleMovementComponent::SetAvoidanceGroup(int32 GroupFlags)
{
	SetAvoidanceGroupMask(GroupFlags);
}

void UChaosVehicleMovementComponent::SetAvoidanceGroupMask(const FNavAvoidanceMask& GroupMask)
{
	SetAvoidanceGroupMask(GroupMask.Packed);
}

void UChaosVehicleMovementComponent::SetGroupsToAvoid(int32 GroupFlags)
{
	SetGroupsToAvoidMask(GroupFlags);
}

void UChaosVehicleMovementComponent::SetGroupsToAvoidMask(const FNavAvoidanceMask& GroupMask)
{
	SetGroupsToAvoidMask(GroupMask.Packed);
}

void UChaosVehicleMovementComponent::SetGroupsToIgnore(int32 GroupFlags)
{
	SetGroupsToIgnoreMask(GroupFlags);
}

void UChaosVehicleMovementComponent::SetGroupsToIgnoreMask(const FNavAvoidanceMask& GroupMask)
{
	SetGroupsToIgnoreMask(GroupMask.Packed);
}

void UChaosVehicleMovementComponent::SetAvoidanceEnabled(bool bEnable)
{
	if (bUseRVOAvoidance != bEnable)
	{
		bUseRVOAvoidance = bEnable;
		
		// reset id, RegisterMovementComponent call is required to initialize update timers in avoidance manager
		AvoidanceUID = 0;

		UAvoidanceManager* AvoidanceManager = GetWorld()->GetAvoidanceManager();
		if (AvoidanceManager && bEnable)
		{
			AvoidanceManager->RegisterMovementComponent(this, AvoidanceWeight);
		}
	}
}


void UChaosVehicleMovementComponent::SetRVOAvoidanceUID(int32 UID)
{
	AvoidanceUID = UID;
}

int32 UChaosVehicleMovementComponent::GetRVOAvoidanceUID()
{
	return AvoidanceUID;
}

void UChaosVehicleMovementComponent::SetRVOAvoidanceWeight(float Weight)
{
	AvoidanceWeight = Weight;
}

float UChaosVehicleMovementComponent::GetRVOAvoidanceWeight()
{
	return AvoidanceWeight;
}

FVector UChaosVehicleMovementComponent::GetRVOAvoidanceOrigin()
{
	return UpdatedComponent->GetComponentLocation();
}

float UChaosVehicleMovementComponent::GetRVOAvoidanceRadius()
{
	return RVOAvoidanceRadius;
}

float UChaosVehicleMovementComponent::GetRVOAvoidanceHeight()
{
	return RVOAvoidanceHeight;
}

float UChaosVehicleMovementComponent::GetRVOAvoidanceConsiderationRadius()
{
	return AvoidanceConsiderationRadius;
}

FVector UChaosVehicleMovementComponent::GetVelocityForRVOConsideration()
{
	FVector Velocity2D = UpdatedComponent->GetComponentVelocity();
	Velocity2D.Z = 0.f;

	return Velocity2D;
}

void UChaosVehicleMovementComponent::SetAvoidanceGroupMask(int32 GroupFlags)
{
	AvoidanceGroup.SetFlagsDirectly(GroupFlags);
}

int32 UChaosVehicleMovementComponent::GetAvoidanceGroupMask()
{
	return AvoidanceGroup.Packed;
}

void UChaosVehicleMovementComponent::SetGroupsToAvoidMask(int32 GroupFlags)
{
	GroupsToAvoid.SetFlagsDirectly(GroupFlags);
}

int32 UChaosVehicleMovementComponent::GetGroupsToAvoidMask()
{
	return GroupsToAvoid.Packed;
}

void UChaosVehicleMovementComponent::SetGroupsToIgnoreMask(int32 GroupFlags)
{
	GroupsToIgnore.SetFlagsDirectly(GroupFlags);
}

int32 UChaosVehicleMovementComponent::GetGroupsToIgnoreMask()
{
	return GroupsToIgnore.Packed;
}

#endif // WANT_RVO

#endif

#undef LOCTEXT_NAMESPACE


#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif

