// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/Declares.h"
#include "PhysicsInterfaceDeclaresCore.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "PhysicsInterfaceTypesCore.h"

//NOTE: Do not include Chaos headers directly as it means recompiling all of engine. This should be reworked to avoid allocations

namespace Chaos
{
	template <typename T, int>
	class TBVHParticles;

	template <typename T, int>
	class TPBDRigidParticles;

	template <typename T, int>
	class PerParticleGravity;

	template <typename T, int>
	class TPBDSpringConstraints;

	class FConvex;

	template <typename T>
	class TCapsule;

	template <typename T, int>
	class TAABB;

	template <typename T, int>
	class TBox;

	template <typename T, int>
	class TSphere;

	class FJointConstraint;

	class FTriangleMeshImplicitObject;
}

struct FCollisionShape;

class FPhysicsAggregateReference_Chaos
{
public:
	bool IsValid() const { return false; }
};

class CHAOSENGINEBRIDGE_API FPhysicsConstraintReference_Chaos
{
public:
	FPhysicsConstraintReference_Chaos() : Constraint(nullptr) {};

	bool IsValid() const;

	Chaos::FJointConstraint* Constraint;
};

class CHAOSENGINEBRIDGE_API FPhysicsShapeReference_Chaos
{
public:
	
	FPhysicsShapeReference_Chaos()
		: Shape(nullptr), ActorRef() { }
	FPhysicsShapeReference_Chaos(Chaos::FPerShapeData* ShapeIn, const FPhysicsActorHandle& ActorRefIn)
		: Shape(ShapeIn), ActorRef(ActorRefIn) { }
	FPhysicsShapeReference_Chaos(const FPhysicsShapeReference_Chaos& Other)
		: Shape(Other.Shape)
		, ActorRef(Other.ActorRef){}

	bool IsValid() const { return (Shape != nullptr); }
	bool Equals(const FPhysicsShapeReference_Chaos& Other) const { return Shape == Other.Shape; }
    bool operator==(const FPhysicsShapeReference_Chaos& Other) const { return Equals(Other); }
	const Chaos::FImplicitObject& GetGeometry() const;

	Chaos::FPerShapeData* Shape;
    FPhysicsActorHandle ActorRef;
};

class CHAOSENGINEBRIDGE_API FPhysicsShapeAdapter_Chaos
{
public:
	FPhysicsShapeAdapter_Chaos(const FQuat& Rot, const FCollisionShape& CollisionShape);
	~FPhysicsShapeAdapter_Chaos();

	const FPhysicsGeometry& GetGeometry() const;
	FTransform GetGeomPose(const FVector& Pos) const;
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
struct CHAOSENGINEBRIDGE_API FPhysicsGeometryCollection_Chaos
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
	const Chaos::FImplicitObject& GetGeometry() const;
	const Chaos::TBox<float, 3>& GetBoxGeometry() const;
	const Chaos::TSphere<float, 3>&  GetSphereGeometry() const;
	const Chaos::TCapsule<float>&  GetCapsuleGeometry() const;
	const Chaos::FConvex& GetConvexGeometry() const;
	const Chaos::FTriangleMeshImplicitObject& GetTriMeshGeometry() const;

private:
	friend class FPhysInterface_Chaos;
	explicit FPhysicsGeometryCollection_Chaos(const FPhysicsShapeReference_Chaos& InShape);

	const Chaos::FImplicitObject& Geom;
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

class CHAOSENGINEBRIDGE_API FChaosEngineInterface
{
public:
	virtual ~FChaosEngineInterface() = default;
};