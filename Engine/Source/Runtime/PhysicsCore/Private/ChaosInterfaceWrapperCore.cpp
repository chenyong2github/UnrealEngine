// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosInterfaceWrapperCore.h"

#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ParticleHandle.h"
#include "PhysXPublicCore.h"

namespace ChaosInterface
{
	ECollisionShapeType GetImplicitType(const Chaos::FImplicitObject& InGeometry)
	{
		switch (Chaos::GetInnerType(InGeometry.GetType()))
		{
		case Chaos::ImplicitObjectType::Sphere: return ECollisionShapeType::Sphere;
		case Chaos::ImplicitObjectType::Box: return ECollisionShapeType::Box;
		case Chaos::ImplicitObjectType::Capsule: return ECollisionShapeType::Capsule;
		case Chaos::ImplicitObjectType::Convex: return ECollisionShapeType::Convex;
		case Chaos::ImplicitObjectType::TriangleMesh: return ECollisionShapeType::Trimesh;
		case Chaos::ImplicitObjectType::HeightField: return ECollisionShapeType::Heightfield;
		default: break;
		}

		return ECollisionShapeType::None;
	}

	float GetRadius(const Chaos::TCapsule<float>& InCapsule)
	{
		return InCapsule.GetRadius();
	}

	float GetHalfHeight(const Chaos::TCapsule<float>& InCapsule)
	{
		return InCapsule.GetHeight() / 2.;
	}

	FCollisionFilterData GetQueryFilterData(const Chaos::TPerShapeData<float, 3>& Shape)
	{
		return Shape.QueryData;
	}

	FCollisionFilterData GetSimulationFilterData(const Chaos::TPerShapeData<float, 3>& Shape)
	{
		return Shape.QueryData;
	}


}
