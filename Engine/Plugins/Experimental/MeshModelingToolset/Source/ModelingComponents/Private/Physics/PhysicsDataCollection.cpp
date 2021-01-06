// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryConversion.h"

#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"



void FPhysicsDataCollection::InitializeFromComponent(const UActorComponent* Component, bool bInitializeAggGeom)
{
	const UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(Component);
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	
	SourceComponent = StaticMeshComponent;
	BodySetup = StaticMesh->GetBodySetup();

	ExternalScale3D = FVector(1.f, 1.f, 1.f);

	if (bInitializeAggGeom)
	{
		AggGeom = BodySetup->AggGeom;
		// transfer AggGeom to FSimpleShapeSet3d...
	}
}


void FPhysicsDataCollection::InitializeFromExisting(const FPhysicsDataCollection& Other)
{
	SourceComponent = Other.SourceComponent;
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

#if WITH_PHYSX
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



