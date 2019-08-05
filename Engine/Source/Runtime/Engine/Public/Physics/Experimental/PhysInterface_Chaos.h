// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS

#include "ChaosInterfaceWrapper.h"
#include "Engine/Engine.h"
#include "Chaos/ArrayCollection.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Declares.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Pair.h"
#include "Chaos/Transform.h"
#include "PhysicsEngine/ConstraintDrives.h"
#include "PhysicsEngine/ConstraintTypes.h"
#include "PhysicsPublic.h"
#include "PhysicsReplication.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "Physics/PhysicsInterfaceDeclares.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "Physics/GenericPhysicsInterface.h"
#include "Physics/Experimental/PhysicsUserData_Chaos.h"

//NOTE: Do not include Chaos headers directly as it means recompiling all of engine. This should be reworked to avoid allocations

static int32 NextBodyIdValue = 0;
static int32 NextConstraintIdValue = 0;

class FPhysInterface_Chaos;
struct FBodyInstance;
struct FPhysxUserData;
class IPhysicsReplicationFactory;

class AWorldSettings;

namespace Chaos
{
	class IDispatcher;

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

	template <typename T, int>
	class TConvex;
}

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
	
	FPhysicsShapeReference_Chaos()
		: Shape(nullptr), bSimulation(false), bQuery(false), ActorRef() {}
	FPhysicsShapeReference_Chaos(Chaos::TPerShapeData<float, 3>* ShapeIn, bool bSimulationIn, bool bQueryIn, FPhysicsActorHandle ActorRefIn)
		: Shape(ShapeIn),bSimulation(bSimulationIn),bQuery(bQueryIn),ActorRef(ActorRefIn){}
	FPhysicsShapeReference_Chaos(const FPhysicsShapeReference_Chaos& Other)
		: Shape(Other.Shape)
		, bSimulation(Other.bSimulation)
		, bQuery(Other.bQuery)
		, ActorRef(Other.ActorRef){}


	bool IsValid() const { return (Shape != nullptr); }
	bool Equals(const FPhysicsShapeReference_Chaos& Other) const { return Shape == Other.Shape; }
    bool operator==(const FPhysicsShapeReference_Chaos& Other) const { return Equals(Other); }
	
	const Chaos::TImplicitObject<float, 3>& GetGeometry() const { check(IsValid()); return *Shape->Geometry; }


	Chaos::TPerShapeData<float, 3>* Shape;
	bool bSimulation;
	bool bQuery;
    FPhysicsActorHandle ActorRef;
};

class FPhysicsShapeAdapter_Chaos
{
public:
	FPhysicsShapeAdapter_Chaos(const FQuat& Rot, const FCollisionShape& CollisionShape);

	const FPhysicsGeometry& GetGeometry() const;
	FTransform GetGeometryPose(const FVector& Pos) const;
	const FQuat& GetGeomOrientation() const;

private:
	TUniquePtr<FPhysicsGeometry> Geometry;
	FQuat GeometryRotation;
};

FORCEINLINE uint32 GetTypeHash(const FPhysicsShapeReference_Chaos& InShapeReference)
{
    return PointerHash(InShapeReference.Shape);
}

/**
 Wrapper around geometry. This is really just needed to make the physx chaos abstraction easier
 */
struct ENGINE_API FPhysicsGeometryCollection_Chaos
{
	// Delete default constructor, want only construction by interface (private constructor below)
	FPhysicsGeometryCollection_Chaos() = delete;
	// No copying or assignment, move construction only, these are defaulted in the source file as they need
	// to be able to delete physx::PxGeometryHolder which is incomplete here
	FPhysicsGeometryCollection_Chaos(const FPhysicsGeometryCollection_Chaos& Copy) = delete;
	FPhysicsGeometryCollection_Chaos& operator=(const FPhysicsGeometryCollection_Chaos& Copy) = delete;
	FPhysicsGeometryCollection_Chaos(FPhysicsGeometryCollection_Chaos&& Steal);
	FPhysicsGeometryCollection_Chaos& operator=(FPhysicsGeometryCollection_Chaos&& Steal) = delete;
	~FPhysicsGeometryCollection_Chaos();

	ECollisionShapeType GetType() const;
	const Chaos::TImplicitObject<float, 3>& GetGeometry() const;
	const Chaos::TBox<float, 3>& GetBoxGeometry() const;
	const Chaos::TSphere<float, 3>&  GetSphereGeometry() const;
	const Chaos::TCapsule<float>&  GetCapsuleGeometry() const;
	const Chaos::TConvex<float, 3>& GetConvexGeometry() const;
	const Chaos::TTriangleMeshImplicitObject<float>& GetTriMeshGeometry() const;

private:
	friend class FPhysInterface_Chaos;
	explicit FPhysicsGeometryCollection_Chaos(const FPhysicsShapeReference_Chaos& InShape);

	const Chaos::TImplicitObject<float, 3>& Geom;
};


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

class ENGINE_API FPhysInterface_Chaos : public FGenericPhysicsInterface
{
public:
    FPhysInterface_Chaos(const AWorldSettings* Settings=nullptr);
    ~FPhysInterface_Chaos();

    // Interface needed for interface
	static void CreateActor(const FActorCreationParams& InParams, FPhysicsActorHandle& Handle);
	static void ReleaseActor(FPhysicsActorHandle& InActorReference, FPhysScene* InScene = nullptr, bool bNeverDeferRelease=false);
	static bool IsValid(const FPhysicsActorHandle& Handle) { return Handle != nullptr; }
	static void AddActorToSolver(FPhysicsActorHandle& Handle, Chaos::FPhysicsSolver* Solver, Chaos::IDispatcher* Dispatcher);
	static void RemoveActorFromSolver(FPhysicsActorHandle& Handle, Chaos::FPhysicsSolver* Solver, Chaos::IDispatcher* Dispatcher);
	static const FBodyInstance* ShapeToOriginalBodyInstance(const FBodyInstance* InCurrentInstance, const Chaos::TPerShapeData<float, 3>* InShape);

	static FPhysicsAggregateReference_Chaos CreateAggregate(int32 MaxBodies);
	static void ReleaseAggregate(FPhysicsAggregateReference_Chaos& InAggregate);
	static int32 GetNumActorsInAggregate(const FPhysicsAggregateReference_Chaos& InAggregate);
	static void AddActorToAggregate_AssumesLocked(const FPhysicsAggregateReference_Chaos& InAggregate, const FPhysicsActorHandle& InActor);

	// Material interface functions
    // @todo(mlentine): How do we set material on the solver?
	static FPhysicsMaterialHandle CreateMaterial(const UPhysicalMaterial* InMaterial) { return nullptr; }
    static void ReleaseMaterial(FPhysicsMaterialHandle& InHandle) {}
    static void UpdateMaterial(const FPhysicsMaterialHandle& InHandle, UPhysicalMaterial* InMaterial) {}
    static void SetUserData(const FPhysicsMaterialHandle& InHandle, void* InUserData) {}

	// Actor interface functions
	template<typename AllocatorType>
	static int32 GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorReference, TArray<FPhysicsShapeHandle, AllocatorType>& OutShapes);
	static int32 GetNumShapes(const FPhysicsActorHandle& InHandle);

	static void ReleaseShape(const FPhysicsShapeHandle& InShape);

	static void AttachShape(const FPhysicsActorHandle& InActor, const FPhysicsShapeHandle& InNewShape);
	static void DetachShape(const FPhysicsActorHandle& InActor, FPhysicsShapeHandle& InShape, bool bWakeTouching = true);

	static void SetActorUserData_AssumesLocked(FPhysicsActorHandle& InActorReference, FPhysicsUserData* InUserData);

	static bool IsRigidBody(const FPhysicsActorHandle& InActorReference);
	static bool IsDynamic(const FPhysicsActorHandle& InActorReference)
    {
        return !IsStatic(InActorReference);
    }
    static bool IsStatic(const FPhysicsActorHandle& InActorReference);
    static bool IsKinematic(const FPhysicsActorHandle& InActorReference);
	static bool IsKinematic_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static bool IsSleeping(const FPhysicsActorHandle& InActorReference);
	static bool IsCcdEnabled(const FPhysicsActorHandle& InActorReference);
    // @todo(mlentine): We don't have a notion of sync vs async and are a bit of both. Does this work?
    static bool HasSyncSceneData(const FPhysicsActorHandle& InHandle) { return true; }
    static bool HasAsyncSceneData(const FPhysicsActorHandle& InHandle) { return false; }
	static bool IsInScene(const FPhysicsActorHandle& InActorReference);
	static FPhysScene* GetCurrentScene(const FPhysicsActorHandle& InHandle);
	static bool CanSimulate_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static float GetMass_AssumesLocked(const FPhysicsActorHandle& InActorReference);

	static void SetSendsSleepNotifies_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bSendSleepNotifies);
	static void PutToSleep_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void WakeUp_AssumesLocked(const FPhysicsActorHandle& InActorReference);

	static void SetIsKinematic_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bIsKinematic);
	static void SetCcdEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bIsCcdEnabled);

	static FTransform GetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewPose, bool bAutoWake = true);

    static FTransform GetTransform_AssumesLocked(const FPhysicsActorHandle& InRef, bool bForceGlobalPose = false);

	static bool HasKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static FTransform GetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewTarget);

	static FVector GetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);

	static FVector GetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);
	static float GetMaxAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetMaxAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InMaxAngularVelocity);

	static float GetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InMaxDepenetrationVelocity);

	static FVector GetWorldVelocityAtPoint_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InPoint);

	static FTransform GetComTransform_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static FTransform GetComTransformLocal_AssumesLocked(const FPhysicsActorHandle& InActorReference);

	static FVector GetLocalInertiaTensor_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static FBox GetBounds_AssumesLocked(const FPhysicsActorHandle& InActorReference);

	static void SetLinearDamping_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InDamping);
	static void SetAngularDamping_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InDamping);

	static void AddImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce);
	static void AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque);
	static void AddVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce);
	static void AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque);
	static void AddImpulseAtLocation_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InImpulse, const FVector& InLocation);
	static void AddRadialImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InOrigin, float InRadius, float InStrength, ERadialImpulseFalloff InFalloff, bool bInVelChange);

	static bool IsGravityEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetGravityEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bEnabled);

	static float GetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InActorReference);
	static void SetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InEnergyThreshold);

	static void SetMass_AssumesLocked(const FPhysicsActorHandle& InHandle, float InMass);
	static void SetMassSpaceInertiaTensor_AssumesLocked(const FPhysicsActorHandle& InHandle, const FVector& InTensor);
	static void SetComLocalPose_AssumesLocked(const FPhysicsActorHandle& InHandle, const FTransform& InComLocalPose);

	static float GetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InHandle);
	static void SetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InHandle, float InThreshold);
	static uint32 GetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle);
	static void SetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle, uint32 InSolverIterationCount);
	static uint32 GetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle);
	static void SetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle, uint32 InSolverIterationCount);
	static float GetWakeCounter_AssumesLocked(const FPhysicsActorHandle& InHandle);
	static void SetWakeCounter_AssumesLocked(const FPhysicsActorHandle& InHandle, float InWakeCounter);

	static SIZE_T GetResourceSizeEx(const FPhysicsActorHandle& InActorRef);
	
    static FPhysicsConstraintReference_Chaos CreateConstraint(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2, const FTransform& InLocalFrame1, const FTransform& InLocalFrame2);
	static void SetConstraintUserData(const FPhysicsConstraintReference_Chaos& InConstraintRef, void* InUserData);
	static void ReleaseConstraint(FPhysicsConstraintReference_Chaos& InConstraintRef);

	static FTransform GetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame);
	static FTransform GetGlobalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame);
	static FVector GetLocation(const FPhysicsConstraintReference_Chaos& InConstraintRef);
	static void GetForce(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinForce, FVector& OutAngForce);
	static void GetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinVelocity);
	static void GetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutAngVelocity);

	static float GetCurrentSwing1(const FPhysicsConstraintReference_Chaos& InConstraintRef);
	static float GetCurrentSwing2(const FPhysicsConstraintReference_Chaos& InConstraintRef);
	static float GetCurrentTwist(const FPhysicsConstraintReference_Chaos& InConstraintRef);

	static void SetCanVisualize(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCanVisualize);
	static void SetCollisionEnabled(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCollisionEnabled);
	static void SetProjectionEnabled_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInProjectionEnabled, float InLinearTolerance = 0.0f, float InAngularToleranceDegrees = 0.0f);
	static void SetParentDominates_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInParentDominates);
	static void SetBreakForces_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLinearBreakForce, float InAngularBreakForce);
	static void SetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FTransform& InPose, EConstraintFrame::Type InFrame);

	static void SetLinearMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, ELinearConstraintMotion InMotion);
	static void SetAngularMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, EAngularConstraintMotion InMotion);

	static void UpdateLinearLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit, float InAverageMass, const FLinearConstraint& InParams);
	static void UpdateConeLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FConeConstraint& InParams);
	static void UpdateTwistLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FTwistConstraint& InParams);
	static void UpdateLinearDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InDriveParams);
	static void UpdateAngularDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FAngularDriveConstraint& InDriveParams);
	static void UpdateDriveTarget_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InLinDrive, const FAngularDriveConstraint& InAngDrive);
	static void SetDrivePosition(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InPosition);
	static void SetDriveOrientation(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FQuat& InOrientation);
	static void SetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InLinVelocity);
	static void SetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InAngVelocity);

	static void SetTwistLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLowerLimit, float InUpperLimit, float InContactDistance);
	static void SetSwingLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InYLimit, float InZLimit, float InContactDistance);
	static void SetLinearLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit);

	static bool IsBroken(const FPhysicsConstraintReference_Chaos& InConstraintRef);

	static bool ExecuteOnUnbrokenConstraintReadOnly(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func);
	static bool ExecuteOnUnbrokenConstraintReadWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func);

    /////////////////////////////////////////////

    // Interface needed for cmd
    static bool ExecuteRead(const FPhysicsActorHandle& InActorReference, TFunctionRef<void(const FPhysicsActorHandle& Actor)> InCallable)
    {
		InCallable(InActorReference);
		return true;
    }

    static bool ExecuteRead(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
    {
        InCallable();
        return true;
    }

    static bool ExecuteRead(const FPhysicsActorHandle& InActorReferenceA, const FPhysicsActorHandle& InActorReferenceB, TFunctionRef<void(const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)> InCallable)
    {
		InCallable(InActorReferenceA, InActorReferenceB);
		return true;
    }

    static bool ExecuteRead(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos& Constraint)> InCallable)
    {
		if(InConstraintRef.IsValid())
		{
			InCallable(InConstraintRef);
			return true;
		}

		return false;
    }

    static bool ExecuteRead(FPhysScene* InScene, TFunctionRef<void()> InCallable)
    {
		if(InScene)
		{
			InCallable();
			return true;
		}

		return false;
    }

    static bool ExecuteWrite(const FPhysicsActorHandle& InActorReference, TFunctionRef<void(const FPhysicsActorHandle& Actor)> InCallable)
    {
		InCallable(InActorReference);
		return true;
    }

    static bool ExecuteWrite(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
    {
        InCallable();
        return true;
    }

    static bool ExecuteWrite(const FPhysicsActorHandle& InActorReferenceA, const FPhysicsActorHandle& InActorReferenceB, TFunctionRef<void(const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)> InCallable)
    {
		InCallable(InActorReferenceA, InActorReferenceB);
		return true;
    }

    static bool ExecuteWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos& Constraint)> InCallable)
    {
		if(InConstraintRef.IsValid())
		{
			InCallable(InConstraintRef);
			return true;
		}

		return false;
    }
	
    static bool ExecuteWrite(FPhysScene* InScene, TFunctionRef<void()> InCallable)
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

	// Misc

	static bool ExecPhysCommands(const TCHAR* Cmd, FOutputDevice* Ar, UWorld* InWorld);

#if WITH_PHYSX
    static void CalculateMassPropertiesFromShapeCollection(physx::PxMassProperties& OutProperties, const TArray<FPhysicsShapeHandle>& InShapes, float InDensityKGPerCM);
#endif

	// Shape interface functions
	static FPhysicsShapeHandle CreateShape(physx::PxGeometry* InGeom, bool bSimulation = true, bool bQuery = true, UPhysicalMaterial* InSimpleMaterial = nullptr, TArray<UPhysicalMaterial*>* InComplexMaterials = nullptr);
	
	static void AddGeometry(FPhysicsActorHandle& InActor, const FGeometryAddParams& InParams, TArray<FPhysicsShapeHandle>* OutOptShapes = nullptr);
	static FPhysicsShapeHandle CloneShape(const FPhysicsShapeHandle& InShape);
	static FPhysicsGeometryCollection_Chaos GetGeometryCollection(const FPhysicsShapeHandle& InShape);
	
	static FCollisionFilterData GetSimulationFilter(const FPhysicsShapeHandle& InShape);
	static FCollisionFilterData GetQueryFilter(const FPhysicsShapeHandle& InShape);
	static bool IsSimulationShape(const FPhysicsShapeHandle& InShape);
	static bool IsQueryShape(const FPhysicsShapeHandle& InShape);
	static bool IsShapeType(const FPhysicsShapeHandle& InShape, ECollisionShapeType InType);
	static ECollisionShapeType GetShapeType(const FPhysicsShapeHandle& InShape);
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
	static void SetSimulationFilter(const FPhysicsShapeHandle& InShape, const FCollisionFilterData& InFilter);
	static void SetQueryFilter(const FPhysicsShapeHandle& InShape, const FCollisionFilterData& InFilter);
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
FORCEINLINE ECollisionShapeType GetGeometryType(const Chaos::TPerShapeData<float, 3>& Shape)
{
	return GetType(*Shape.Geometry);
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

FVector FindBoxOpposingNormal(const FLocationHit& PHit, const FVector& TraceDirectionDenorm, const FVector& InNormal);
FVector FindHeightFieldOpposingNormal(const FLocationHit& PHit, const FVector& TraceDirectionDenorm, const FVector& InNormal);
FVector FindConvexMeshOpposingNormal(const FLocationHit& PHit, const FVector& TraceDirectionDenorm, const FVector& InNormal);
FVector FindTriMeshOpposingNormal(const FLocationHit& PHit, const FVector& TraceDirectionDenorm, const FVector& InNormal);

FORCEINLINE void DrawOverlappingTris(const UWorld* World, const FLocationHit& Hit, const Chaos::TImplicitObject<float, 3>& Geom, const FTransform& QueryTM)
{
	//TODO_SQ_IMPLEMENTATION
}

FORCEINLINE void ComputeZeroDistanceImpactNormalAndPenetration(const UWorld* World, const FLocationHit& Hit, const Chaos::TImplicitObject<float, 3>& Geom, const FTransform& QueryTM, FHitResult& OutResult)
{
	//TODO_SQ_IMPLEMENTATION
}

inline FPhysTypeDummy* GetMaterialFromInternalFaceIndex(const FPhysicsShape& Shape, uint32 InternalFaceIndex)
{
	return nullptr;
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
ENGINE_API int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorHandle, TArray<FPhysicsShapeReference_Chaos, FDefaultAllocator>& OutShapes);
template <>
ENGINE_API int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorHandle, PhysicsInterfaceTypes::FInlineShapeArray& OutShapes);

template<>
bool FGenericPhysicsInterface::GeomSweepMulti(const UWorld* World, const FPhysicsGeometryCollection& InGeom, const FQuat& InGeomRot, TArray<FHitResult>& OutHits, FVector Start, FVector End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams /*= FCollisionObjectQueryParams::DefaultObjectQueryParam*/);

template<>
bool FGenericPhysicsInterface::GeomOverlapMulti(const UWorld* World, const FPhysicsGeometryCollection& InGeom, const FVector& InPosition, const FQuat& InRotation, TArray<FOverlapResult>& OutOverlaps, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams);

#endif
