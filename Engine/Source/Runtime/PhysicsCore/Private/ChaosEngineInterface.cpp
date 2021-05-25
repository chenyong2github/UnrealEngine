// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosEngineInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsSettingsCore.h"
#include "PhysicsPublicCore.h"
#include "BodyInstanceCore.h"
#include "Chaos/ChaosScene.h"
#include "Chaos/KinematicTargets.h"
#include "PhysicsInterfaceDeclaresCore.h"

FPhysicsDelegatesCore::FOnUpdatePhysXMaterial FPhysicsDelegatesCore::OnUpdatePhysXMaterial;

#if WITH_CHAOS
#include "ChaosInterfaceWrapperCore.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "CollisionShape.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDJointConstraintData.h"
#include "Chaos/PBDSuspensionConstraintData.h"
#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

bool bEnableChaosJointConstraints = true;
FAutoConsoleVariableRef CVarEnableChaosJointConstraints(TEXT("p.ChaosSolverEnableJointConstraints"), bEnableChaosJointConstraints, TEXT("Enable Joint Constraints defined within the Physics Asset Editor"));

bool bEnableChaosCollisionManager = true;
FAutoConsoleVariableRef CVarEnableChaosCollisionManager(TEXT("p.Chaos.Collision.EnableCollisionManager"), bEnableChaosCollisionManager, TEXT("Enable Chaos's Collision Manager for ignoring collisions between rigid bodies. [def:1]"));

bool FPhysicsConstraintReference_Chaos::IsValid() const
{
	return Constraint!=nullptr ? Constraint->IsValid() : false;
}
const Chaos::FImplicitObject& FPhysicsShapeReference_Chaos::GetGeometry() const
{
	check(IsValid()); return *Shape->GetGeometry();
}

FPhysicsGeometryCollection_Chaos::~FPhysicsGeometryCollection_Chaos() = default;
FPhysicsGeometryCollection_Chaos::FPhysicsGeometryCollection_Chaos(FPhysicsGeometryCollection_Chaos&& Steal) = default;

ECollisionShapeType FPhysicsGeometryCollection_Chaos::GetType() const
{
	return GetImplicitType(Geom);
}

const Chaos::FImplicitObject& FPhysicsGeometryCollection_Chaos::GetGeometry() const
{
	return Geom;
}

const Chaos::TBox<Chaos::FReal,3>& FPhysicsGeometryCollection_Chaos::GetBoxGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TBox<Chaos::FReal,3>>();
}

const Chaos::TSphere<Chaos::FReal,3>&  FPhysicsGeometryCollection_Chaos::GetSphereGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TSphere<Chaos::FReal,3>>();
}
const Chaos::FCapsule&  FPhysicsGeometryCollection_Chaos::GetCapsuleGeometry() const
{
	return Geom.GetObjectChecked<Chaos::FCapsule>();
}

const Chaos::FConvex& FPhysicsGeometryCollection_Chaos::GetConvexGeometry() const
{
	return Geom.GetObjectChecked<Chaos::FConvex>();
}

const Chaos::FTriangleMeshImplicitObject& FPhysicsGeometryCollection_Chaos::GetTriMeshGeometry() const
{
	return Geom.GetObjectChecked<Chaos::FTriangleMeshImplicitObject>();
}

FPhysicsGeometryCollection_Chaos::FPhysicsGeometryCollection_Chaos(const FPhysicsShapeReference_Chaos& InShape)
	: Geom(InShape.GetGeometry())
{
}

FPhysicsShapeAdapter_Chaos::FPhysicsShapeAdapter_Chaos(const FQuat& Rot,const FCollisionShape& CollisionShape)
	: GeometryRotation(Rot)
{
	switch(CollisionShape.ShapeType)
	{
	case ECollisionShape::Capsule:
	{
		const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
		const float CapsuleHalfHeight = CollisionShape.GetCapsuleHalfHeight();
		if(CapsuleRadius < CapsuleHalfHeight)
		{
			const float UseHalfHeight = FMath::Max(CollisionShape.GetCapsuleAxisHalfLength(),FCollisionShape::MinCapsuleAxisHalfHeight());
			const FVector Bot = FVector(0.f,0.f,-UseHalfHeight);
			const FVector Top = FVector(0.f,0.f,UseHalfHeight);
			const float UseRadius = FMath::Max(CapsuleRadius,FCollisionShape::MinCapsuleRadius());
			Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::FCapsule(Bot,Top,UseRadius));
		} else
		{
			// Use a sphere instead.
			const float UseRadius = FMath::Max(CapsuleRadius,FCollisionShape::MinSphereRadius());
			Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TSphere<Chaos::FReal,3>(Chaos::FVec3(0),UseRadius));
		}
		break;
	}
	case ECollisionShape::Box:
	{
		Chaos::FVec3 HalfExtents = CollisionShape.GetBox();
		HalfExtents.X = FMath::Max(HalfExtents.X,FCollisionShape::MinBoxExtent());
		HalfExtents.Y = FMath::Max(HalfExtents.Y,FCollisionShape::MinBoxExtent());
		HalfExtents.Z = FMath::Max(HalfExtents.Z,FCollisionShape::MinBoxExtent());

		Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TBox<Chaos::FReal,3>(-HalfExtents,HalfExtents));
		break;
	}
	case ECollisionShape::Sphere:
	{
		const float UseRadius = FMath::Max(CollisionShape.GetSphereRadius(),FCollisionShape::MinSphereRadius());
		Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TSphere<Chaos::FReal,3>(Chaos::FVec3(0),UseRadius));
		break;
	}
	default:
	ensure(false);
	break;
	}
}

FPhysicsShapeAdapter_Chaos::~FPhysicsShapeAdapter_Chaos() = default;

const FPhysicsGeometry& FPhysicsShapeAdapter_Chaos::GetGeometry() const
{
	return *Geometry;
}

FTransform FPhysicsShapeAdapter_Chaos::GetGeomPose(const FVector& Pos) const
{
	return FTransform(GeometryRotation,Pos);
}

const FQuat& FPhysicsShapeAdapter_Chaos::GetGeomOrientation() const
{
	return GeometryRotation;
}

void FChaosEngineInterface::AddActorToSolver(FPhysicsActorHandle& Handle,Chaos::FPhysicsSolver* Solver)
{
	LLM_SCOPE(ELLMTag::Chaos);
	Solver->RegisterObject(Handle);
}


void FChaosEngineInterface::RemoveActorFromSolver(FPhysicsActorHandle& Handle,Chaos::FPhysicsSolver* Solver)
{
	// Should we stop passing solver in? (need to check it's not null regardless in case proxy was never registered)
	if(Solver && Handle && Handle->GetSolverBase() == Solver)
	{
		Solver->UnregisterObject(Handle);
	}
	else
	{
		delete Handle;
	}
}

// Aggregate is not relevant for Chaos yet
FPhysicsAggregateReference_Chaos FChaosEngineInterface::CreateAggregate(int32 MaxBodies)
{
	// #todo : Implement
	FPhysicsAggregateReference_Chaos NewAggregate;
	return NewAggregate;
}

void FChaosEngineInterface::ReleaseAggregate(FPhysicsAggregateReference_Chaos& InAggregate) {}
int32 FChaosEngineInterface::GetNumActorsInAggregate(const FPhysicsAggregateReference_Chaos& InAggregate) { return 0; }
void FChaosEngineInterface::AddActorToAggregate_AssumesLocked(const FPhysicsAggregateReference_Chaos& InAggregate,const FPhysicsActorHandle& InActor) {}

Chaos::FChaosPhysicsMaterial::ECombineMode UToCCombineMode(EFrictionCombineMode::Type Mode)
{
	using namespace Chaos;
	switch(Mode)
	{
	case EFrictionCombineMode::Average: return FChaosPhysicsMaterial::ECombineMode::Avg;
	case EFrictionCombineMode::Min: return FChaosPhysicsMaterial::ECombineMode::Min;
	case EFrictionCombineMode::Multiply: return FChaosPhysicsMaterial::ECombineMode::Multiply;
	case EFrictionCombineMode::Max: return FChaosPhysicsMaterial::ECombineMode::Max;
	default: ensure(false);
	}

	return FChaosPhysicsMaterial::ECombineMode::Avg;
}

FPhysicsMaterialHandle FChaosEngineInterface::CreateMaterial(const UPhysicalMaterial* InMaterial)
{
	Chaos::FMaterialHandle NewHandle = Chaos::FPhysicalMaterialManager::Get().Create();

	return NewHandle;
}

void FChaosEngineInterface::UpdateMaterial(FPhysicsMaterialHandle& InHandle,UPhysicalMaterial* InMaterial)
{
	if(Chaos::FChaosPhysicsMaterial* Material = InHandle.Get())
	{
		Material->Friction = InMaterial->Friction;
		Material->StaticFriction = InMaterial->StaticFriction;
		Material->FrictionCombineMode = UToCCombineMode(InMaterial->FrictionCombineMode);
		Material->Restitution = InMaterial->Restitution;
		Material->RestitutionCombineMode = UToCCombineMode(InMaterial->RestitutionCombineMode);
		Material->SleepingLinearThreshold = InMaterial->SleepLinearVelocityThreshold;
		Material->SleepingAngularThreshold = InMaterial->SleepAngularVelocityThreshold;
		Material->SleepCounterThreshold = InMaterial->SleepCounterThreshold;
	}

	Chaos::FPhysicalMaterialManager::Get().UpdateMaterial(InHandle);
}

void FChaosEngineInterface::ReleaseMaterial(FPhysicsMaterialHandle& InHandle)
{
	Chaos::FPhysicalMaterialManager::Get().Destroy(InHandle);
}

void FChaosEngineInterface::SetUserData(const FPhysicsShapeHandle& InShape,void* InUserData)
{
	if(CHAOS_ENSURE(InShape.Shape))
	{
		InShape.Shape->SetUserData(InUserData);
	}
}


void FChaosEngineInterface::SetUserData(FPhysicsMaterialHandle& InHandle,void* InUserData)
{
	if(Chaos::FChaosPhysicsMaterial* Material = InHandle.Get())
	{
		Material->UserData = InUserData;
	}

	Chaos::FPhysicalMaterialManager::Get().UpdateMaterial(InHandle);
}

void FChaosEngineInterface::ReleaseMaterialMask(FPhysicsMaterialMaskHandle& InHandle)
{
	Chaos::FPhysicalMaterialManager::Get().Destroy(InHandle);
}


void* FChaosEngineInterface::GetUserData(const FPhysicsShapeHandle& InShape)
{
	if(ensure(InShape.Shape))
	{
		return InShape.Shape->GetUserData();
	}
	return nullptr;
}

int32 FChaosEngineInterface::GetNumShapes(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return InHandle->GetGameThreadAPI().ShapesArray().Num();
}

void FChaosEngineInterface::ReleaseShape(const FPhysicsShapeHandle& InShape)
{
	check(!IsValid(InShape.ActorRef));
	//no need to delete because ownership is on actor. Is this an invalid assumption with the current API?
	//delete InShape.Shape;
}

void FChaosEngineInterface::AttachShape(const FPhysicsActorHandle& InActor,const FPhysicsShapeHandle& InNewShape)
{
	// #todo : Implement - this path is never used welding actually goes through FPhysInterface_Chaos::AddGeometry
	CHAOS_ENSURE(false);
}

void FChaosEngineInterface::DetachShape(const FPhysicsActorHandle& InActor,FPhysicsShapeHandle& InShape,bool bWakeTouching)
{
	if (CHAOS_ENSURE(InShape.Shape))
	{
		InActor->GetGameThreadAPI().RemoveShape(InShape.Shape, bWakeTouching);
	}
}

void FChaosEngineInterface::AddDisabledCollisionsFor_AssumesLocked(const TMap<FPhysicsActorHandle, TArray< FPhysicsActorHandle > >& InMap)
{
	if (bEnableChaosCollisionManager)
	{
		for (auto Elem : InMap)
		{
			FPhysicsActorHandle& ActorReference = Elem.Key;
			Chaos::FUniqueIdx ActorIndex = ActorReference->GetGameThreadAPI().UniqueIdx();

			Chaos::FPhysicsSolver* Solver = ActorReference->GetSolver<Chaos::FPhysicsSolver>();
			Chaos::FIgnoreCollisionManager& CollisionManager = Solver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
			int32 ExternalTimestamp = Solver->GetMarshallingManager().GetExternalTimestamp_External();
			Chaos::FIgnoreCollisionManager::FPendingMap& ActivationMap = CollisionManager.GetPendingActivationsForGameThread(ExternalTimestamp);

			if (ActivationMap.Contains(ActorIndex))
			{
				ActivationMap.Remove(ActorIndex);
			}

			TArray< Chaos::FUniqueIdx > DisabledCollisions;
			DisabledCollisions.Reserve(Elem.Value.Num());

			if (Chaos::FPBDRigidParticle* Rigid0 = ActorReference->GetParticle_LowLevel()->CastToRigidParticle())
			{
				Rigid0->SetCollisionConstraintFlag((uint32)Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
				for (auto Handle1 : Elem.Value)
				{
					if (Chaos::FPBDRigidParticle* Rigid1 = Handle1->GetParticle_LowLevel()->CastToRigidParticle())
					{
						Rigid1->SetCollisionConstraintFlag((uint32)Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						DisabledCollisions.Add(Handle1->GetGameThreadAPI().UniqueIdx());
					}
				}
			}

			ActivationMap.Add(ActorIndex, DisabledCollisions);
		}
	}
}

void FChaosEngineInterface::RemoveDisabledCollisionsFor_AssumesLocked(TArray< FPhysicsActorHandle >& InPhysicsActors)
{
	if (bEnableChaosCollisionManager)
	{
		for (FPhysicsActorHandle& ActorReference : InPhysicsActors)
		{
			Chaos::FUniqueIdx ActorIndex = ActorReference->GetGameThreadAPI().UniqueIdx();

			Chaos::FPhysicsSolver* Solver = ActorReference->GetSolver<Chaos::FPhysicsSolver>();
			Chaos::FIgnoreCollisionManager& CollisionManager = Solver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
			int32 ExternalTimestamp = Solver->GetMarshallingManager().GetExternalTimestamp_External();

			Chaos::FIgnoreCollisionManager::FDeactivationArray& PendingMap = CollisionManager.GetPendingDeactivationsForGameThread(ExternalTimestamp);
			if (!PendingMap.Contains(ActorReference->GetGameThreadAPI().UniqueIdx()))
			{
				PendingMap.Add(ActorReference->GetGameThreadAPI().UniqueIdx());
			}
		}
	}
}

void FChaosEngineInterface::SetActorUserData_AssumesLocked(FPhysicsActorHandle& InActorReference,FPhysicsUserData* InUserData)
{
	InActorReference->GetGameThreadAPI().SetUserData(InUserData);
}

bool FChaosEngineInterface::IsRigidBody(const FPhysicsActorHandle& InActorReference)
{
	return !IsStatic(InActorReference);
}

bool FChaosEngineInterface::IsDynamic(const FPhysicsActorHandle& InActorReference)
{
	// Do this to match the PhysX interface behavior: :( :( :(
	return !IsStatic(InActorReference);
}

bool FChaosEngineInterface::IsStatic(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->GetGameThreadAPI().ObjectState() == Chaos::EObjectStateType::Static;
}

bool FChaosEngineInterface::IsKinematic(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->GetGameThreadAPI().ObjectState() == Chaos::EObjectStateType::Kinematic;
}

bool FChaosEngineInterface::IsKinematic_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return IsKinematic(InActorReference);
}

bool FChaosEngineInterface::IsSleeping(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->GetGameThreadAPI().ObjectState() == Chaos::EObjectStateType::Sleeping;
}

bool FChaosEngineInterface::IsCcdEnabled(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->GetGameThreadAPI().CCDEnabled();
}


bool FChaosEngineInterface::CanSimulate_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return true;
}

float FChaosEngineInterface::GetMass_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->GetGameThreadAPI().M();
}

void FChaosEngineInterface::SetSendsSleepNotifies_AssumesLocked(const FPhysicsActorHandle& InActorReference,bool bSendSleepNotifies)
{
	// # todo: Implement
	//check(bSendSleepNotifies == false);
}

void FChaosEngineInterface::PutToSleep_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// NOTE: We want to set the state whether or not it's asleep - if we currently think we're
	// asleep but the physics thread has queued up a wake event, then we still need to call
	// SetObjectState, so that this manual call will take priority.
	Chaos::FRigidBodyHandle_External& BodyHandle_External = InActorReference->GetGameThreadAPI();
	if (BodyHandle_External.ObjectState() == Chaos::EObjectStateType::Dynamic || BodyHandle_External.ObjectState() == Chaos::EObjectStateType::Sleeping)
	{
		InActorReference->GetGameThreadAPI().SetObjectState(Chaos::EObjectStateType::Sleeping);
	}

}

void FChaosEngineInterface::WakeUp_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// NOTE: We want to set the state whether or not it's asleep - if we currently think we're
	// dynamic but the physics thread has queued up a sleep event, then we still need to call
	// SetObjectState, so that this manual call will take priority.
	Chaos::FRigidBodyHandle_External& BodyHandle_External = InActorReference->GetGameThreadAPI();
	if(BodyHandle_External.ObjectState() == Chaos::EObjectStateType::Dynamic || BodyHandle_External.ObjectState() == Chaos::EObjectStateType::Sleeping)
	{
		BodyHandle_External.SetObjectState(Chaos::EObjectStateType::Dynamic);
		BodyHandle_External.ClearEvents();
	}
}

void FChaosEngineInterface::SetIsKinematic_AssumesLocked(const FPhysicsActorHandle& InActorReference,bool bIsKinematic)
{
	using namespace Chaos;
	{
		const EObjectStateType NewState
			= bIsKinematic
			? EObjectStateType::Kinematic
			: EObjectStateType::Dynamic;

		bool AllowedToChangeToNewState = false;

		switch(InActorReference->GetGameThreadAPI().ObjectState())
		{
		case EObjectStateType::Kinematic:
		// from kinematic we can only go dynamic
		if(NewState == EObjectStateType::Dynamic)
		{
			AllowedToChangeToNewState = true;
		}
		break;

		case EObjectStateType::Dynamic:
		// from dynamic we can go to sleeping or to kinematic
		if(NewState == EObjectStateType::Kinematic)
		{
			AllowedToChangeToNewState = true;
		}
		break;

		case EObjectStateType::Sleeping:
		// this case was not allowed from CL 10506092, but it needs to in order for
		// FBodyInstance::SetInstanceSimulatePhysics to work on dynamic bodies which
		// have fallen asleep.
		if (NewState == EObjectStateType::Kinematic)
		{
			AllowedToChangeToNewState = true;
		}
		break;
		}

		if(AllowedToChangeToNewState)
		{
			InActorReference->GetGameThreadAPI().SetObjectState(NewState);
			//we mark as full resim only if going from kinematic to simulated
			//going from simulated to kinematic we assume user is doing some optimization so we leave it up to them
			if(NewState == EObjectStateType::Dynamic)
			{
				InActorReference->GetGameThreadAPI().SetResimType(EResimType::FullResim);
			}
			else if (NewState == Chaos::EObjectStateType::Kinematic)
			{
				// Reset velocity on a state change here
				InActorReference->GetGameThreadAPI().SetV(Chaos::FVec3((Chaos::FReal) 0));
				InActorReference->GetGameThreadAPI().SetW(Chaos::FVec3((Chaos::FReal) 0));
			}
		}
	}
}

void FChaosEngineInterface::SetCcdEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference,bool bIsCcdEnabled)
{
	InActorReference->GetGameThreadAPI().SetCCDEnabled(bIsCcdEnabled);
}

void FChaosEngineInterface::SetIgnoreAnalyticCollisions_AssumesLocked(const FPhysicsActorHandle& InActorReference,bool bIgnoreAnalyticCollisions)
{
	InActorReference->GetGameThreadAPI().SetIgnoreAnalyticCollisions(bIgnoreAnalyticCollisions);
}

FTransform FChaosEngineInterface::GetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return Chaos::FRigidTransform3(InActorReference->GetGameThreadAPI().X(),InActorReference->GetGameThreadAPI().R());
}

FTransform FChaosEngineInterface::GetTransform_AssumesLocked(const FPhysicsActorHandle& InRef,bool bForceGlobalPose /*= false*/)
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

bool FChaosEngineInterface::HasKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return IsStatic(InActorReference);
}

FTransform FChaosEngineInterface::GetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	//for now just use global pose
	return FChaosEngineInterface::GetGlobalPose_AssumesLocked(InActorReference);
}

FVector FChaosEngineInterface::GetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		return InActorReference->GetGameThreadAPI().V();
	}

	return FVector(0);
}

void FChaosEngineInterface::SetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InNewVelocity,bool bAutoWake)
{
	// TODO: Implement bAutoWake == false.
	// For now we don't support auto-awake == false.
	// This feature is meant to detect when velocity change small
	// and the velocity is nearly zero, and to not wake up the
	// body in that case.
	ensure(bAutoWake);

	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		InActorReference->GetGameThreadAPI().SetV(InNewVelocity);
	}
}

FVector FChaosEngineInterface::GetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		return InActorReference->GetGameThreadAPI().W();
	}

	return FVector(0);
}

void FChaosEngineInterface::SetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InNewAngularVelocity,bool bAutoWake)
{
	// TODO: Implement bAutoWake == false.
	ensure(bAutoWake);

	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		InActorReference->GetGameThreadAPI().SetW(InNewAngularVelocity);
	}
}

float FChaosEngineInterface::GetMaxAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	CHAOS_ENSURE(false);
	return FLT_MAX;
}

void FChaosEngineInterface::SetMaxAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference,float InMaxAngularVelocity)
{
	CHAOS_ENSURE(false);
}

float FChaosEngineInterface::GetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	CHAOS_ENSURE(false);
	return FLT_MAX;
}

void FChaosEngineInterface::SetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference,float InMaxDepenetrationVelocity)
{
	CHAOS_ENSURE(false);
}

FVector FChaosEngineInterface::GetWorldVelocityAtPoint_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InPoint)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		const Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
		if(ensure(Body_External.CanTreatAsKinematic()))
		{
			const bool bIsRigid = Body_External.CanTreatAsRigid();
			const Chaos::FVec3 COM = bIsRigid ? Chaos::FParticleUtilitiesGT::GetCoMWorldPosition(&Body_External) : Chaos::FParticleUtilitiesGT::GetActorWorldTransform(&Body_External).GetTranslation();
			const Chaos::FVec3 Diff = InPoint - COM;
			return Body_External.V() - Chaos::FVec3::CrossProduct(Diff, Body_External.W());
		}
	}
	return FVector(0);
}

#if WITH_CHAOS
FVector FChaosEngineInterface::GetWorldVelocityAtPoint_AssumesLocked(const Chaos::FRigidBodyHandle_Internal* Body_Internal, const FVector& InPoint)
{
	const Chaos::FVec3 COM = Body_Internal->CanTreatAsRigid() ? Chaos::FParticleUtilitiesGT::GetCoMWorldPosition(Body_Internal) : Chaos::FParticleUtilitiesGT::GetActorWorldTransform(Body_Internal).GetTranslation();
	const Chaos::FVec3 Diff = InPoint - COM;
	return Body_Internal->V() - Chaos::FVec3::CrossProduct(Diff, Body_Internal->W());
}
#endif

FTransform FChaosEngineInterface::GetComTransform_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		return Chaos::FParticleUtilitiesGT::GetCoMWorldTransform(&InActorReference->GetGameThreadAPI());
	}
	return FTransform();
}

FTransform FChaosEngineInterface::GetComTransformLocal_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		return FTransform(InActorReference->GetGameThreadAPI().RotationOfMass(),InActorReference->GetGameThreadAPI().CenterOfMass());
	}
	return FTransform();
}

FVector FChaosEngineInterface::GetLocalInertiaTensor_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	const Chaos::FMatrix33 Tensor = InActorReference->GetGameThreadAPI().I();
	return FVector(Tensor.M[0][0],Tensor.M[1][1],Tensor.M[2][2]);
}

FBox FChaosEngineInterface::GetBounds_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	using namespace Chaos;
	const Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
	if(const FImplicitObject* Geometry = Body_External.Geometry().Get())
	{
		if(Geometry->HasBoundingBox())
		{
			const FAABB3 LocalBounds = Geometry->BoundingBox();
			const FRigidTransform3 WorldTM(Body_External.X(), Body_External.R());
			const FAABB3 WorldBounds = LocalBounds.TransformedAABB(WorldTM);
			return FBox(WorldBounds.Min(),WorldBounds.Max());
		}
	}

	return FBox(EForceInit::ForceInitToZero);
}

void FChaosEngineInterface::SetLinearDamping_AssumesLocked(const FPhysicsActorHandle& InActorReference,float InDrag)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		InActorReference->GetGameThreadAPI().SetLinearEtherDrag(InDrag);
	}
}

void FChaosEngineInterface::SetAngularDamping_AssumesLocked(const FPhysicsActorHandle& InActorReference,float InDamping)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		InActorReference->GetGameThreadAPI().SetAngularEtherDrag(InDamping);
	}
}

void FChaosEngineInterface::AddImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InForce)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
		Body_External.SetLinearImpulse(Body_External.LinearImpulse() + InForce);
	}
}

void FChaosEngineInterface::AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InTorque)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
		Body_External.SetAngularImpulse(Body_External.AngularImpulse() + InTorque);
	}
}

void FChaosEngineInterface::AddVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InVelocityDelta)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		AddImpulse_AssumesLocked(InActorReference, InActorReference->GetGameThreadAPI().M() * InVelocityDelta);
	}
}

void FChaosEngineInterface::AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InAngularVelocityDeltaRad)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		const Chaos::FMatrix33 WorldI = Chaos::FParticleUtilitiesXR::GetWorldInertia(&InActorReference->GetGameThreadAPI());
		AddAngularImpulseInRadians_AssumesLocked(InActorReference,WorldI * InAngularVelocityDeltaRad);
	}
}

void FChaosEngineInterface::AddImpulseAtLocation_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InImpulse,const FVector& InLocation)
{
	if(ensure(FChaosEngineInterface::IsValid(InActorReference)))
	{
		const Chaos::FVec3 WorldCOM = Chaos::FParticleUtilitiesGT::GetCoMWorldPosition(&InActorReference->GetGameThreadAPI());
		const Chaos::FVec3 AngularImpulse = Chaos::FVec3::CrossProduct(InLocation - WorldCOM,InImpulse);
		AddImpulse_AssumesLocked(InActorReference,InImpulse);
		AddAngularImpulseInRadians_AssumesLocked(InActorReference,AngularImpulse);
	}
}

void FChaosEngineInterface::AddRadialImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FVector& InOrigin,float InRadius,float InStrength,ERadialImpulseFalloff InFalloff,bool bInVelChange)
{
	using namespace Chaos;
	if (ensure(InActorReference->GetGameThreadAPI().CanTreatAsRigid()))
	{
		const FVec3 WorldCOM = FParticleUtilitiesGT::GetCoMWorldPosition(&InActorReference->GetGameThreadAPI());
		const FVec3 OriginToActor = WorldCOM - InOrigin;
		const FReal OriginToActorDistance = OriginToActor.Size();
		if(OriginToActorDistance < InRadius)
		{
			FVec3 FinalImpulse = FVector::ZeroVector;
			if(OriginToActorDistance > 0)
			{
				const FVec3 OriginToActorNorm = OriginToActor / OriginToActorDistance;

				if(InFalloff == ERadialImpulseFalloff::RIF_Constant)
				{
					FinalImpulse = OriginToActorNorm * InStrength;
				}
				else if(InFalloff == ERadialImpulseFalloff::RIF_Linear)
				{
					const FReal DistanceOverlapping = InRadius - OriginToActorDistance;
					if(DistanceOverlapping > 0)
					{
						FinalImpulse = OriginToActorNorm * FMath::Lerp(0.0f, InStrength, DistanceOverlapping / InRadius);
					}
				}
				else
				{
					// Unimplemented falloff type
					ensure(false);
				}
			}
			else
			{
				// Sphere and actor center are coincident, just pick a direction and apply maximum strength impulse.
				FinalImpulse = FVector::ForwardVector * InStrength;
			}

			if(bInVelChange)
			{
				AddVelocity_AssumesLocked(InActorReference, FinalImpulse);
			}
			else
			{
				AddImpulse_AssumesLocked(InActorReference, FinalImpulse);
			}
		}
	}
}

bool FChaosEngineInterface::IsGravityEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->GetGameThreadAPI().GravityEnabled();
}
void FChaosEngineInterface::SetGravityEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference,bool bEnabled)
{
	InActorReference->GetGameThreadAPI().SetGravityEnabled(bEnabled);
}

void FChaosEngineInterface::SetOneWayInteraction_AssumesLocked(const FPhysicsActorHandle& InHandle, bool InOneWayInteraction)
{
	InHandle->GetGameThreadAPI().SetOneWayInteraction(InOneWayInteraction);
}

float FChaosEngineInterface::GetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return 0;
}
void FChaosEngineInterface::SetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InActorReference,float InEnergyThreshold)
{
}

void FChaosEngineInterface::SetMass_AssumesLocked(FPhysicsActorHandle& InActorReference,float InMass)
{
	Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
	Body_External.SetM(InMass);
	if(CHAOS_ENSURE(!FMath::IsNearlyZero(InMass)))
	{
		Body_External.SetInvM(1./InMass);
	} else
	{
		Body_External.SetInvM(0);
	}
}

void FChaosEngineInterface::SetMassSpaceInertiaTensor_AssumesLocked(FPhysicsActorHandle& InActorReference,const FVector& InTensor)
{
	if(CHAOS_ENSURE(!FMath::IsNearlyZero(InTensor.X)) && CHAOS_ENSURE(!FMath::IsNearlyZero(InTensor.Y)) && CHAOS_ENSURE(!FMath::IsNearlyZero(InTensor.Z)))
	{
		Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
		Body_External.SetI(Chaos::FMatrix33(InTensor.X,InTensor.Y,InTensor.Z));
		Body_External.SetInvI(Chaos::FMatrix33(1./InTensor.X,1./InTensor.Y,1./InTensor.Z));
	}
}

void FChaosEngineInterface::SetComLocalPose_AssumesLocked(const FPhysicsActorHandle& InHandle,const FTransform& InComLocalPose)
{
	//@todo(mlentine): What is InComLocalPose? If the center of an object is not the local pose then many things break including the three vector represtnation of inertia.
	Chaos::FRigidBodyHandle_External& Body_External = InHandle->GetGameThreadAPI();
	Body_External.SetCenterOfMass(InComLocalPose.GetLocation());
	Body_External.SetRotationOfMass(InComLocalPose.GetRotation());
}

void FChaosEngineInterface::SetIsSimulationShape(const FPhysicsShapeHandle& InShape,bool bIsSimShape)
{
	InShape.Shape->SetSimEnabled(bIsSimShape);
}

void FChaosEngineInterface::SetIsQueryShape(const FPhysicsShapeHandle& InShape,bool bIsQueryShape)
{
	InShape.Shape->SetQueryEnabled(bIsQueryShape);
}

float FChaosEngineInterface::GetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0.0f;
}

void FChaosEngineInterface::SetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InHandle,float InThreshold)
{
	// #todo : Implement
}

uint32 FChaosEngineInterface::GetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0;
}

void FChaosEngineInterface::SetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle,uint32 InSolverIterationCount)
{
	// #todo : Implement
}

uint32 FChaosEngineInterface::GetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0;
}

void FChaosEngineInterface::SetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle,uint32 InSolverIterationCount)
{
	// #todo : Implement
}

float FChaosEngineInterface::GetWakeCounter_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0.0f;
}

void FChaosEngineInterface::SetWakeCounter_AssumesLocked(const FPhysicsActorHandle& InHandle,float InWakeCounter)
{
	// #todo : Implement
}

void FChaosEngineInterface::SetInitialized_AssumesLocked(const FPhysicsActorHandle& InHandle,bool InInitialized)
{
	//why is this needed?
	Chaos::FPBDRigidParticle* Rigid = InHandle->GetParticle_LowLevel()->CastToRigidParticle();
	if(Rigid)
	{
		Rigid->SetInitialized(InInitialized);
	}
}

SIZE_T FChaosEngineInterface::GetResourceSizeEx(const FPhysicsActorHandle& InActorRef)
{
	return sizeof(FPhysicsActorHandle);
}

// Constraints
FPhysicsConstraintHandle FChaosEngineInterface::CreateConstraint(const FPhysicsActorHandle& InActorRef1,const FPhysicsActorHandle& InActorRef2,const FTransform& InLocalFrame1,const FTransform& InLocalFrame2)
{
	FPhysicsConstraintHandle ConstraintRef;

	if(bEnableChaosJointConstraints)
	{
		if(InActorRef1 && InActorRef2 && InActorRef1->GetSolverBase() && InActorRef2->GetSolverBase())
		{
			if(InActorRef1->GetSolverBase() && InActorRef2->GetSolverBase())
			{
				LLM_SCOPE(ELLMTag::Chaos);

				auto* JointConstraint = new Chaos::FJointConstraint();
				ConstraintRef.Constraint = JointConstraint;

				JointConstraint->SetParticleProxies({ InActorRef1,InActorRef2 });
				JointConstraint->SetJointTransforms({ InLocalFrame1,InLocalFrame2 });

				Chaos::FPhysicsSolver* Solver = InActorRef1->GetSolver<Chaos::FPhysicsSolver>();
				checkSlow(Solver == InActorRef2->GetSolver<Chaos::FPhysicsSolver>());
				Solver->RegisterObject(JointConstraint);
			}
		}
		else if (InActorRef1 != nullptr || InActorRef2 != nullptr)
		{
			LLM_SCOPE(ELLMTag::Chaos);

			FPhysicsActorHandle ValidParticle = InActorRef1;
			bool bSwapped = false;
			if (ValidParticle == nullptr)
			{
				bSwapped = true;
				ValidParticle = InActorRef2;
			}
			if(ValidParticle->GetSolverBase())
			{
				FChaosScene* Scene = FChaosEngineInterface::GetCurrentScene(ValidParticle);

				// Create kinematic actor to attach to joint
				FPhysicsActorHandle KinematicEndPoint;
				FActorCreationParams Params;
				Params.bSimulatePhysics = false;
				Params.bQueryOnly = false;
				Params.Scene = Scene;
				Params.bStatic = false;
				Params.InitialTM = FTransform::Identity;
				FChaosEngineInterface::CreateActor(Params, KinematicEndPoint);

				// Chaos requires our particles have geometry.
				auto Sphere = MakeUnique<Chaos::FImplicitSphere3>(FVector(0, 0, 0), 0);
				KinematicEndPoint->GetGameThreadAPI().SetGeometry(MoveTemp(Sphere));
				KinematicEndPoint->GetGameThreadAPI().SetUserData(nullptr);

				auto* JointConstraint = new Chaos::FJointConstraint();
				JointConstraint->SetKinematicEndPoint(KinematicEndPoint, Scene->GetSolver());
				ConstraintRef.Constraint = JointConstraint;

				JointConstraint->SetParticleProxies({ ValidParticle, KinematicEndPoint });

				Chaos::FJointConstraint::FTransformPair TransformPair = { InLocalFrame1, InLocalFrame2 };
				if (bSwapped)
				{
					Swap(TransformPair[0], TransformPair[1]);
				}
				JointConstraint->SetJointTransforms(TransformPair);

				Chaos::FPhysicsSolver* Solver = ValidParticle->GetSolver<Chaos::FPhysicsSolver>();
				checkSlow(Solver == KinematicEndPoint->GetSolver<Chaos::FPhysicsSolver>());
				Solver->RegisterObject(JointConstraint);
			}
		}
	}
	return ConstraintRef;
}


FPhysicsConstraintHandle FChaosEngineInterface::CreateSuspension(const FPhysicsActorHandle& InActorRef, const FVector& InLocalFrame)
{
	FPhysicsConstraintHandle ConstraintRef;

	if (bEnableChaosJointConstraints)
	{
		if (InActorRef)
		{
			if (InActorRef->GetSolverBase())
			{
				LLM_SCOPE(ELLMTag::Chaos);

				auto* SuspensionConstraint = new Chaos::FSuspensionConstraint();
				ConstraintRef.Constraint = SuspensionConstraint;

				SuspensionConstraint->SetParticleProxies({ InActorRef,nullptr });
				SuspensionConstraint->SetLocation( InLocalFrame );

				Chaos::FPhysicsSolver* Solver = InActorRef->GetSolver<Chaos::FPhysicsSolver>();
				Solver->RegisterObject(SuspensionConstraint);
			}
		}
	}
	return ConstraintRef;
}


void FChaosEngineInterface::SetConstraintUserData(const FPhysicsConstraintHandle& InConstraintRef,void* InUserData)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetUserData(InUserData);
		}
	}
}

void FChaosEngineInterface::ReleaseConstraint(FPhysicsConstraintHandle& InConstraintRef)
{
	if (bEnableChaosJointConstraints)
	{
		LLM_SCOPE(ELLMTag::Chaos);
		if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
		{
			if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
			{
				if (FJointConstraintPhysicsProxy* Proxy = Constraint->GetProxy<FJointConstraintPhysicsProxy>())
				{
					check(Proxy->GetSolver<Chaos::FPhysicsSolver>());
					Chaos::FPhysicsSolver* Solver = Proxy->GetSolver<Chaos::FPhysicsSolver>();

					Solver->UnregisterObject(Constraint);

					InConstraintRef.Constraint = nullptr; // freed by the joint constraint physics proxy
				}
			}
		}
		else if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::SuspensionConstraintType))
		{
			if (Chaos::FSuspensionConstraint* Constraint = static_cast<Chaos::FSuspensionConstraint*>(InConstraintRef.Constraint))
			{
				if (FSuspensionConstraintPhysicsProxy* Proxy = Constraint->GetProxy<FSuspensionConstraintPhysicsProxy>())
				{
					check(Proxy->GetSolver<Chaos::FPhysicsSolver>());
					Chaos::FPhysicsSolver* Solver = Proxy->GetSolver<Chaos::FPhysicsSolver>();

					Solver->UnregisterObject(Constraint);

					InConstraintRef.Constraint = nullptr;  // freed by the joint constraint physics proxy
				}
			}

		}
	}
}

FTransform FChaosEngineInterface::GetLocalPose(const FPhysicsConstraintHandle& InConstraintRef,EConstraintFrame::Type InFrame)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			const Chaos::FJointConstraint::FTransformPair& M = Constraint->GetJointTransforms();
			if (InFrame == EConstraintFrame::Frame1)
			{
				return M[0];
			}
			else if (InFrame == EConstraintFrame::Frame2)
			{
				return M[1];
			}
		}
	}
	return FTransform::Identity;
}

Chaos::FGeometryParticle*
GetParticleFromProxy(IPhysicsProxyBase* ProxyBase)
{
	if (ProxyBase)
	{
		if (ProxyBase->GetType() == EPhysicsProxyType::SingleParticleProxy)
		{
			return ((FSingleParticlePhysicsProxy*)ProxyBase)->GetParticle_LowLevel();
		}
	}
	return nullptr;
}


FTransform FChaosEngineInterface::GetGlobalPose(const FPhysicsConstraintHandle& InConstraintRef, EConstraintFrame::Type InFrame)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Chaos::FConstraintBase::FProxyBasePair BasePairs = Constraint->GetParticleProxies();
			const Chaos::FJointConstraint::FTransformPair& M = Constraint->GetJointTransforms();

			if (InFrame == EConstraintFrame::Frame1)
			{
				if (Chaos::FGeometryParticle* Particle = GetParticleFromProxy(BasePairs[0]))
				{
					return FTransform(Particle->R(), Particle->X()) * M[0];
				}
			}
			else if (InFrame == EConstraintFrame::Frame2)
			{
				if (Chaos::FGeometryParticle* Particle = GetParticleFromProxy(BasePairs[1]))
				{
					return FTransform(Particle->R(), Particle->X()) * M[1];
				}
			}
		}
	}
	return FTransform::Identity;
}

FVector FChaosEngineInterface::GetLocation(const FPhysicsConstraintHandle& InConstraintRef)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			return 0.5 * (GetGlobalPose(InConstraintRef, EConstraintFrame::Frame1).GetTranslation() + GetGlobalPose(InConstraintRef, EConstraintFrame::Frame2).GetTranslation());
		}
	}
	return FVector::ZeroVector;

}

void FChaosEngineInterface::GetForce(const FPhysicsConstraintHandle& InConstraintRef, FVector& OutLinForce, FVector& OutAngForce)
{
	OutLinForce = FVector::ZeroVector;
	OutAngForce = FVector::ZeroVector;

	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			OutLinForce = Constraint->GetOutputData().Force;
			OutAngForce = Constraint->GetOutputData().Torque;
		}
	}
}

void FChaosEngineInterface::GetDriveLinearVelocity(const FPhysicsConstraintHandle& InConstraintRef,FVector& OutLinVelocity)
{
	OutLinVelocity = FVector::ZeroVector;

	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			OutLinVelocity = Constraint->GetLinearDriveVelocityTarget();
		}
	}
}

void FChaosEngineInterface::GetDriveAngularVelocity(const FPhysicsConstraintHandle& InConstraintRef,FVector& OutAngVelocity)
{
	OutAngVelocity = FVector::ZeroVector;

	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			OutAngVelocity = Constraint->GetAngularDriveVelocityTarget();
		}
	}
}

float FChaosEngineInterface::GetCurrentSwing1(const FPhysicsConstraintHandle& InConstraintRef)
{
	return GetLocalPose(InConstraintRef,EConstraintFrame::Frame2).GetRotation().Euler().X;
}

float FChaosEngineInterface::GetCurrentSwing2(const FPhysicsConstraintHandle& InConstraintRef)
{
	return GetLocalPose(InConstraintRef,EConstraintFrame::Frame2).GetRotation().Euler().Y;
}

float FChaosEngineInterface::GetCurrentTwist(const FPhysicsConstraintHandle& InConstraintRef)
{
	return GetLocalPose(InConstraintRef,EConstraintFrame::Frame2).GetRotation().Euler().Z;
}

void FChaosEngineInterface::SetCanVisualize(const FPhysicsConstraintHandle& InConstraintRef,bool bInCanVisualize)
{
	// @todo(chaos) :  Joint Constraints : Debug Tools
}

void FChaosEngineInterface::SetCollisionEnabled(const FPhysicsConstraintHandle& InConstraintRef,bool bInCollisionEnabled)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetCollisionEnabled(bInCollisionEnabled);
		}
	}
}

void FChaosEngineInterface::SetProjectionEnabled_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef,bool bInProjectionEnabled,float InLinearAlpha,float InAngularAlpha)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetProjectionEnabled(bInProjectionEnabled);
			Constraint->SetProjectionLinearAlpha(InLinearAlpha);
			Constraint->SetProjectionAngularAlpha(InAngularAlpha);
		}
	}
}

void FChaosEngineInterface::SetParentDominates_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef,bool bInParentDominates)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			if(bInParentDominates)
			{
				Constraint->SetParentInvMassScale(0.f);
			} else
			{
				Constraint->SetParentInvMassScale(1.f);
			}
		}
	}
}

void FChaosEngineInterface::SetBreakForces_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef,float InLinearBreakForce,float InAngularBreakTorque)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetLinearBreakForce(InLinearBreakForce);
			Constraint->SetAngularBreakTorque(InAngularBreakTorque);
		}
	}
}

void FChaosEngineInterface::SetPlasticityLimits_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, float InLinearPlasticityLimit, float InAngularPlasticityLimit)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetLinearPlasticityLimit(InLinearPlasticityLimit);
			Constraint->SetAngularPlasticityLimit(InAngularPlasticityLimit);
		}
	}
}

void FChaosEngineInterface::SetLocalPose(const FPhysicsConstraintHandle& InConstraintRef,const FTransform& InPose,EConstraintFrame::Type InFrame)
{
	// @todo(chaos) :  Joint Constraints : Motors
}

void FChaosEngineInterface::SetDrivePosition(const FPhysicsConstraintHandle& InConstraintRef,const FVector& InPosition)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetLinearDrivePositionTarget(InPosition);
		}
	}
}

void FChaosEngineInterface::SetDriveOrientation(const FPhysicsConstraintHandle& InConstraintRef,const FQuat& InOrientation)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetAngularDrivePositionTarget(InOrientation);
		}
	}
}

void FChaosEngineInterface::SetDriveLinearVelocity(const FPhysicsConstraintHandle& InConstraintRef,const FVector& InLinVelocity)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetLinearDriveVelocityTarget(InLinVelocity);
		}
	}
}

void FChaosEngineInterface::SetDriveAngularVelocity(const FPhysicsConstraintHandle& InConstraintRef,const FVector& InAngVelocity)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetAngularDriveVelocityTarget(InAngVelocity);
		}
	}
}

void FChaosEngineInterface::SetTwistLimit(const FPhysicsConstraintHandle& InConstraintRef,float InLowerLimit,float InUpperLimit,float InContactDistance)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Chaos::FVec3 Limit = Constraint->GetAngularLimits();
			Limit[(int32)Chaos::EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(InUpperLimit - InLowerLimit);
			Constraint->SetAngularLimits(Limit);
			Constraint->SetTwistContactDistance(InContactDistance);
		}
	}
}

void FChaosEngineInterface::SetSwingLimit(const FPhysicsConstraintHandle& InConstraintRef,float InYLimit,float InZLimit,float InContactDistance)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Chaos::FVec3 Limit = Constraint->GetAngularLimits();
			Limit[(int32)Chaos::EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(InYLimit);
			Limit[(int32)Chaos::EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(InZLimit);
			Constraint->SetAngularLimits(Limit);
			Constraint->SetSwingContactDistance(InContactDistance);
		}
	}
}

void FChaosEngineInterface::SetLinearLimit(const FPhysicsConstraintHandle& InConstraintRef,float InLinearLimit)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			Constraint->SetLinearLimit(InLinearLimit);
		}
	}
}

bool FChaosEngineInterface::IsBroken(const FPhysicsConstraintHandle& InConstraintRef)
{
	if (InConstraintRef.IsValid() && InConstraintRef.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
	{
		if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(InConstraintRef.Constraint))
		{
			return Constraint->GetOutputData().bIsBroken;
		}
	}
	return false;
}


void FChaosEngineInterface::SetGeometry(FPhysicsShapeHandle& InShape, TUniquePtr<Chaos::FImplicitObject>&& InGeometry)
{
	using namespace Chaos;

	// This sucks, we build a new union with input geometry. All other geo is copied.
	// Cannot modify union as it is shared between threads.
	const FShapesArray& ShapeArray = InShape.ActorRef->GetGameThreadAPI().ShapesArray();

	TArray<TUniquePtr<FImplicitObject>> NewGeometry;
	NewGeometry.Reserve(ShapeArray.Num());

	int32 ShapeIdx = 0;
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : ShapeArray)
	{
		if (Shape.Get() == InShape.Shape)
		{
			NewGeometry.Emplace(MoveTemp(InGeometry));
		}
		else
		{
			NewGeometry.Emplace(Shape->GetGeometry()->Copy());
		}

		ShapeIdx++;
	}

	if (ensure(NewGeometry.Num() == ShapeArray.Num()))
	{
		InShape.ActorRef->GetGameThreadAPI().SetGeometry(MakeUnique<Chaos::FImplicitObjectUnion>(MoveTemp(NewGeometry)));
		
		FChaosScene* Scene = FChaosEngineInterface::GetCurrentScene(InShape.ActorRef);
		if (ensure(Scene))
		{
			Scene->UpdateActorInAccelerationStructure(InShape.ActorRef);
		}
	}
}

// @todo(chaos): We probably need to actually duplicate the data here, add virtual TImplicitObject::NewCopy()
FPhysicsShapeHandle FChaosEngineInterface::CloneShape(const FPhysicsShapeHandle& InShape)
{
	FPhysicsActorHandle NewActor = nullptr;
	return {InShape.Shape,NewActor};
}

FPhysicsGeometryCollection_Chaos FChaosEngineInterface::GetGeometryCollection(const FPhysicsShapeHandle& InShape)
{
	FPhysicsGeometryCollection_Chaos NewCollection(InShape);
	return NewCollection;
}


FCollisionFilterData FChaosEngineInterface::GetSimulationFilter(const FPhysicsShapeReference_Chaos& InShape)
{
	if(ensure(InShape.Shape))
	{
		return InShape.Shape->GetSimData();
	} else
	{
		return FCollisionFilterData();
	}
}

FCollisionFilterData FChaosEngineInterface::GetQueryFilter(const FPhysicsShapeReference_Chaos& InShape)
{
	if(ensure(InShape.Shape))
	{
		return InShape.Shape->GetQueryData();
	} else
	{
		return FCollisionFilterData();
	}
}

void FChaosEngineInterface::SetQueryFilter(const FPhysicsShapeReference_Chaos& InShapeRef,const FCollisionFilterData& InFilter)
{
	InShapeRef.Shape->SetQueryData(InFilter);
}

void FChaosEngineInterface::SetSimulationFilter(const FPhysicsShapeReference_Chaos& InShapeRef,const FCollisionFilterData& InFilter)
{
	InShapeRef.Shape->SetSimData(InFilter);
}

bool FChaosEngineInterface::IsSimulationShape(const FPhysicsShapeHandle& InShape)
{
	return InShape.Shape->GetSimEnabled();
}

bool FChaosEngineInterface::IsQueryShape(const FPhysicsShapeHandle& InShape)
{
	// This data is not stored on concrete shape. TODO: Remove ensure if we actually use this flag when constructing shape handles.
	CHAOS_ENSURE(false);
	return InShape.Shape->GetQueryEnabled();
}

ECollisionShapeType FChaosEngineInterface::GetShapeType(const FPhysicsShapeReference_Chaos& InShapeRef)
{
	return GetImplicitType(*InShapeRef.Shape->GetGeometry());
}

FTransform FChaosEngineInterface::GetLocalTransform(const FPhysicsShapeReference_Chaos& InShapeRef)
{
	// Transforms are baked into the object so there is never a local transform
	if(InShapeRef.Shape->GetGeometry()->GetType() == Chaos::ImplicitObjectType::Transformed && FChaosEngineInterface::IsValid(InShapeRef.ActorRef))
	{
		return InShapeRef.Shape->GetGeometry()->GetObject<Chaos::TImplicitObjectTransformed<float,3>>()->GetTransform();
	} else
	{
		return FTransform();
	}
}

void FChaosEngineInterface::SetLocalTransform(const FPhysicsShapeHandle& InShape,const FTransform& NewLocalTransform)
{
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
	if(InShape.ActorRef.IsValid())
	{
		TArray<RigidBodyId> Ids ={InShape.ActorRef.GetId()};
		const auto Index = InShape.ActorRef.GetScene()->GetIndexFromId(InShape.ActorRef.GetId());
		if(InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed)
		{
			// @todo(mlentine): We can avoid creating a new object here by adding delayed update support for the object transforms
			LocalParticles.SetDynamicGeometry(Index,MakeUnique<Chaos::TImplicitObjectTransformed<float,3>>(InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float,3>>()->Object(),NewLocalTransform));
		} else
		{
			LocalParticles.SetDynamicGeometry(Index,MakeUnique<Chaos::TImplicitObjectTransformed<float,3>>(InShape.Object,NewLocalTransform));
		}
	}
	{
		if(InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed)
		{
			InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float,3>>()->SetTransform(NewLocalTransform);
		} else
		{
			const_cast<FPhysicsShapeHandle&>(InShape).Object = new Chaos::TImplicitObjectTransformed<float,3>(InShape.Object,NewLocalTransform);
		}
	}
#endif
}

template<typename AllocatorType>
int32 GetAllShapesInternalImp_AssumedLocked(const FPhysicsActorHandle& InActorHandle,TArray<FPhysicsShapeReference_Chaos,AllocatorType>& OutShapes)
{
	const Chaos::FShapesArray& ShapesArray = InActorHandle->GetGameThreadAPI().ShapesArray();
	OutShapes.Reset(ShapesArray.Num());
	//todo: can we avoid this construction?
	for(const TUniquePtr<Chaos::FPerShapeData>& Shape : ShapesArray)
	{
		OutShapes.Add(FPhysicsShapeReference_Chaos(Shape.Get(),InActorHandle));
	}
	return OutShapes.Num();
}

int32 FChaosEngineInterface::GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorHandle,TArray<FPhysicsShapeReference_Chaos,FDefaultAllocator>& OutShapes)
{
	return GetAllShapesInternalImp_AssumedLocked(InActorHandle,OutShapes);
}

int32 FChaosEngineInterface::GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorHandle,PhysicsInterfaceTypes::FInlineShapeArray& OutShapes)
{
	return GetAllShapesInternalImp_AssumedLocked(InActorHandle,OutShapes);
}

void FChaosEngineInterface::CreateActor(const FActorCreationParams& InParams,FPhysicsActorHandle& Handle)
{
	LLM_SCOPE(ELLMTag::Chaos);
	using namespace Chaos;

	TUniquePtr<FGeometryParticle> Particle;
	// Set object state based on the requested particle type
	if(InParams.bStatic)
	{
		Particle = FGeometryParticle::CreateParticle();
	}
	else
	{
		// Create an underlying dynamic particle
		TUniquePtr<FPBDRigidParticle> Rigid = FPBDRigidParticle::CreateParticle();
		Rigid->SetGravityEnabled(InParams.bEnableGravity);
		if(InParams.bSimulatePhysics)
		{
			if(InParams.bStartAwake)
			{
				Rigid->SetObjectState(EObjectStateType::Dynamic);
			} else
			{
				Rigid->SetObjectState(EObjectStateType::Sleeping);
			}
			Rigid->SetResimType(EResimType::FullResim);
		} else
		{
			Rigid->SetObjectState(EObjectStateType::Kinematic);
			Rigid->SetResimType(EResimType::ResimAsSlave);	//for now kinematics are never changed during resim
		}
		//Particle.Reset(Rigid.Release());
		Particle = MoveTemp(Rigid);
	}

	Handle = FSingleParticlePhysicsProxy::Create(MoveTemp(Particle));
	Chaos::FRigidBodyHandle_External& Body_External = Handle->GetGameThreadAPI();

	// Set up the new particle's game-thread data. This will be sent to physics-thread when
	// the particle is added to the scene later.
	Body_External.SetX(InParams.InitialTM.GetLocation(), /*bInvalidate=*/false);	//do not generate wake event since this is part of initialization
	Body_External.SetR(InParams.InitialTM.GetRotation(), /*bInvalidate=*/false);
#if CHAOS_CHECKED
	Body_External.SetDebugName(InParams.DebugName);
#endif
}

void FChaosEngineInterface::ReleaseActor(FPhysicsActorHandle& Handle,FChaosScene* InScene,bool bNeverDerferRelease)
{
	LLM_SCOPE(ELLMTag::Chaos);
	if(!Handle)
	{
		UE_LOG(LogChaos,Warning,TEXT("Attempting to release an actor with a null handle"));
		CHAOS_ENSURE(false);

		return;
	}

	if(InScene)
	{
		InScene->RemoveActorFromAccelerationStructure(Handle);
		RemoveActorFromSolver(Handle,InScene->GetSolver());
	}
	else
	{
		delete Handle;
	}


	Handle = nullptr;
}


FChaosScene* FChaosEngineInterface::GetCurrentScene(const FPhysicsActorHandle& InHandle)
{
	if(!InHandle)
	{
		return nullptr;
	}

	Chaos::FPBDRigidsSolver* Solver = InHandle->GetSolver<Chaos::FPBDRigidsSolver>();
	return static_cast<FChaosScene*>(Solver ? Solver->PhysSceneHack : nullptr);
}

void FChaosEngineInterface::SetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FTransform& InNewPose,bool bAutoWake)
{
	Chaos::FRigidBodyHandle_External& Body_External = InActorReference->GetGameThreadAPI();
	Body_External.SetX(InNewPose.GetLocation());
	Body_External.SetR(InNewPose.GetRotation());
	Body_External.UpdateShapeBounds();

	FChaosScene* Scene = GetCurrentScene(InActorReference);
	Scene->UpdateActorInAccelerationStructure(InActorReference);
}

void FChaosEngineInterface::SetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference,const FTransform& InNewTarget)
{
	{
	    Chaos::TKinematicTarget<float, 3> newKinematicTarget;
	    Chaos::FRigidTransform3 PreviousTM(InActorReference->GetGameThreadAPI().X(), InActorReference->GetGameThreadAPI().R());
	    newKinematicTarget.SetTargetMode(InNewTarget, PreviousTM);
	    InActorReference->GetGameThreadAPI().SetKinematicTarget(newKinematicTarget);

	    InActorReference->GetGameThreadAPI().SetX(InNewTarget.GetLocation());
	    InActorReference->GetGameThreadAPI().SetR(InNewTarget.GetRotation());
	    InActorReference->GetGameThreadAPI().UpdateShapeBounds();

	    FChaosScene* Scene = GetCurrentScene(InActorReference);
	    Scene->UpdateActorInAccelerationStructure(InActorReference);
	}
}

#elif WITH_ENGINE //temp physx code to make moving out of engine easier

#include "PhysXSupportCore.h"

FPhysicsMaterialHandle FChaosEngineInterface::CreateMaterial(const UPhysicalMaterial* InMaterial)
{
	check(GPhysXSDK);

	FPhysicsMaterialHandle_PhysX NewRef;

	const float Friction = InMaterial->Friction;
	const float Restitution = InMaterial->Restitution;

	NewRef.Material = GPhysXSDK->createMaterial(Friction,Friction,Restitution);

	return NewRef;
}

void FChaosEngineInterface::ReleaseMaterial(FPhysicsMaterialHandle_PhysX& InHandle)
{
	if(InHandle.IsValid())
	{
		InHandle.Material->userData = nullptr;
		GPhysXPendingKillMaterial.Add(InHandle.Material);
		InHandle.Material = nullptr;
	}
}

void FChaosEngineInterface::UpdateMaterial(FPhysicsMaterialHandle_PhysX& InHandle,UPhysicalMaterial* InMaterial)
{
	if(InHandle.IsValid())
	{
		PxMaterial* PMaterial = InHandle.Material;

		PMaterial->setStaticFriction(InMaterial->Friction);
		PMaterial->setDynamicFriction(InMaterial->Friction);
		PMaterial->setRestitution(InMaterial->Restitution);

		const uint32 UseFrictionCombineMode = (InMaterial->bOverrideFrictionCombineMode ? InMaterial->FrictionCombineMode.GetValue() : UPhysicsSettingsCore::Get()->FrictionCombineMode.GetValue());
		PMaterial->setFrictionCombineMode(static_cast<physx::PxCombineMode::Enum>(UseFrictionCombineMode));

		const uint32 UseRestitutionCombineMode = (InMaterial->bOverrideRestitutionCombineMode ? InMaterial->RestitutionCombineMode.GetValue() : UPhysicsSettingsCore::Get()->RestitutionCombineMode.GetValue());
		PMaterial->setRestitutionCombineMode(static_cast<physx::PxCombineMode::Enum>(UseRestitutionCombineMode));

		FPhysicsDelegatesCore::OnUpdatePhysXMaterial.Broadcast(InMaterial);
	}
}

void FChaosEngineInterface::SetUserData(FPhysicsMaterialHandle_PhysX& InHandle,void* InUserData)
{
	if(InHandle.IsValid())
	{
		InHandle.Material->userData = InUserData;
	}
}

#endif