// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "VehicleAnimationInstance.h"
#include "ChaosVehicleManager.h"
#include "ChaosVehicleWheel.h"
#include "SuspensionUtility.h"
#include "SteeringUtility.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Chaos/PBDSuspensionConstraintData.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#endif

using namespace Chaos;

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif


FWheeledVehicleDebugParams GWheeledVehicleDebugParams;
extern FVehicleDebugParams GVehicleDebugParams;

EDebugPages UChaosWheeledVehicleMovementComponent::DebugPage = EDebugPages::BasicPage;

FAutoConsoleVariableRef CVarChaosVehiclesShowWheelCollisionNormal(TEXT("p.Vehicle.ShowWheelCollisionNormal"), GWheeledVehicleDebugParams.ShowWheelCollisionNormal, TEXT("Enable/Disable Wheel Collision Normal Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowSuspensionRaycasts(TEXT("p.Vehicle.ShowSuspensionRaycasts"), GWheeledVehicleDebugParams.ShowSuspensionRaycasts, TEXT("Enable/Disable Suspension Raycast Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowSuspensionLimits(TEXT("p.Vehicle.ShowSuspensionLimits"), GWheeledVehicleDebugParams.ShowSuspensionLimits, TEXT("Enable/Disable Suspension Limits Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowWheelForces(TEXT("p.Vehicle.ShowWheelForces"), GWheeledVehicleDebugParams.ShowWheelForces, TEXT("Enable/Disable Wheel Forces Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowSuspensionForces(TEXT("p.Vehicle.ShowSuspensionForces"), GWheeledVehicleDebugParams.ShowSuspensionForces, TEXT("Enable/Disable Suspension Forces Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowBatchQueryExtents(TEXT("p.Vehicle.ShowBatchQueryExtents"), GWheeledVehicleDebugParams.ShowBatchQueryExtents, TEXT("Enable/Disable Suspension Forces Visualisation."));

FAutoConsoleVariableRef CVarChaosVehiclesDisableSuspensionForces(TEXT("p.Vehicle.DisableSuspensionForces"), GWheeledVehicleDebugParams.DisableSuspensionForces, TEXT("Enable/Disable Suspension Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableFrictionForces(TEXT("p.Vehicle.DisableFrictionForces"), GWheeledVehicleDebugParams.DisableFrictionForces, TEXT("Enable/Disable Wheel Friction Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableRollbarForces(TEXT("p.Vehicle.DisableRollbarForces"), GWheeledVehicleDebugParams.DisableRollbarForces, TEXT("Enable/Disable Rollbar Forces."));

FAutoConsoleVariableRef CVarChaosVehiclesThrottleOverride(TEXT("p.Vehicle.ThrottleOverride"), GWheeledVehicleDebugParams.ThrottleOverride, TEXT("Hard code throttle input on."));
FAutoConsoleVariableRef CVarChaosVehiclesSteeringOverride(TEXT("p.Vehicle.SteeringOverride"), GWheeledVehicleDebugParams.SteeringOverride, TEXT("Hard code steering input on."));

FAutoConsoleVariableRef CVarChaosVehiclesResetMeasurements(TEXT("p.Vehicle.ResetMeasurements"), GWheeledVehicleDebugParams.ResetPerformanceMeasurements, TEXT("Reset Vehicle Performance Measurements."));

FAutoConsoleVariableRef CVarChaosVehiclesDisableSuspensionConstraints(TEXT("p.Vehicle.DisableSuspensionConstraint"), GWheeledVehicleDebugParams.DisableSuspensionConstraint, TEXT("Enable/Disable Suspension Constraints."));

FAutoConsoleCommand CVarCommandVehiclesNextDebugPage(
	TEXT("p.Vehicle.NextDebugPage"),
	TEXT("Display the next page of vehicle debug data."),
	FConsoleCommandDelegate::CreateStatic(UChaosWheeledVehicleMovementComponent::NextDebugPage));

FAutoConsoleCommand CVarCommandVehiclesPrevDebugPage(
	TEXT("p.Vehicle.PrevDebugPage"),
	TEXT("Display the previous page of vehicle debug data."),
	FConsoleCommandDelegate::CreateStatic(UChaosWheeledVehicleMovementComponent::PrevDebugPage));


FString FWheelStatus::ToString() const
{
	return FString::Printf(TEXT("bInContact:%s ContactPoint:%s PhysMaterial:%s NormSuspensionLength:%f SpringForce:%f bIsSlipping:%s SlipMagnitude:%f bIsSkidding:%s SkidMagnitude:%f SkidNormal:%s"),
		bInContact == true ? TEXT("True") : TEXT("False"),
		*ContactPoint.ToString(),
		PhysMaterial.IsValid() ? *PhysMaterial->GetName() : TEXT("None"),
		NormalizedSuspensionLength,
		SpringForce,
		bIsSlipping == true ? TEXT("True") : TEXT("False"),
		SlipMagnitude,
		bIsSkidding == true ? TEXT("True") : TEXT("False"),
		SkidMagnitude,
		*SkidNormal.ToString());
}

void FWheelState::CaptureState(int WheelIdx, const FVector& WheelOffset, const FBodyInstance* TargetInstance)
{
	check(TargetInstance);
	const FTransform WorldTransform = TargetInstance->GetUnrealWorldTransform();
	WheelWorldLocation[WheelIdx] = WorldTransform.TransformPosition(WheelOffset);
	WorldWheelVelocity[WheelIdx] = TargetInstance->GetUnrealWorldVelocityAtPoint(WheelWorldLocation[WheelIdx]);
	LocalWheelVelocity[WheelIdx] = WorldTransform.InverseTransformVector(WorldWheelVelocity[WheelIdx]);
}

UChaosWheeledVehicleMovementComponent::UChaosWheeledVehicleMovementComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// default values setup

	EngineSetup.InitDefaults();
	DifferentialSetup.InitDefaults();
	TransmissionSetup.InitDefaults();
	SteeringSetup.InitDefaults();

	// It's possible to switch whole systems off if they are not required
	bMechanicalSimEnabled = true;
	bSuspensionEnabled = true;
	bWheelFrictionEnabled = true;

	NumDrivenWheels = 0;

}

// Public
void UChaosWheeledVehicleMovementComponent::Serialize(FArchive & Ar)
{
	Super::Serialize(Ar);

	// custom serialization goes here..
}

#if WITH_EDITOR
void UChaosWheeledVehicleMovementComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	RecalculateAxles();
}
#endif

void UChaosWheeledVehicleMovementComponent::FixupSkeletalMesh()
{
	Super::FixupSkeletalMesh();

	if (USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(GetMesh()))
	{
		if (UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset())
		{
			for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
			{
				FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
				if (WheelSetup.BoneName != NAME_None)
				{
					int32 BodySetupIdx = PhysicsAsset->FindBodyIndex(WheelSetup.BoneName);

					if (BodySetupIdx >= 0 && (BodySetupIdx < Mesh->Bodies.Num()))
					{
						FBodyInstance* BodyInstanceWheel = Mesh->Bodies[BodySetupIdx];
						BodyInstanceWheel->SetResponseToAllChannels(ECR_Ignore);	//turn off collision for wheel automatically

						if (UBodySetup* BodySetup = PhysicsAsset->SkeletalBodySetups[BodySetupIdx])
						{

							{
								BodyInstanceWheel->SetInstanceSimulatePhysics(false);
								//BodyInstanceWheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							}

							bool DeleteOriginalWheelConstraints = true;
							if (DeleteOriginalWheelConstraints)
							{
								//and get rid of constraints on the wheels. TODO: right now we remove all wheel constraints, we probably only want to remove parent constraints
								TArray<int32> WheelConstraints;
								PhysicsAsset->BodyFindConstraints(BodySetupIdx, WheelConstraints);
								for (int32 ConstraintIdx = 0; ConstraintIdx < WheelConstraints.Num(); ++ConstraintIdx)
								{
									FConstraintInstance* ConInst = Mesh->Constraints[WheelConstraints[ConstraintIdx]];
									ConInst->TermConstraint();
								}
							}
						}

						if (!GWheeledVehicleDebugParams.DisableSuspensionConstraint)
						{
							FBodyInstance* TargetInstance = UpdatedPrimitive->GetBodyInstance();

							FPhysicsCommand::ExecuteWrite(TargetInstance->ActorHandle, [&](const FPhysicsActorHandle& Chassis)
								{
#if WITH_CHAOS
									const FVector LocalWheel = GetWheelRestingPosition(WheelSetup);
									FPhysicsConstraintHandle ConstraintHandle = FPhysicsInterface::CreateSuspension(Chassis, LocalWheel);

									if (ConstraintHandle.IsValid())
									{
										const Chaos::FSimpleSuspensionConfig& SusSettings = PVehicle->GetSuspension(WheelIdx).Setup();
										ConstraintHandles.Add(ConstraintHandle);
										if (Chaos::FSuspensionConstraint* Constraint = static_cast<Chaos::FSuspensionConstraint*>(ConstraintHandle.Constraint))
										{
											Constraint->SetHardstopStiffness(1.0f);
											Constraint->SetSpringStiffness(SusSettings.SpringRate * 0.25f);
											Constraint->SetSpringPreload(SusSettings.SpringPreload);
											Constraint->SetSpringDamping(SusSettings.DampingRatio * 5.0f);
											Constraint->SetMinLength(-SusSettings.SuspensionMaxRaise);
											Constraint->SetMaxLength(SusSettings.SuspensionMaxDrop);
											Constraint->SetAxis(-SusSettings.SuspensionAxis);
										}
									}
#endif // WITH_CHAOS
								});
						}
					}
				}
			}
		}

		Mesh->KinematicBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipSimulatingBones;

	}

}


bool UChaosWheeledVehicleMovementComponent::CanCreateVehicle() const
{
	if (!Super::CanCreateVehicle())
		return false;

	check(GetOwner());
	FString ActorName = GetOwner()->GetName();

	for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
	{
		const FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];

		if (WheelSetup.WheelClass == NULL)
		{
			UE_LOG(LogVehicle, Warning, TEXT("Can't create vehicle %s (%s). Wheel %d is not set."), *ActorName, *GetPathName(), WheelIdx);
			return false;
		}

		if (WheelSetup.BoneName == NAME_None)
		{
			UE_LOG(LogVehicle, Warning, TEXT("Can't create vehicle %s (%s). Bone name for wheel %d is not set."), *ActorName, *GetPathName(), WheelIdx);
			return false;
		}

	}

	return true;
}

bool UChaosWheeledVehicleMovementComponent::CanSimulate() const
{
	if (Super::CanSimulate() == false)
	{
		return false;
	}

	return (PVehicle && PVehicle.IsValid()
		&& PVehicle->Engine.Num() == PVehicle->Transmission.Num()
		&& Wheels.Num() > 0 && Wheels.Num() == PVehicle->Suspension.Num());
}

void UChaosWheeledVehicleMovementComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	if (PVehicle)
	{
		CreateWheels();

		// Need to bind to the notify delegate on the mesh in case physics state is changed
		if (USkeletalMeshComponent* MeshComp = GetSkeletalMesh())
		{
			MeshOnPhysicsStateChangeHandle = MeshComp->RegisterOnPhysicsCreatedDelegate(FOnSkelMeshPhysicsCreated::CreateUObject(this, &UChaosWheeledVehicleMovementComponent::RecreatePhysicsState));
			if (UVehicleAnimationInstance* VehicleAnimInstance = Cast<UVehicleAnimationInstance>(MeshComp->GetAnimInstance()))
			{
				VehicleAnimInstance->SetWheeledVehicleComponent(this);
			}
		}
	}
}

void UChaosWheeledVehicleMovementComponent::OnDestroyPhysicsState()
{
	if (PVehicle.IsValid())
	{
		if (MeshOnPhysicsStateChangeHandle.IsValid())
		{
			if (USkeletalMeshComponent* MeshComp = GetSkeletalMesh())
			{
				MeshComp->UnregisterOnPhysicsCreatedDelegate(MeshOnPhysicsStateChangeHandle);
			}
		}

		DestroyWheels();

		if (ConstraintHandles.Num() > 0)
		{
			for (FPhysicsConstraintHandle ConstraintHandle : ConstraintHandles)
			{
				FPhysicsCommand::ExecuteWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& Constraint)
					{
						FPhysicsInterface::ReleaseConstraint(ConstraintHandle);
					});
			}
		}
		ConstraintHandles.Empty();
	}
	
	Super::OnDestroyPhysicsState();
	
}

void UChaosWheeledVehicleMovementComponent::TickVehicle(float DeltaTime)
{
	Super::TickVehicle(DeltaTime);

	// update wheels
	for (int32 i = 0; i < Wheels.Num(); i++)
	{
		UChaosVehicleWheel* VehicleWheel = Wheels[i];
		Wheels[i]->Tick(DeltaTime);
	}
}

void UChaosWheeledVehicleMovementComponent::NextDebugPage()
{
	int PageAsInt = (int)DebugPage;
	PageAsInt++;
	if (PageAsInt >= EDebugPages::MaxDebugPages)
	{
		PageAsInt = 0;
	}
	DebugPage = (EDebugPages)PageAsInt;
}

void UChaosWheeledVehicleMovementComponent::PrevDebugPage()
{
	int PageAsInt = (int)DebugPage;
	PageAsInt--;
	if (PageAsInt < 0)
	{
		PageAsInt = EDebugPages::MaxDebugPages - 1;
	}
	DebugPage = (EDebugPages)PageAsInt;
}


// Setup
void UChaosWheeledVehicleMovementComponent::ComputeConstants()
{
	Super::ComputeConstants();
}

void UChaosWheeledVehicleMovementComponent::CreateWheels()
{
	// Wheels num is getting copied when blueprint recompiles, so we have to manually reset here
	Wheels.Reset();

	// Instantiate the wheels
	for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
	{
		UChaosVehicleWheel* Wheel = NewObject<UChaosVehicleWheel>(this, WheelSetups[WheelIdx].WheelClass);
		check(Wheel);

		Wheels.Add(Wheel);
	}

	// Initialize the wheels
	for (int32 WheelIdx = 0; WheelIdx < Wheels.Num(); ++WheelIdx)
	{
		Wheels[WheelIdx]->Init(this, WheelIdx);
	}

	WheelStatus.SetNum(WheelSetups.Num());

	RecalculateAxles();
}

void UChaosWheeledVehicleMovementComponent::DestroyWheels()
{
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		Wheels[i]->Shutdown();
	}

	Wheels.Reset();
}

void UChaosWheeledVehicleMovementComponent::SetupVehicle()
{
	check(PVehicle);

	Super::SetupVehicle();

	// we are allowed any number of wheels not limited to only 4
	NumDrivenWheels = 0;
	for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
	{
		UChaosVehicleWheel* Wheel = WheelSetups[WheelIdx].WheelClass.GetDefaultObject();

		// create Dynamic states passing in pointer to their Static setup data
		Chaos::FSimpleWheelSim WheelSim(&Wheel->GetPhysicsWheelConfig());

		if (Wheel->GetAxleType() != EAxleType::Undefined)
		{
			bool EngineEnable = false;
			if (Wheel->GetAxleType() == EAxleType::Front)
			{
				if (DifferentialSetup.DifferentialType == EVehicleDifferential::AllWheelDrive
					|| DifferentialSetup.DifferentialType == EVehicleDifferential::FrontWheelDrive)
				{
					EngineEnable = true;
				}
			}
			else if (Wheel->GetAxleType() == EAxleType::Rear)
			{
				if (DifferentialSetup.DifferentialType == EVehicleDifferential::AllWheelDrive
					|| DifferentialSetup.DifferentialType == EVehicleDifferential::RearWheelDrive)
				{
					EngineEnable = true;
				}
			}

			WheelSim.AccessSetup().EngineEnabled = EngineEnable;
		}

		WheelSim.SetWheelRadius(Wheel->WheelRadius); // initial radius
		PVehicle->Wheels.Add(WheelSim);

		Chaos::FSimpleSuspensionSim SuspensionSim(&Wheel->GetPhysicsSuspensionConfig());
		PVehicle->Suspension.Add(SuspensionSim);

		if (WheelSim.Setup().EngineEnabled)
		{
			NumDrivenWheels++;
		}

		// for debugging to identify a single wheel
		PVehicle->Wheels[WheelIdx].SetWheelIndex(WheelIdx);
		PVehicle->Suspension[WheelIdx].SetSpringIndex(WheelIdx);
	}

	// cache this value as it's useful for steering setup calculations and debug rendering
	WheelTrackDimensions = CalculateWheelLayoutDimensions();

	if (bMechanicalSimEnabled)
	{
		Chaos::FSimpleEngineSim EngineSim(&EngineSetup.GetPhysicsEngineConfig());
		PVehicle->Engine.Add(EngineSim);

		Chaos::FSimpleTransmissionSim TransmissionSim(&TransmissionSetup.GetPhysicsTransmissionConfig());
		PVehicle->Transmission.Add(TransmissionSim);
	}

	Chaos::FSimpleSteeringSim SteeringSim(&SteeringSetup.GetPhysicsSteeringConfig(WheelTrackDimensions));
	PVehicle->Steering.Add(SteeringSim);

	WheelState.Init(PVehicle->Wheels.Num());

	// Setup the chassis and wheel shapes
	SetupVehicleShapes();

	// Setup mass properties
	SetupVehicleMass();

	// Setup Suspension
	SetupSuspension();

}

void UChaosWheeledVehicleMovementComponent::SetupVehicleShapes()
{
	if (!UpdatedPrimitive)
	{
		return;
	}

}

void UChaosWheeledVehicleMovementComponent::SetupSuspension()
{
	if (!PVehicle.IsValid())
	{
		return;
	}
	
	float TotalMass = this->Mass;
	ensureMsgf(TotalMass >= 1.0f, TEXT("The mass of this vehicle is too small."));

	TArray<FVector> LocalSpringPositions;

	// cache vehicle local position of springs
	for (int SpringIdx = 0; SpringIdx < PVehicle->Suspension.Num(); SpringIdx++)
	{
		auto& PSuspension = PVehicle->Suspension[SpringIdx];

		PSuspension.AccessSetup().MaxLength = PSuspension.Setup().SuspensionMaxDrop + PSuspension.Setup().SuspensionMaxRaise;

		FVector TotalOffset = GetWheelRestingPosition(WheelSetups[SpringIdx]);
		LocalSpringPositions.Add(TotalOffset);
		PVehicle->Suspension[SpringIdx].SetLocalRestingPosition(LocalSpringPositions[SpringIdx]);
	}

	// Calculate the mass that will rest on each of the springs
	TArray<float> OutSprungMasses;
	if (!FSuspensionUtility::ComputeSprungMasses(LocalSpringPositions, TotalMass, OutSprungMasses))
	{
		// if the sprung mass calc fails fall back to something that will still simulate
		for (int Index = 0; Index < OutSprungMasses.Num(); Index++)
		{
			OutSprungMasses[Index] = TotalMass / OutSprungMasses.Num();
		}
	}

	// Calculate spring damping values we will use for physics simulation from the normalized damping ratio
	for (int SpringIdx = 0; SpringIdx < PVehicle->Suspension.Num(); SpringIdx++)
	{
		auto& Susp = PVehicle->Suspension[SpringIdx];
		float NaturalFrequency = FSuspensionUtility::ComputeNaturalFrequency(Susp.Setup().SpringRate, OutSprungMasses[SpringIdx]);
		float Damping = FSuspensionUtility::ComputeDamping(Susp.Setup().SpringRate, OutSprungMasses[SpringIdx], Susp.Setup().DampingRatio);
		UE_LOG(LogChaos, Verbose, TEXT("Spring %d: OutNaturalFrequency %.1f Hz  (@1.0) DampingRate %.1f"), SpringIdx, NaturalFrequency / (2.0f * PI), Damping);

		PVehicle->Suspension[SpringIdx].AccessSetup().ReboundDamping = Damping;
		PVehicle->Suspension[SpringIdx].AccessSetup().CompressionDamping = Damping;
		PVehicle->Suspension[SpringIdx].AccessSetup().RestingForce = OutSprungMasses[SpringIdx] * -GetGravityZ();
	}

}

void UChaosWheeledVehicleMovementComponent::RecalculateAxles()
{
	AxleToWheelMap.Empty();

	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
		UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();

		if (auto* WheelIdxArray = AxleToWheelMap.Find(Wheel))
		{
			WheelIdxArray->Add(WheelIdx);
		}
		else
		{
			TArray<int> WheelIndices;
			WheelIndices.Add(WheelIdx);
			AxleToWheelMap.Add(Wheel, WheelIndices);
		}
	}
}

FVector UChaosWheeledVehicleMovementComponent::GetWheelRestingPosition(const FChaosWheelSetup& WheelSetup)
{
	FVector Offset = WheelSetup.WheelClass.GetDefaultObject()->Offset + WheelSetup.AdditionalOffset;
	return LocateBoneOffset(WheelSetup.BoneName, Offset);
}

// Update

void UChaosWheeledVehicleMovementComponent::UpdateSimulation(float DeltaTime)
{
	// Inherit common vehicle simulation stages ApplyAerodynamics, ApplyTorqueControl, etc
	Super::UpdateSimulation(DeltaTime);

	FBodyInstance* TargetInstance = GetBodyInstance();

	if (CanSimulate() && TargetInstance)
	{
		// sanity check that everything is setup ok
		ensure(Wheels.Num() == PVehicle->Suspension.Num());
		ensure(Wheels.Num() == PVehicle->Wheels.Num());
		ensure(WheelState.LocalWheelVelocity.Num() == Wheels.Num());
		ensure(WheelState.WheelWorldLocation.Num() == Wheels.Num());
		ensure(WheelState.WorldWheelVelocity.Num() == Wheels.Num());

		///////////////////////////////////////////////////////////////////////
		// Cache useful state so we are not re-calculating the same data

		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			WheelState.CaptureState(WheelIdx, PVehicle->Suspension[WheelIdx].GetLocalRestingPosition(), TargetInstance);
		}

		///////////////////////////////////////////////////////////////////////
		// Suspension Raycast

		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			auto& PSuspension = PVehicle->Suspension[WheelIdx];
			auto& PWheel = PVehicle->Wheels[WheelIdx];
			PSuspension.UpdateWorldRaycastLocation(VehicleState.VehicleWorldTransform, PWheel.Setup().WheelRadius, WheelState.Trace[WheelIdx]);
		}

		if (!GWheeledVehicleDebugParams.DisableSuspensionForces && bSuspensionEnabled)
		{
			PerformSuspensionTraces(WheelState.Trace);
		}

		//////////////////////////////////////////////////////////////////////////
		// Wheel and Vehicle in air state

		VehicleState.bVehicleInAir = true;
		VehicleState.NumWheelsOnGround = 0;
		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			// tell systems who care that wheel is touching the ground
			PVehicle->Wheels[WheelIdx].SetOnGround(Wheels[WheelIdx]->HitResult.bBlockingHit);

			// only requires one wheel to be on the ground for the vehicle to be NOT in the air
			if (PVehicle->Wheels[WheelIdx].InContact())
			{
				VehicleState.bVehicleInAir = false;
				VehicleState.NumWheelsOnGround++;
			}
		}
		VehicleState.bAllWheelsOnGround = (VehicleState.NumWheelsOnGround == Wheels.Num());

		///////////////////////////////////////////////////////////////////////
		// Input
		ApplyInput(DeltaTime);

		///////////////////////////////////////////////////////////////////////
		// Engine/Transmission
		if (!GWheeledVehicleDebugParams.DisableSuspensionForces && bMechanicalSimEnabled)
		{
			ProcessMechanicalSimulation(DeltaTime);
		}

		///////////////////////////////////////////////////////////////////////
		// Suspension

		if (!GWheeledVehicleDebugParams.DisableSuspensionForces && bSuspensionEnabled)
		{
			ApplySuspensionForces(DeltaTime);
		}

		///////////////////////////////////////////////////////////////////////
		// Steering

		ProcessSteering();

		///////////////////////////////////////////////////////////////////////
		// Wheel Friction

		if (!GWheeledVehicleDebugParams.DisableFrictionForces && bWheelFrictionEnabled)
		{		
			ApplyWheelFrictionForces(DeltaTime);
		}

		if (PerformanceMeasure.IsEnabled())
		{
			PerformanceMeasure.Update(DeltaTime, VehicleState.VehicleWorldTransform.GetLocation(), VehicleState.ForwardSpeed);
		}

		///////////////////////////////////////////////////////////////////////
		// Consolidate wheel output state
		FillWheelOutputState();

	}

}

void UChaosWheeledVehicleMovementComponent::PerformSuspensionTraces(const TArray<FSuspensionTrace>& SuspensionTrace)
{
	//#todo: SpringCollisionChannel should be a parameter setup
	ECollisionChannel SpringCollisionChannel = ECollisionChannel::ECC_WorldDynamic;

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(GetPawnOwner()); // ignore self in scene query

	FCollisionQueryParams TraceParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(), false, nullptr);
	TraceParams.bReturnPhysicalMaterial = true;	// we need this to get the surface friction coefficient
	TraceParams.AddIgnoredActors(ActorsToIgnore);

	FCollisionResponseParams ResponseParams;

	// batching is about 0.5ms (25%) faster when there's 100 vehicles on a flat terrain
	if (GVehicleDebugParams.BatchQueries)
	{
		FBox QueryBox;
		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{

			FVector TraceStart = SuspensionTrace[WheelIdx].Start;
			FVector TraceEnd = SuspensionTrace[WheelIdx].End;

			if (WheelIdx == 0)
			{
				QueryBox = FBox(TraceStart, TraceEnd);
			}
			else
			{
				QueryBox.Min = QueryBox.Min.ComponentMin(TraceStart);
				QueryBox.Min = QueryBox.Min.ComponentMin(TraceEnd);
				QueryBox.Max = QueryBox.Max.ComponentMax(TraceStart);
				QueryBox.Max = QueryBox.Max.ComponentMax(TraceEnd);
			}
		}
		float OneWheelRadius = PVehicle->Wheels[0].GetEffectiveRadius(); // or wheel width

		QueryBox.ExpandBy(FVector(OneWheelRadius, OneWheelRadius, OneWheelRadius)); // little extra just to be on the safe side consider 1 or 2 wheel vehicle
		TArray<FOverlapResult> OverlapResults;
		FCollisionShape CollisionBox;
		CollisionBox.SetBox(QueryBox.GetExtent());

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GWheeledVehicleDebugParams.ShowBatchQueryExtents)
		{
			DrawDebugBox(GetWorld(), QueryBox.GetCenter(), QueryBox.GetExtent(), FColor::Yellow, false, -1.0f, 0, 2.0f);
		}
#endif

		const bool bOverlapHit = GetWorld()->OverlapMultiByChannel(OverlapResults, QueryBox.GetCenter(), FQuat::Identity, SpringCollisionChannel, CollisionBox, TraceParams, ResponseParams);

		for (int32 WheelIdx = 0; WheelIdx < Wheels.Num(); ++WheelIdx)
		{
			FHitResult& HitResult = Wheels[WheelIdx]->HitResult;
			HitResult = FHitResult();

			if (bOverlapHit)
			{
				const FVector& TraceStart = SuspensionTrace[WheelIdx].Start;
				const FVector& TraceEnd = SuspensionTrace[WheelIdx].End;
				TraceParams.bTraceComplex = (Wheels[WheelIdx]->SweepType == ESweepType::ComplexSweep);

				// Test each overlapped object for a hit result
				for (FOverlapResult OverlapResult : OverlapResults)
				{
					if (!OverlapResult.bBlockingHit)
						continue;

					FHitResult ComponentHit;

					switch (Wheels[WheelIdx]->SweepShape)
					{
						case ESweepShape::Spherecast:
						{
							float WheelRadius = PVehicle->Wheels[WheelIdx].GetEffectiveRadius(); // or wheel width
							FVector VehicleUpAxis = GetOwner()->GetTransform().GetUnitAxis(EAxis::Z);

							FVector Start = TraceStart + VehicleUpAxis * WheelRadius;
							FVector End = TraceEnd + VehicleUpAxis * WheelRadius;

							if (OverlapResult.Component->SweepComponent(ComponentHit, Start, End, FQuat::Identity, FCollisionShape::MakeSphere(WheelRadius), TraceParams.bTraceComplex))
							{
								if (ComponentHit.Time < HitResult.Time)
								{
									HitResult = ComponentHit;
									HitResult.bBlockingHit = OverlapResult.bBlockingHit;
								}
							}
						}
						break;

						case ESweepShape::Raycast:
						default:
						{
							if (OverlapResult.Component->LineTraceComponent(ComponentHit, TraceStart, TraceEnd, TraceParams))
							{
								if (ComponentHit.Time < HitResult.Time)
								{
									HitResult = ComponentHit;
									HitResult.bBlockingHit = OverlapResult.bBlockingHit;
								}
							}
						}
						break;
					}
				}
			}
		}
	}
	else
	{
		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			FHitResult& HitResult = Wheels[WheelIdx]->HitResult;

			FVector TraceStart = SuspensionTrace[WheelIdx].Start;
			FVector TraceEnd = SuspensionTrace[WheelIdx].End;
			TraceParams.bTraceComplex = (Wheels[WheelIdx]->SweepType == ESweepType::ComplexSweep);

			switch (Wheels[WheelIdx]->SweepShape)
			{
				case ESweepShape::Spherecast:
				{
					//float Radius = PVehicle->Wheels[WheelIdx].GetEffectiveRadius(); // or wheel width
					float Radius = PVehicle->Wheels[WheelIdx].Setup().WheelWidth * 0.5f; // or wheel width
					FVector VehicleUpAxis = GetOwner()->GetTransform().GetUnitAxis(EAxis::Z);

					GetWorld()->SweepSingleByChannel(HitResult
						, TraceStart + VehicleUpAxis * Radius
						, TraceEnd + VehicleUpAxis * Radius
						, FQuat::Identity, SpringCollisionChannel
						, FCollisionShape::MakeSphere(Radius), TraceParams
						, FCollisionResponseParams::DefaultResponseParam);
				}
				break;



				case ESweepShape::Raycast:
				default:
				{
					GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, SpringCollisionChannel, TraceParams, FCollisionResponseParams::DefaultResponseParam);
				}
				break;
			}
		}
	}

}

void UChaosWheeledVehicleMovementComponent::ApplyWheelFrictionForces(float DeltaTime)
{
	FBodyInstance* TargetInstance = GetBodyInstance();

	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		auto& PWheel = PVehicle->Wheels[WheelIdx]; // Physics Wheel
		FHitResult& HitResult = Wheels[WheelIdx]->HitResult;
		UChaosVehicleWheel* VehicleWheel = Wheels[WheelIdx];

		if (PWheel.InContact())
		{
			if (HitResult.PhysMaterial.IsValid())
			{
				PWheel.SetSurfaceFriction(HitResult.PhysMaterial->Friction);
			}

			// take into account steering angle
			float SteerAngleDegrees = VehicleWheel->GetSteerAngle(); // temp
			FRotator SteeringRotator(0.f, SteerAngleDegrees, 0.f);
			FVector SteerLocalWheelVelocity = SteeringRotator.UnrotateVector(WheelState.LocalWheelVelocity[WheelIdx]);

			PWheel.SetVehicleGroundSpeed(SteerLocalWheelVelocity);
			PWheel.Simulate(DeltaTime);

			float RotationAngle = FMath::RadiansToDegrees(PWheel.GetAngularPosition());
			FVector FrictionForceLocal = PWheel.GetForceFromFriction();
			FrictionForceLocal = SteeringRotator.RotateVector(FrictionForceLocal);

			FVector GroundZVector = HitResult.Normal;
			FVector GroundXVector = FVector::CrossProduct(VehicleState.VehicleRightAxis, GroundZVector);
			FVector GroundYVector = FVector::CrossProduct(GroundZVector, GroundXVector);

			// the force should be applied along the ground surface not along vehicle forward vector?
			//FVector FrictionForceVector = VehicleState.VehicleWorldTransform.TransformVector(FrictionForceLocal);
			FMatrix Mat(GroundXVector, GroundYVector, GroundZVector, VehicleState.VehicleWorldTransform.GetLocation());
			FVector FrictionForceVector = Mat.TransformVector(FrictionForceLocal);

			check(PWheel.InContact());
			AddForceAtPosition(FrictionForceVector, WheelState.WheelWorldLocation[WheelIdx]);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (GWheeledVehicleDebugParams.ShowWheelForces)
			{
				// show longitudinal drive force
				{
					DrawDebugLine(GetWorld()
						, WheelState.WheelWorldLocation[WheelIdx]
						, WheelState.WheelWorldLocation[WheelIdx] + FrictionForceVector * 0.001f
						, FColor::Yellow, false, -1.0f, 0, 2);

					DrawDebugLine(GetWorld()
						, WheelState.WheelWorldLocation[WheelIdx]
						, WheelState.WheelWorldLocation[WheelIdx] + GroundZVector * 100.f
						, FColor::Orange, false, -1.0f, 0, 2);

				}

			}
#endif

		}
		else
		{
			PWheel.SetVehicleGroundSpeed(WheelState.LocalWheelVelocity[WheelIdx]);
			PWheel.Simulate(DeltaTime);
		}

	}
}

void UChaosWheeledVehicleMovementComponent::ApplySuspensionForces(float DeltaTime)
{
	TArray<float> SusForces;
	SusForces.Init(0.f, Wheels.Num());
	FBodyInstance* TargetInstance = GetBodyInstance();

	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
		UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
		FHitResult& HitResult = Wheels[WheelIdx]->HitResult;

		float NewDesiredLength = 1.0f; // suspension max length
		float ForceMagnitude2 = 0.f;
		auto& PWheel = PVehicle->Wheels[WheelIdx];
		auto& PSuspension = PVehicle->Suspension[WheelIdx];
		float SuspensionMovePosition = -PSuspension.Setup().MaxLength;
		
		if (!GWheeledVehicleDebugParams.DisableSuspensionConstraint)
		{
			FPhysicsCommand::ExecuteWrite(TargetInstance->ActorHandle, [&](const FPhysicsActorHandle& Chassis)
			{
#if WITH_CHAOS
				if (ConstraintHandles.Num() > 0)
				{
					FPhysicsConstraintHandle& ConstraintHandle = ConstraintHandles[WheelIdx];
					if (ConstraintHandle.IsValid())
					{
						if (Chaos::FSuspensionConstraint* Constraint = static_cast<Chaos::FSuspensionConstraint*>(ConstraintHandle.Constraint))
						{
							FVec3 P = HitResult.ImpactPoint + (PWheel.Setup().WheelRadius * VehicleState.VehicleUpAxis);
							Constraint->SetTarget( P );
							Constraint->SetEnabled(PWheel.InContact());
						}
					}

				}
#endif // WITH_CHAOS
			});
		}

		if (PWheel.InContact())
		{
			NewDesiredLength = HitResult.Distance;

			SuspensionMovePosition = -FVector::DotProduct(WheelState.WheelWorldLocation[WheelIdx] - HitResult.ImpactPoint, VehicleState.VehicleUpAxis) + Wheel->WheelRadius;

			PSuspension.SetSuspensionLength(NewDesiredLength, PWheel.Setup().WheelRadius);
			PSuspension.SetLocalVelocity(WheelState.LocalWheelVelocity[WheelIdx]);
			PSuspension.Simulate(DeltaTime);

			float ForceMagnitude = PSuspension.GetSuspensionForce();

			FVector GroundZVector = HitResult.Normal;
			FVector SuspensionForceVector = VehicleState.VehicleUpAxis * ForceMagnitude;

			FVector SusApplicationPoint = WheelState.WheelWorldLocation[WheelIdx] + PVehicle->Suspension[WheelIdx].Setup().SuspensionForceOffset;

			check(PWheel.InContact());
			if (GWheeledVehicleDebugParams.DisableSuspensionConstraint)
			{
				AddForceAtPosition(SuspensionForceVector, SusApplicationPoint);
			}

			ForceMagnitude = PSuspension.Setup().WheelLoadRatio * ForceMagnitude + (1.f - PSuspension.Setup().WheelLoadRatio) * PSuspension.Setup().RestingForce;
			PWheel.SetWheelLoadForce(ForceMagnitude);
			PWheel.SetMassPerWheel(TargetInstance->GetBodyMass() / PVehicle->Wheels.Num());
			SusForces[WheelIdx] = ForceMagnitude;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (GWheeledVehicleDebugParams.ShowSuspensionForces)
			{
				DrawDebugLine(GetWorld()
					, SusApplicationPoint
					, SusApplicationPoint + SuspensionForceVector * GVehicleDebugParams.ForceDebugScaling
					, FColor::Blue, false, -1.0f, 0, 5);

				DrawDebugLine(GetWorld()
					, SusApplicationPoint
					, SusApplicationPoint + GroundZVector * 140.f
					, FColor::Yellow, false, -1.0f, 0, 5);
			}
#endif

		}
		else
		{
			PSuspension.SetSuspensionLength(PSuspension.GetTraceLength(PWheel.Setup().WheelRadius), PWheel.Setup().WheelRadius);
		}

	}


	if (!GWheeledVehicleDebugParams.DisableRollbarForces && AxleToWheelMap.Num() > 0)
	{
		for (auto& Axle : AxleToWheelMap)
		{
			const UChaosVehicleWheel* Wheel = Axle.Key;
			const TArray<int>& WheelIndices = Axle.Value;

			//#todo: only works with 2 wheels on an axle at present
			if (WheelIndices.Num() == 2)
			{
				int WheelIdxA = WheelIndices[0];
				int WheelIdxB = WheelIndices[1];

				float FV = Wheel->RollbarScaling;
				float ForceDiffOnAxleF = SusForces[WheelIdxA] - SusForces[WheelIdxB];
				FVector ForceVector0 = VehicleState.VehicleUpAxis * ForceDiffOnAxleF * FV;
				FVector ForceVector1 = VehicleState.VehicleUpAxis * ForceDiffOnAxleF * -FV;

				FVector SusApplicationPoint0 = WheelState.WheelWorldLocation[WheelIdxA] + PVehicle->Suspension[WheelIdxA].Setup().SuspensionForceOffset;
				AddForceAtPosition(ForceVector0, SusApplicationPoint0);

				FVector SusApplicationPoint1 = WheelState.WheelWorldLocation[WheelIdxB] + PVehicle->Suspension[WheelIdxB].Setup().SuspensionForceOffset;
				AddForceAtPosition(ForceVector1, SusApplicationPoint1);
			}
		}
	}
}

void UChaosWheeledVehicleMovementComponent::ProcessSteering()
{
	auto& PSteering = PVehicle->GetSteering();

	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		auto& PWheel = PVehicle->Wheels[WheelIdx]; // Physics Wheel
		FHitResult& HitResult = Wheels[WheelIdx]->HitResult;

		if (PWheel.Setup().SteeringEnabled)
		{
			FRichCurve* SteeringCurveData = SteeringSetup.SteeringCurve.GetRichCurve();
			float SpeedScale = 1.0f;

			// allow full counter steering when steering into a power slide
			if (SteeringInput * VehicleState.VehicleLocalVelocity.Y > 0.1f)
			{
				SpeedScale = SteeringCurveData->Eval(CmSToMPH(VehicleState.ForwardSpeed));
			}

			float UseSteeringValue = SteeringInput * SpeedScale;

			float SteeringAngle = 0.f;
			if (FMath::Abs(GWheeledVehicleDebugParams.SteeringOverride) > 0.01f)
			{
				SteeringAngle = PWheel.Setup().MaxSteeringAngle * GWheeledVehicleDebugParams.SteeringOverride;
			}
			else
			{
				float WheelSide = PVehicle->GetSuspension(WheelIdx).GetLocalRestingPosition().Y;
				SteeringAngle = PSteering.GetSteeringAngle(UseSteeringValue, PWheel.Setup().MaxSteeringAngle, WheelSide);
			}

			PWheel.SetSteeringAngle(SteeringAngle);
		}
		else
		{
			PWheel.SetSteeringAngle(0.0f);
		}
	}
}

void UChaosWheeledVehicleMovementComponent::ApplyInput(float DeltaTime)
{
	Super::ApplyInput(DeltaTime);

	float EngineBraking = 0.f;
	if (bMechanicalSimEnabled)
	{
		auto& PEngine = PVehicle->GetEngine();
		auto& PTransmission = PVehicle->GetTransmission();

		if (bRawGearUpInput)
		{
			PTransmission.ChangeUp();
			bRawGearUpInput = false;
		}

		if (bRawGearDownInput)
		{
			PTransmission.ChangeDown();
			bRawGearDownInput = false;
		}

		if (GWheeledVehicleDebugParams.ThrottleOverride > 0.f)
		{
			PTransmission.SetGear(1, true);
			BrakeInput = 0.f;
			PEngine.SetThrottle(GWheeledVehicleDebugParams.ThrottleOverride);
		}
		else
		{
			PEngine.SetThrottle(ThrottleInput* ThrottleInput);
		}

		EngineBraking = PEngine.GetEngineRPM() * PEngine.Setup().EngineBrakeEffect;
	}

	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		auto& PWheel = PVehicle->Wheels[WheelIdx];

		float EngineBrakingForce = 0.0f;
		if ((ThrottleInput < SMALL_NUMBER) && FMath::Abs(VehicleState.ForwardSpeed) > SMALL_NUMBER && PWheel.Setup().EngineEnabled)
		{
			EngineBrakingForce = EngineBraking;
		}

		if (PWheel.Setup().BrakeEnabled)
		{
			float BrakeForce = PWheel.Setup().MaxBrakeTorque * BrakeInput;
			PWheel.SetBrakeTorque(MToCm(BrakeForce + EngineBrakingForce));
		}
		else
		{
			PWheel.SetBrakeTorque(MToCm(EngineBraking));
		}

		if (bRawHandbrakeInput && PWheel.Setup().HandbrakeEnabled)
		{
			PWheel.SetBrakeTorque(MToCm(bRawHandbrakeInput * PWheel.Setup().HandbrakeTorque));
		}
	}


}

void UChaosWheeledVehicleMovementComponent::ProcessMechanicalSimulation(float DeltaTime)
{
	auto& PEngine = PVehicle->GetEngine();
	auto& PTransmission = PVehicle->GetTransmission();

	float WheelRPM = 0;
	for (int I=0; I<PVehicle->Wheels.Num(); I++)
	{
		if (PVehicle->Wheels[I].Setup().EngineEnabled)
		{ 
			WheelRPM = FMath::Abs(PVehicle->Wheels[I].GetWheelRPM());
		}
	}

	PEngine.SetEngineRPM(PTransmission.IsOutOfGear() , PTransmission.GetEngineRPMFromWheelRPM(WheelRPM));
	PEngine.Simulate(DeltaTime);

	PTransmission.SetEngineRPM(PEngine.GetEngineRPM()); // needs engine RPM to decide when to change gear (automatic gearbox)
	PTransmission.SetAllowedToChangeGear(!VehicleState.bVehicleInAir && !IsWheelSpinning());
	float GearRatio = PTransmission.GetGearRatio(PTransmission.GetCurrentGear());

	PTransmission.Simulate(DeltaTime);

	float TransmissionTorque = PTransmission.GetTransmissionTorque(PEngine.GetEngineTorque());

	// apply drive torque to wheels
	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		UChaosVehicleWheel* Wheel = WheelSetups[WheelIdx].WheelClass.GetDefaultObject();
		auto& PWheel = PVehicle->Wheels[WheelIdx];
		if (PWheel.Setup().EngineEnabled)
		{
			check(NumDrivenWheels > 0);

			if (DifferentialSetup.DifferentialType == EVehicleDifferential::AllWheelDrive)
			{
				float SplitTorque = 1.0f;

				if (Wheel->GetAxleType() == EAxleType::Front)
				{
					SplitTorque = (1.0f - DifferentialSetup.FrontRearSplit);
				}
				else
				{
					SplitTorque = DifferentialSetup.FrontRearSplit;
				}	

				PWheel.SetDriveTorque(MToCm(TransmissionTorque * SplitTorque) / (float)NumDrivenWheels);
			}
			else
			{
				PWheel.SetDriveTorque(MToCm(TransmissionTorque) / (float)NumDrivenWheels);
			}
		}
	}
}


// Access to data
float UChaosWheeledVehicleMovementComponent::GetEngineRotationSpeed() const
{
	float EngineRPM = 0.f;

	if (PVehicle.IsValid() && PVehicle->HasEngine())
	{
		EngineRPM = PVehicle->GetEngine().GetEngineRPM();
	}

	return EngineRPM;
}

float UChaosWheeledVehicleMovementComponent::GetEngineMaxRotationSpeed() const
{
	float MaxEngineRPM = 0.f;
	
	if (PVehicle.IsValid() && PVehicle->HasEngine())
	{
		MaxEngineRPM = PVehicle->GetEngine().Setup().MaxRPM;
	}

	return MaxEngineRPM;
}

bool UChaosWheeledVehicleMovementComponent::IsWheelSpinning() const // rename? / skidding too, covered by GetWheelState
{
	for (auto& Wheel : PVehicle->Wheels)
	{
		if (Wheel.IsSlipping())
		{
			return true;
		}
	}

	return false;
}

// Helper
FVector2D UChaosWheeledVehicleMovementComponent::CalculateWheelLayoutDimensions()
{
	FVector2D MaxSize(0.f, 0.f);

	for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
	{
		FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
		UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
		check(Wheel);

		const FVector WheelOffset = GetWheelRestingPosition(WheelSetup);
		if (FMath::Abs(WheelOffset.Y) > MaxSize.Y)
		{
			MaxSize.Y = FMath::Abs(WheelOffset.Y);
		}

		if (FMath::Abs(WheelOffset.X) > MaxSize.X)
		{
			MaxSize.X = FMath::Abs(WheelOffset.X);
		}

	}

	// full width/length not half
	MaxSize *= 2.0f;

	return MaxSize;
}

// Debug
void UChaosWheeledVehicleMovementComponent::DrawDebug(UCanvas* Canvas, float& YL, float& YPos)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	Super::DrawDebug(Canvas, YL, YPos);

	FChaosVehicleManager* MyVehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(GetWorld()->GetPhysicsScene());
	FBodyInstance* TargetInstance = GetBodyInstance();

	// #todo: is this rendering multiple times in multiplayer
	if (!PVehicle.IsValid() || TargetInstance == nullptr || MyVehicleManager == nullptr)
	{
		return;
	}

	float ForwardSpeedMPH = CmSToMPH(GetForwardSpeed());

	// always draw this even on (DebugPage == EDebugPages::BasicPage)
	if (PVehicle->HasTransmission())
	{
		auto& PTransmission = PVehicle->GetTransmission();
		auto& PEngine = PVehicle->GetEngine();
		auto& PTransmissionSetup = PTransmission.Setup();

		UFont* RenderFont = GEngine->GetLargeFont();
		Canvas->SetDrawColor(FColor::Yellow);

		// draw MPH, RPM and current gear
		float X, Y;
		Canvas->GetCenter(X, Y);
		float YLine = Y * 2.f - 50.f;
		float Scaling = 2.f;
		Canvas->DrawText(RenderFont, FString::Printf(TEXT("%d mph"), (int)ForwardSpeedMPH), X-100, YLine, Scaling, Scaling);
		Canvas->DrawText(RenderFont, FString::Printf(TEXT("[%d]"), (int)PTransmission.GetCurrentGear()), X, YLine, Scaling, Scaling);
		Canvas->DrawText(RenderFont, FString::Printf(TEXT("%d rpm"), (int)PEngine.GetEngineRPM()), X+50, YLine, Scaling, Scaling);

		FVector2D DialPos(X+10, YLine-40);
		float DialRadius = 50;
		DrawDial(Canvas, DialPos, DialRadius, PEngine.GetEngineRPM(), PEngine.Setup().MaxRPM);

	}

	UFont* RenderFont = GEngine->GetMediumFont();
	// draw drive data
	{
		Canvas->SetDrawColor(FColor::White);
		YPos += 16;
		
		if (PVehicle->HasTransmission())
		{
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("RPM: %.1f (ChangeUp RPM %d, ChangeDown RPM %d)")
				, GetEngineRotationSpeed()
				, PVehicle->GetTransmission().Setup().ChangeUpRPM
				, PVehicle->GetTransmission().Setup().ChangeDownRPM), 4, YPos);

			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Gear: %d (Target %d)")
				, GetCurrentGear(), GetTargetGear()), 4, YPos);
		}
		//YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Drag: %.1f"), DebugDragMagnitude), 4, YPos);

		YPos += 16;
		for (int i = 0; i < PVehicle->Wheels.Num(); i++)
		{
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("WheelLoad: [%d] %1.f N"), i, CmToM(PVehicle->Wheels[i].GetWheelLoadForce())), 4, YPos);
		}

		YPos += 16;
		for (int i = 0; i < PVehicle->Wheels.Num(); i++)
		{
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("SurfaceFriction: [%d] %.2f"), i, PVehicle->Wheels[i].GetSurfaceFriction()), 4, YPos);
		}
		
	}

	if (DebugPage == EDebugPages::PerformancePage)
	{
		if (GWheeledVehicleDebugParams.ResetPerformanceMeasurements)
		{
			GWheeledVehicleDebugParams.ResetPerformanceMeasurements = false;
			PerformanceMeasure.ResetAll();
		}

		PerformanceMeasure.Enable();

		YPos += 16;
		for (int I=0; I<PerformanceMeasure.GetNumMeasures(); I++)
		{
			const FTimeAndDistanceMeasure& Measure = PerformanceMeasure.GetMeasure(I);

			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("%s"), *Measure.ToString()), 4, YPos);
		}
	}

	// draw wheel layout
	if (DebugPage == EDebugPages::FrictionPage)
	{
		FVector2D MaxSize = GetWheelLayoutDimensions();

		// Draw a top down representation of the wheels in position, with the direction forces being shown
		for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
		{

			auto& PWheel = PVehicle->Wheels[WheelIdx];
			FVector Forces = PWheel.GetForceFromFriction();

			FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
			UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
			check(Wheel);
			UPhysicalMaterial* ContactMat = Wheel->GetContactSurfaceMaterial();

			const FVector WheelOffset = GetWheelRestingPosition(WheelSetup);

			float DrawScale = 300;
			FVector2D CentreDrawPosition(350, 400);
			FVector2D WheelDrawPosition(WheelOffset.Y, -WheelOffset.X);
			WheelDrawPosition *= DrawScale;
			WheelDrawPosition /= MaxSize.X;
			WheelDrawPosition += CentreDrawPosition;

			FVector2D WheelDimensions(Wheel->WheelWidth, Wheel->WheelRadius * 2.0f);
			FVector2D HalfDimensions = WheelDimensions * 0.5f;
			FCanvasBoxItem BoxItem(WheelDrawPosition - HalfDimensions, WheelDimensions);
			BoxItem.SetColor(FColor::Green);
			Canvas->DrawItem(BoxItem);

			float VisualScaling = 0.0001f;
			FVector2D Force2D(Forces.Y * VisualScaling, -Forces.X * VisualScaling);
			DrawLine2D(Canvas, WheelDrawPosition, WheelDrawPosition + Force2D, FColor::Red);

			float SlipAngle = FMath::Abs(PWheel.GetSlipAngle());
			float X = FMath::Sin(SlipAngle) * 50.f;
			float Y = FMath::Cos(SlipAngle) * 50.f;

			int Xpos = WheelDrawPosition.X + 20;
			int Ypos = WheelDrawPosition.Y - 75.f;
			DrawLine2D(Canvas, WheelDrawPosition, WheelDrawPosition - FVector2D(X, Y), FColor::White);
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Slip Angle : %d %"), (int)RadToDeg(SlipAngle)), Xpos, Ypos);

			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("AccelT : %.1f"), PWheel.GetDriveTorque()), Xpos, Ypos);
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("BrakeT : %.1f"), PWheel.GetBrakeTorque()), Xpos, Ypos);

			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Omega : %.2f"), PWheel.GetAngularVelocity()), Xpos, Ypos);

			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("GroundV : %.1f"), PWheel.GetRoadSpeed()), Xpos, Ypos);
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("WheelV : %.1f"), PWheel.GetWheelGroundSpeed()), Xpos, Ypos);
//			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Sx : %.2f"), PWheel.GetNormalizedLongitudinalSlip()), Xpos, Ypos);
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Long Ad Limit : %.2f"), PWheel.LongitudinalAdhesiveLimit), Xpos, Ypos);
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Lat Ad Limit : %.2f"), PWheel.LateralAdhesiveLimit), Xpos, Ypos);
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Side Slip : %.2f"), PWheel.SideSlipModifier), Xpos, Ypos);

			if (PWheel.AppliedLinearDriveForce > PWheel.LongitudinalAdhesiveLimit)
			{
				Canvas->SetDrawColor(FColor::Red);
			}
			else
			{
				Canvas->SetDrawColor(FColor::Green);
			}
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Ap Drive : %.2f"), PWheel.AppliedLinearDriveForce), Xpos, Ypos);

			if (PWheel.AppliedLinearBrakeForce > PWheel.LongitudinalAdhesiveLimit)
			{
				Canvas->SetDrawColor(FColor::Red);
			}
			else
			{
				Canvas->SetDrawColor(FColor::Green);
			}
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Ap Brake : %.2f"), PWheel.AppliedLinearBrakeForce), Xpos, Ypos);
			Canvas->SetDrawColor(FColor::White);

			//if (PWheel.Setup().EngineEnabled)
			//{
			//	Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("RPM        : %.1f"), PWheel.GetWheelRPM()), Xpos, Ypos);
			//	Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Geared RPM : %.1f"), PTransmission.GetEngineRPMFromWheelRPM(PWheel.GetWheelRPM())), Xpos, Ypos);

			//}

			if (ContactMat)
			{
				Canvas->DrawText(RenderFont
					, FString::Printf(TEXT("Friction %d"), ContactMat->Friction)
					, WheelDrawPosition.X, WheelDrawPosition.Y-95.f);
			}
		
		}

	}

	if (DebugPage == EDebugPages::SteeringPage)
	{
		FVector2D MaxSize = GetWheelLayoutDimensions();

		auto& PSteering = PVehicle->GetSteering();

		FVector2D J1, J2;
		for (int WheelIdx = 0; WheelIdx < PVehicle->Wheels.Num(); WheelIdx++)
		{
			FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
			auto& PWheel = PVehicle->Wheels[WheelIdx];
			const FVector WheelOffset = GetWheelRestingPosition(WheelSetup);

			float Scale = 300.0f / MaxSize.X;
			FVector2D CentreDrawPosition(450, 400);
			FVector2D WheelDrawPosition(WheelOffset.Y, -WheelOffset.X);
			WheelDrawPosition *= Scale;
			WheelDrawPosition += CentreDrawPosition;

			if (PWheel.Setup().SteeringEnabled)
			{
				if (WheelOffset.Y > 0)
				{
					float SteerAngle = DegToRad(PWheel.GetSteeringAngle());
					FVector2D Tire = FVector2D(FMath::Sin(SteerAngle), -FMath::Cos(SteerAngle)) * 30.0f;
					FVector2D WPt = WheelDrawPosition;
					DrawLine2D(Canvas, WPt - Tire, WPt + Tire, FColor::Black, 8);

					if (SteeringSetup.SteeringType == ESteeringType::Ackermann)
					{
						FVector2D C1, P, C2;
						PSteering.Ackermann.GetRightHingeLocations(C1, P, C2);
						C1.Y = -C1.Y;
						P.Y = -P.Y;
						C2.Y = -C2.Y;

						FVector2D JPt = WheelDrawPosition + (P - C2) * Scale;
						FVector2D CPt = WheelDrawPosition + (C1 - C2) * Scale;
						DrawLine2D(Canvas, CPt, JPt, FColor::Orange, 3);
						DrawLine2D(Canvas, WPt, JPt, FColor::Orange, 3);
						J1 = CPt;
					}
				}
				else
				{
					float SteerAngle = DegToRad(PWheel.GetSteeringAngle());
					FVector2D Tire = FVector2D(FMath::Sin(SteerAngle), -FMath::Cos(SteerAngle)) * 30.0f;
					FVector2D WPt = WheelDrawPosition;
					DrawLine2D(Canvas, WPt - Tire, WPt + Tire, FColor::Black, 8);

					if (SteeringSetup.SteeringType == ESteeringType::Ackermann)
					{

						FVector2D C1, P, C2;
						PSteering.Ackermann.GetLeftHingeLocations(C1, P, C2);
						C1.Y = -C1.Y;
						P.Y = -P.Y;
						C2.Y = -C2.Y;

						FVector2D JPt = WheelDrawPosition + (P - C2) * Scale;
						FVector2D CPt = WheelDrawPosition + (C1 - C2) * Scale;
						DrawLine2D(Canvas, CPt, JPt, FColor::Orange, 3);
						DrawLine2D(Canvas, WPt, JPt, FColor::Orange, 3);
						J2 = CPt;
					}
				}
			}
			else
			{
				FVector2D CPt = WheelDrawPosition;
				FVector2D Tire = FVector2D(0.f, 30.0f);
				DrawLine2D(Canvas, CPt - Tire, CPt + Tire, FColor::Black, 8);
			}

			Canvas->DrawText(RenderFont
				, FString::Printf(TEXT("Angle %.1f"), PWheel.GetSteeringAngle())
				, WheelDrawPosition.X, WheelDrawPosition.Y - 15.f);

		}
		DrawLine2D(Canvas, J1, J2, FColor::Red, 3);

	}

	// draw engine torque curve - just putting engine under transmission
	if (DebugPage == EDebugPages::TransmissionPage && PVehicle->HasTransmission())
	{
		auto& PTransmission = PVehicle->GetTransmission();
		auto& PEngine = PVehicle->GetEngine();
		auto& PTransmissionSetup = PTransmission.Setup();

		float MaxTorque = PEngine.Setup().MaxTorque;
		int CurrentRPM = (int)PEngine.GetEngineRPM();
		FVector2D CurrentValue(CurrentRPM, PEngine.GetEngineTorque());
		int GraphWidth = 200; int GraphHeight = 120;
		int GraphXPos = 200; int GraphYPos = 400;

		Canvas->DrawDebugGraph(FString("Engine Torque Graph")
			, CurrentValue.X, CurrentValue.Y
			, GraphXPos, GraphYPos, 
			GraphWidth, GraphHeight, 
			FVector2D(0, PEngine.Setup().MaxRPM), FVector2D(MaxTorque, 0));

		FVector2D LastPoint;
		for (float RPM = 0; RPM <= PEngine.Setup().MaxRPM; RPM += 10.f)
		{
			float X = RPM / PEngine.Setup().MaxRPM;
			float Y = PEngine.GetTorqueFromRPM(RPM, false) / MaxTorque;
			FVector2D NextPoint(GraphXPos + GraphWidth * X, GraphYPos + GraphHeight - GraphHeight * Y);
			if (RPM > SMALL_NUMBER)
			{
				DrawLine2D(Canvas, LastPoint, NextPoint, FColor::Cyan);
			}
			LastPoint = NextPoint;
		}

		Canvas->DrawText(RenderFont
			, FString::Printf(TEXT("RevRate %.1f"), PEngine.GetEngineRevRate())
			, GraphXPos, GraphYPos);

	}

	// draw transmission torque curve
	if (DebugPage == EDebugPages::TransmissionPage && PVehicle->HasTransmission())
	{
		auto& PTransmission = PVehicle->GetTransmission();
		auto& PEngine = PVehicle->GetEngine();
		auto& PTransmissionSetup = PTransmission.Setup();

		auto& ESetup = PEngine.Setup();
		auto& TSetup = PTransmission.Setup();
		float MaxTorque = ESetup.MaxTorque;
		float MaxGearRatio = TSetup.ForwardRatios[0] * TSetup.FinalDriveRatio; // 1st gear always has the highest multiplier
		float LongGearRatio = TSetup.ForwardRatios[TSetup.ForwardRatios.Num()-1] * TSetup.FinalDriveRatio;
		int GraphWidth = 400; int GraphHeight = 240;
		int GraphXPos = 500; int GraphYPos = 150;

		{
			float X = PTransmission.GetTransmissionRPM();
			float Y = PTransmission.GetTransmissionTorque(PEngine.GetTorqueFromRPM(false));

			FVector2D CurrentValue(X, Y);
			Canvas->DrawDebugGraph(FString("Transmission Torque Graph")
				, CurrentValue.X, CurrentValue.Y
				, GraphXPos, GraphYPos
				, GraphWidth, GraphHeight
				, FVector2D(0, ESetup.MaxRPM / LongGearRatio), FVector2D(MaxTorque* MaxGearRatio, 0));
		}

		FVector2D LastPoint;

		for (int Gear = 1; Gear <= TSetup.ForwardRatios.Num(); Gear++)
		{
			for (int EngineRPM = 0; EngineRPM <= ESetup.MaxRPM; EngineRPM += 10)
			{
				float RPMOut = PTransmission.GetTransmissionRPM(EngineRPM, Gear);

				float X = RPMOut / (ESetup.MaxRPM / LongGearRatio);
				float Y = PEngine.GetTorqueFromRPM(EngineRPM, false) * PTransmission.GetGearRatio(Gear) / (MaxTorque*MaxGearRatio);
				FVector2D NextPoint(GraphXPos + GraphWidth * X, GraphYPos + GraphHeight - GraphHeight * Y);
				if (EngineRPM > 0)
				{
					DrawLine2D(Canvas, LastPoint, NextPoint, FColor::Cyan);
				}
				LastPoint = NextPoint;
			}
		}
	}

	// for each of the wheel positions, draw the expected suspension movement limits and the current length
	if (DebugPage == EDebugPages::SuspensionPage)
	{
		FVector2D MaxSize = GetWheelLayoutDimensions();

		for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
		{
			auto& PSuspension = PVehicle->Suspension[WheelIdx];
			FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
			UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
			check(Wheel);
			UChaosVehicleWheel* VehicleWheel = Wheels[WheelIdx];

			const FVector WheelOffset = GetWheelRestingPosition(WheelSetup);

			float DrawScale = 200;
			FVector2D CentreDrawPosition(500, 350);
			FVector2D WheelDrawPosition(WheelOffset.Y, -WheelOffset.X);
			WheelDrawPosition *= DrawScale;
			WheelDrawPosition /= MaxSize.X;
			WheelDrawPosition += CentreDrawPosition;

			{
				// suspension resting position
				FVector2D Start = WheelDrawPosition + FVector2D(-10.f, 0.f);
				FVector2D End = Start + FVector2D(20.f, 0.f);
				DrawLine2D(Canvas, Start, End, FColor::Yellow, 2.f);
			}

			float Raise = PSuspension.Setup().SuspensionMaxRaise;
			float Drop = PSuspension.Setup().SuspensionMaxDrop;
			float Scale = 5.0f;

			{
				// suspension compression limit
				FVector2D Start = WheelDrawPosition + FVector2D(-20.f, -Raise * Scale);
				FVector2D End = Start + FVector2D(40.f, 0.f);
				DrawLine2D(Canvas, Start, End, FColor::White, 2.f);
				Canvas->DrawText(RenderFont, FString::Printf(TEXT("Raise Limit %.1f"), Raise), Start.X, Start.Y-16);
			}

			{
				// suspension extension limit
				FVector2D Start = WheelDrawPosition + FVector2D(-20.f, Drop * Scale);
				FVector2D End = Start + FVector2D(40.f, 0.f);
				DrawLine2D(Canvas, Start, End, FColor::White, 2.f);
				Canvas->DrawText(RenderFont, FString::Printf(TEXT("Drop Limit %.1f"), Drop), Start.X, Start.Y);
			}

			{
				// current suspension length
				FVector2D Start = WheelDrawPosition;
				FVector2D End = Start - FVector2D(0.f, VehicleWheel->GetSuspensionOffset() * Scale);
				DrawLine2D(Canvas, Start, End, FColor::Green, 4.f);
			}

		}
	}
#endif

}

void UChaosWheeledVehicleMovementComponent::DrawDebug3D()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	Super::DrawDebug3D();

	FBodyInstance* TargetInstance = GetBodyInstance();
	if (TargetInstance == nullptr)
	{
		return;
	}

	const FTransform BodyTransform = VehicleState.VehicleWorldTransform;

	if (GWheeledVehicleDebugParams.ShowSuspensionLimits)
	{
		for (int WheelIdx = 0; WheelIdx < PVehicle->Suspension.Num(); WheelIdx++)
		{
			auto& PSuspension = PVehicle->Suspension[WheelIdx];
			auto& PWheel = PVehicle->Wheels[WheelIdx];
			// push the visualization out a bit sideways from the wheel model so we can actually see it
			FVector VehicleRightAxis = VehicleState.VehicleWorldTransform.GetUnitAxis(EAxis::Y) * 48.0f;
			const FVector& WheelOffset = PSuspension.GetLocalRestingPosition();
			if (WheelOffset.Y < 0.0f)
			{
				VehicleRightAxis = VehicleRightAxis * -1.0f;
			}

			FVector LocalDirection = PSuspension.Setup().SuspensionAxis;
			FVector WorldLocation = BodyTransform.TransformPosition(WheelOffset);
			FVector WorldDirection = BodyTransform.TransformVector(LocalDirection);

			FVector Start = WorldLocation + WorldDirection * (PWheel.GetEffectiveRadius() - PSuspension.Setup().SuspensionMaxRaise);
			FVector End = WorldLocation + WorldDirection * (PWheel.GetEffectiveRadius() + PSuspension.Setup().SuspensionMaxDrop);

			DrawDebugLine(GetWorld(), Start + VehicleRightAxis, End + VehicleRightAxis, FColor::Orange, false, -1.f, 0, 3.f);

			FVector Start2 = WorldLocation - WorldDirection * PSuspension.Setup().SuspensionMaxRaise;
			FVector End2 = WorldLocation + WorldDirection * PSuspension.Setup().SuspensionMaxDrop;

			DrawDebugLine(GetWorld(), Start2 + VehicleRightAxis, End2 + VehicleRightAxis, FColor::Yellow, false, -1.f, 0, 3.f);
		}
	}

	if (GWheeledVehicleDebugParams.ShowWheelCollisionNormal)
	{
		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			FHitResult& Hit = Wheels[WheelIdx]->HitResult;
			DrawDebugLine(GetWorld(), Hit.ImpactPoint, Hit.ImpactPoint + Hit.Normal * 20.0f, FColor::Yellow, true, 1.0f, 0, 1.0f);
		}
	}

	if (GWheeledVehicleDebugParams.ShowSuspensionRaycasts)
	{
		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			const FVector& TraceStart = WheelState.Trace[WheelIdx].Start;
			const FVector& TraceEnd = WheelState.Trace[WheelIdx].End;

			// push the visualization out a bit sideways from the wheel model so we can actually see it
			FVector VehicleRightAxis = VehicleState.VehicleWorldTransform.GetUnitAxis(EAxis::Y) * 50.0f;
			const FVector& WheelOffset = PVehicle->Suspension[WheelIdx].GetLocalRestingPosition();
			if (WheelOffset.Y < 0.0f)
			{
				VehicleRightAxis = VehicleRightAxis * -1.0f;
			}

			FColor UseColor = PVehicle->Wheels[WheelIdx].InContact() ? FColor::Green : FColor::Red;
			DrawDebugDirectionalArrow(GetWorld(), TraceStart + VehicleRightAxis, TraceEnd + VehicleRightAxis, 10.f, UseColor, false, -1.f, 0, 2.f);

			DrawDebugLine(GetWorld(), TraceStart, TraceStart + VehicleRightAxis, FColor::White, false, -1.f, 0, 1.f);
			DrawDebugLine(GetWorld(), TraceEnd, TraceEnd + VehicleRightAxis, FColor::White, false, -1.f, 0, 1.f);
		}
	}
#endif
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

float UChaosWheeledVehicleMovementComponent::CalcDialAngle(float CurrentValue, float MaxValue)
{
	return (CurrentValue / MaxValue) * 3.f / 2.f * PI - (PI * 0.25f);
}

void UChaosWheeledVehicleMovementComponent::DrawDial(UCanvas* Canvas, FVector2D Pos, float Radius, float CurrentValue, float MaxValue)
{
	float Angle = CalcDialAngle(CurrentValue, MaxValue);
	FVector2D PtEnd(Pos.X - FMath::Cos(Angle) * Radius, Pos.Y - FMath::Sin(Angle) * Radius);
	DrawLine2D(Canvas, Pos, PtEnd, FColor::White, 3.f);

	for (float I = 0; I < MaxValue; I += 1000.0f)
	{
		Angle = CalcDialAngle(I, MaxValue);
		PtEnd.Set(-FMath::Cos(Angle) * Radius, -FMath::Sin(Angle) * Radius);
		FVector2D PtStart = PtEnd * 0.8f;
		DrawLine2D(Canvas, Pos + PtStart, Pos + PtEnd, FColor::White, 2.f);
	}

	// the last checkmark
	Angle = CalcDialAngle(MaxValue, MaxValue);
	PtEnd.Set(-FMath::Cos(Angle) * Radius, -FMath::Sin(Angle) * Radius);
	FVector2D PtStart = PtEnd * 0.8f;
	DrawLine2D(Canvas, Pos+PtStart, Pos+PtEnd, FColor::Red, 2.f);

}

#endif

void UChaosWheeledVehicleMovementComponent::FillWheelOutputState()
{
	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
		UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
		auto& PWheel = PVehicle->Wheels[WheelIdx];
		auto& PSuspension = PVehicle->Suspension[WheelIdx];
		FHitResult& HitResult = Wheels[WheelIdx]->HitResult;

		FWheelStatus& State = WheelStatus[WheelIdx];
	
		State.bInContact = HitResult.bBlockingHit;
		State.ContactPoint = HitResult.ImpactPoint;
		State.PhysMaterial = HitResult.PhysMaterial;
		State.NormalizedSuspensionLength = PSuspension.GetNormalizedLength();
		State.SpringForce = PSuspension.GetSuspensionForce();
		State.bIsSlipping = PWheel.IsSlipping();
		State.SlipMagnitude = PWheel.GetSlipMagnitude();
		State.bIsSkidding = PWheel.IsSkidding();
		State.SkidMagnitude = PWheel.GetSkidMagnitude();
		if (State.bIsSkidding)
		{
			State.SkidNormal = WheelState.WorldWheelVelocity[WheelIdx].GetSafeNormal();
			//DrawDebugLine(GetWorld()
			//	, State.ContactPoint
			//	, State.ContactPoint + State.SkidNormal
			//	, FColor::Yellow, true, -1.0f, 0, 4);
		}
		else
		{
			State.SkidNormal = FVector::ZeroVector;
		}
	}
}

void UChaosWheeledVehicleMovementComponent::BreakWheelStatus(const struct FWheelStatus& Status, bool& bInContact, FVector& ContactPoint, UPhysicalMaterial*& PhysMaterial
	, float& NormalizedSuspensionLength, float& SpringForce, bool& bIsSlipping, float& SlipMagnitude, bool& bIsSkidding, float& SkidMagnitude, FVector& SkidNormal)
{
	bInContact = Status.bInContact;
	ContactPoint = Status.ContactPoint;
	PhysMaterial = Status.PhysMaterial.Get();
	NormalizedSuspensionLength = Status.NormalizedSuspensionLength;
	SpringForce = Status.SpringForce;
	bIsSlipping = Status.bIsSlipping;
	SlipMagnitude = Status.SlipMagnitude;
	bIsSkidding = Status.bIsSkidding;
	SkidMagnitude = Status.SkidMagnitude;
	SkidNormal = Status.SkidNormal;
}

FWheelStatus UChaosWheeledVehicleMovementComponent::MakeWheelStatus(bool bInContact, FVector& ContactPoint, UPhysicalMaterial* PhysMaterial
	, float NormalizedSuspensionLength, float SpringForce, bool bIsSlipping, float SlipMagnitude, bool bIsSkidding, float SkidMagnitude, FVector& SkidNormal)
{
	FWheelStatus Status;
	Status.bInContact = bInContact;
	Status.ContactPoint = ContactPoint;
	Status.PhysMaterial = PhysMaterial;
	Status.NormalizedSuspensionLength = NormalizedSuspensionLength;
	Status.SpringForce = SpringForce;
	Status.bIsSlipping = bIsSlipping;
	Status.SlipMagnitude = SlipMagnitude;
	Status.bIsSkidding = bIsSkidding;
	Status.SkidMagnitude = SkidMagnitude;
	Status.SkidNormal = SkidNormal;

	return Status;
}


FChaosWheelSetup::FChaosWheelSetup()
	: WheelClass(UChaosVehicleWheel::StaticClass())
//	, SteeringBoneName(NAME_None)
	, BoneName(NAME_None)
	, AdditionalOffset(0.0f)
{

}

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif


