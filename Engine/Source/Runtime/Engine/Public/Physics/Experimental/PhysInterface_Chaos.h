// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS

#include "Engine/Engine.h"
#include "Physics/PhysicsInterfaceDeclares.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsEngine/ConstraintDrives.h"
#include "PhysicsEngine/ConstraintTypes.h"
#include "PhysicsPublic.h"
#include "PhysicsReplication.h"
#include "Chaos/ArrayCollection.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Pair.h"
#include "Chaos/Transform.h"
#include "Physics/GenericPhysicsInterface.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "ChaosInterfaceWrapper.h"

//NOTE: Do not include Chaos headers directly as it means recompiling all of engine. This should be reworked to avoid allocations

static int32 NextBodyIdValue = 0;
static int32 NextConstraintIdValue = 0;

class FPhysInterface_Chaos;
struct FBodyInstance;
struct FPhysxUserData;
class IPhysicsReplicationFactory;

class FBodyInstancePhysicsObject;

class AWorldSettings;

namespace Chaos
{
	template <typename T, int>
	class TBVHParticles;

	template <typename T, int>
	class TImplicitObject;

	template <typename T, int>
	class TPBDRigidParticles;

	template <typename T, int>
	class PerParticleGravity;

	template <typename T, int>
	class TPBDSpringConstraints;
}

class FPhysicsActorReference_Chaos 
{
public:

	FPhysicsActorReference_Chaos( 
		FBodyInstance * InBodyInstance = nullptr,
		FPhysScene_ChaosInterface* InScene = nullptr, 
		FBodyInstancePhysicsObject* InPhysicsObject = nullptr)
		: BodyInstance(InBodyInstance)
		, Scene(InScene)
		, PhysicsObject(InPhysicsObject){}

	bool IsValid() const { return PhysicsObject != nullptr; }
	bool Equals(const FPhysicsActorReference_Chaos& Handle) const { return PhysicsObject == Handle.PhysicsObject; }

	FPhysScene_ChaosInterface* GetScene() { return Scene; }
	const FPhysScene_ChaosInterface* GetScene() const { return Scene; }
	void SetScene(FPhysScene_ChaosInterface* InScene) { Scene = InScene; }

	FBodyInstancePhysicsObject* GetPhysicsObject() { return PhysicsObject; }
	/*const*/ FBodyInstancePhysicsObject* GetPhysicsObject() const { return PhysicsObject; }
	void SetPhysicsObject(FBodyInstancePhysicsObject* InPhysicsObject) { PhysicsObject = InPhysicsObject; }

	FBodyInstance* GetBodyInstance() { return BodyInstance; }
	const FBodyInstance* GetBodyInstance() const { return BodyInstance; }
	void SetBodyInstance(FBodyInstance * InBodyInstance) { BodyInstance = InBodyInstance; }

private:
	FBodyInstance* BodyInstance;
	FPhysScene_ChaosInterface* Scene;
	FBodyInstancePhysicsObject* PhysicsObject;
};

class FPhysicsConstraintReference_Chaos
{
public:
	bool IsValid() const {return false;}
};

class FPhysicsAggregateReference_Chaos
{
public:
	bool IsValid() const { return false; }
};

class FPhysicsShapeReference_Chaos
{
public:
	typedef Chaos::TImplicitObject<float, 3> Internal;


	FPhysicsShapeReference_Chaos()
		: Object(nullptr), bSimulation(false), bQuery(false), ActorRef() {}
	FPhysicsShapeReference_Chaos(Internal* ObjectIn, bool bSimulationIn, bool bQueryIn, FPhysicsActorReference_Chaos ActorRefIn)
		: Object(ObjectIn),bSimulation(bSimulationIn),bQuery(bQueryIn),ActorRef(ActorRefIn){}
	FPhysicsShapeReference_Chaos(const FPhysicsShapeReference_Chaos& Other)
		: Object(Other.Object)
		, bSimulation(Other.bSimulation)
		, bQuery(Other.bQuery)
		, ActorRef(Other.ActorRef){}


	bool IsValid() const { return (Object != nullptr); }
	bool Equals(const FPhysicsShapeReference_Chaos& Other) const { return Object == Other.Object; }
    bool operator==(const FPhysicsShapeReference_Chaos& Other) const { return Equals(Other); }

	Internal* Object;
	bool bSimulation;
	bool bQuery;
    FPhysicsActorReference_Chaos ActorRef;
};

FORCEINLINE uint32 GetTypeHash(const FPhysicsShapeReference_Chaos& InShapeReference)
{
    return PointerHash(InShapeReference.Object);
}

// Temp interface
namespace physx
{
    class PxActor;
    class PxScene;
	class PxSimulationEventCallback;
    class PxGeometry;
    class PxTransform;
    class PxQuat;
	class PxMassProperties;
}
struct FContactModifyCallback;
class ULineBatchComponent;

class FPhysInterface_Chaos : public FGenericPhysicsInterface
{
public:
    ENGINE_API FPhysInterface_Chaos(const AWorldSettings* Settings=nullptr);
    ENGINE_API ~FPhysInterface_Chaos();

    // Interface needed for interface
	static ENGINE_API void CreateActor(const FActorCreationParams& InParams, FPhysicsActorHandle& Handle);
	static ENGINE_API void ReleaseActor(FPhysicsActorHandle& InActorReference, FPhysScene* InScene = nullptr, bool bNeverDeferRelease=false);

	static ENGINE_API FPhysicsAggregateReference_Chaos CreateAggregate(int32 MaxBodies);
	static ENGINE_API void ReleaseAggregate(FPhysicsAggregateReference_Chaos& InAggregate);
	static ENGINE_API int32 GetNumActorsInAggregate(const FPhysicsAggregateReference_Chaos& InAggregate);
	static ENGINE_API void AddActorToAggregate_AssumesLocked(const FPhysicsAggregateReference_Chaos& InAggregate, const FPhysicsActorReference_Chaos& InActor);

	// Material interface functions
    // @todo(mlentine): How do we set material on the solver?
	static FPhysicsMaterialHandle CreateMaterial(const UPhysicalMaterial* InMaterial) { return nullptr; }
    static void ReleaseMaterial(FPhysicsMaterialHandle& InHandle) {}
    static void UpdateMaterial(const FPhysicsMaterialHandle& InHandle, UPhysicalMaterial* InMaterial) {}
    static void SetUserData(const FPhysicsMaterialHandle& InHandle, void* InUserData) {}

	// Actor interface functions
	template<typename AllocatorType>
	static ENGINE_API int32 GetAllShapes_AssumedLocked(const FPhysicsActorReference_Chaos& InActorReference, TArray<FPhysicsShapeHandle, AllocatorType>& OutShapes);
	static int32 GetNumShapes(const FPhysicsActorHandle& InHandle);

	static void ReleaseShape(const FPhysicsShapeHandle& InShape);

	static void AttachShape(const FPhysicsActorHandle& InActor, const FPhysicsShapeHandle& InNewShape);
	static void DetachShape(const FPhysicsActorHandle& InActor, FPhysicsShapeHandle& InShape, bool bWakeTouching = true);

	static ENGINE_API void SetActorUserData_AssumesLocked(FPhysicsActorReference_Chaos& InActorReference, FPhysxUserData* InUserData);

	static ENGINE_API bool IsRigidBody(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API bool IsDynamic(const FPhysicsActorReference_Chaos& InActorReference)
    {
        return !IsStatic(InActorReference);
    }
    static ENGINE_API bool IsStatic(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API bool IsKinematic_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API bool IsSleeping(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API bool IsCcdEnabled(const FPhysicsActorReference_Chaos& InActorReference);
    // @todo(mlentine): We don't have a notion of sync vs async and are a bit of both. Does this work?
    static bool HasSyncSceneData(const FPhysicsActorReference_Chaos& InHandle) { return true; }
    static bool HasAsyncSceneData(const FPhysicsActorReference_Chaos& InHandle) { return false; }
	static ENGINE_API bool IsInScene(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API bool CanSimulate_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API float GetMass_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);

	static ENGINE_API void SetSendsSleepNotifies_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bSendSleepNotifies);
	static ENGINE_API void PutToSleep_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void WakeUp_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);

	static ENGINE_API void SetIsKinematic_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bIsKinematic);
	static ENGINE_API void SetCcdEnabled_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bIsCcdEnabled);

	static ENGINE_API FTransform GetGlobalPose_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetGlobalPose_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FTransform& InNewPose, bool bAutoWake = true);

    static ENGINE_API FTransform GetTransform_AssumesLocked(const FPhysicsActorHandle& InRef, bool bForceGlobalPose = false);

	static ENGINE_API bool HasKinematicTarget_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API FTransform GetKinematicTarget_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetKinematicTarget_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FTransform& InNewTarget);

	static ENGINE_API FVector GetLinearVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetLinearVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);

	static ENGINE_API FVector GetAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);
	static ENGINE_API float GetMaxAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetMaxAngularVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InMaxAngularVelocity);

	static ENGINE_API float GetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InMaxDepenetrationVelocity);

	static ENGINE_API FVector GetWorldVelocityAtPoint_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InPoint);

	static ENGINE_API FTransform GetComTransform_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API FTransform GetComTransformLocal_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);

	static ENGINE_API FVector GetLocalInertiaTensor_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API FBox GetBounds_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);

	static ENGINE_API void SetLinearDamping_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InDamping);
	static ENGINE_API void SetAngularDamping_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InDamping);

	static ENGINE_API void AddImpulse_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InForce);
	static ENGINE_API void AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InTorque);
	static ENGINE_API void AddVelocity_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InForce);
	static ENGINE_API void AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InTorque);
	static ENGINE_API void AddImpulseAtLocation_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InImpulse, const FVector& InLocation);
	static ENGINE_API void AddRadialImpulse_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, const FVector& InOrigin, float InRadius, float InStrength, ERadialImpulseFalloff InFalloff, bool bInVelChange);

	static ENGINE_API bool IsGravityEnabled_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetGravityEnabled_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, bool bEnabled);

	static ENGINE_API float GetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference);
	static ENGINE_API void SetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InActorReference, float InEnergyThreshold);

	static void SetMass_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, float InMass);
	static void SetMassSpaceInertiaTensor_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, const FVector& InTensor);
	static void SetComLocalPose_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, const FTransform& InComLocalPose);

	static ENGINE_API float GetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle);
	static ENGINE_API void SetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, float InThreshold);
	static ENGINE_API uint32 GetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle);
	static ENGINE_API void SetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, uint32 InSolverIterationCount);
	static ENGINE_API uint32 GetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle);
	static ENGINE_API void SetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, uint32 InSolverIterationCount);
	static ENGINE_API float GetWakeCounter_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle);
	static ENGINE_API void SetWakeCounter_AssumesLocked(const FPhysicsActorReference_Chaos& InHandle, float InWakeCounter);

	static ENGINE_API SIZE_T GetResourceSizeEx(const FPhysicsActorReference_Chaos& InActorRef);
	
    static ENGINE_API FPhysicsConstraintReference_Chaos CreateConstraint(const FPhysicsActorReference_Chaos& InActorRef1, const FPhysicsActorReference_Chaos& InActorRef2, const FTransform& InLocalFrame1, const FTransform& InLocalFrame2);
	static ENGINE_API void SetConstraintUserData(const FPhysicsConstraintReference_Chaos& InConstraintRef, void* InUserData);
	static ENGINE_API void ReleaseConstraint(FPhysicsConstraintReference_Chaos& InConstraintRef);

	static ENGINE_API FTransform GetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame);
	static ENGINE_API FTransform GetGlobalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame);
	static ENGINE_API FVector GetLocation(const FPhysicsConstraintReference_Chaos& InConstraintRef);
	static ENGINE_API void GetForce(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinForce, FVector& OutAngForce);
	static ENGINE_API void GetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinVelocity);
	static ENGINE_API void GetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutAngVelocity);

	static ENGINE_API float GetCurrentSwing1(const FPhysicsConstraintReference_Chaos& InConstraintRef);
	static ENGINE_API float GetCurrentSwing2(const FPhysicsConstraintReference_Chaos& InConstraintRef);
	static ENGINE_API float GetCurrentTwist(const FPhysicsConstraintReference_Chaos& InConstraintRef);

	static ENGINE_API void SetCanVisualize(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCanVisualize);
	static ENGINE_API void SetCollisionEnabled(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCollisionEnabled);
	static ENGINE_API void SetProjectionEnabled_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInProjectionEnabled, float InLinearTolerance = 0.0f, float InAngularToleranceDegrees = 0.0f);
	static ENGINE_API void SetParentDominates_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInParentDominates);
	static ENGINE_API void SetBreakForces_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLinearBreakForce, float InAngularBreakForce);
	static ENGINE_API void SetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FTransform& InPose, EConstraintFrame::Type InFrame);

	static ENGINE_API void SetLinearMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, ELinearConstraintMotion InMotion);
	static ENGINE_API void SetAngularMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, EAngularConstraintMotion InMotion);

	static ENGINE_API void UpdateLinearLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit, float InAverageMass, const FLinearConstraint& InParams);
	static ENGINE_API void UpdateConeLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FConeConstraint& InParams);
	static ENGINE_API void UpdateTwistLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FTwistConstraint& InParams);
	static ENGINE_API void UpdateLinearDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InDriveParams);
	static ENGINE_API void UpdateAngularDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FAngularDriveConstraint& InDriveParams);
	static ENGINE_API void UpdateDriveTarget_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InLinDrive, const FAngularDriveConstraint& InAngDrive);
	static ENGINE_API void SetDrivePosition(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InPosition);
	static ENGINE_API void SetDriveOrientation(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FQuat& InOrientation);
	static ENGINE_API void SetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InLinVelocity);
	static ENGINE_API void SetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InAngVelocity);

	static ENGINE_API void SetTwistLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLowerLimit, float InUpperLimit, float InContactDistance);
	static ENGINE_API void SetSwingLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InYLimit, float InZLimit, float InContactDistance);
	static ENGINE_API void SetLinearLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit);

	static ENGINE_API bool IsBroken(const FPhysicsConstraintReference_Chaos& InConstraintRef);

	static ENGINE_API bool ExecuteOnUnbrokenConstraintReadOnly(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func);
	static ENGINE_API bool ExecuteOnUnbrokenConstraintReadWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func);

    /////////////////////////////////////////////

    // Interface needed for cmd
    static ENGINE_API bool ExecuteRead(const FPhysicsActorReference_Chaos& InActorReference, TFunctionRef<void(const FPhysicsActorReference_Chaos& Actor)> InCallable)
    {
		if(InActorReference.IsValid())
		{
			InCallable(InActorReference);
			return true;
		}

		return false;
    }

    static ENGINE_API bool ExecuteRead(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
    {
        InCallable();
        return true;
    }

    static ENGINE_API bool ExecuteRead(const FPhysicsActorReference_Chaos& InActorReferenceA, const FPhysicsActorReference_Chaos& InActorReferenceB, TFunctionRef<void(const FPhysicsActorReference_Chaos& ActorA, const FPhysicsActorReference_Chaos& ActorB)> InCallable)
    {
		if(InActorReferenceA.IsValid() || InActorReferenceB.IsValid())
		{
			InCallable(InActorReferenceA, InActorReferenceB);
			return true;
		}

		return false;
    }

    static ENGINE_API bool ExecuteRead(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos& Constraint)> InCallable)
    {
		if(InConstraintRef.IsValid())
		{
			InCallable(InConstraintRef);
			return true;
		}

		return false;
    }

    static ENGINE_API bool ExecuteRead(FPhysScene* InScene, TFunctionRef<void()> InCallable)
    {
		if(InScene)
		{
			InCallable();
			return true;
		}

		return false;
    }

    static ENGINE_API bool ExecuteWrite(const FPhysicsActorReference_Chaos& InActorReference, TFunctionRef<void(const FPhysicsActorReference_Chaos& Actor)> InCallable)
    {
		if(InActorReference.IsValid())
		{
			InCallable(InActorReference);
			return true;
		}

		return false;
    }

    static ENGINE_API bool ExecuteWrite(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
    {
        InCallable();
        return true;
    }

    static ENGINE_API bool ExecuteWrite(const FPhysicsActorReference_Chaos& InActorReferenceA, const FPhysicsActorReference_Chaos& InActorReferenceB, TFunctionRef<void(const FPhysicsActorReference_Chaos& ActorA, const FPhysicsActorReference_Chaos& ActorB)> InCallable)
    {
		if(InActorReferenceA.IsValid() || InActorReferenceB.IsValid())
		{
			InCallable(InActorReferenceA, InActorReferenceB);
			return true;
		}

		return false;
    }

    static ENGINE_API bool ExecuteWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos& Constraint)> InCallable)
    {
		if(InConstraintRef.IsValid())
		{
			InCallable(InConstraintRef);
			return true;
		}

		return false;
    }
	
    static ENGINE_API bool ExecuteWrite(FPhysScene* InScene, TFunctionRef<void()> InCallable)
    {
		if(InScene)
		{
			InCallable();
			return true;
		}

		return false;
    }

    static void ExecuteShapeWrite(FBodyInstance* InInstance, FPhysicsShapeHandle& InShape, TFunctionRef<void(FPhysicsShapeHandle& InShape)> InCallable)
    {
		if(InInstance && InShape.IsValid())
        {
			InCallable(InShape);
		}
    }

	// Scene query interface functions

	static ENGINE_API bool RaycastTest(const UWorld* World, const FVector Start, const FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
    static ENGINE_API bool RaycastSingle(const UWorld* World, struct FHitResult& OutHit, const FVector Start, const FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
    static ENGINE_API bool RaycastMulti(const UWorld* World, TArray<struct FHitResult>& OutHits, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }

    static ENGINE_API bool GeomOverlapBlockingTest(const UWorld* World, const FCollisionShape& CollisionShape, const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
    static ENGINE_API bool GeomOverlapAnyTest(const UWorld* World, const FCollisionShape& CollisionShape, const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
    static ENGINE_API bool GeomOverlapMulti(const UWorld* World, const FCollisionShape& CollisionShape, const FVector& Pos, const FQuat& Rot, TArray<FOverlapResult>& OutOverlaps, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }

	// GEOM SWEEP

    static ENGINE_API bool GeomSweepTest(const UWorld* World, const FCollisionShape& CollisionShape, const FQuat& Rot, FVector Start, FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
    static ENGINE_API bool GeomSweepSingle(const UWorld* World, const FCollisionShape& CollisionShape, const FQuat& Rot, FHitResult& OutHit, FVector Start, FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
    static ENGINE_API bool GeomSweepMulti(const UWorld* World, const FCollisionShape& CollisionShape, const FQuat& Rot, TArray<FHitResult>& OutHits, FVector Start, FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }

	template<typename GeomType>
    static bool GeomSweepMulti(const UWorld* World, const GeomType& InGeom, const FQuat& InGeomRot, TArray<FHitResult>& OutHits, FVector Start, FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams = FCollisionObjectQueryParams::DefaultObjectQueryParam)
    {
        return false;
    }
	template<typename GeomType>
    static bool GeomOverlapMulti(const UWorld* World, const GeomType& InGeom, const FVector& InPosition, const FQuat& InRotation, TArray<FOverlapResult>& OutOverlaps, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams)
    {
        return false;
    }

	// Misc

	static ENGINE_API bool ExecPhysCommands(const TCHAR* Cmd, FOutputDevice* Ar, UWorld* InWorld);

	static ENGINE_API FPhysScene* GetCurrentScene(FPhysicsActorHandle& InActorReference)
	{
		return InActorReference.GetScene();
	}

	static ENGINE_API const FPhysScene* GetCurrentScene(const FPhysicsActorHandle& InActorReference)
	{
		return InActorReference.GetScene();
	}

#if WITH_PHYSX
    static void CalculateMassPropertiesFromShapeCollection(physx::PxMassProperties& OutProperties, const TArray<FPhysicsShapeHandle>& InShapes, float InDensityKGPerCM);
#endif

	// Shape interface functions
	static FPhysicsShapeHandle CreateShape(physx::PxGeometry* InGeom, bool bSimulation = true, bool bQuery = true, UPhysicalMaterial* InSimpleMaterial = nullptr, TArray<UPhysicalMaterial*>* InComplexMaterials = nullptr);
	
	static void ENGINE_API AddGeometry(FPhysicsActorHandle& InActor, const FGeometryAddParams& InParams, TArray<FPhysicsShapeHandle>* OutOptShapes = nullptr);
	static FPhysicsShapeHandle CloneShape(const FPhysicsShapeHandle& InShape);

	static ENGINE_API FCollisionFilterData GetSimulationFilter(const FPhysicsShapeHandle& InShape);
	static ENGINE_API FCollisionFilterData GetQueryFilter(const FPhysicsShapeHandle& InShape);
	static bool IsSimulationShape(const FPhysicsShapeHandle& InShape);
	static bool IsQueryShape(const FPhysicsShapeHandle& InShape);
	static bool IsShapeType(const FPhysicsShapeHandle& InShape, ECollisionShapeType InType);
	static ENGINE_API ECollisionShapeType GetShapeType(const FPhysicsShapeHandle& InShape);
	static FTransform GetLocalTransform(const FPhysicsShapeHandle& InShape);
    static void* GetUserData(const FPhysicsShapeHandle& InShape) { return nullptr; }

	// Trace functions for testing specific geometry (not against a world)
	static bool LineTrace_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& InStart, const FVector& InEnd, bool bTraceComplex, bool bExtractPhysMaterial = false);
	static bool Sweep_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& InStart, const FVector& InEnd, const FQuat& InShapeRotation, const FCollisionShape& InShape, bool bSweepComplex);
	static bool Overlap_Geom(const FBodyInstance* InBodyInstance, const FPhysicsGeometryCollection& InGeometry, const FTransform& InShapeTransform, FMTDResult* OutOptResult = nullptr);
	static bool Overlap_Geom(const FBodyInstance* InBodyInstance, const FCollisionShape& InCollisionShape, const FQuat& InShapeRotation, const FTransform& InShapeTransform, FMTDResult* OutOptResult = nullptr);
	static bool GetSquaredDistanceToBody(const FBodyInstance* InInstance, const FVector& InPoint, float& OutDistanceSquared, FVector* OutOptPointOnBody = nullptr);

    // @todo(mlentine): Which of these do we need to support?
	// Set the mask filter of a shape, which is an extra level of filtering during collision detection / query for extra channels like "Blue Team" and "Red Team"
    static void SetMaskFilter(const FPhysicsShapeHandle& InShape, FMaskFilter InFilter) {}
    static void SetSimulationFilter(const FPhysicsShapeHandle& InShape, const FCollisionFilterData& InFilter) {}
    static void SetQueryFilter(const FPhysicsShapeHandle& InShape, const FCollisionFilterData& InFilter) {}
    static void SetIsSimulationShape(const FPhysicsShapeHandle& InShape, bool bIsSimShape) { const_cast<FPhysicsShapeHandle&>(InShape).bSimulation = bIsSimShape; }
    static void SetIsQueryShape(const FPhysicsShapeHandle& InShape, bool bIsQueryShape) { const_cast<FPhysicsShapeHandle&>(InShape).bSimulation = bIsQueryShape; }
    static void SetUserData(const FPhysicsShapeHandle& InShape, void* InUserData) {}
    static void SetGeometry(const FPhysicsShapeHandle& InShape, physx::PxGeometry& InGeom) {}
	static void SetLocalTransform(const FPhysicsShapeHandle& InShape, const FTransform& NewLocalTransform);
    static void SetMaterials(const FPhysicsShapeHandle& InShape, const TArrayView<UPhysicalMaterial*>InMaterials) {}
};

/*
FORCEINLINE ECollisionShapeType GetType(const Chaos::TImplicitObject<float, 3>& Geom)
{
	if (Geom.GetType() == Chaos::ImplicitObjectType::Box)
	{
		return ECollisionShapeType::Box;
	}
	if (Geom.GetType() == Chaos::ImplicitObjectType::Sphere)
	{
		return ECollisionShapeType::Sphere;
	}
	if (Geom.GetType() == Chaos::ImplicitObjectType::Plane)
	{
		return ECollisionShapeType::Plane;
	}
	return ECollisionShapeType::None;
}
*/
FORCEINLINE ECollisionShapeType GetGeometryType(const Chaos::TImplicitObject<float, 3>& Geom)
{
	return GetType(Geom);
}

/*
FORCEINLINE float GetRadius(const Chaos::TCapsule<float>& Capsule)
{
	return Capsule.GetRadius();
}

FORCEINLINE float GetHalfHeight(const Chaos::TCapsule<float>& Capsule)
{
	return Capsule.GetHeight() / 2.f;
}
*/

FORCEINLINE FVector FindBoxOpposingNormal(const FPhysTypeDummy& PHit, const FVector& TraceDirectionDenorm, const FVector InNormal)
{
	return FVector(0.0f, 0.0f, 1.0f);
}

FORCEINLINE FVector FindHeightFieldOpposingNormal(const FPhysTypeDummy& PHit, const FVector& TraceDirectionDenorm, const FVector InNormal)
{
	return FVector(0.0f, 0.0f, 1.0f);
}

FORCEINLINE FVector FindConvexMeshOpposingNormal(const FPhysTypeDummy& PHit, const FVector& TraceDirectionDenorm, const FVector InNormal)
{
	return FVector(0.0f, 0.0f, 1.0f);
}

FORCEINLINE FVector FindTriMeshOpposingNormal(const FPhysTypeDummy& PHit, const FVector& TraceDirectionDenorm, const FVector InNormal)
{
	return FVector(0.0f, 0.0f, 1.0f);
}

FORCEINLINE void DrawOverlappingTris(const UWorld* World, const FPhysTypeDummy& Hit, const Chaos::TImplicitObject<float, 3>& Geom, const FTransform& QueryTM)
{

}

FORCEINLINE void ComputeZeroDistanceImpactNormalAndPenetration(const UWorld* World, const FPhysTypeDummy& Hit, const Chaos::TImplicitObject<float, 3>& Geom, const FTransform& QueryTM, FHitResult& OutResult)
{

}

inline FPhysTypeDummy* GetMaterialFromInternalFaceIndex(const FPhysicsShape& Shape, uint32 InternalFaceIndex)
{
	return nullptr;
}

inline FCollisionFilterData GetSimulationFilterData(const FPhysicsShape& Shape)
{
	return FCollisionFilterData();
}

inline uint32 GetTriangleMeshExternalFaceIndex(const FPhysicsShape& Shape, uint32 InternalFaceIndex)
{
	return GetInvalidPhysicsFaceIndex();
}

inline void GetShapes(const FPhysActorDummy& RigidActor, FPhysTypeDummy** ShapesBuffer, uint32 NumShapes)
{
	
}

inline void SetShape(FPhysTypeDummy& Hit, FPhysTypeDummy* Shape)
{

}

bool IsBlocking(const FPhysicsShape& PShape, const FCollisionFilterData& QueryFilter);

template <>
ENGINE_API int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorReference_Chaos& InActorHandle, TArray<FPhysicsShapeReference_Chaos, FDefaultAllocator>& OutShapes);
template <>
ENGINE_API int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorReference_Chaos& InActorHandle, PhysicsInterfaceTypes::FInlineShapeArray& OutShapes);

#endif
