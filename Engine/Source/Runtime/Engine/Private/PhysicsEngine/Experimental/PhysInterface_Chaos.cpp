// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_CHAOS

#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "SolverObjects/BodyInstancePhysicsObject.h"

#include "Chaos/Box.h"
#include "Chaos/Cylinder.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "PBDRigidsSolver.h"
#include "Templates/UniquePtr.h"
#include "ChaosSolversModule.h"

#include "Async/ParallelFor.h"
#include "Components/PrimitiveComponent.h"

#if WITH_PHYSX
#include "geometry/PxConvexMesh.h"
#include "geometry/PxTriangleMesh.h"
#include "foundation/PxVec3.h"
#include "extensions/PxMassProperties.h"
#endif

#define FORCE_ANALYTICS 0

DEFINE_STAT(STAT_TotalPhysicsTime);
DEFINE_STAT(STAT_NumCloths);
DEFINE_STAT(STAT_NumClothVerts);

DECLARE_CYCLE_STAT(TEXT("Start Physics Time (sync)"), STAT_PhysicsKickOffDynamicsTime, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Fetch Results Time (sync)"), STAT_PhysicsFetchDynamicsTime, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Start Physics Time (async)"), STAT_PhysicsKickOffDynamicsTime_Async, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Fetch Results Time (async)"), STAT_PhysicsFetchDynamicsTime_Async, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"), STAT_UpdateKinematicsOnDeferredSkelMeshes, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Phys Events Time"), STAT_PhysicsEventTime, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("SyncComponentsToBodies (sync)"), STAT_SyncComponentsToBodies, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("SyncComponentsToBodies (async)"), STAT_SyncComponentsToBodies_Async, STATGROUP_Physics);

DECLARE_DWORD_COUNTER_STAT(TEXT("Broadphase Adds"), STAT_NumBroadphaseAdds, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Broadphase Removes"), STAT_NumBroadphaseRemoves, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Constraints"), STAT_NumActiveConstraints, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Simulated Bodies"), STAT_NumActiveSimulatedBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Kinematic Bodies"), STAT_NumActiveKinematicBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Mobile Bodies"), STAT_NumMobileBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Static Bodies"), STAT_NumStaticBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Shapes"), STAT_NumShapes, STATGROUP_Physics);

DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Broadphase Adds"), STAT_NumBroadphaseAddsAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Broadphase Removes"), STAT_NumBroadphaseRemovesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Constraints"), STAT_NumActiveConstraintsAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Simulated Bodies"), STAT_NumActiveSimulatedBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Kinematic Bodies"), STAT_NumActiveKinematicBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Mobile Bodies"), STAT_NumMobileBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Static Bodies"), STAT_NumStaticBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Shapes"), STAT_NumShapesAsync, STATGROUP_Physics);

FPhysInterface_Chaos::FPhysInterface_Chaos(const AWorldSettings* Settings) 
{

}

FPhysInterface_Chaos::~FPhysInterface_Chaos()
{
}


// Interface functions
void FPhysInterface_Chaos::CreateActor(const FActorCreationParams& InParams, FPhysicsActorHandle& Handle)
{
	if (FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers"))
	{
		if (Chaos::IDispatcher* Dispatcher = ChaosModule->GetDispatcher())
		{
			if (FPhysScene_ChaosInterface* Scene = InParams.Scene)
			{
				// Create the new physics proxy
				if (!Handle.GetPhysicsObject())
				{
					FBodyInstancePhysicsObject * PhysicsObject = new FBodyInstancePhysicsObject(nullptr, { 1.0, 1.0, FVector(1.f) });
					PhysicsObject->CreationParameters = InParams;
					Handle.SetScene(Scene);
					Handle.SetPhysicsObject(PhysicsObject);
					Scene->Scene.AddObject(nullptr, PhysicsObject);
				}

				// Pass the proxy off to the physics thread
				Dispatcher->EnqueueCommand([Scene, InParams, Handle](Chaos::FPersistentPhysicsTask* PhysThread)
				{
					if (Chaos::FPBDRigidsSolver* Solver = Scene->GetSolver())
					{
						// Collect references
						if (FBodyInstancePhysicsObject* Object = Handle.GetPhysicsObject())
						{
							FParticlesType& Particles = Solver->GetRigidParticles();

							int32 Index = Particles.Size();
							Object->SetRigidBodyId(Index);
							Object->InitializedIndices.Add(Index);

							// Add the particle
							Particles.AddParticles(1);
							Particles.X(Index) = InParams.InitialTM.GetTranslation();
							Particles.R(Index) = InParams.InitialTM.GetRotation();
							Particles.M(Index) = 1;					// TODO: This should be based on InParams?
							Particles.I(Index) = FMatrix::Identity; // TODO: This should be based on InParams?
							Particles.V(Index) = Chaos::TVector<float, 3>(0); // TODO: Do we want to support initial velocity?
							Particles.W(Index) = Chaos::TVector<float, 3>(0); // TODO: Do we want to support initial velocity?

							// Set the initial object state
							// TODO: Right now we only support static or dynamic.
							//       Params will probably also need to be able to support Kinematic and initial sleep states.
							const Chaos::EObjectStateType InitialState = InParams.bStatic ? Chaos::EObjectStateType::Static : Chaos::EObjectStateType::Dynamic;
							Particles.SetObjectState(Index, InitialState);

							// Initially disable simulation if this is a query only object
							Particles.Disabled(Index) = InParams.bQueryOnly;

							// TODO: At the moment, we don't actually have support for objects with no gravity...
							check(InParams.bEnableGravity);


							Solver->InitializeFromParticleData(Index);
						}
					}
				});
			}
		}
	}
}

void FPhysInterface_Chaos::ReleaseActor(FPhysicsActorHandle& Handle, FPhysScene* InScene, bool bNeverDerferRelease)
{
	if (FPhysScene_ChaosInterface* Scene = Handle.GetScene())
	{
		check(Scene);
		check(InScene == Scene);

		if (FBodyInstancePhysicsObject* Object = Handle.GetPhysicsObject())
		{
			// Invalidate the reference
			Handle.SetPhysicsObject(nullptr);

			// Queue a task to remove the object from the scene
			if (FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers"))
			{
				if (Chaos::IDispatcher* Dispatcher = ChaosModule->GetDispatcher())
				{
					if (Chaos::FPBDRigidsSolver* Solver = Scene->GetSolver())
					{
						Dispatcher->EnqueueCommand([Solver, Object](Chaos::FPersistentPhysicsTask* PhysThread)
						{
							// Remove the object from the solver, & delete the object
							Solver->UnregisterObject(Object);
							Object->OnRemoveFromScene();
							Object->SyncBeforeDestroy();
							delete Object;
						});
					}
				}
			}
		}
	}
}

// Aggregate is not relevant for Chaos yet
FPhysicsAggregateReference_Chaos FPhysInterface_Chaos::CreateAggregate(int32 MaxBodies)
{
	// #todo : Implement
    FPhysicsAggregateReference_Chaos NewAggregate;
    return NewAggregate;
}

void FPhysInterface_Chaos::ReleaseAggregate(FPhysicsAggregateReference_Chaos& InAggregate) {}
int32 FPhysInterface_Chaos::GetNumActorsInAggregate(const FPhysicsAggregateReference_Chaos& InAggregate) { return 0; }
void FPhysInterface_Chaos::AddActorToAggregate_AssumesLocked(const FPhysicsAggregateReference_Chaos& InAggregate, const FPhysicsActorReference_Chaos& InActor) {}


int32 FPhysInterface_Chaos::GetNumShapes(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 1;
}

void FPhysInterface_Chaos::ReleaseShape(const FPhysicsShapeHandle& InShape)
{
    check(!InShape.ActorRef.IsValid());
	delete InShape.Object;
}

void FPhysInterface_Chaos::AttachShape(const FPhysicsActorHandle& InActor, const FPhysicsShapeHandle& InNewShape)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::DetachShape(const FPhysicsActorHandle& InActor, FPhysicsShapeHandle& InShape, bool bWakeTouching)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetActorUserData_AssumesLocked(FPhysicsActorReference_Chaos& InActorReference, FPhysxUserData* InUserData)
{
    FBodyInstance* BodyInstance = InUserData->Get<FBodyInstance>(InUserData);

    if (BodyInstance)
    {
        InActorReference.SetBodyInstance(BodyInstance);
    }
}

bool FPhysInterface_Chaos::IsRigidBody(const FPhysicsActorReference_Chaos& InActorReference)
{
    return InActorReference.IsValid();
}

bool FPhysInterface_Chaos::IsStatic(const FPhysicsActorReference_Chaos& InActorReference)
{
	if (InActorReference.GetPhysicsObject())
	{
		return InActorReference.GetPhysicsObject()->GetInitialState().GetInverseMass()==0;
	}
	check(false);
	return 0.f;
}

bool FPhysInterface_Chaos::IsKinematic_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
    return IsStatic(InActorReference);
}

bool FPhysInterface_Chaos::IsSleeping(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return false;
}

bool FPhysInterface_Chaos::IsCcdEnabled(const FPhysicsActorReference_Chaos& InActorReference)
{
    return false;
}

bool FPhysInterface_Chaos::IsInScene(const FPhysicsActorReference_Chaos& InActorReference)
{
    return InActorReference.GetScene() != nullptr;
}

bool FPhysInterface_Chaos::CanSimulate_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return true;
}

float FPhysInterface_Chaos::GetMass_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return 1.f;
}

void FPhysInterface_Chaos::SetSendsSleepNotifies_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bSendSleepNotifies)
{
    check(bSendSleepNotifies == false);
}

void FPhysInterface_Chaos::PutToSleep_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::WakeUp_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetIsKinematic_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bIsKinematic)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetCcdEnabled_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bIsCcdEnabled)
{
    check(bIsCcdEnabled == false);
}

FTransform FPhysInterface_Chaos::GetGlobalPose_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return Chaos::TRigidTransform<float, 3>();
}

void FPhysInterface_Chaos::SetGlobalPose_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FTransform& InNewPose, bool bAutoWake)
{
	// #todo : Implement
}

FTransform FPhysInterface_Chaos::GetTransform_AssumesLocked(const FPhysicsActorHandle& InRef, bool bForceGlobalPose /*= false*/)
{
	if(!bForceGlobalPose)
	{
		if(IsDynamic(InRef))
		{
			if(HasKinematicTarget_AssumesLocked(InRef))
			{
				return GetKinematicTarget_AssumesLocked(InRef);
			}
		}
	}

	return GetGlobalPose_AssumesLocked(InRef);
}

bool FPhysInterface_Chaos::HasKinematicTarget_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
    return IsStatic(InActorReference);
}

FTransform FPhysInterface_Chaos::GetKinematicTarget_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return FTransform();
}

void FPhysInterface_Chaos::SetKinematicTarget_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FTransform& InNewTarget)
{
	// #todo : Implement
    //InActorReference.GetScene()->SetKinematicTransform(InActorReference, InNewTarget);
}

FVector FPhysInterface_Chaos::GetLinearVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return FVector(0);
}

void FPhysInterface_Chaos::SetLinearVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InNewVelocity, bool bAutoWake)
{
	// #todo : Implement
}

FVector FPhysInterface_Chaos::GetAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return FVector(0);
}

void FPhysInterface_Chaos::SetAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InNewVelocity, bool bAutoWake)
{
	// #todo : Implement
}

float FPhysInterface_Chaos::GetMaxAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
    return FLT_MAX;
}

void FPhysInterface_Chaos::SetMaxAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InMaxAngularVelocity)
{
}

float FPhysInterface_Chaos::GetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
    return FLT_MAX;
}

void FPhysInterface_Chaos::SetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InMaxDepenetrationVelocity)
{
}

FVector FPhysInterface_Chaos::GetWorldVelocityAtPoint_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InPoint)
{
	// #todo : Implement
	return FVector(0);
}

FTransform FPhysInterface_Chaos::GetComTransform_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return FTransform();
}

FTransform FPhysInterface_Chaos::GetComTransformLocal_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return FTransform();
}

FVector FPhysInterface_Chaos::GetLocalInertiaTensor_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	return FVector(1);
}

FBox FPhysInterface_Chaos::GetBounds_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
	// #todo : Implement
	//const auto& Box = InActorReference.GetScene()->Scene.GetSolver()->GetRigidParticles().Geometry(InActorReference.GetScene()->GetIndexFromId(InActorReference.GetId()))->BoundingBox();
    return FBox(FVector(-0.5), FVector(0.5));
}

void FPhysInterface_Chaos::SetLinearDamping_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InDamping)
{

}

void FPhysInterface_Chaos::SetAngularDamping_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InDamping)
{

}

void FPhysInterface_Chaos::AddImpulse_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InForce)
{
	// #todo : Implement
    //InActorReference.GetScene()->AddForce(InForce, InActorReference.GetId());
}

void FPhysInterface_Chaos::AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InTorque)
{
	// #todo : Implement
    //InActorReference.GetScene()->AddTorque(InTorque, InActorReference.GetId());
}

void FPhysInterface_Chaos::AddVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InForce)
{	
	// #todo : Implement
    //InActorReference.GetScene()->AddForce(InForce * InActorReference.GetScene()->Scene.GetSolver()->GetRigidParticles().M(InActorReference.GetScene()->GetIndexFromId(InActorReference.GetId())), InActorReference.GetId());
}

void FPhysInterface_Chaos::AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InTorque)
{
	// #todo : Implement
    //InActorReference.GetScene()->AddTorque(InActorReference.GetScene()->Scene.GetSolver()->GetRigidParticles().I(InActorReference.GetScene()->GetIndexFromId(InActorReference.GetId())) * Chaos::TVector<float, 3>(InTorque), InActorReference.GetId());
}

void FPhysInterface_Chaos::AddImpulseAtLocation_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InImpulse, const FVector& InLocation)
{
    // @todo(mlentine): We don't currently have a way to apply an instantaneous force. Do we need this?
}

void FPhysInterface_Chaos::AddRadialImpulse_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InOrigin, float InRadius, float InStrength, ERadialImpulseFalloff InFalloff, bool bInVelChange)
{
    // @todo(mlentine): We don't currently have a way to apply an instantaneous force. Do we need this?
}

bool FPhysInterface_Chaos::IsGravityEnabled_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
    // @todo(mlentine): Gravity is system wide currently. This should change.
    return true;
}
void FPhysInterface_Chaos::SetGravityEnabled_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bEnabled)
{
    // @todo(mlentine): Gravity is system wide currently. This should change.
}

float FPhysInterface_Chaos::GetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference)
{
    return 0;
}
void FPhysInterface_Chaos::SetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InEnergyThreshold)
{
}

void FPhysInterface_Chaos::SetMass_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, float InMass)
{
	// #todo : Implement
	//LocalParticles.M(Index) = InMass;
}

void FPhysInterface_Chaos::SetMassSpaceInertiaTensor_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, const FVector& InTensor)
{
	// #todo : Implement
	//LocalParticles.I(Index).M[0][0] = InTensor[0];
    //LocalParticles.I(Index).M[1][1] = InTensor[1];
    //LocalParticles.I(Index).M[2][2] = InTensor[2];
}

void FPhysInterface_Chaos::SetComLocalPose_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, const FTransform& InComLocalPose)
{
    //@todo(mlentine): What is InComLocalPose? If the center of an object is not the local pose then many things break including the three vector represtnation of inertia.
}

float FPhysInterface_Chaos::GetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle)
{
	// #todo : Implement
	return 0.0f;
}

void FPhysInterface_Chaos::SetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, float InThreshold)
{
	// #todo : Implement
}

uint32 FPhysInterface_Chaos::GetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle)
{
	// #todo : Implement
	return 0;
}

void FPhysInterface_Chaos::SetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, uint32 InSolverIterationCount)
{
	// #todo : Implement
}

uint32 FPhysInterface_Chaos::GetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle)
{
	// #todo : Implement
	return 0;
}

void FPhysInterface_Chaos::SetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, uint32 InSolverIterationCount)
{
	// #todo : Implement
}

float FPhysInterface_Chaos::GetWakeCounter_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle)
{
	// #todo : Implement
	return 0.0f;
}

void FPhysInterface_Chaos::SetWakeCounter_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, float InWakeCounter)
{
	// #todo : Implement
}

SIZE_T FPhysInterface_Chaos::GetResourceSizeEx(const FPhysicsActorReference_Chaos& InActorRef)
{
    return sizeof(FPhysicsActorReference_Chaos);
}
	
// Constraints
FPhysicsConstraintReference_Chaos FPhysInterface_Chaos::CreateConstraint(const FPhysicsActorReference_Chaos& InActorRef1, const FPhysicsActorReference_Chaos& InActorRef2, const FTransform& InLocalFrame1, const FTransform& InLocalFrame2)
{
	// #todo : Implement
	FPhysicsConstraintReference_Chaos ConstraintRef;
	return ConstraintRef;
}

void FPhysInterface_Chaos::SetConstraintUserData(const FPhysicsConstraintReference_Chaos& InConstraintRef, void* InUserData)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::ReleaseConstraint(FPhysicsConstraintReference_Chaos& InConstraintRef)
{
	// #todo : Implement
}

FTransform FPhysInterface_Chaos::GetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame)
{
	// #todo : Implement
	//
	//int32 Index1 = InConstraintRef.GetScene()->MSpringConstraints->Constraints()[InConstraintRef.GetScene()->GetConstraintIndexFromId(InConstraintRef.GetId())][0];
	//int32 Index2 = InConstraintRef.GetScene()->MSpringConstraints->Constraints()[InConstraintRef.GetScene()->GetConstraintIndexFromId(InConstraintRef.GetId())][1];
	//Chaos::TRigidTransform<float, 3> Transform1(InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().X(Index1), InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().R(Index1));
	//Chaos::TRigidTransform<float, 3> Transform2(InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().X(Index2), InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().R(Index2));
	// @todo(mlentine): This is likely broken
	//FTransform(Transform1.Inverse() * Transform2);

	return  FTransform();
}

FTransform FPhysInterface_Chaos::GetGlobalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame)
{
	// #todo : Implement
	return  FTransform();
}

FVector FPhysInterface_Chaos::GetLocation(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
	// #todo : Implement
	return  FVector(0.f);
}

void FPhysInterface_Chaos::GetForce(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinForce, FVector& OutAngForce)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::GetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinVelocity)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::GetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutAngVelocity)
{
	// #todo : Implement
}

float FPhysInterface_Chaos::GetCurrentSwing1(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
    return GetLocalPose(InConstraintRef, EConstraintFrame::Frame2).GetRotation().Euler().X;
}

float FPhysInterface_Chaos::GetCurrentSwing2(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
    return GetLocalPose(InConstraintRef, EConstraintFrame::Frame2).GetRotation().Euler().Y;
}

float FPhysInterface_Chaos::GetCurrentTwist(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
    return GetLocalPose(InConstraintRef, EConstraintFrame::Frame2).GetRotation().Euler().Z;
}

void FPhysInterface_Chaos::SetCanVisualize(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCanVisualize)
{

}

void FPhysInterface_Chaos::SetCollisionEnabled(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCollisionEnabled)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetProjectionEnabled_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInProjectionEnabled, float InLinearTolerance, float InAngularToleranceDegrees)
{

}

void FPhysInterface_Chaos::SetParentDominates_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInParentDominates)
{

}

void FPhysInterface_Chaos::SetBreakForces_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLinearBreakForce, float InAngularBreakForce)
{

}

void FPhysInterface_Chaos::SetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FTransform& InPose, EConstraintFrame::Type InFrame)
{

}

void FPhysInterface_Chaos::SetLinearMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, ELinearConstraintMotion InMotion)
{

}

void FPhysInterface_Chaos::SetAngularMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, EAngularConstraintMotion InMotion)
{

}

void FPhysInterface_Chaos::UpdateLinearLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit, float InAverageMass, const FLinearConstraint& InParams)
{

}

void FPhysInterface_Chaos::UpdateConeLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FConeConstraint& InParams)
{

}

void FPhysInterface_Chaos::UpdateTwistLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FTwistConstraint& InParams)
{

}

void FPhysInterface_Chaos::UpdateLinearDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InDriveParams)
{

}

void FPhysInterface_Chaos::UpdateAngularDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FAngularDriveConstraint& InDriveParams)
{

}

void FPhysInterface_Chaos::UpdateDriveTarget_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InLinDrive, const FAngularDriveConstraint& InAngDrive)
{

}

void FPhysInterface_Chaos::SetDrivePosition(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InPosition)
{

}

void FPhysInterface_Chaos::SetDriveOrientation(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FQuat& InOrientation)
{

}

void FPhysInterface_Chaos::SetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InLinVelocity)
{

}

void FPhysInterface_Chaos::SetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InAngVelocity)
{

}

void FPhysInterface_Chaos::SetTwistLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLowerLimit, float InUpperLimit, float InContactDistance)
{

}

void FPhysInterface_Chaos::SetSwingLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InYLimit, float InZLimit, float InContactDistance)
{

}

void FPhysInterface_Chaos::SetLinearLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit)
{

}

bool FPhysInterface_Chaos::IsBroken(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
	// #todo : Implement
	return true;
}

bool FPhysInterface_Chaos::ExecuteOnUnbrokenConstraintReadOnly(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func)
{
    if (!IsBroken(InConstraintRef))
    {
        Func(InConstraintRef);
        return true;
    }
    return false;
}

bool FPhysInterface_Chaos::ExecuteOnUnbrokenConstraintReadWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func)
{
    if (!IsBroken(InConstraintRef))
    {
        Func(InConstraintRef);
        return true;
    }
    return false;
}

template<class PHYSX_MESH>
TArray<Chaos::TVector<int32, 3>> GetMeshElements(const PHYSX_MESH* PhysXMesh)
{
	check(false);
}

#if WITH_PHYSX

template<>
TArray<Chaos::TVector<int32, 3>> GetMeshElements(const physx::PxConvexMesh* PhysXMesh)
{
	TArray<Chaos::TVector<int32, 3>> CollisionMeshElements;
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
	int32 offset = 0;
	int32 NbPolygons = static_cast<int32>(PhysXMesh->getNbPolygons());
	for (int32 i = 0; i < NbPolygons; i++)
	{
		PxHullPolygon Poly;
		bool status = PhysXMesh->getPolygonData(i, Poly);
		const auto Indices = PhysXMesh->getIndexBuffer() + Poly.mIndexBase;

		for (int32 j = 2; j < static_cast<int32>(Poly.mNbVerts); j++)
		{
			CollisionMeshElements.Add(Chaos::TVector<int32, 3>(Indices[offset], Indices[offset + j], Indices[offset + j - 1]));
		}
	}
#endif
	return CollisionMeshElements;
}

template<>
TArray<Chaos::TVector<int32, 3>> GetMeshElements(const physx::PxTriangleMesh* PhysXMesh)
{
	TArray<Chaos::TVector<int32, 3>> CollisionMeshElements;
	const auto MeshFlags = PhysXMesh->getTriangleMeshFlags();
	for (int32 j = 0; j < static_cast<int32>(PhysXMesh->getNbTriangles()); ++j)
	{
		if (MeshFlags | PxTriangleMeshFlag::e16_BIT_INDICES)
		{
			const PxU16* Indices = reinterpret_cast<const PxU16*>(PhysXMesh->getTriangles());
			CollisionMeshElements.Add(Chaos::TVector<int32, 3>(Indices[3 * j], Indices[3 * j + 1], Indices[3 * j + 2]));
		}
		else
		{
			const PxU32* Indices = reinterpret_cast<const PxU32*>(PhysXMesh->getTriangles());
			CollisionMeshElements.Add(Chaos::TVector<int32, 3>(Indices[3 * j], Indices[3 * j + 1], Indices[3 * j + 2]));
		}
	}
	return CollisionMeshElements;
}

template<class PHYSX_MESH>
TUniquePtr<Chaos::TImplicitObject<float, 3>> ConvertPhysXMeshToLevelset(const PHYSX_MESH* PhysXMesh, const FVector& Scale)
{
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
    TArray<Chaos::TVector<int32, 3>> CollisionMeshElements = GetMeshElements(PhysXMesh);
	Chaos::TParticles<float, 3> CollisionMeshParticles;
	CollisionMeshParticles.AddParticles(PhysXMesh->getNbVertices());
	for (uint32 j = 0; j < CollisionMeshParticles.Size(); ++j)
	{
		const auto& Vertex = PhysXMesh->getVertices()[j];
		CollisionMeshParticles.X(j) = Scale * Chaos::TVector<float, 3>(Vertex.x, Vertex.y, Vertex.z);
	}
	Chaos::TBox<float, 3> BoundingBox(CollisionMeshParticles.X(0), CollisionMeshParticles.X(0));
	for (uint32 j = 1; j < CollisionMeshParticles.Size(); ++j)
	{
		BoundingBox.GrowToInclude(CollisionMeshParticles.X(j));
	}
#if FORCE_ANALYTICS
	return TUniquePtr<Chaos::TImplicitObject<float, 3>>(new Chaos::TBox<float, 3>(BoundingBox));
#else
	int32 MaxAxisSize = 10;
	int32 MaxAxis;
	const auto Extents = BoundingBox.Extents();
	if (Extents[0] > Extents[1] && Extents[0] > Extents[2])
	{
		MaxAxis = 0;
	}
	else if (Extents[1] > Extents[2])
	{
		MaxAxis = 1;
	}
	else
	{
		MaxAxis = 2;
	}
    Chaos::TVector<int32, 3> Counts(MaxAxisSize * Extents[0] / Extents[MaxAxis], MaxAxisSize * Extents[1] / Extents[MaxAxis], MaxAxisSize * Extents[2] / Extents[MaxAxis]);
    Counts[0] = Counts[0] < 1 ? 1 : Counts[0];
    Counts[1] = Counts[1] < 1 ? 1 : Counts[1];
    Counts[2] = Counts[2] < 1 ? 1 : Counts[2];
    Chaos::TUniformGrid<float, 3> Grid(BoundingBox.Min(), BoundingBox.Max(), Counts, 1);
	Chaos::TTriangleMesh<float> CollisionMesh(MoveTemp(CollisionMeshElements));
	return TUniquePtr<Chaos::TImplicitObject<float, 3>>(new Chaos::TLevelSet<float, 3>(Grid, CollisionMeshParticles, CollisionMesh));
#endif

#else
	return TUniquePtr<Chaos::TImplicitObject<float, 3>>();
#endif // !WITH_CHAOS_NEEDS_TO_BE_FIXED

}

#endif

FPhysicsShapeHandle FPhysInterface_Chaos::CreateShape(physx::PxGeometry* InGeom, bool bSimulation, bool bQuery, UPhysicalMaterial* InSimpleMaterial, TArray<UPhysicalMaterial*>* InComplexMaterials)
{
	// #todo : Implement
	// @todo(mlentine): Should we be doing anything with the InGeom here?
    FPhysicsActorHandle NewActor;
	return {nullptr,bSimulation ,bQuery,NewActor };
}
	
void FPhysInterface_Chaos::AddGeometry(FPhysicsActorHandle& InActor, const FGeometryAddParams& InParams, TArray<FPhysicsShapeHandle>* OutOptShapes)
{
	const FVector& Scale = InParams.Scale;
	FBodyInstancePhysicsObject * PhysicsObject = InActor.GetPhysicsObject();
	if (InParams.Geometry!=nullptr && ( PhysicsObject!=nullptr || OutOptShapes!=nullptr ) ) 
	{
		for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->SphereElems.Num()); ++i)
		{
			check(Scale[0] == Scale[1] && Scale[1] == Scale[2]);
			const auto& CollisionSphere = InParams.Geometry->SphereElems[i];
			Chaos::TImplicitObject<float, 3>* ImplicitSphere = new Chaos::TSphere<float, 3>(Chaos::TVector<float, 3>(0.f, 0.f, 0.f), CollisionSphere.Radius * Scale[0]);
			if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphere);
			else if (OutOptShapes) OutOptShapes->Add({ImplicitSphere,true,true,InActor});
		}
		for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->BoxElems.Num()); ++i)
		{
			const auto& Box = InParams.Geometry->BoxElems[i];
			Chaos::TVector<float, 3> half_extents = Scale * Chaos::TVector<float, 3>(Box.X / 2.f, Box.Y / 2.f, Box.Z / 2.f);
			Chaos::TImplicitObject<float, 3>* ImplicitBox = new Chaos::TBox<float, 3>(-half_extents, half_extents);
			if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitBox);
			else if (OutOptShapes) OutOptShapes->Add({ImplicitBox,true,true,InActor});
		}
		for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->SphylElems.Num()); ++i)
		{
			check(Scale[0] == Scale[1] && Scale[1] == Scale[2]);
			const auto& TCapsule = InParams.Geometry->SphylElems[i];
			if (TCapsule.Length == 0)
			{
				Chaos::TSphere<float, 3> * ImplicitSphere = new Chaos::TSphere<float, 3>(Chaos::TVector<float, 3>(0), TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphere);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphere,true,true,InActor});
			}
			else
			{
				Chaos::TVector<float, 3> half_extents(0, 0, TCapsule.Length / 2 * Scale[0]);

				Chaos::TCylinder<float> * ImplicitCylinder = new Chaos::TCylinder<float>(-half_extents, half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitCylinder);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitCylinder,true,true,InActor});

				Chaos::TSphere<float, 3> * ImplicitSphereA = new Chaos::TSphere<float, 3>(-half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphereA);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphereA,true,true,InActor});

				Chaos::TSphere<float, 3> * ImplicitSphereB = new Chaos::TSphere<float, 3>(half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphereB);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphereB,true,true,InActor});
			}
		}
#if 0
		for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->TaperedCapsuleElems.Num()); ++i)
		{
			check(Scale[0] == Scale[1] && Scale[1] == Scale[2]);
			const auto& TCapsule = InParams.Geometry->TaperedCapsuleElems[i];
			if (TCapsule.Length == 0)
			{
				Chaos::TSphere<float, 3> * ImplicitSphere = new Chaos::TSphere<float, 3>(-half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphere);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphere,true,true,InActor});
			}
			else
			{
				Chaos::TVector<float, 3> half_extents(0, 0, TCapsule.Length / 2 * Scale[0]);
				Chaos::TCylinder<float> * ImplicitCylinder = new Chaos::TCylinder<float>(-half_extents, half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphere);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphere,true,true,InActor});

				Chaos::TSphere<float, 3> * ImplicitSphereA = new Chaos::TSphere<float, 3>(-half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphereA);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphereA,true,true,InActor});

				Chaos::TSphere<float, 3> * ImplicitSphereB = new Chaos::TSphere<float, 3>(half_extents, TCapsule.Radius * Scale[0]);
				if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(ImplicitSphereB);
				else if (OutOptShapes) OutOptShapes->Add({ImplicitSphereB,true,true,InActor});
			}
		}
#endif

#if WITH_PHYSX
		for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->ConvexElems.Num()); ++i)
		{
			const auto& CollisionBody = InParams.Geometry->ConvexElems[i];
			Chaos::TImplicitObject<float, 3>* Implicit = ConvertPhysXMeshToLevelset(CollisionBody.GetConvexMesh(), Scale).Release();
			if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(Implicit);
			else if (OutOptShapes) OutOptShapes->Add({Implicit,true,true,InActor});
		}
#endif
	}
	else
	{
#if WITH_PHYSX
		for (const auto& PhysXMesh : InParams.TriMeshes)
		{
			Chaos::TImplicitObject<float, 3>* Implicit = ConvertPhysXMeshToLevelset(PhysXMesh, Scale).Release();
			if (PhysicsObject) PhysicsObject->ImplicitObjects_GameThread.Add(Implicit);
			else if (OutOptShapes) OutOptShapes->Add({Implicit,true,true,InActor});
		}
#endif
	}
}


// todo(brice): Implicit Initialization Pipeline(WIP)
// ... add virtual TImplicitObject::NewCopy()
// @todo(mlentine,brice): We probably need to actually duplicate the data here, add virtual TImplicitObject::NewCopy()
FPhysicsShapeHandle FPhysInterface_Chaos::CloneShape(const FPhysicsShapeHandle& InShape)
{
	FPhysicsActorHandle NewActor; // why zero and not the default INDEX_NONE?
	return { InShape.Object,InShape.bSimulation, InShape.bQuery, NewActor };
}

FCollisionFilterData FPhysInterface_Chaos::GetSimulationFilter(const FPhysicsShapeHandle& InShape)
{
	return FCollisionFilterData();
}

FCollisionFilterData FPhysInterface_Chaos::GetQueryFilter(const FPhysicsShapeHandle& InShape)
{
	return FCollisionFilterData();
}

bool FPhysInterface_Chaos::IsSimulationShape(const FPhysicsShapeHandle& InShape)
{
    return InShape.bSimulation;
}

bool FPhysInterface_Chaos::IsQueryShape(const FPhysicsShapeHandle& InShape)
{
    return InShape.bQuery;
}

bool FPhysInterface_Chaos::IsShapeType(const FPhysicsShapeHandle& InShape, ECollisionShapeType InType)
{
    if (InType == ECollisionShapeType::Box && InShape.Object->GetType() == Chaos::ImplicitObjectType::Box)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Sphere && InShape.Object->GetType() == Chaos::ImplicitObjectType::Sphere)
    {
        return true;
    }
    // Other than sphere and box the basic types do not correlate so we return false
    return false;
}

ECollisionShapeType FPhysInterface_Chaos::GetShapeType(const FPhysicsShapeHandle& InShape)
{
    if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Box)
    {
        return ECollisionShapeType::Box;
    }
    if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Sphere)
    {
        return ECollisionShapeType::Sphere;
    }
    return ECollisionShapeType::None;
}

FTransform FPhysInterface_Chaos::GetLocalTransform(const FPhysicsShapeHandle& InShape)
{
    // Transforms are baked into the object so there is never a local transform
    if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed && InShape.ActorRef.IsValid())
    {
        return InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float, 3>>()->GetTransform();
    }
    else
    {
        return FTransform();
    }
}

void FPhysInterface_Chaos::SetLocalTransform(const FPhysicsShapeHandle& InShape, const FTransform& NewLocalTransform)
{
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
    if (InShape.ActorRef.IsValid())
    {
        TArray<RigidBodyId> Ids = {InShape.ActorRef.GetId()};
        const auto Index = InShape.ActorRef.GetScene()->GetIndexFromId(InShape.ActorRef.GetId());
        if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed)
        {
            // @todo(mlentine): We can avoid creating a new object here by adding delayed update support for the object transforms
            LocalParticles.SetDynamicGeometry(Index, MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float, 3>>()->Object(), NewLocalTransform));
        }
        else
        {
            LocalParticles.SetDynamicGeometry(Index, MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(InShape.Object, NewLocalTransform));
        }
    }
    {
        if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed)
        {
            InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float, 3>>()->SetTransform(NewLocalTransform);
        }
        else
        {
            const_cast<FPhysicsShapeHandle&>(InShape).Object = new Chaos::TImplicitObjectTransformed<float, 3>(InShape.Object, NewLocalTransform);
        }
    }
#endif
}

void FinishSceneStat()
{
}

#if WITH_PHYSX

void FPhysInterface_Chaos::CalculateMassPropertiesFromShapeCollection(physx::PxMassProperties& OutProperties, const TArray<FPhysicsShapeHandle>& InShapes, float InDensityKGPerCM)
{
	// #todo : Implement
    //OutProperties.inertiaTensor = physx::PxMat33();
    //OutProperties.inertiaTensor(0, 0) = Inertia.M[0][0];
    //OutProperties.inertiaTensor(1, 1) = Inertia.M[1][1];
    //OutProperties.inertiaTensor(2, 2) = Inertia.M[2][2];
    //OutProperties.mass = LocalParticles.M(Index);
}

#endif

bool FPhysInterface_Chaos::LineTrace_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& InStart, const FVector& InEnd, bool bTraceComplex, bool bExtractPhysMaterial)
{
	OutHit.TraceStart = InStart;
	OutHit.TraceEnd = InEnd;
    
    TArray<FPhysicsShapeReference_Chaos> OutShapes;
    InInstance->GetAllShapes_AssumesLocked(OutShapes);
    check(OutShapes.Num() == 1);
    const auto& Result = OutShapes[0].Object->FindClosestIntersection(InStart, InEnd, 0);
    if (Result.Second)
    {
        // @todo(mlentine): What should we fill here?
        OutHit.ImpactPoint = Result.First;
        OutHit.ImpactNormal = OutShapes[0].Object->Normal(OutHit.ImpactPoint);
    }
    return Result.Second;
}

bool FPhysInterface_Chaos::Sweep_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& InStart, const FVector& InEnd, const FQuat& InShapeRotation, const FCollisionShape& InShape, bool bSweepComplex)
{
    // @todo(mlentine): Need to implement this
    return false;
}

bool FPhysInterface_Chaos::Overlap_Geom(const FBodyInstance* InBodyInstance, const FPhysicsGeometryCollection& InGeometry, const FTransform& InShapeTransform, FMTDResult* OutOptResult)
{
    // @todo(mlentine): Need to implement this
    return false;
}

bool FPhysInterface_Chaos::Overlap_Geom(const FBodyInstance* InBodyInstance, const FCollisionShape& InCollisionShape, const FQuat& InShapeRotation, const FTransform& InShapeTransform, FMTDResult* OutOptResult)
{
    // @todo(mlentine): Need to implement this
    return false;
}

bool FPhysInterface_Chaos::GetSquaredDistanceToBody(const FBodyInstance* InInstance, const FVector& InPoint, float& OutDistanceSquared, FVector* OutOptPointOnBody)
{
    // @todo(mlentine): What spaces are in and out point?
    TArray<FPhysicsShapeReference_Chaos> OutShapes;
    InInstance->GetAllShapes_AssumesLocked(OutShapes);
    check(OutShapes.Num() == 1);
    Chaos::TVector<float, 3> Normal;
    const auto Phi = OutShapes[0].Object->PhiWithNormal(InPoint, Normal);
    OutDistanceSquared = Phi * Phi;
    if (OutOptPointOnBody)
    {
        *OutOptPointOnBody = InPoint - Phi * Normal;
    }
    return true;
}

template<typename AllocatorType>
int32 GetAllShapesInternal_AssumedLocked(const FPhysicsActorReference_Chaos& InActorHandle, TArray<FPhysicsShapeReference_Chaos, AllocatorType>& OutShapes)
{
	OutShapes.Reset();
	//FPhysicsShapeHandle NewShape;
	//NewShape.bSimulation = true;
	//NewShape.bQuery = true;
	//NewShape.Object = LocalParticles.Geometry(Index);
	//OutShapes.Add(NewShape);
	return OutShapes.Num();
}

template <>
int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorReference_Chaos& InActorHandle, TArray<FPhysicsShapeReference_Chaos, FDefaultAllocator>& OutShapes)
{
	return GetAllShapesInternal_AssumedLocked(InActorHandle, OutShapes);
}

template <>
int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorReference_Chaos& InActorHandle, PhysicsInterfaceTypes::FInlineShapeArray& OutShapes)
{
	return GetAllShapesInternal_AssumedLocked(InActorHandle, OutShapes);
}

#endif
