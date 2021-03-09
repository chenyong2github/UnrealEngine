// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryConversion.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

using namespace UE::Geometry;

void FPhysicsDataCollection::InitializeFromComponent(const UActorComponent* Component, bool bInitializeAggGeom)
{
	const UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(Component);
	if (ensure(StaticMeshComponent))
	{
		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		if (ensure(StaticMesh))
		{
			SourceComponent = StaticMeshComponent;
			SourceStaticMesh = StaticMesh;
			BodySetup = StaticMesh->GetBodySetup();

			ExternalScale3D = FVector(1.f, 1.f, 1.f);

			if (bInitializeAggGeom)
			{
				AggGeom = BodySetup->AggGeom;
				UE::Geometry::GetShapeSet(AggGeom, Geometry);
			}
		}
	}
}


void FPhysicsDataCollection::InitializeFromStaticMesh(const UStaticMesh* StaticMesh, bool bInitializeAggGeom)
{
	if (ensure(StaticMesh))
	{
		SourceStaticMesh = StaticMesh;
		BodySetup = StaticMesh->GetBodySetup();

		ExternalScale3D = FVector(1.f, 1.f, 1.f);

		if (bInitializeAggGeom)
		{
			AggGeom = BodySetup->AggGeom;
			UE::Geometry::GetShapeSet(AggGeom, Geometry);
		}
	}
}


void FPhysicsDataCollection::InitializeFromExisting(const FPhysicsDataCollection& Other)
{
	SourceComponent = Other.SourceComponent;
	SourceStaticMesh = Other.SourceStaticMesh;
	BodySetup = Other.BodySetup;

	ExternalScale3D = Other.ExternalScale3D;
}



void FPhysicsDataCollection::CopyGeometryFromExisting(const FPhysicsDataCollection& Other)
{
	Geometry = Other.Geometry;
	AggGeom = Other.AggGeom;
}


void FPhysicsDataCollection::ClearAggregate()
{
	AggGeom = FKAggregateGeom();
}

void FPhysicsDataCollection::CopyGeometryToAggregate()
{
	for (FBoxShape3d& BoxGeom : Geometry.Boxes)
	{
		FKBoxElem Element;
		UE::Geometry::GetFKElement(BoxGeom.Box, Element);
		AggGeom.BoxElems.Add(Element);
	}

	for (FSphereShape3d& SphereGeom : Geometry.Spheres)
	{
		FKSphereElem Element;
		UE::Geometry::GetFKElement(SphereGeom.Sphere, Element);
		AggGeom.SphereElems.Add(Element);
	}

	for (FCapsuleShape3d& CapsuleGeom : Geometry.Capsules)
	{
		FKSphylElem Element;
		UE::Geometry::GetFKElement(CapsuleGeom.Capsule, Element);
		AggGeom.SphylElems.Add(Element);
	}

	for (FConvexShape3d& ConvexGeom : Geometry.Convexes)
	{
		FKConvexElem Element;
		UE::Geometry::GetFKElement(ConvexGeom.Mesh, Element);

#if !WITH_CHAOS
		// Chaos will compute the IndexData itself on the call to FKConvexElem::UpdateElemBox() in ::GetFKElement() above.
		// PhysX will not, so initialize that data with the mesh triangles.
		// (This code should go into ::GetFKElement but cannot because it needs to be added in a hotfix)
		for (FIndex3i Triangle : ConvexGeom.Mesh.TrianglesItr())
		{
			Element.IndexData.Add(Triangle.A);
			Element.IndexData.Add(Triangle.B);
			Element.IndexData.Add(Triangle.C);
		}
#endif

		AggGeom.ConvexElems.Add(Element);
	}
}



