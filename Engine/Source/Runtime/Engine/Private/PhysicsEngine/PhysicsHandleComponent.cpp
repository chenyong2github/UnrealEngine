// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "EngineDefines.h"
#include "PhysxUserData.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsPublic.h"
#include "Physics/PhysicsInterfaceCore.h"

#if PHYSICS_INTERFACE_PHYSX
	#include "PhysXPublic.h"
#elif WITH_CHAOS
	#include "Chaos/ParticleHandle.h"
	#include "Chaos/KinematicGeometryParticles.h"
	#include "Chaos/PBDJointConstraintTypes.h"
	#include "Chaos/PBDJointConstraintData.h"
	#include "Chaos/Sphere.h"
	#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#endif

#include "ChaosCheck.h"

UPhysicsHandleComponent::UPhysicsHandleComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_CHAOS
	, bPendingConstraint(false)
	, PhysicsUserData(&ConstraintInstance)
	, GrabbedHandle(nullptr)
	, KinematicHandle(nullptr)
	, ConstraintLocalPosition(FVector::ZeroVector)
	, ConstraintLocalRotation(FRotator::ZeroRotator)
#endif
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	LinearDamping = 200.0f;
	LinearStiffness = 750.0f;
	AngularDamping = 500.0f;
	AngularStiffness = 1500.0f;
	InterpolationSpeed = 50.f;
	bSoftAngularConstraint = true;
	bSoftLinearConstraint = true;
	bInterpolateTarget = true;
}

void UPhysicsHandleComponent::OnUnregister()
{
	if(GrabbedComponent)
	{
		ReleaseComponent();
	}

#if PHYSICS_INTERFACE_PHYSX
	if(HandleData)
	{
		check(KinActorData);

		// use correct scene
		PxScene* PScene = HandleData->getScene();
		if(PScene)
		{
			PScene->lockWrite();

			// destroy joint
			HandleData->release();
			HandleData = NULL;

			// Destroy temporary actor.
			KinActorData->release();
			KinActorData = NULL;

			PScene->unlockWrite();
		}
	}
#endif // WITH_PHYSX

	Super::OnUnregister();
}

void UPhysicsHandleComponent::GrabComponent(class UPrimitiveComponent* InComponent, FName InBoneName, FVector GrabLocation, bool bInConstrainRotation)
{
	//Old behavior was automatically using grabbed body's orientation. This is an edge case that we'd rather not support automatically. We do it here for backwards compat

	if (!InComponent)
	{
		return;
	}

	// Get the PxRigidDynamic that we want to grab.
	FBodyInstance* BodyInstance = InComponent->GetBodyInstance(InBoneName);
	if (!BodyInstance)
	{
		return;
	}

	FRotator GrabbedRotation = FRotator::ZeroRotator;

	if(FPhysicsInterface::IsValid(BodyInstance->ActorHandle))
	{
		FPhysicsCommand::ExecuteRead(BodyInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
		{
			GrabbedRotation = FPhysicsInterface::GetGlobalPose_AssumesLocked(Actor).Rotator();
		});
	}

	GrabComponentImp(InComponent, InBoneName, GrabLocation, GrabbedRotation, bInConstrainRotation);
}

void UPhysicsHandleComponent::GrabComponentAtLocation(class UPrimitiveComponent* Component, FName InBoneName, FVector GrabLocation)
{
	GrabComponentImp(Component, InBoneName, GrabLocation, FRotator::ZeroRotator, false);
}

void UPhysicsHandleComponent::GrabComponentAtLocationWithRotation(class UPrimitiveComponent* Component, FName InBoneName, FVector GrabLocation, FRotator Rotation)
{
	GrabComponentImp(Component, InBoneName, GrabLocation, Rotation, true);
}

void UPhysicsHandleComponent::GrabComponentImp(UPrimitiveComponent* InComponent, FName InBoneName, const FVector& Location, const FRotator& Rotation, bool bInConstrainRotation)
{
	// If we are already holding something - drop it first.
	if(GrabbedComponent != NULL)
	{
		ReleaseComponent();
	}

	if(!InComponent)
	{
		return;
	}

	// Get the PxRigidDynamic that we want to grab.
	FBodyInstance* BodyInstance = InComponent->GetBodyInstance(InBoneName);
	if (!BodyInstance)
	{
		return;
	}

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

	FPhysicsCommand::ExecuteWrite(BodyInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
	{
		if(PxRigidActor* PActor = FPhysicsInterface::GetPxRigidActor_AssumesLocked(Actor))
		{
			PxScene* Scene = PActor->getScene();

			// Get transform of actor we are grabbing
			PxVec3 KinLocation = U2PVector(Location);
			PxQuat KinOrientation = U2PQuat(Rotation.Quaternion());
			PxTransform GrabbedActorPose = PActor->getGlobalPose();
			PxTransform KinPose(KinLocation, KinOrientation);

			// set target and current, so we don't need another "Tick" call to have it right
			TargetTransform = CurrentTransform = P2UTransform(KinPose);

			// If we don't already have a handle - make one now.
			if(!HandleData)
			{
				// Create kinematic actor we are going to create joint with. This will be moved around with calls to SetLocation/SetRotation.
				PxRigidDynamic* KinActor = Scene->getPhysics().createRigidDynamic(KinPose);
				KinActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
				KinActor->setMass(1.0f);
				KinActor->setMassSpaceInertiaTensor(PxVec3(1.0f, 1.0f, 1.0f));

				// No bodyinstance
				KinActor->userData = NULL;

				// Add to Scene
				Scene->addActor(*KinActor);

				// Save reference to the kinematic actor.
				KinActorData = KinActor;

				// Create the joint
				PxD6Joint* NewJoint = PxD6JointCreate(Scene->getPhysics(), KinActor, PxTransform(PxIdentity), PActor, GrabbedActorPose.transformInv(KinPose));

				if(!NewJoint)
				{
					HandleData = 0;
				}
				else
				{
					// No constraint instance
					NewJoint->userData = NULL;
					HandleData = NewJoint;

					// Remember the scene index that the handle joint/actor are in.
					FPhysScene* RBScene = FPhysxUserData::Get<FPhysScene>(Scene->userData);

					// Setting up the joint

					PxD6Motion::Enum const LocationMotionType = bSoftLinearConstraint ? PxD6Motion::eFREE : PxD6Motion::eLOCKED;
					PxD6Motion::Enum const RotationMotionType = (bSoftAngularConstraint || !bInConstrainRotation) ? PxD6Motion::eFREE : PxD6Motion::eLOCKED;

					NewJoint->setMotion(PxD6Axis::eX, LocationMotionType);
					NewJoint->setMotion(PxD6Axis::eY, LocationMotionType);
					NewJoint->setMotion(PxD6Axis::eZ, LocationMotionType);
					NewJoint->setDrivePosition(PxTransform(PxVec3(0, 0, 0)));

					NewJoint->setMotion(PxD6Axis::eTWIST, RotationMotionType);
					NewJoint->setMotion(PxD6Axis::eSWING1, RotationMotionType);
					NewJoint->setMotion(PxD6Axis::eSWING2, RotationMotionType);

					bRotationConstrained = bInConstrainRotation;

					UpdateDriveSettings();
				}
			}
		}
	});
	
#elif WITH_CHAOS
	// simulatable bodies should have handles.
	FPhysicsActorHandle& InHandle = BodyInstance->GetPhysicsActorHandle();
	if (!InHandle)
	{
		return;
	}

	// the kinematic rigid body needs to be created before the constraint. 
	if (!KinematicHandle)
	{
		using namespace Chaos;

		FActorCreationParams Params;
		Params.InitialTM = FTransform(Rotation, Location);
		FPhysicsInterface::CreateActor(Params, KinematicHandle);

		KinematicHandle->GetGameThreadAPI().SetGeometry(TUniquePtr<FImplicitObject>(new TSphere<FReal, 3>(TVector<FReal, 3>(0.f), 1000.f)));
		KinematicHandle->GetGameThreadAPI().SetObjectState(EObjectStateType::Kinematic);

		if (FPhysScene* Scene = BodyInstance->GetPhysicsScene())
		{
			FPhysicsInterface::AddActorToSolver(KinematicHandle, Scene->GetSolver());
			ConstraintInstance.PhysScene = Scene;
		}
	}

	FTransform KinematicTransform(Rotation, Location);

	// set target and current, so we don't need another "Tick" call to have it right
	TargetTransform = CurrentTransform = KinematicTransform;

	KinematicHandle->GetGameThreadAPI().SetX(KinematicTransform.GetLocation());
	KinematicHandle->GetGameThreadAPI().SetR(KinematicTransform.GetRotation());	
	
	FTransform GrabbedTransform(InHandle->GetGameThreadAPI().R(), InHandle->GetGameThreadAPI().X());
	ConstraintLocalPosition = GrabbedTransform.InverseTransformPosition(Location);
	ConstraintLocalRotation = FRotator(GrabbedTransform.InverseTransformRotation(FQuat(Rotation)));

	bRotationConstrained = bInConstrainRotation;
	GrabbedHandle = InHandle; 
#endif

	GrabbedComponent = InComponent;
	GrabbedBoneName = InBoneName;
}

void UPhysicsHandleComponent::UpdateDriveSettings()
{
#if PHYSICS_INTERFACE_PHYSX
	if(HandleData != nullptr)
	{
		if (bSoftLinearConstraint)
		{
		HandleData->setDrive(PxD6Drive::eX, PxD6JointDrive(LinearStiffness, LinearDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
		HandleData->setDrive(PxD6Drive::eY, PxD6JointDrive(LinearStiffness, LinearDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
		HandleData->setDrive(PxD6Drive::eZ, PxD6JointDrive(LinearStiffness, LinearDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
		}

		if (bSoftAngularConstraint && bRotationConstrained)
		{
			HandleData->setDrive(PxD6Drive::eSLERP, PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));

			//NewJoint->setDrive(PxD6Drive::eTWIST, PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
			//NewJoint->setDrive(PxD6Drive::eSWING, PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
		}
	}
#elif WITH_CHAOS

	if (ConstraintHandle.IsValid() && ConstraintHandle.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		FPhysicsCommand::ExecuteWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InConstraintHandle)
			{
				if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(ConstraintHandle.Constraint))
				{
					Chaos::EJointMotionType LocationMotionType = bSoftLinearConstraint ? Chaos::EJointMotionType::Free : Chaos::EJointMotionType::Locked;
					Chaos::EJointMotionType RotationMotionType = (bSoftAngularConstraint || !bRotationConstrained) ? Chaos::EJointMotionType::Free : Chaos::EJointMotionType::Locked;

					Constraint->SetCollisionEnabled(false);
					Constraint->SetLinearVelocityDriveEnabled(Chaos::TVec3<bool>(LocationMotionType != Chaos::EJointMotionType::Locked));
					Constraint->SetLinearPositionDriveEnabled(Chaos::TVec3<bool>(LocationMotionType != Chaos::EJointMotionType::Locked));
					Constraint->SetLinearMotionTypesX(LocationMotionType);
					Constraint->SetLinearMotionTypesY(LocationMotionType);
					Constraint->SetLinearMotionTypesZ(LocationMotionType);

					Constraint->SetAngularSLerpPositionDriveEnabled(bRotationConstrained && RotationMotionType != Chaos::EJointMotionType::Locked);
					Constraint->SetAngularSLerpVelocityDriveEnabled(bRotationConstrained && RotationMotionType != Chaos::EJointMotionType::Locked);
					Constraint->SetAngularMotionTypesX(RotationMotionType);
					Constraint->SetAngularMotionTypesY(RotationMotionType);
					Constraint->SetAngularMotionTypesZ(RotationMotionType);
					FTransform GrabConstraintLocalTransform(ConstraintLocalRotation, ConstraintLocalPosition);
					Constraint->SetJointTransforms({ FTransform::Identity , GrabConstraintLocalTransform });

					if (LocationMotionType != Chaos::EJointMotionType::Locked)
					{
						Constraint->SetLinearDriveStiffness(LinearStiffness);
						Constraint->SetLinearDriveDamping(LinearDamping);
					}

					if (bRotationConstrained && RotationMotionType != Chaos::EJointMotionType::Locked)
					{
						Constraint->SetAngularDriveStiffness(AngularStiffness);
						Constraint->SetAngularDriveDamping(AngularDamping);
					}
				}
			});
	}
#endif // WITH_PHYSX
}

void UPhysicsHandleComponent::ReleaseComponent()
{
#if PHYSICS_INTERFACE_PHYSX
	if(GrabbedComponent)
	{
		if(HandleData)
		{
			check(KinActorData);

			// use correct scene
			PxScene* PScene = HandleData->getScene();
			if(PScene)
			{
				SCOPED_SCENE_WRITE_LOCK(PScene);
				// Destroy joint.
				HandleData->release();
				
				// Destroy temporary actor.
				KinActorData->release();
				
			}
			KinActorData = NULL;
			HandleData = NULL;
		}

		bRotationConstrained = false;

		GrabbedComponent->WakeRigidBody(GrabbedBoneName);

		GrabbedComponent = NULL;
		GrabbedBoneName = NAME_None;
	}
#elif WITH_CHAOS
	if (ConstraintHandle.IsValid())
	{
		FPhysicsInterface::ReleaseConstraint(ConstraintHandle);
		bPendingConstraint = false;
	}

	if (GrabbedComponent)
	{
		GrabbedComponent = NULL;
		GrabbedBoneName = NAME_None;
		GrabbedHandle = nullptr;
	}

	if (KinematicHandle)
	{
		FChaosEngineInterface::ReleaseActor(KinematicHandle, ConstraintInstance.GetPhysicsScene());
	}

	ConstraintInstance.Reset();

#endif
}

UPrimitiveComponent* UPhysicsHandleComponent::GetGrabbedComponent() const
{
	return GrabbedComponent;
}

void UPhysicsHandleComponent::SetTargetLocation(FVector NewLocation)
{
	TargetTransform.SetTranslation(NewLocation);
}

void UPhysicsHandleComponent::SetTargetRotation(FRotator NewRotation)
{
	TargetTransform.SetRotation(NewRotation.Quaternion());
}

void UPhysicsHandleComponent::SetTargetLocationAndRotation(FVector NewLocation, FRotator NewRotation)
{
	TargetTransform = FTransform(NewRotation, NewLocation);
}


void UPhysicsHandleComponent::UpdateHandleTransform(const FTransform& NewTransform)
{
#if PHYSICS_INTERFACE_PHYSX
	if (!KinActorData)
	{
		return;
	}

	bool bChangedPosition = true;
	bool bChangedRotation = true;

	PxRigidDynamic* KinActor = KinActorData;
	PxScene* PScene = KinActor->getScene();
	SCOPED_SCENE_WRITE_LOCK(PScene);

	// Check if the new location is worthy of change
	PxVec3 PNewLocation = U2PVector(NewTransform.GetTranslation());
	PxVec3 PCurrentLocation = KinActor->getGlobalPose().p;
	if((PNewLocation - PCurrentLocation).magnitudeSquared() <= 0.01f*0.01f)
	{
		PNewLocation = PCurrentLocation;
		bChangedPosition = false;
	}

	// Check if the new rotation is worthy of change
	PxQuat PNewOrientation = U2PQuat(NewTransform.GetRotation());
	PxQuat PCurrentOrientation = KinActor->getGlobalPose().q;
	if((FMath::Abs(PNewOrientation.dot(PCurrentOrientation)) > (1.f - SMALL_NUMBER)))
	{
		PNewOrientation = PCurrentOrientation;
		bChangedRotation = false;
	}

	// Don't call moveKinematic if it hasn't changed - that will stop bodies from going to sleep.
	if (bChangedPosition || bChangedRotation)
	{
		KinActor->setKinematicTarget(PxTransform(PNewLocation, PNewOrientation));

		//LOC_MOD
		//PxD6Joint* Joint = (PxD6Joint*) HandleData;
		//if(Joint)// && (PNewLocation - PCurrentLocation).magnitudeSquared() > 0.01f*0.01f)
		//{
		//	PxRigidActor* Actor0, *Actor1;
		//	Joint->getActors(Actor0, Actor1);
		//	//Joint->setDrivePosition(PxTransform(Actor0->getGlobalPose().transformInv(PNewLocation)));
		//	Joint->setDrivePosition(PxTransform::createIdentity());
		//	//Joint->setDriveVelocity(PxVec3(0), PxVec3(0));
		//}
	}
#elif WITH_CHAOS
	if (!CurrentTransform.Equals(PreviousTransform))
	{
		FPhysicsCommand::ExecuteWrite(KinematicHandle, [&](const FPhysicsActorHandle& InKinematicHandle)
			{
				KinematicHandle->GetGameThreadAPI().SetX(CurrentTransform.GetTranslation());
				KinematicHandle->GetGameThreadAPI().SetR(CurrentTransform.GetRotation());
			});

		PreviousTransform = CurrentTransform;
	}
#endif
}

void UPhysicsHandleComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);


#if WITH_CHAOS
	if (bPendingConstraint)
	{
		if (!ConstraintHandle.IsValid())
			return;
		bPendingConstraint = false;
	}

	if (ConstraintHandle.IsValid())
	{
#endif
		if (bInterpolateTarget)
		{
			const float Alpha = FMath::Clamp(DeltaTime * InterpolationSpeed, 0.f, 1.f);
			FTransform C = CurrentTransform;
			FTransform T = TargetTransform;
			C.NormalizeRotation();
			T.NormalizeRotation();
			CurrentTransform.Blend(C, T, Alpha);
		}
		else
		{
			CurrentTransform = TargetTransform;
		}

		UpdateHandleTransform(CurrentTransform);
#if WITH_CHAOS
	}
	else if (KinematicHandle && GrabbedHandle)
	{
		using namespace Chaos;

		ConstraintHandle = FChaosEngineInterface::CreateConstraint(KinematicHandle, GrabbedHandle, FTransform::Identity, FTransform::Identity); // Correct transforms will be set in the update
		if (ConstraintHandle.IsValid() && ConstraintHandle.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
		{
			if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(ConstraintHandle.Constraint))
			{
				// need to tie together the instance and the handle for scene read/write locks
				Constraint->SetUserData(&PhysicsUserData/*has a (void*)FConstraintInstanceBase*/);
				ConstraintInstance.ConstraintHandle = ConstraintHandle;

				UpdateDriveSettings();
			}
		}
		bPendingConstraint = true;
	}
#endif
}

void UPhysicsHandleComponent::GetTargetLocationAndRotation(FVector& OutLocation, FRotator& OutRotation) const 
{
	OutRotation = TargetTransform.Rotator();
	OutLocation = TargetTransform.GetTranslation();
}

void UPhysicsHandleComponent::SetLinearDamping(float NewLinearDamping)
{
	LinearDamping = NewLinearDamping;
	UpdateDriveSettings();
}

void UPhysicsHandleComponent::SetLinearStiffness(float NewLinearStiffness)
{
	LinearStiffness = NewLinearStiffness;
	UpdateDriveSettings();
}

void UPhysicsHandleComponent::SetAngularDamping(float NewAngularDamping)
{
	AngularDamping = NewAngularDamping;
	UpdateDriveSettings();
}

void UPhysicsHandleComponent::SetAngularStiffness(float NewAngularStiffness)
{
	AngularStiffness = NewAngularStiffness;
	UpdateDriveSettings();
}

void UPhysicsHandleComponent::SetInterpolationSpeed(float NewInterpolationSpeed)
{
	InterpolationSpeed = NewInterpolationSpeed;
}
