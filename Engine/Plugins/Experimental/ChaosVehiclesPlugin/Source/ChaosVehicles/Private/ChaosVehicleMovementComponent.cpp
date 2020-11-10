// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVehicleMovementComponent.h"
#include "EngineGlobals.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Components/StaticMeshComponent.h"
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
#include "DisplayDebugHelpers.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Chaos/PBDJointConstraintData.h"

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



FVehicleDebugParams GVehicleDebugParams;

FAutoConsoleVariableRef CVarChaosVehiclesShowCOM(TEXT("p.Vehicle.ShowCOM"), GVehicleDebugParams.ShowCOM, TEXT("Enable/Disable Center Of Mass Debug Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowModelAxis(TEXT("p.Vehicle.ShowModelOrigin"), GVehicleDebugParams.ShowModelOrigin, TEXT("Enable/Disable Model Origin Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowAllForces(TEXT("p.Vehicle.ShowAllForces"), GVehicleDebugParams.ShowAllForces, TEXT("Enable/Disable Force Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesAerofoilForces(TEXT("p.Vehicle.ShowAerofoilForces"), GVehicleDebugParams.ShowAerofoilForces, TEXT("Enable/Disable Aerofoil Force Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesAerofoilSurface(TEXT("p.Vehicle.ShowAerofoilSurface"), GVehicleDebugParams.ShowAerofoilSurface, TEXT("Enable/Disable a very approximate visualisation of where the Aerofoil surface is located and its orientation."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableTorqueControl(TEXT("p.Vehicle.DisableTorqueControl"), GVehicleDebugParams.DisableTorqueControl, TEXT("Enable/Disable Direct Torque Control."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableStabilizeControl(TEXT("p.Vehicle.DisableStabilizeControl"), GVehicleDebugParams.DisableStabilizeControl, TEXT("Enable/Disable Position Stabilization Control."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableAerodynamics(TEXT("p.Vehicle.DisableAerodynamics"), GVehicleDebugParams.DisableAerodynamics, TEXT("Enable/Disable Aerodynamic Forces Drag/Downforce."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableAerofoils(TEXT("p.Vehicle.DisableAerofoils"), GVehicleDebugParams.DisableAerofoils, TEXT("Enable/Disable Aerofoil Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableThrusters(TEXT("p.Vehicle.DisableThrusters"), GVehicleDebugParams.DisableThrusters, TEXT("Enable/Disable Thruster Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesBatchQueries(TEXT("p.Vehicle.BatchQueries"), GVehicleDebugParams.BatchQueries, TEXT("Enable/Disable Batching Of Suspension Raycasts."));
FAutoConsoleVariableRef CVarChaosVehiclesForceDebugScaling(TEXT("p.Vehicle.SetForceDebugScaling"), GVehicleDebugParams.ForceDebugScaling, TEXT("Set Scaling For Force Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesSleepCounterThreshold(TEXT("p.Vehicle.SleepCounterThreshold"), GVehicleDebugParams.SleepCounterThreshold, TEXT("Set The Sleep Counter Iteration Threshold."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableVehicleSleep(TEXT("p.Vehicle.DisableVehicleSleep"), GVehicleDebugParams.DisableVehicleSleep, TEXT("Disable Vehicle Agressive Sleeping."));


void FVehicleState::CaptureState(FBodyInstance* TargetInstance, float GravityZ, float DeltaTime)
{
	if (TargetInstance)
	{
		VehicleUpAxis = VehicleWorldTransform.GetUnitAxis(EAxis::Z);
		VehicleForwardAxis = VehicleWorldTransform.GetUnitAxis(EAxis::X);
		VehicleRightAxis = VehicleWorldTransform.GetUnitAxis(EAxis::Y);

		VehicleWorldTransform = TargetInstance->GetUnrealWorldTransform();
		VehicleWorldVelocity = TargetInstance->GetUnrealWorldVelocity();
		VehicleWorldAngularVelocity = TargetInstance->GetUnrealWorldAngularVelocityInRadians();
		VehicleWorldCOM = TargetInstance->GetCOMPosition();
		WorldVelocityNormal = VehicleWorldVelocity.GetSafeNormal();

		VehicleLocalVelocity = VehicleWorldTransform.InverseTransformVector(VehicleWorldVelocity);
		LocalAcceleration = (VehicleLocalVelocity - LastFrameVehicleLocalVelocity) / DeltaTime;
		LocalGForce = LocalAcceleration / FMath::Abs(GravityZ);
		LastFrameVehicleLocalVelocity = VehicleLocalVelocity;

		ForwardSpeed = FVector::DotProduct(VehicleWorldVelocity, VehicleForwardAxis);
		ForwardsAcceleration = LocalAcceleration.X;
	}
}

UChaosVehicleMovementComponent::UChaosVehicleMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReverseAsBrake = true;
	Mass = 1500.0f;
	ChassisWidth = 180.f;
	ChassisHeight = 140.f;
	DragCoefficient = 0.3f;
	DownforceCoefficient = 0.3f;
	InertiaTensorScale = FVector( 1.0f, 1.0f, 1.0f );
	SleepThreshold = 10.0f;
	SleepSlopeLimit = 0.866f;	// 30 degrees, Cos(30)

	TorqueControl.InitDefaults();
	TargetRotationControl.InitDefaults();
	StabilizeControl.InitDefaults();

	AngErrorAccumulator = 0.0f;

	IdleBrakeInput = 0.0f;
	StopThreshold = 10.0f; 
	WrongDirectionThreshold = 100.f;
	ThrottleInputRate.RiseRate = 6.0f;
	ThrottleInputRate.FallRate = 10.0f;
	BrakeInputRate.RiseRate = 6.0f;
	BrakeInputRate.FallRate = 10.0f;
	SteeringInputRate.RiseRate = 2.5f;
	SteeringInputRate.FallRate = 5.0f;
	HandbrakeInputRate.RiseRate = 12.0f;
	HandbrakeInputRate.FallRate = 12.0f;
	PitchInputRate.RiseRate = 6.0f;
	PitchInputRate.FallRate = 10.0f;
	RollInputRate.RiseRate = 6.0f;
	RollInputRate.FallRate = 10.0f;
	YawInputRate.RiseRate = 6.0f;
	YawInputRate.FallRate = 10.0f;

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
		UE_LOG(LogVehicle, Warning, TEXT("Can't create vehicle %s (%s). UpdatedComponent is not set."), *ActorName, *GetPathName());
		return false;
	}

	if (UpdatedPrimitive == NULL)
	{
		UE_LOG(LogVehicle, Warning, TEXT("Can't create vehicle %s (%s). UpdatedComponent is not a PrimitiveComponent."), *ActorName, *GetPathName());
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
			CreateVehicle();
			FixupSkeletalMesh();

			if (PVehicle)
			{
				FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(PhysScene);
				VehicleManager->AddVehicle(this);
			}
		}
	}

	FBodyInstance* BodyInstance = nullptr;
	if (USkeletalMeshComponent* SkeletalMesh = GetSkeletalMesh())
	{
		SkeletalMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		BodyInstance = &SkeletalMesh->BodyInstance;
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
	// movement updates and replication
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (PVehicle && UpdatedComponent && TargetInstance)
	{
		APawn* MyOwner = Cast<APawn>(UpdatedComponent->GetOwner());
		if (MyOwner)
		{
			if (!GVehicleDebugParams.DisableVehicleSleep)
			{
				ProcessSleeping();
			}

			if (!VehicleState.bSleeping)
			{
				UpdateSimulation(DeltaTime);
			}
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	DrawDebug3D();
#endif
}

void UChaosVehicleMovementComponent::StopMovementImmediately()
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (TargetInstance)
	{
		TargetInstance->SetLinearVelocity(FVector::ZeroVector, false);
		TargetInstance->SetAngularVelocityInRadians(FVector::ZeroVector, false);
		TargetInstance->ClearForces();
		TargetInstance->ClearTorques();
	}
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
	RawThrottleInput = FMath::Clamp(RawThrottleInput + ThrottleDelta, 0.f, 1.0f);
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
		PVehicle->GetTransmission().SetGear(GearNum, bImmediate);
	}
}

void UChaosVehicleMovementComponent::SetUseAutomaticGears(bool bUseAuto)
{
	if (PVehicle.IsValid() && PVehicle->HasTransmission())
	{
		Chaos::ETransmissionType TransmissionType = bUseAuto ? Chaos::ETransmissionType::Automatic : Chaos::ETransmissionType::Manual;
		PVehicle->GetTransmission().AccessSetup().TransmissionType = TransmissionType;
	}
}

// Data access

int32 UChaosVehicleMovementComponent::GetCurrentGear() const
{
	int32 CurrentGear = 0;

	if (PVehicle.IsValid() && PVehicle->HasTransmission())
	{
		CurrentGear = PVehicle->GetTransmission().GetCurrentGear();
	}

	return CurrentGear;
}

int32 UChaosVehicleMovementComponent::GetTargetGear() const
{
	int32 TargetGear = 0;

	if (PVehicle.IsValid() && PVehicle->HasTransmission())
	{
		TargetGear = PVehicle->GetTransmission().GetTargetGear();
	}

	return TargetGear;
}

bool UChaosVehicleMovementComponent::GetUseAutoGears() const
{
	bool UseAutoGears = 0;

	if (PVehicle.IsValid() && PVehicle->HasTransmission())
	{
		UseAutoGears = PVehicle->GetTransmission().Setup().TransmissionType == Chaos::ETransmissionType::Automatic;
	}

	return UseAutoGears;
}

float UChaosVehicleMovementComponent::GetForwardSpeed() const
{
	return VehicleState.ForwardSpeed;
}

float UChaosVehicleMovementComponent::GetForwardSpeedMPH() const
{
	return Chaos::CmSToMPH(GetForwardSpeed());
}


// input related
float UChaosVehicleMovementComponent::CalcSteeringInput()
{
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
				NewBrakeInput = 1.0f;
			}
		}
		// if player isn't pressing forward or backwards...
		else
		{
			if (VehicleState.ForwardSpeed < StopThreshold && VehicleState.ForwardSpeed > -StopThreshold)	//auto brake 
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
			if (VehicleState.ForwardSpeed < StopThreshold && VehicleState.ForwardSpeed > -StopThreshold)	//auto brake 
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
	float NewThrottleInput = RawThrottleInput;
	if (bReverseAsBrake && PVehicle->HasTransmission())
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

	AController* Controller = GetController();
	if (Controller && Controller->IsLocalController() && PVehicle)
	{
		ServerUpdateState(SteeringInput, ThrottleInput, BrakeInput, HandbrakeInput, CurrentGear, RollInput, PitchInput, YawInput);
	}
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

	// IsLocallyControlled will fail if the owner is unpossessed (i.e. Controller == nullptr);
	// Should we remove input instead of relying on replicated state in that case?
	if (Controller && Controller->IsLocalController() && PVehicle)
	{
		if (PVehicle->HasTransmission())
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
			else
			{
				if (PVehicle->GetTransmission().Setup().TransmissionType == Chaos::ETransmissionType::Automatic
					&& RawThrottleInput > KINDA_SMALL_NUMBER
					&& PVehicle->GetTransmission().GetCurrentGear() == 0
					&& PVehicle->GetTransmission().GetTargetGear() == 0)
				{
					SetTargetGear(1, true);
				}
			}
		}

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
		ServerUpdateState(SteeringInput, ThrottleInput, BrakeInput, HandbrakeInput, TargetGear, RollInput, PitchInput, YawInput);

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
		VehicleState.CaptureState(TargetInstance, GetGravityZ(), DeltaTime);

		ApplyAerodynamics(DeltaTime);
		ApplyAerofoilForces(DeltaTime);
		ApplyThrustForces(DeltaTime);
		ApplyTorqueControl(DeltaTime);
	}
}

/** Pass control Input to the vehicle systems */
void UChaosVehicleMovementComponent::ApplyInput(float DeltaTime)
{
	for (int AerofoilIdx = 0; AerofoilIdx < Aerofoils.Num(); AerofoilIdx++)
	{
		Chaos::FAerofoil& Aerofoil = PVehicle->GetAerofoil(AerofoilIdx);
		switch (Aerofoil.Setup().Type)
		{
			case Chaos::EAerofoilType::Rudder:
			Aerofoil.SetControlSurface(-YawInput);
			break;

			case Chaos::EAerofoilType::Elevator:
			Aerofoil.SetControlSurface(PitchInput);
			break;

			case Chaos::EAerofoilType::Wing:
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

	for (int Thrusterdx = 0; Thrusterdx < Thrusters.Num(); Thrusterdx++)
	{
		Chaos::FSimpleThrustSim& Thruster = PVehicle->GetThruster(Thrusterdx);

		Thruster.SetThrottle(ThrottleInput);

		switch (Thruster.Setup().Type)
		{
			case Chaos::EThrustType::HelicopterRotor:
			{
				Thruster.SetPitch(PitchInput);
				Thruster.SetRoll(RollInput);
			}
			break;

			case Chaos::EThrustType::Rudder:
			{
				Thruster.SetYaw(-YawInput - SteeringInput);
			}
			break;

			case Chaos::EThrustType::Elevator:
			{
				Thruster.SetPitch(PitchInput);
			}
			break;

			case Chaos::EThrustType::Wing:
			{
				if (Thruster.Setup().Offset.Y < 0.0f)
				{
					Thruster.SetRoll(RollInput);
				}
				else
				{
					Thruster.SetRoll(-RollInput);
				}
			}
			break;


		}

	}

}


void UChaosVehicleMovementComponent::ApplyAerodynamics(float DeltaTime)
{
	if (!GVehicleDebugParams.DisableAerodynamics)
	{
		// This force applied all the time whether the vehicle is on the ground or not
		Chaos::FSimpleAerodynamicsSim& PAerodynamics = PVehicle->GetAerodynamics();
		FVector LocalDragLiftForce = (PAerodynamics.GetCombinedForces(Chaos::CmToM(VehicleState.ForwardSpeed))) * Chaos::MToCmScaling();
		FVector WorldLiftDragForce = VehicleState.VehicleWorldTransform.TransformVector(LocalDragLiftForce);
		AddForce(WorldLiftDragForce);
	}
}

void UChaosVehicleMovementComponent::ApplyAerofoilForces(float DeltaTime)
{
	if (GVehicleDebugParams.DisableAerofoils || GetBodyInstance() == nullptr)
		return;

	TArray<FVector> VelocityLocal;
	TArray<FVector> VelocityWorld;
	VelocityLocal.SetNum(PVehicle->Aerofoils.Num());
	VelocityWorld.SetNum(PVehicle->Aerofoils.Num());

	float Altitude = VehicleState.VehicleWorldTransform.GetLocation().Z;

	// Work out velocity at each aerofoil before applying any forces so there's no bias on the first ones processed
	for (int AerofoilIdx = 0; AerofoilIdx < PVehicle->Aerofoils.Num(); AerofoilIdx++)
	{
		FVector WorldLocation = VehicleState.VehicleWorldTransform.TransformPosition(PVehicle->GetAerofoil(AerofoilIdx).Setup().Offset * Chaos::MToCmScaling());
		VelocityWorld[AerofoilIdx] = GetBodyInstance()->GetUnrealWorldVelocityAtPoint(WorldLocation);
		VelocityLocal[AerofoilIdx] = VehicleState.VehicleWorldTransform.InverseTransformVector(VelocityWorld[AerofoilIdx]);
	}

	for (int AerofoilIdx = 0; AerofoilIdx < PVehicle->Aerofoils.Num(); AerofoilIdx++)
	{
		Chaos::FAerofoil& Aerofoil = PVehicle->GetAerofoil(AerofoilIdx);

		FVector LocalForce = Aerofoil.GetForce(VehicleState.VehicleWorldTransform, VelocityLocal[AerofoilIdx] * Chaos::CmToMScaling(), Chaos::CmToM(Altitude), DeltaTime);

		FVector WorldForce = VehicleState.VehicleWorldTransform.TransformVector(LocalForce);
		FVector WorldLocation = VehicleState.VehicleWorldTransform.TransformPosition(Aerofoil.GetCenterOfLiftOffset() * Chaos::MToCmScaling());
		AddForceAtPosition(WorldForce * Chaos::MToCmScaling(), WorldLocation);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		FVector WorldAxis = VehicleState.VehicleWorldTransform.TransformVector(FVector::CrossProduct(FVector(1,0,0), Aerofoil.Setup().UpAxis));
		if (GVehicleDebugParams.ShowAerofoilSurface)
		{
			DrawDebugLine(GetWorld(), WorldLocation - WorldAxis * 150.0f, WorldLocation + WorldAxis * 150.0f, FColor::Black, false, -1.f, 0, 5.f);
		}
		if (GVehicleDebugParams.ShowAerofoilForces)
		{
			DrawDebugLine(GetWorld(), WorldLocation, WorldLocation + WorldForce * GVehicleDebugParams.ForceDebugScaling, FColor::Green, false, -1.f, 0, 16.f);
		}
#endif
	}

}


void UChaosVehicleMovementComponent::ApplyThrustForces(float DeltaTime)
{
	if (GVehicleDebugParams.DisableThrusters || GetBodyInstance() == nullptr)
		return;

	for (int ThrusterIdx = 0; ThrusterIdx < PVehicle->Thrusters.Num(); ThrusterIdx++)
	{
		Chaos::FSimpleThrustSim& Thruster = PVehicle->GetThruster(ThrusterIdx);

		FVector COM_Offset = GetBodyInstance()->GetMassSpaceLocal().GetLocation();
		COM_Offset.Z = 0.0f;
		Thruster.SetWorldVelocity(VehicleState.VehicleWorldVelocity);

		Thruster.Simulate(DeltaTime);
		FVector ThrustWorldLocation = VehicleState.VehicleWorldTransform.TransformPosition(Thruster.GetThrustLocation() + COM_Offset);
		FVector ThrustForce = VehicleState.VehicleWorldTransform.TransformPosition(Thruster.GetThrustForce());

		AddForceAtPosition(ThrustForce, ThrustWorldLocation);
	}

}


void UChaosVehicleMovementComponent::ApplyTorqueControl(float DeltaTime)
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (!GVehicleDebugParams.DisableTorqueControl && TargetInstance)
	{
		FVector TotalTorque = FVector::ZeroVector;
		if (TargetRotationControl.Enabled)
		{
			auto ComputeTorque = [](const FVector& TargetUp, const FVector& CurrentUp, const FVector& AngVelocityWorld, float Stiffness, float Damping, float MaxAccel) -> FVector
			{
				const FQuat CurUpToTargetUp = FQuat::FindBetweenNormals(CurrentUp, TargetUp);
				const FVector Axis = CurUpToTargetUp.GetRotationAxis();
				const float Angle = CurUpToTargetUp.GetAngle();

				float Strength = (Angle * Stiffness - FVector::DotProduct(AngVelocityWorld, Axis) * Damping);
				Strength = FMath::Clamp(Strength, -MaxAccel, MaxAccel);
				const FVector Torque = Axis * Strength;
				return Torque;
			};

			 
			FVector TargetUp = FVector(0.f, 0.f, 1.f);
			float RollMaxAngleRadians = Chaos::DegToRad(TargetRotationControl.RollMaxAngle);
			float PitchMaxAngleRadians = Chaos::DegToRad(TargetRotationControl.PitchMaxAngle);
			float Speed = FMath::Min(Chaos::CmToM(VehicleState.ForwardSpeed), 20.0f); // cap here

			float SpeeScaledRollAmount = 1.0f;
			float TargetRoll = 0.f;
			if (TargetRotationControl.bRollVsSpeedEnabled)
			{
				if (PVehicle->Wheels[0].InContact()) // HACK need IsAllowedToSteer virtual method
				{
					TargetRoll = SteeringInput * TargetRotationControl.RollControlScaling * (Speed * Speed) * DeltaTime * 60.0f;
				}
			}
			else
			{
				TargetRoll = SteeringInput * TargetRotationControl.RollControlScaling;
			}

			FVector Rt = VehicleState.VehicleRightAxis * FMath::Max(FMath::Min(TargetRoll, RollMaxAngleRadians), -RollMaxAngleRadians);
			FVector Pt = VehicleState.VehicleForwardAxis * FMath::Max(FMath::Min(PitchInput * TargetRotationControl.PitchControlScaling, PitchMaxAngleRadians), -PitchMaxAngleRadians);

			FVector UseUp = TargetUp + Rt + Pt;
			UseUp.Normalize();

			TargetUp = UseUp;

			const FVector UpVector = VehicleState.VehicleUpAxis;
			const FVector AngVelocityWorld = VehicleState.VehicleWorldAngularVelocity;

			const FVector AirControlTorque = ComputeTorque(TargetUp, UpVector, AngVelocityWorld,TargetRotationControl.RotationStiffness, TargetRotationControl.RotationDamping, TargetRotationControl.MaxAccel);
			const FVector ForwardVector = VehicleState.VehicleForwardAxis;
			const FVector RightVector = VehicleState.VehicleRightAxis;

			const float RollAirControl = FVector::DotProduct(AirControlTorque, ForwardVector);
			const float PitchAirControl = FVector::DotProduct(AirControlTorque, RightVector);
			const float YawAirControl = FVector::DotProduct(AirControlTorque, UpVector);

			TotalTorque = RollAirControl * ForwardVector * TargetRotationControl.AutoCentreRollStrength
				+ YawAirControl * UpVector * TargetRotationControl.AutoCentreYawStrength
				+ PitchAirControl * RightVector * TargetRotationControl.AutoCentrePitchStrength;
		}

		if (TorqueControl.Enabled)
		{
			TotalTorque -= VehicleState.VehicleForwardAxis * RollInput * TorqueControl.RollTorqueScaling;
			TotalTorque += VehicleState.VehicleRightAxis * PitchInput * TorqueControl.PitchTorqueScaling;
			TotalTorque += VehicleState.VehicleUpAxis * YawInput * TorqueControl.YawTorqueScaling;
			TotalTorque += VehicleState.VehicleUpAxis * RollInput * TorqueControl.YawFromRollTorqueScaling;

			// slowing rotation effect
			FVector DampingTorque = (VehicleState.VehicleWorldAngularVelocity/* / DeltaTime*/) * TorqueControl.RotationDamping;

			// combined world torque
			TotalTorque -= DampingTorque;
		}

		AddTorqueInRadians(TotalTorque /*/ DeltaTime * 0.016f*/, /*bAllowSubstepping=*/true, /*bAccelChange=*/true);
	}


	if (!GVehicleDebugParams.DisableStabilizeControl && StabilizeControl.Enabled && TargetInstance)
	{
		// try to cancel out velocity on Z axis
		FVector CorrectionalForce = FVector::ZeroVector;
		{
			bool MaintainAltitude = true;
			if (MaintainAltitude)
			{
				CorrectionalForce.Z = -StabilizeControl.AltitudeHoldZ * VehicleState.VehicleWorldVelocity.Z / DeltaTime;
			}
		}

		// try to cancel out velocity on X/Y plane
		// #todo: Will break helicopter setup??if (FMath::Abs(RollInput) < SMALL_NUMBER && FMath::Abs(PitchInput) < SMALL_NUMBER)
		{
			CorrectionalForce.X = -StabilizeControl.PositionHoldXY * VehicleState.VehicleWorldVelocity.X / DeltaTime;
			CorrectionalForce.Y = -StabilizeControl.PositionHoldXY * VehicleState.VehicleWorldVelocity.Y / DeltaTime;
		}
		AddForce(CorrectionalForce);
	}
}

void UChaosVehicleMovementComponent::ProcessSleeping()
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (TargetInstance)
	{
		bool PrevSleeping = VehicleState.bSleeping;
		VehicleState.bSleeping = !TargetInstance->IsInstanceAwake();

		// The physics system has woken vehicle up due to a collision or something
		if (PrevSleeping && !VehicleState.bSleeping)
		{
			VehicleState.SleepCounter = 0;
		}

		// If the vehicle is locally controlled, we want to use the raw inputs to determine sleep.
		// However, if it's on the Server or is just being replicated to other Clients then there
		// won't be any Raw input. In that case, use ReplicatedState instead.
		
		// NOTE: Even on local clients, ReplicatedState will still be populated (the call to ServerUpdateState will
		//			be processed locally). Maybe we should *just* use ReplicatedState?

		// TODO: What about other inputs, like handbrake, roll, pitch, yaw?
		const AController* Controller = GetController();
		const bool bIsLocallyControlled = (Controller && Controller->IsLocalController());
		const bool bControlInputPressed = bIsLocallyControlled ?
			(RawThrottleInput >= SMALL_NUMBER) || (RawBrakeInput >= SMALL_NUMBER) || (FMath::Abs(RawSteeringInput) > SMALL_NUMBER) :
			(ReplicatedState.ThrottleInput >= SMALL_NUMBER) || (ReplicatedState.BrakeInput >= SMALL_NUMBER) || (FMath::Abs(ReplicatedState.SteeringInput) > SMALL_NUMBER);

		// Wake if control input pressed
		if (VehicleState.bSleeping && (bControlInputPressed || !VehicleState.bAllWheelsOnGround))
		{
			VehicleState.bSleeping = false;
			VehicleState.SleepCounter = 0;
			TargetInstance->WakeInstance();
		}
		else if (!VehicleState.bSleeping && !bControlInputPressed && VehicleState.bAllWheelsOnGround && (VehicleState.VehicleUpAxis.Z > SleepSlopeLimit))
		{
			float SpeedSqr = TargetInstance->GetUnrealWorldVelocity().SizeSquared();
			if (SpeedSqr < (SleepThreshold* SleepThreshold))
			{
				if (VehicleState.SleepCounter < GVehicleDebugParams.SleepCounterThreshold)
				{
					VehicleState.SleepCounter++;
				}
				else
				{
					VehicleState.bSleeping = true;
					TargetInstance->PutInstanceToSleep();
				}
			}
		}
	}
}

/// @cond DOXYGEN_WARNINGS

bool UChaosVehicleMovementComponent::ServerUpdateState_Validate(float InSteeringInput, float InThrottleInput, float InBrakeInput, float InHandbrakeInput, int32 InCurrentGear, float InRollInput, float InPitchInput, float InYawInput)
{
	return true;
}

void UChaosVehicleMovementComponent::ServerUpdateState_Implementation(float InSteeringInput, float InThrottleInput, float InBrakeInput
	, float InHandbrakeInput, int32 InCurrentGear, float InRollInput, float InPitchInput, float InYawInput)
{
	SteeringInput = InSteeringInput;
	ThrottleInput = InThrottleInput;
	BrakeInput = InBrakeInput;
	HandbrakeInput = InHandbrakeInput;
	RollInput = InRollInput;
	PitchInput = InPitchInput;
	YawInput = InYawInput;

	if (!GetUseAutoGears())
	{
		SetTargetGear(InCurrentGear, true);
	}

	// update state of inputs
	ReplicatedState.SteeringInput = InSteeringInput;
	ReplicatedState.ThrottleInput = InThrottleInput;
	ReplicatedState.BrakeInput = InBrakeInput;
	ReplicatedState.HandbrakeInput = InHandbrakeInput;
	ReplicatedState.TargetGear = InCurrentGear;
	ReplicatedState.RollInput = InRollInput;
	ReplicatedState.PitchInput = InPitchInput;
	ReplicatedState.YawInput = InYawInput;

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


UMeshComponent* UChaosVehicleMovementComponent::GetMesh() const
{
	return Cast<UMeshComponent>(UpdatedComponent);
}

USkeletalMeshComponent* UChaosVehicleMovementComponent::GetSkeletalMesh()
{
	return Cast<USkeletalMeshComponent>(UpdatedComponent);
}

UStaticMeshComponent* UChaosVehicleMovementComponent::GetStaticMesh()
{
	return Cast<UStaticMeshComponent>(UpdatedComponent);
}

FVector UChaosVehicleMovementComponent::LocateBoneOffset(const FName InBoneName, const FVector& InExtraOffset) const
{
	FVector Offset = InExtraOffset;

	if (InBoneName != NAME_None)
	{
		if (USkinnedMeshComponent* Mesh = Cast<USkinnedMeshComponent>(GetMesh()))
		{
			check(Mesh->SkeletalMesh);
			const FVector BonePosition = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(InBoneName).GetOrigin() * Mesh->GetRelativeScale3D();
			//BonePosition is local for the root BONE of the skeletal mesh - however, we are using the Root BODY which may have its own transform, so we need to return the position local to the root BODY
			FMatrix RootBodyMTX = FMatrix::Identity;
			// Body Instance is no longer valid at this point in the code
			if (Mesh->GetBodyInstance())
			{
				RootBodyMTX = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(Mesh->GetBodyInstance()->BodySetup->BoneName);
			}
			const FVector LocalBonePosition = RootBodyMTX.InverseTransformPosition(BonePosition);
			Offset += LocalBonePosition;
		}
	}
	return Offset;
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
				// Low level physics representation
				CreatePhysicsVehicle();

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
		Chaos::FAerofoil AerofoilSim(&AerofoilSetup.GetPhysicsAerofoilConfig(*this));
		PVehicle->Aerofoils.Add(AerofoilSim);
	}

	for (FVehicleThrustConfig& ThrustSetup : Thrusters)
	{
		Chaos::FSimpleThrustSim ThrustSim(&ThrustSetup.GetPhysicsThrusterConfig(*this));
		PVehicle->Thrusters.Add(ThrustSim);
	}
}

void UChaosVehicleMovementComponent::PostSetupVehicle()
{
}

void UChaosVehicleMovementComponent::SetupVehicleMass()
{
	if (UpdatedPrimitive && UpdatedPrimitive->GetBodyInstance())
	{
		//Ensure that if mass properties ever change we set them back to our override
		UpdatedPrimitive->GetBodyInstance()->OnRecalculatedMassProperties().AddUObject(this, &UChaosVehicleMovementComponent::UpdateMassProperties);

		UpdateMassProperties(UpdatedPrimitive->GetBodyInstance());
	}
}

void UChaosVehicleMovementComponent::UpdateMassProperties(FBodyInstance* BodyInstance)
{
	if (BodyInstance && FPhysicsInterface::IsValid(BodyInstance->ActorHandle) && FPhysicsInterface::IsRigidBody(BodyInstance->ActorHandle))
	{
		FPhysicsCommand::ExecuteWrite(BodyInstance->ActorHandle, [&](FPhysicsActorHandle& Actor)
			{
				const float MassRatio = this->Mass > 0.0f ? this->Mass / BodyInstance->GetBodyMass() : 1.0f;

				FVector InertiaTensor = BodyInstance->GetBodyInertiaTensor();

				InertiaTensor.X *= this->InertiaTensorScale.X * MassRatio;
				InertiaTensor.Y *= this->InertiaTensorScale.Y * MassRatio;
				InertiaTensor.Z *= this->InertiaTensorScale.Z * MassRatio;

				FPhysicsInterface::SetMassSpaceInertiaTensor_AssumesLocked(Actor, InertiaTensor);
				FPhysicsInterface::SetMass_AssumesLocked(Actor, this->Mass);
			});
	}

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
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (!PVehicle.IsValid() || TargetInstance == nullptr)
	{
		return;
	}

	UFont* RenderFont = GEngine->GetMediumFont();
	// draw general vehicle data
	{
		Canvas->SetDrawColor(FColor::White);
		YPos += 16;

		float ForwardSpeedKmH = Chaos::CmSToKmH(GetForwardSpeed());
		float ForwardSpeedMPH = Chaos::CmSToMPH(GetForwardSpeed());
		float ForwardSpeedMSec = Chaos::CmToM(GetForwardSpeed());

		if (TargetInstance)
		{
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Mass (Kg): %.1f"), TargetInstance->GetBodyMass()), 4, YPos);
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Inertia : %s"), *TargetInstance->GetBodyInertiaTensor().ToString()), 4, YPos);
		}

		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Awake %d"), TargetInstance->IsInstanceAwake()), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Speed (km/h): %.1f  (MPH): %.1f  (m/s): %.1f"), ForwardSpeedKmH, ForwardSpeedMPH, ForwardSpeedMSec), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Acceleration (m/s-2): %.1f"), Chaos::CmToM(VehicleState.LocalAcceleration.X)), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("GForce : %2.1f"), VehicleState.LocalGForce.X), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Steering: %.1f (RAW %.1f)"), SteeringInput, RawSteeringInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Throttle: %.1f (RAW %.1f)"), ThrottleInput, RawThrottleInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Brake: %.1f (RAW %.1f)"), BrakeInput, RawBrakeInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Roll: %.1f (RAW %.1f)"), RollInput, RawRollInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Pitch: %.1f (RAW %.1f)"), PitchInput, RawPitchInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Yaw: %.1f (RAW %.1f)"), YawInput, RawYawInput), 4, YPos);
		FString GearState = GetUseAutoGears() ? "Automatic" : "Manual";
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Gears: %s"), *GearState), 4, YPos);
	}

#endif
}

void UChaosVehicleMovementComponent::DrawDebug3D()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
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
#endif
}

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


void UChaosVehicleMovementComponent::AddForce(const FVector& Force, bool bAllowSubstepping /*= true*/, bool bAccelChange /*= false*/)
{
	check(GetBodyInstance());
	GetBodyInstance()->AddForce(Force, bAllowSubstepping, bAccelChange);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GVehicleDebugParams.ShowAllForces)
	{
		FVector Position = VehicleState.VehicleWorldCOM;
		DrawDebugDirectionalArrow(GetWorld(), Position, Position + Force * GVehicleDebugParams.ForceDebugScaling
			, 20.f, FColor::Blue, false, 0, 0, 2.f);
	}
#endif
}

void UChaosVehicleMovementComponent::AddForceAtPosition(const FVector& Force, const FVector& Position, bool bAllowSubstepping /*= true*/, bool bIsLocalForce /*= false*/)
{
	check(GetBodyInstance());
	GetBodyInstance()->AddForceAtPosition(Force, Position, bAllowSubstepping, bIsLocalForce);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GVehicleDebugParams.ShowAllForces)
	{
		DrawDebugDirectionalArrow(GetWorld(), Position, Position + Force * GVehicleDebugParams.ForceDebugScaling
			, 20.f, FColor::Blue, false, 0, 0, 2.f);
	}
#endif
}

void UChaosVehicleMovementComponent::AddImpulse(const FVector& Impulse, bool bVelChange)
{
	check(GetBodyInstance());
	GetBodyInstance()->AddImpulse(Impulse, bVelChange);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GVehicleDebugParams.ShowAllForces)
	{
		FVector Position = VehicleState.VehicleWorldCOM;
		DrawDebugDirectionalArrow(GetWorld(), Position, Position + Impulse * GVehicleDebugParams.ForceDebugScaling
			, 20.f, FColor::Red, false, 0, 0, 2.f);
	}
#endif
}

void UChaosVehicleMovementComponent::AddImpulseAtPosition(const FVector& Impulse, const FVector& Position)
{
	check(GetBodyInstance());
	GetBodyInstance()->AddImpulseAtPosition(Impulse, Position);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GVehicleDebugParams.ShowAllForces)
	{
		DrawDebugDirectionalArrow(GetWorld(), Position, Position + Impulse * GVehicleDebugParams.ForceDebugScaling
			, 20.f, FColor::Red, false, 0, 0, 2.f);
	}
#endif

}

void UChaosVehicleMovementComponent::AddTorqueInRadians(const FVector& Torque, bool bAllowSubstepping /*= true*/, bool bAccelChange /*= false*/)
{
	check(GetBodyInstance());
	GetBodyInstance()->AddTorqueInRadians(Torque, bAllowSubstepping, bAccelChange);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	//#todo: how do we visualize torque?
#endif
}

void UChaosVehicleMovementComponent::CreatePhysicsVehicle()
{
	PVehicle = MakeUnique<Chaos::FSimpleWheeledVehicle>();
}


void FVehicleAerofoilConfig::FillAerofoilSetup(const UChaosVehicleMovementComponent& MovementComponent)
{
	PAerofoilConfig.Type = (Chaos::EAerofoilType)(this->AerofoilType);
	PAerofoilConfig.Offset = MovementComponent.LocateBoneOffset(this->BoneName, this->Offset);
	PAerofoilConfig.UpAxis = this->UpAxis;
	PAerofoilConfig.Area = this->Area;
	PAerofoilConfig.Camber = this->Camber;
	PAerofoilConfig.MaxControlAngle = this->MaxControlAngle;
	PAerofoilConfig.StallAngle = this->StallAngle;
	PAerofoilConfig.LiftMultiplier = this->LiftMultiplier;
	PAerofoilConfig.DragMultiplier = this->DragMultiplier;
}

void FVehicleThrustConfig::FillThrusterSetup(const UChaosVehicleMovementComponent& MovementComponent)
{
	PThrusterConfig.Type = (Chaos::EThrustType)(this->ThrustType);
	PThrusterConfig.Offset = MovementComponent.LocateBoneOffset(this->BoneName, this->Offset);
	PThrusterConfig.Axis = this->ThrustAxis;
	//	PThrusterConfig.ThrustCurve = this->ThrustCurve;
	PThrusterConfig.MaxThrustForce = Chaos::MToCm(this->MaxThrustForce);
	PThrusterConfig.MaxControlAngle = this->MaxControlAngle;
}

#undef LOCTEXT_NAMESPACE


#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif

