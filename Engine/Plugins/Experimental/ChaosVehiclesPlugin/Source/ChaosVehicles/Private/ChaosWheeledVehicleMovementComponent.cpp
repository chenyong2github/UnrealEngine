// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "VehicleAnimationInstance.h"
#include "ChaosVehicleManager.h"
#include "SuspensionUtility.h"
#include "SteeringUtility.h"

using namespace Chaos;

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

#if WITH_CHAOS

FWheeledVehicleDebugParams GWheeledVehicleDebugParams;
extern FVehicleDebugParams GVehicleDebugParams;

EDebugPages UChaosWheeledVehicleMovementComponent::DebugPage = EDebugPages::BasicPage;

FAutoConsoleVariableRef CVarChaosVehiclesShowWheelCollisionNormal(TEXT("p.Vehicles.ShowWheelCollisionNormal"), GWheeledVehicleDebugParams.ShowWheelCollisionNormal, TEXT("Enable/Disable Wheel Collision Normal Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowSuspensionRaycasts(TEXT("p.Vehicles.ShowSuspensionRaycasts"), GWheeledVehicleDebugParams.ShowSuspensionRaycasts, TEXT("Enable/Disable Suspension Raycast Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowSuspensionLimits(TEXT("p.Vehicles.ShowSuspensionLimits"), GWheeledVehicleDebugParams.ShowSuspensionLimits, TEXT("Enable/Disable Suspension Limits Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowWheelForces(TEXT("p.Vehicles.ShowWheelForces"), GWheeledVehicleDebugParams.ShowWheelForces, TEXT("Enable/Disable Wheel Forces Visualisation."));
FAutoConsoleVariableRef CVarChaosVehiclesShowSuspensionForces(TEXT("p.Vehicles.ShowSuspensionForces"), GWheeledVehicleDebugParams.ShowSuspensionForces, TEXT("Enable/Disable Suspension Forces Visualisation."));

FAutoConsoleVariableRef CVarChaosVehiclesDisableSuspensionForces(TEXT("p.Vehicles.DisableSuspensionForces"), GWheeledVehicleDebugParams.DisableSuspensionForces, TEXT("Enable/Disable Suspension Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableFrictionForces(TEXT("p.Vehicles.DisableFrictionForces"), GWheeledVehicleDebugParams.DisableFrictionForces, TEXT("Enable/Disable Wheel Friction Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesDisableRollbarForces(TEXT("p.Vehicles.DisableRollbarForces"), GWheeledVehicleDebugParams.DisableRollbarForces, TEXT("Enable/Disable Rollbar Forces."));
FAutoConsoleVariableRef CVarChaosVehiclesApplyWheelForcetoSurface(TEXT("p.Vehicles.ApplyWheelForcetoSurface"), GWheeledVehicleDebugParams.ApplyWheelForcetoSurface, TEXT("Enable/Disable Apply Wheel Force To Underlyinh Surface."));

FAutoConsoleVariableRef CVarChaosVehiclesThrottleOverride(TEXT("p.Vehicles.ThrottleOverride"), GWheeledVehicleDebugParams.ThrottleOverride, TEXT("Hard code throttle input on."));
FAutoConsoleVariableRef CVarChaosVehiclesSteeringOverride(TEXT("p.Vehicles.SteeringOverride"), GWheeledVehicleDebugParams.SteeringOverride, TEXT("Hard code steering input on."));


FAutoConsoleCommand CVarCommandVehiclesNextDebugPage(
	TEXT("p.Vehicles.NextDebugPage"),
	TEXT("Display the next page of vehicle debug data."),
	FConsoleCommandDelegate::CreateStatic(UChaosWheeledVehicleMovementComponent::NextDebugPage));

FAutoConsoleCommand CVarCommandVehiclesPrevDebugPage(
	TEXT("p.Vehicles.PrevDebugPage"),
	TEXT("Display the previous page of vehicle debug data."),
	FConsoleCommandDelegate::CreateStatic(UChaosWheeledVehicleMovementComponent::PrevDebugPage));


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

	MechanicalSimEnabled = true;
	SuspensionEnabled = true;
	WheelFrictionEnabled = true;

	NumDrivenWheels = 0;

	EngineSetup.MaxRPM = 6000.f;
	EngineSetup.MaxTorque = 10000.f;
	EngineSetup.EngineIdleRPM = 1200.f;
	EngineSetup.EngineBrakeEffect = 0.001f;
	
	TransmissionSetup.ForwardGearRatios.Add(4.0f);
	TransmissionSetup.ForwardGearRatios.Add(3.0f);
	TransmissionSetup.ForwardGearRatios.Add(2.0f);
	TransmissionSetup.ForwardGearRatios.Add(1.0f);
	TransmissionSetup.FinalRatio = 4.0f;

	TransmissionSetup.ReverseGearRatios.Add(3.0f);
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

	if (PropertyName == TEXT("SteeringCurve"))
	{
		// make sure values are capped between 0 and 1
		TArray<FRichCurveKey> SteerKeys = SteeringSetup.SteeringCurve.GetRichCurve()->GetCopyOfKeys();
		for (int32 KeyIdx = 0; KeyIdx < SteerKeys.Num(); ++KeyIdx)
		{
			float NewValue = FMath::Clamp(SteerKeys[KeyIdx].Value, 0.f, 1.f);
			SteeringSetup.SteeringCurve.GetRichCurve()->UpdateOrAddKey(SteerKeys[KeyIdx].Time, NewValue);
		}
	}
}
#endif

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
		&& PVehicle->Engine.Num() > 0 && PVehicle->Engine.Num() == PVehicle->Transmission.Num()
		&& Wheels.Num() > 0 &&Wheels.Num() == PVehicle->Suspension.Num());
}

void UChaosWheeledVehicleMovementComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	if (PVehicle)
	{
		CreateWheels();

		// Need to bind to the notify delegate on the mesh incase physics state is changed
		if (USkeletalMeshComponent* MeshComp = Cast<USkeletalMeshComponent>(GetMesh()))
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
			if (USkeletalMeshComponent* MeshComp = Cast<USkeletalMeshComponent>(GetMesh()))
			{
				MeshComp->UnregisterOnPhysicsCreatedDelegate(MeshOnPhysicsStateChangeHandle);
			}
		}

		DestroyWheels();
	}

	Super::OnDestroyPhysicsState();
}

void UChaosWheeledVehicleMovementComponent::TickVehicle(float DeltaTime)
{
	Super::TickVehicle(DeltaTime);

	////if (AvoidanceLockTimer > 0.0f)
	////{
	////	AvoidanceLockTimer -= DeltaTime;
	////}

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
				if (DifferentialSetup.DifferentialType == EVehicleDifferential::FourWheelDrive
					|| DifferentialSetup.DifferentialType == EVehicleDifferential::FrontWheelDrive)
				{
					EngineEnable = true;
				}
			}
			else if (Wheel->GetAxleType() == EAxleType::Rear)
			{
				if (DifferentialSetup.DifferentialType == EVehicleDifferential::FourWheelDrive
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

	Chaos::FSimpleEngineSim EngineSim(&EngineSetup.GetPhysicsEngineConfig());
	PVehicle->Engine.Add(EngineSim);

	Chaos::FSimpleTransmissionSim TransmissionSim(&TransmissionSetup.GetPhysicsTransmissionConfig());
	PVehicle->Transmission.Add(TransmissionSim);

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

	//static PxMaterial* WheelMaterial = GPhysXSDK->createMaterial(0.0f, 0.0f, 0.0f);
	//FBodyInstance* TargetInstance = UpdatedPrimitive->GetBodyInstance();

	//FPhysicsCommand::ExecuteWrite(TargetInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
	//	{
	//		PxRigidActor* PActor = FPhysicsInterface::GetPxRigidActor_AssumesLocked(Actor);
	//		if (!PActor)
	//		{
	//			return;
	//		}

	//		if (PxRigidDynamic* PVehicleActor = PActor->is<PxRigidDynamic>())
	//		{
	//			// Add wheel shapes to actor
	for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
	{
		FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
		UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
		check(Wheel);

		const FVector WheelOffset = GetWheelRestingPosition(WheelSetup);

		//				const PxTransform PLocalPose = PxTransform(U2PVector(WheelOffset));
		//				PxShape* PWheelShape = NULL;

		//				// Prepare shape
		//				const UBodySetup* WheelBodySetup = nullptr;
		//				FVector MeshScaleV(1.f, 1.f, 1.f);
		//				if (Wheel->bDontCreateShape)
		//				{
		//					//don't create shape so grab it directly from the bodies associated with the vehicle
		//					if (USkinnedMeshComponent* SkinnedMesh = GetMesh())
		//					{
		//						if (const FBodyInstance* WheelBI = SkinnedMesh->GetBodyInstance(WheelSetup.BoneName))
		//						{
		//							WheelBodySetup = WheelBI->BodySetup.Get();
		//						}
		//					}

		//				}
		//				else if (Wheel->CollisionMesh && Wheel->CollisionMesh->BodySetup)
		//				{
		//					WheelBodySetup = Wheel->CollisionMesh->BodySetup;

		//					FBoxSphereBounds MeshBounds = Wheel->CollisionMesh->GetBounds();
		//					if (Wheel->bAutoAdjustCollisionSize)
		//					{
		//						MeshScaleV.X = Wheel->WheelRadius / MeshBounds.BoxExtent.X;
		//						MeshScaleV.Y = Wheel->WheelWidth / MeshBounds.BoxExtent.Y;
		//						MeshScaleV.Z = Wheel->WheelRadius / MeshBounds.BoxExtent.Z;
		//					}
		//				}

		//				if (WheelBodySetup)
		//				{
		//					PxMeshScale MeshScale(U2PVector(UpdatedComponent->GetRelativeScale3D() * MeshScaleV), PxQuat(physx::PxIdentity));

		//					if (WheelBodySetup->AggGeom.ConvexElems.Num() == 1)
		//					{
		//						PxConvexMesh* ConvexMesh = WheelBodySetup->AggGeom.ConvexElems[0].GetConvexMesh();
		//						PWheelShape = GPhysXSDK->createShape(PxConvexMeshGeometry(ConvexMesh, MeshScale), *WheelMaterial, /*bIsExclusive=*/true);
		//						PVehicleActor->attachShape(*PWheelShape);
		//						PWheelShape->release();
		//					}
		//					else if (WheelBodySetup->TriMeshes.Num())
		//					{
		//						PxTriangleMesh* TriMesh = WheelBodySetup->TriMeshes[0];

		//						// No eSIMULATION_SHAPE flag for wheels
		//						PWheelShape = GPhysXSDK->createShape(PxTriangleMeshGeometry(TriMesh, MeshScale), *WheelMaterial, /*bIsExclusive=*/true, PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eVISUALIZATION);
		//						PWheelShape->setLocalPose(PLocalPose);
		//						PVehicleActor->attachShape(*PWheelShape);
		//						PWheelShape->release();
		//					}
		//				}

		//				if (!PWheelShape)
		//				{
		//					//fallback onto simple spheres
		//					PWheelShape = GPhysXSDK->createShape(PxSphereGeometry(Wheel->WheelRadius), *WheelMaterial, /*bIsExclusive=*/true);
		//					PWheelShape->setLocalPose(PLocalPose);
		//					PVehicleActor->attachShape(*PWheelShape);
		//					PWheelShape->release();
		//				}

						// Init filter data
						//FCollisionResponseContainer CollisionResponse;
						//CollisionResponse.SetAllChannels(ECR_Ignore);

						//FCollisionFilterData WheelQueryFilterData, DummySimData;
						//CreateShapeFilterData(ECC_Vehicle, FMaskFilter(0), UpdatedComponent->GetOwner()->GetUniqueID(), CollisionResponse, UpdatedComponent->GetUniqueID(), 0, WheelQueryFilterData, DummySimData, false, false, false);

						//if (Wheel->SweepType != EWheelSweepType::Complex)
						//{
						//	WheelQueryFilterData.Word3 |= EPDF_SimpleCollision;
						//}

						//if (Wheel->SweepType != EWheelSweepType::Simple)
						//{
						//	WheelQueryFilterData.Word3 |= EPDF_ComplexCollision;
						//}

						//// Give suspension raycasts the same group ID as the chassis so that they don't hit each other
						//PWheelShape->setQueryFilterData(U2PFilterData(WheelQueryFilterData));
	}
	//		}
	//	});
}

void UChaosWheeledVehicleMovementComponent::SetupSuspension()
{
	FBodyInstance* TargetInstance = GetBodyInstance();
	if (!PVehicle.IsValid() || TargetInstance == nullptr)
	{
		return;
	}

	float TotalMass = TargetInstance->GetBodyMass();

	TArray<FVector> LocalSpringPositions;

	// cache vehicle local position of springs
	for (int SpringIdx = 0; SpringIdx < PVehicle->Suspension.Num(); SpringIdx++)
	{
		PVehicle->Suspension[SpringIdx].AccessSetup().MaxLength = PVehicle->Suspension[SpringIdx].Setup().SuspensionMaxDrop;

		LocalSpringPositions.Add(GetWheelRestingPosition(WheelSetups[SpringIdx]));
		PVehicle->Suspension[SpringIdx].SetLocalRestingPosition(LocalSpringPositions[SpringIdx]);
	}

	// Calculate the mass that will rest on each of the springs
	TArray<float> OutSprungMasses;
	FSuspensionUtility::ComputeSprungMasses(LocalSpringPositions, TotalMass, OutSprungMasses);

	// Calculate spring damping values we will use for physics simulation from the normalized damping ratio
	for (int SpringIdx = 0; SpringIdx < PVehicle->Suspension.Num(); SpringIdx++)
	{
		auto& Susp = PVehicle->Suspension[SpringIdx];
		float NaturalFrequency = FSuspensionUtility::ComputeNaturalFrequency(Susp.Setup().SpringRate, OutSprungMasses[SpringIdx]);
		float Damping = FSuspensionUtility::ComputeDamping(Susp.Setup().SpringRate, OutSprungMasses[SpringIdx], Susp.Setup().DampingRatio);
		//UE_LOG(LogChaos, Warning, TEXT("OutNaturalFrequency %.1f Hz  (@1.0) DampingRate %.1f"), NaturalFrequency / (2.0f * PI), Damping);

		PVehicle->Suspension[SpringIdx].AccessSetup().ReboundDamping = Damping;
		PVehicle->Suspension[SpringIdx].AccessSetup().CompressionDamping = Damping;
	}

}

FVector UChaosWheeledVehicleMovementComponent::GetWheelRestingPosition(const FChaosWheelSetup& WheelSetup)
{
	FVector Offset = WheelSetup.WheelClass.GetDefaultObject()->Offset + WheelSetup.AdditionalOffset;

	if (WheelSetup.BoneName != NAME_None)
	{
		USkinnedMeshComponent* Mesh = GetMesh();
		if (Mesh && Mesh->SkeletalMesh)
		{
			const FVector BonePosition = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(WheelSetup.BoneName).GetOrigin() * Mesh->GetRelativeScale3D();
			//BonePosition is local for the root BONE of the skeletal mesh - however, we are using the Root BODY which may have its own transform, so we need to return the position local to the root BODY
			const FMatrix RootBodyMTX = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(Mesh->GetBodyInstance()->BodySetup->BoneName);
			const FVector LocalBonePosition = RootBodyMTX.InverseTransformPosition(BonePosition);
			Offset += LocalBonePosition;
		
		}
	}
	return Offset;
}

// Update
void UChaosWheeledVehicleMovementComponent::UpdateSimulation(float DeltaTime)
{
	// Inherit common vehicle simulation stages ApplyAerodynamics, ApplyAirControl, etc
	Super::UpdateSimulation(DeltaTime);

	FBodyInstance* TargetInstance = GetBodyInstance();

	if (CanSimulate() && TargetInstance)
	{
		// sanity check that everything is setup ok
		check(Wheels.Num() == PVehicle->Suspension.Num());
		check(Wheels.Num() == PVehicle->Wheels.Num());
		check(WheelState.LocalWheelVelocity.Num() == Wheels.Num());
		check(WheelState.WheelWorldLocation.Num() == Wheels.Num());
		check(WheelState.WorldWheelVelocity.Num() == Wheels.Num());

		auto& PEngine = PVehicle->GetEngine();
		auto& PTransmission = PVehicle->GetTransmission();

		///////////////////////////////////////////////////////////////////////
		// Cache useful state so we are not re-calculating the same transforms

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

		if (!GWheeledVehicleDebugParams.DisableSuspensionForces && SuspensionEnabled)
		{
			PerformSuspensionTraces(WheelState.Trace);
		}

		//////////////////////////////////////////////////////////////////////////
		// Wheel and Vehicle in air state

		VehicleState.bVehicleInAir = true;
		for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
		{
			// tell systems who care that wheel is touching the ground
			PVehicle->Wheels[WheelIdx].SetOnGround(Wheels[WheelIdx]->HitResult.bBlockingHit);

			// only requires one wheel to be on the ground for the vehicle to be NOT in the air
			if (PVehicle->Wheels[WheelIdx].InContact())
			{
				VehicleState.bVehicleInAir = false;
			}
		}

		///////////////////////////////////////////////////////////////////////
		// Input
		ApplyInput(DeltaTime);

		///////////////////////////////////////////////////////////////////////
		// Engine/Transmission

		if (!GWheeledVehicleDebugParams.DisableSuspensionForces && MechanicalSimEnabled)
		{
			ProcessMechanicalSimulation(DeltaTime);
		}

		///////////////////////////////////////////////////////////////////////
		// Suspension

		if (!GWheeledVehicleDebugParams.DisableSuspensionForces && SuspensionEnabled)
		{
			ApplySuspensionForces(DeltaTime);
		}

		///////////////////////////////////////////////////////////////////////
		// Steering

		ProcessSteering();

		///////////////////////////////////////////////////////////////////////
		// Wheel Friction

		if (!GWheeledVehicleDebugParams.DisableFrictionForces && WheelFrictionEnabled)
		{		
			ApplyWheelFrictionForces(DeltaTime);
		}

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

		QueryBox.ExpandBy(FVector(1.0f, 1.0f, 1.0f)); // little extra just to be on yhe safe side
		TArray<FOverlapResult> OverlapResults;
		FCollisionShape CollisionBox;
		CollisionBox.SetBox(QueryBox.GetExtent());

		//DrawDebugBox(GetWorld(), QueryBox.GetCenter(), QueryBox.GetExtent(), FColor::Yellow, false, -1.0f, 0, 2.0f);
		const bool bOverlapHit = GetWorld()->OverlapMultiByChannel(OverlapResults, QueryBox.GetCenter(), FQuat::Identity, SpringCollisionChannel, CollisionBox, TraceParams, ResponseParams);

		for (int32 WheelIdx = 0; WheelIdx < Wheels.Num(); ++WheelIdx)
		{
			FHitResult& HitResult = Wheels[WheelIdx]->HitResult;
			HitResult = FHitResult();

			const FVector& TraceStart = SuspensionTrace[WheelIdx].Start;
			const FVector& TraceEnd = SuspensionTrace[WheelIdx].End;
			bool MadeContact = false;
			if (bOverlapHit)
			{

				// Test each overlapped object for a hit result
				for (FOverlapResult OverlapResult : OverlapResults)
				{
					FHitResult ComponentHit;
					if (OverlapResult.Component->LineTraceComponent(ComponentHit, TraceStart, TraceEnd, TraceParams))
					{
						if (ComponentHit.Time < HitResult.Time)
						{
							HitResult = ComponentHit;
							HitResult.bBlockingHit = OverlapResult.bBlockingHit;
						}
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

			TraceParams.bTraceComplex = (Wheels[WheelIdx]->SweepType == ESweepType::ComplexSweep || Wheels[WheelIdx]->SweepType == ESweepType::SimpleAndComplexSweep);
			FVector TraceStart = SuspensionTrace[WheelIdx].Start;
			FVector TraceEnd = SuspensionTrace[WheelIdx].End;

			// #todo: should select method/shape from options passed in
			bool MadeContact = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, SpringCollisionChannel, TraceParams, FCollisionResponseParams::DefaultResponseParam);
			/*	float WheelRadius = PVehicle->Wheels[WheelIdx].GetEffectiveRadius();

				FVector VehicleUpAxis = GetOwner()->GetTransform().GetUnitAxis(EAxis::Z);

				bool MadeContact = GetWorld()->SweepSingleByChannel(HitResult
					, TraceStart + VehicleUpAxis * WheelRadius
					, TraceEnd + VehicleUpAxis * WheelRadius
					, FQuat::Identity, ECollisionChannel::ECC_WorldDynamic
					, FCollisionShape::MakeSphere(WheelRadius), TraceParams
					, FCollisionResponseParams::DefaultResponseParam);*/
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

			FVector GroundZVector = HitResult.ImpactNormal;
			FVector GroundXVector = FVector::CrossProduct(VehicleState.VehicleRightAxis, GroundZVector);
			FVector GroundYVector = FVector::CrossProduct(GroundZVector, GroundXVector);

			// the force should be applied along the ground surface not along vehicle forward vector?
			// NEW FVector FrictionForceVector = VehicleState.VehicleWorldTransform.TransformVector(FrictionForceLocal);
			FMatrix Mat(GroundXVector, GroundYVector, GroundZVector, VehicleState.VehicleWorldTransform.GetLocation());
			FVector FrictionForceVector = Mat.TransformVector(FrictionForceLocal);

			if (GWheeledVehicleDebugParams.ShowWheelForces)
			{
				// show longitudinal drive force
				{
					DrawDebugLine(GetWorld()
						, WheelState.WheelWorldLocation[WheelIdx]
						, WheelState.WheelWorldLocation[WheelIdx] + FrictionForceVector * 0.001f
						, FColor::Yellow, false, -1.0f, 0, 2);
				}

			}

			check(PWheel.InContact());
			TargetInstance->AddForceAtPosition(FrictionForceVector, WheelState.WheelWorldLocation[WheelIdx]);

			if (GWheeledVehicleDebugParams.ApplyWheelForcetoSurface)
			{
				// #todo: trying this - bit of a special case for the brick assets
				const FHitResult& Hit = Wheels[WheelIdx]->HitResult;
				if (Hit.GetComponent() && Hit.GetComponent()->IsAnyRigidBodyAwake())
				{
					//FVector Location = Hit.GetComponent()->GetComponentLocation();
					//FVector Location = Hit.ImpactPoint
					//Hit.GetComponent()->AddForceAtLocation(-FrictionForceVector, Location);

					FVector SpeedDiffLocal(PWheel.GetRoadSpeed() - PWheel.GetWheelGroundSpeed(), 0.f, 0.f);
					FVector SpeedDiffWorld = Mat.TransformVector(SpeedDiffLocal);

					Hit.GetComponent()->AddImpulse(SpeedDiffWorld, NAME_None, true);

					//DrawDebugLine(GetWorld()
					//	, Hit.Location
					//	, Hit.Location - FrictionForceVector * 0.001f
					//	, FColor::Yellow, true, 1.0f, 0, 2);

				}
			}
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

		if (PWheel.InContact())
		{
			NewDesiredLength = HitResult.Distance;

			// #todo: is this actually correct??
			SuspensionMovePosition = -FVector::DotProduct(WheelState.WheelWorldLocation[WheelIdx] - HitResult.ImpactPoint, VehicleState.VehicleUpAxis) + Wheel->WheelRadius;

			PSuspension.SetSuspensionLength(NewDesiredLength, PWheel.Setup().WheelRadius);
			PSuspension.SetLocalVelocity(WheelState.LocalWheelVelocity[WheelIdx]);
			PSuspension.Simulate(DeltaTime);

			float ForceMagnitude = PSuspension.GetSuspensionForce();


			FVector GroundZVector = HitResult.Normal;
			FVector SuspensionForceVector = /*VehicleUpAxis*/ GroundZVector * ForceMagnitude;

			FVector SusApplicationPoint = WheelState.WheelWorldLocation[WheelIdx] + PVehicle->Suspension[WheelIdx].Setup().SuspensionForceOffset;

			check(PWheel.InContact());
			TargetInstance->AddForceAtPosition(SuspensionForceVector, SusApplicationPoint);

			if (GWheeledVehicleDebugParams.ShowSuspensionForces)
			{
				DrawDebugLine(GetWorld()
					, SusApplicationPoint
					, SusApplicationPoint + SuspensionForceVector * 0.0005f
					, FColor::Blue, false, -1.0f, 0, 5);

				DrawDebugLine(GetWorld()
					, SusApplicationPoint
					, SusApplicationPoint + GroundZVector * 140.f
					, FColor::Yellow, false, -1.0f, 0, 5);
			}

			PWheel.SetWheelLoadForce(ForceMagnitude);
			PWheel.SetMassPerWheel(TargetInstance->GetBodyMass() / PVehicle->Wheels.Num());
			SusForces[WheelIdx] = ForceMagnitude;

		}

	}

	if (!GWheeledVehicleDebugParams.DisableRollbarForces)
	{
		// anti-roll forces
		static float FV = 0.01f; // 0.1f better
		float ForceDiffOnAxleF = SusForces[0] - SusForces[1];
		FVector SuspensionForceVector0 = VehicleState.VehicleUpAxis * ForceDiffOnAxleF * FV;
		FVector SuspensionForceVector1 = VehicleState.VehicleUpAxis * ForceDiffOnAxleF * -FV;

		FVector SusApplicationPoint0 = WheelState.WheelWorldLocation[0] + PVehicle->Suspension[0].Setup().SuspensionForceOffset;
		TargetInstance->AddForceAtPosition(SuspensionForceVector0, SusApplicationPoint0);

		FVector SusApplicationPoint1 = WheelState.WheelWorldLocation[1] + PVehicle->Suspension[1].Setup().SuspensionForceOffset;
		TargetInstance->AddForceAtPosition(SuspensionForceVector1, SusApplicationPoint1);


		float ForceDiffOnAxleR = SusForces[2] - SusForces[3];

		FVector SuspensionForceVector2 = VehicleState.VehicleUpAxis * ForceDiffOnAxleR * FV;
		FVector SuspensionForceVector3 = VehicleState.VehicleUpAxis * ForceDiffOnAxleR * -FV;

		FVector SusApplicationPoint2 = WheelState.WheelWorldLocation[2] + PVehicle->Suspension[2].Setup().SuspensionForceOffset;
		TargetInstance->AddForceAtPosition(SuspensionForceVector2, SusApplicationPoint2);

		FVector SusApplicationPoint3 = WheelState.WheelWorldLocation[3] + PVehicle->Suspension[3].Setup().SuspensionForceOffset;
		TargetInstance->AddForceAtPosition(SuspensionForceVector3, SusApplicationPoint3);
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
			// cheap Ackerman steering - outside wheel steers more than inside wheel

			FVector2D PtA; FVector2D PtB; float SteerLHS; float SteerRHS;
			//FSteeringUtility::CalculateAkermannAngle(-SteeringInput, PtA, PtB, SteerLHS, SteerRHS);

			PSteering.CalculateAkermannAngle(-SteeringInput, SteerLHS, SteerRHS);

			float MaxAngle = PVehicle->GetSuspension(WheelIdx).GetLocalRestingPosition().Y < 0.0f ? -SteerLHS : SteerRHS;
			{
				float SpeedScaling = 1.0f - (VehicleState.ForwardSpeed * 0.0001f); // #todo: do this scaling properly
				SpeedScaling = FMath::Min(1.f, FMath::Max(SpeedScaling, 0.2f));
				if (FMath::Abs(GWheeledVehicleDebugParams.SteeringOverride) > 0.01f)
				{
					PWheel.SetSteeringAngle(PWheel.Setup().MaxSteeringAngle * GWheeledVehicleDebugParams.SteeringOverride);
				}
				else
				{
					PWheel.SetSteeringAngle(MaxAngle * SpeedScaling);
				}
			}

		}
		else
		{
			PWheel.SetSteeringAngle(0.0f);
		}

		//float EngineBraking = 0.f;

		//if (PWheel.Setup().EngineEnabled)
		//{
		//	check(NumDrivenWheels > 0);
		//	PWheel.SetDriveTorque(TransmissionTorque / (float)NumDrivenWheels);
		//	if (ThrottleInput < SMALL_NUMBER)
		//	{
		//		EngineBraking = 0.f;// PWheel.Setup().HandbrakeTorque * 0.015f; // PEngine.GetEngineRPM()* PEngine.Setup().EngineBrakeEffect * 0.001f;
		//	}
		//}

	}
}

void UChaosWheeledVehicleMovementComponent::ApplyInput(float DeltaTime)
{
	Super::ApplyInput(DeltaTime);

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
		PTransmission.SetGear(1);
		PEngine.SetThrottle(GWheeledVehicleDebugParams.ThrottleOverride);
	}
	else
	{
		PEngine.SetThrottle(ThrottleInput);
	}

	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		auto& PWheel = PVehicle->Wheels[WheelIdx];

		float EngineBraking = 0.0f;
		if ((ThrottleInput < SMALL_NUMBER) && VehicleState.ForwardSpeed > SMALL_NUMBER && PWheel.Setup().EngineEnabled)
		{
			EngineBraking = PEngine.GetEngineRPM()* PEngine.Setup().EngineBrakeEffect;
		}

		if (PWheel.Setup().BrakeEnabled)
		{
			float BrakeForce = PWheel.Setup().MaxBrakeTorque * BrakeInput;
			PWheel.SetBrakeTorque(MToCm(BrakeForce + EngineBraking));
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

	PEngine.Simulate(DeltaTime);

	// SET SPEED FROM A DRIVEN WHEEL!!! - average all driven wheel speeds?? No notion of a differential as yet
	PEngine.SetEngineRPM(PTransmission.GetEngineRPMFromWheelRPM(FMath::Abs(PVehicle->Wheels[2].GetWheelRPM())));
	PTransmission.SetEngineRPM(PEngine.GetEngineRPM()); // needs engine RPM to decide when to change gear (automatic gearbox)
	PTransmission.SetAllowedToChangeGear(!VehicleState.bVehicleInAir);
	float GearRatio = PTransmission.GetGearRatio(PTransmission.GetCurrentGear());

	PTransmission.Simulate(DeltaTime);

	float TransmissionTorque = PTransmission.GetTransmissionTorque(PEngine.GetEngineTorque());

	// apply drive torque to wheels
	for (int WheelIdx = 0; WheelIdx < Wheels.Num(); WheelIdx++)
	{
		auto& PWheel = PVehicle->Wheels[WheelIdx];
		if (PWheel.Setup().EngineEnabled)
		{
			check(NumDrivenWheels > 0);
			PWheel.SetDriveTorque(MToCm(TransmissionTorque) / (float)NumDrivenWheels);
		}
	}
}


// Access to data
float UChaosWheeledVehicleMovementComponent::GetEngineRotationSpeed() const
{
	float EngineRPM = 0.f;

	if (PVehicle.IsValid())
	{
		EngineRPM = PVehicle->GetEngine().GetEngineRPM();
	}

	return EngineRPM;
}

float UChaosWheeledVehicleMovementComponent::GetEngineMaxRotationSpeed() const
{
	float MaxEngineRPM = 0.f;
	
	if (PVehicle.IsValid())
	{
		MaxEngineRPM = PVehicle->GetEngine().Setup().MaxRPM;
	}

	return MaxEngineRPM;
}

int32 UChaosWheeledVehicleMovementComponent::GetCurrentGear() const
{
	int32 CurrentGear = 0;

	if (PVehicle.IsValid())
	{
		CurrentGear = PVehicle->GetTransmission().GetCurrentGear();
	}

	return CurrentGear;
}

int32 UChaosWheeledVehicleMovementComponent::GetTargetGear() const
{
	int32 TargetGear = 0;

	if (PVehicle.IsValid())
	{
		TargetGear = PVehicle->GetTransmission().GetTargetGear();
	}

	return TargetGear;
}

bool UChaosWheeledVehicleMovementComponent::GetUseAutoGears() const
{
	bool UseAutoGears = 0;

	if (PVehicle.IsValid())
	{
		UseAutoGears = PVehicle->GetTransmission().Setup().TransmissionType == Chaos::ETransmissionType::Automatic;
	}

	return UseAutoGears;
}

float UChaosWheeledVehicleMovementComponent::GetMaxSpringForce() const
{
	// #todo:
	check(false);
	return 0.0f;
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


float FVehicleEngineConfig::FindPeakTorque() const
{
	// Find max torque
	float PeakTorque = 0.f;
	TArray<FRichCurveKey> TorqueKeys = TorqueCurve.GetRichCurveConst()->GetCopyOfKeys();
	for (int32 KeyIdx = 0; KeyIdx < TorqueKeys.Num(); KeyIdx++)
	{
		FRichCurveKey& Key = TorqueKeys[KeyIdx];
		PeakTorque = FMath::Max(PeakTorque, Key.Value);
	}
	return PeakTorque;
}

// Debug
void UChaosWheeledVehicleMovementComponent::DrawDebug(UCanvas* Canvas, float& YL, float& YPos)
{
	FChaosVehicleManager* MyVehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(GetWorld()->GetPhysicsScene());
	FBodyInstance* TargetInstance = GetBodyInstance();

	if (!PVehicle.IsValid() || TargetInstance == nullptr || MyVehicleManager == nullptr)
	{
		return;
	}

	float ForwardSpeedKmH = CmSToKmH(GetForwardSpeed());
	float ForwardSpeedMPH = CmSToMPH(GetForwardSpeed());
	float ForwardSpeedMSec = CmToM(GetForwardSpeed());
	auto& PTransmission = PVehicle->GetTransmission();
	auto& PEngine = PVehicle->GetEngine();
	auto& PTransmissionSetup = PTransmission.Setup();

	// always draw this even on (DebugPage == EDebugPages::BasicPage)
	{
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
	}

	UFont* RenderFont = GEngine->GetMediumFont();
	// draw drive data
	{
		Canvas->SetDrawColor(FColor::White);
		YPos += 16;

		if (TargetInstance)
		{
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Mass (Kg): %.1f"), TargetInstance->GetBodyMass()), 4, YPos);
			YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Inertia : %s"), *TargetInstance->GetBodyInertiaTensor().ToString()), 4, YPos);
		}

		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Awake %d"), TargetInstance->IsInstanceAwake()), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Speed (km/h): %.1f  (MPH): %.1f  (m/s): %.1f"), ForwardSpeedKmH, ForwardSpeedMPH, ForwardSpeedMSec), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Acceleration (m/s-2): %.1f"), CmToM(GetForwardAcceleration())), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Steering: %.1f (RAW %.1f)"), SteeringInput, RawSteeringInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Throttle: %.1f (RAW %.1f)"), ThrottleInput, RawThrottleInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Brake: %.1f (RAW %.1f)"), BrakeInput, RawBrakeInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Pitch: %.1f (RAW %.1f)"), PitchInput, RawPitchInput), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("RPM: %.1f (ChangeUp RPM %d, ChangeDown RPM %d)"), GetEngineRotationSpeed(), PTransmissionSetup.ChangeUpRPM, PTransmissionSetup.ChangeDownRPM), 4, YPos);
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Gear: %d (Target %d)"), GetCurrentGear(), GetTargetGear()), 4, YPos);
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

	// draw wheel layout
	if (DebugPage == EDebugPages::FrictionPage)
	{
		FVector2D MaxSize = GetWheelLayoutDimensions();

		// Draw a top down representation of the wheels in position, with the directionl forces being shown
		for (int32 WheelIdx = 0; WheelIdx < WheelSetups.Num(); ++WheelIdx)
		{

			auto& PWheel = PVehicle->Wheels[WheelIdx];
			FVector Forces = PWheel.GetForceFromFriction();

			FChaosWheelSetup& WheelSetup = WheelSetups[WheelIdx];
			UChaosVehicleWheel* Wheel = WheelSetup.WheelClass.GetDefaultObject();
			check(Wheel);
			UPhysicalMaterial* ContactMat = Wheel->GetContactSurfaceMaterial();

			const FVector WheelOffset = GetWheelRestingPosition(WheelSetup);

			float DrawScale = 200;
			FVector2D CentreDrawPosition(350, 400);
			FVector2D WheelDrawPosition(WheelOffset.Y, -WheelOffset.X);
			WheelDrawPosition *= DrawScale;
			WheelDrawPosition /= MaxSize;
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
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Ad Limit : %.2f"), PWheel.AdhesiveLimit), Xpos, Ypos);

			if (PWheel.AppliedLinearDriveForce > PWheel.AdhesiveLimit)
			{
				Canvas->SetDrawColor(FColor::Red);
			}
			else
			{
				Canvas->SetDrawColor(FColor::Green);
			}
			Ypos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("Ap Drive : %.2f"), PWheel.AppliedLinearDriveForce), Xpos, Ypos);

			if (PWheel.AppliedLinearBrakeForce > PWheel.AdhesiveLimit)
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
			

			// ground velocity
			// wheel ground velocity
			// Sx
			// Accel Torque
			// Brake Torque

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

			float Scale = 200.0f / MaxSize.Y;
			FVector2D CentreDrawPosition(450, 400);
			FVector2D WheelDrawPosition(WheelOffset.Y, -WheelOffset.X);
			WheelDrawPosition *= Scale;
			WheelDrawPosition += CentreDrawPosition;

			if (PWheel.Setup().SteeringEnabled)
			{
				if (WheelOffset.Y > 0)
				{
					FVector2D C1, P, C2;
					PSteering.GetRightHingeLocations(C1, P, C2);
					C1.Y = -C1.Y;
					P.Y = -P.Y;
					C2.Y = -C2.Y;

					float SteerAngle = DegToRad(PWheel.GetSteeringAngle());
					FVector2D Tire = FVector2D(FMath::Sin(SteerAngle), -FMath::Cos(SteerAngle)) * 30.0f;

					FVector2D WPt = WheelDrawPosition;
					FVector2D JPt = WheelDrawPosition + (P - C2) * Scale;
					FVector2D CPt = WheelDrawPosition + (C1 - C2) * Scale;
					DrawLine2D(Canvas, WPt - Tire, WPt + Tire, FColor::Black, 8);
					DrawLine2D(Canvas, CPt, JPt, FColor::Orange, 3);
					DrawLine2D(Canvas, WPt, JPt, FColor::Orange, 3);
					J1 = CPt;
				}
				else
				{
					FVector2D C1, P, C2;
					PSteering.GetLeftHingeLocations(C1, P, C2);
					C1.Y = -C1.Y;
					P.Y = -P.Y;
					C2.Y = -C2.Y;

					float SteerAngle = DegToRad(PWheel.GetSteeringAngle());
					FVector2D Tire = FVector2D(FMath::Sin(SteerAngle), -FMath::Cos(SteerAngle)) * 30.0f;

					FVector2D WPt = WheelDrawPosition;
					FVector2D JPt = WheelDrawPosition + (P - C2) * Scale;
					FVector2D CPt = WheelDrawPosition + (C1 - C2) * Scale;
					DrawLine2D(Canvas, WPt - Tire, WPt + Tire, FColor::Black, 8);
					DrawLine2D(Canvas, CPt, JPt, FColor::Orange, 3);
					DrawLine2D(Canvas, WPt, JPt, FColor::Orange, 3);
					J2 = CPt;
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

	// draw longitudinal friction slip curve for each wheel
	//if (DebugPage == EDebugPages::FrictionPage)
	//{
	//	for (int WheelIdx=0; WheelIdx < PVehicle->Wheels.Num(); WheelIdx++)
	//	{
	//		int GraphWidth = 100; int GraphHeight = 60; int Spacing = 35;
	//		int GraphXPos = 500 + (GraphWidth+Spacing) * (int)(WheelIdx % 2); 
	//		int GraphYPos = 50 + (GraphHeight + Spacing) * (int)(WheelIdx / 2);
	//		float XSample = PVehicle->Wheels[WheelIdx].GetNormalizedLongitudinalSlip();
	//		
	//		FVector2D CurrentValue(XSample, Chaos::FSimpleWheelSim::GetNormalisedFrictionFromSlipAngle(XSample));
	//		Canvas->DrawDebugGraph(FString::Printf(TEXT("Longitudinal Slip Graph [%d]"), WheelIdx)
	//				, CurrentValue.X, CurrentValue.Y
	//				, GraphXPos, GraphYPos
	//				, GraphWidth, GraphHeight
	//				, FVector2D(0, 1), FVector2D(1, 0));

	//		float Step = 0.02f;
	//		FVector2D LastPoint;
	//		for (float X = 0; X < 1.0f; X += Step)
	//		{
	//			float Y = Chaos::FSimpleWheelSim::GetNormalisedFrictionFromSlipAngle(X);
	//			FVector2D NextPoint(GraphXPos + GraphWidth * X, GraphYPos + GraphHeight - GraphHeight * Y);
	//			if (X > SMALL_NUMBER)
	//			{
	//				DrawLine2D(Canvas, LastPoint, NextPoint, FColor::Cyan);
	//			}
	//			LastPoint = NextPoint;
	//		}	
	//	}

	//}

	//// draw lateral friction slip curve for each wheel
	//if (DebugPage == EDebugPages::FrictionPage)
	//{
	//	for (int WheelIdx = 0; WheelIdx < PVehicle->Wheels.Num(); WheelIdx++)
	//	{
	//		int GraphWidth = 100; int GraphHeight = 60; int Spacing = 35;
	//		int GraphXPos = 500 + (GraphWidth + Spacing) * (int)(WheelIdx % 2);
	//		int GraphYPos = 350 + (GraphHeight + Spacing) * (int)(WheelIdx / 2);
	//		float XSample = PVehicle->Wheels[WheelIdx].GetNormalizedLateralSlip();

	//		FVector2D CurrentValue(XSample, Chaos::FSimpleWheelSim::GetNormalisedFrictionFromSlipAngle(XSample));
	//		Canvas->DrawDebugGraph(FString::Printf(TEXT("Lateral Slip Graph [%d]"), WheelIdx)
	//			, CurrentValue.X, CurrentValue.Y
	//			, GraphXPos, GraphYPos
	//			, GraphWidth, GraphHeight
	//			, FVector2D(0, 1), FVector2D(1, 0));

	//		float Step = 0.02f;
	//		FVector2D LastPoint;
	//		for (float X = 0; X < 1.0f; X += Step)
	//		{
	//			float Y = Chaos::FSimpleWheelSim::GetNormalisedFrictionFromSlipAngle(X);
	//			FVector2D NextPoint(GraphXPos + GraphWidth * X, GraphYPos + GraphHeight - GraphHeight * Y);
	//			if (X > SMALL_NUMBER)
	//			{
	//				DrawLine2D(Canvas, LastPoint, NextPoint, FColor::Cyan);
	//			}
	//			LastPoint = NextPoint;
	//		}
	//	}

	//}

	// draw engine torque curve - just putting engine under transmission
	if (DebugPage == EDebugPages::TransmissionPage)
	{
		float MaxTorque = PEngine.Setup().MaxTorque;
		int CurrentRPM = (int)PEngine.GetEngineRPM();
		FVector2D CurrentValue(CurrentRPM, PEngine.GetEngineTorque());
		int GraphWidth = 200; int GraphHeight = 120;
		int GraphXPos = 200; int GraphYPos = 350;

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
	}

	// draw transmission torque curve
	if (DebugPage == EDebugPages::TransmissionPage)
	{
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

			float DrawScale = 100;
			FVector2D CentreDrawPosition(500, 350);
			FVector2D WheelDrawPosition(WheelOffset.Y, -WheelOffset.X);
			WheelDrawPosition *= DrawScale;
			WheelDrawPosition /= MaxSize;
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

}

void UChaosWheeledVehicleMovementComponent::DrawDebug3D()
{
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
			// push the visualization out a bit sideways from the wheel model so we can actually see it
			FVector VehicleRightAxis = BodyTransform.GetUnitAxis(EAxis::Y) * -40.0f;
			const FVector& WheelOffset = PSuspension.GetLocalRestingPosition();
			if (WheelOffset.Y < 0.0f)
			{
				VehicleRightAxis = VehicleRightAxis * -1.0f;
			}

			FVector Up(0.f, 0.f, -PSuspension.Setup().SuspensionMaxRaise);
			FVector Down(0.f, 0.f, PSuspension.Setup().SuspensionMaxDrop);

			Up = BodyTransform.TransformVector(Up);
			Down = BodyTransform.TransformVector(Down);

			FVector Position = PSuspension.GetLocalRestingPosition();
			Position = BodyTransform.TransformPosition(Position);

			DrawDebugLine(GetWorld(), Position + Up + VehicleRightAxis, Position + Down + VehicleRightAxis, FColor::Orange, false, -1.f, 0, 2.f);
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
			FVector VehicleRightAxis = GetOwner()->GetTransform().GetUnitAxis(EAxis::Y) * 50.0f;
			const FVector& WheelOffset = PVehicle->Suspension[WheelIdx].GetLocalRestingPosition();
			if (WheelOffset.Y < 0.0f)
			{
				VehicleRightAxis = VehicleRightAxis * -1.0f;
			}

			FColor UseColor = PVehicle->Wheels[WheelIdx].InContact() ? FColor::Green : FColor::Red;
			DrawDebugLine(GetWorld(), TraceStart + VehicleRightAxis, TraceEnd + VehicleRightAxis, UseColor, false, -1.f, 0, 2.f);
		}
	}
}


FChaosWheelSetup::FChaosWheelSetup()
	: WheelClass(UChaosVehicleWheel::StaticClass())
	, BoneName(NAME_None)
	, AdditionalOffset(0.0f)
{

}

#endif // WITH_CHAOS

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
