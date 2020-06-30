// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosEngineInterface.h"
#include "ChaosInterfaceWrapperCore.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "CollisionShape.h"
#include "Chaos/PBDJointConstraintData.h"


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

const Chaos::TBox<float,3>& FPhysicsGeometryCollection_Chaos::GetBoxGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TBox<float,3>>();
}

const Chaos::TSphere<float,3>&  FPhysicsGeometryCollection_Chaos::GetSphereGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TSphere<float,3>>();
}
const Chaos::TCapsule<float>&  FPhysicsGeometryCollection_Chaos::GetCapsuleGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TCapsule<float>>();
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
			Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TCapsule<float>(Bot,Top,UseRadius));
		} else
		{
			// Use a sphere instead.
			const float UseRadius = FMath::Max(CapsuleRadius,FCollisionShape::MinSphereRadius());
			Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TSphere<float,3>(Chaos::TVector<float,3>(0),UseRadius));
		}
		break;
	}
	case ECollisionShape::Box:
	{
		Chaos::TVector<float,3> HalfExtents = CollisionShape.GetBox();
		HalfExtents.X = FMath::Max(HalfExtents.X,FCollisionShape::MinBoxExtent());
		HalfExtents.Y = FMath::Max(HalfExtents.Y,FCollisionShape::MinBoxExtent());
		HalfExtents.Z = FMath::Max(HalfExtents.Z,FCollisionShape::MinBoxExtent());

		Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TBox<float,3>(-HalfExtents,HalfExtents));
		break;
	}
	case ECollisionShape::Sphere:
	{
		const float UseRadius = FMath::Max(CollisionShape.GetSphereRadius(),FCollisionShape::MinSphereRadius());
		Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TSphere<float,3>(Chaos::TVector<float,3>(0),UseRadius));
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