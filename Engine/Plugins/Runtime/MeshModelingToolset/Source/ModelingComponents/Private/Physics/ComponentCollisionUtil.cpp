// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Physics/ComponentCollisionUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "ShapeApproximation/SimpleShapeSet3.h"

#include "Physics/PhysicsDataCollection.h"


#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"

using namespace UE::Geometry;

bool UE::Geometry::ComponentTypeSupportsCollision(
	const UPrimitiveComponent* Component)
{
	// currently only supporting StaticMeshComponent
	const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	return (StaticMeshComponent != nullptr);
}


FComponentCollisionSettings UE::Geometry::GetCollisionSettings(const UPrimitiveComponent* Component)
{
	FComponentCollisionSettings Settings;

	const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (ensure(StaticMeshComponent))
	{
		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		if (ensure(StaticMesh))
		{
			UBodySetup* BodySetup = StaticMesh->GetBodySetup();
			if (ensure(BodySetup))
			{
				Settings.CollisionTypeFlag = (int32)BodySetup->CollisionTraceFlag;
			}
		}
	}
	return Settings;
}


void UE::Geometry::UpdateSimpleCollision(
	UBodySetup* BodySetup, 
	const FKAggregateGeom* NewGeometry,
	UStaticMesh* StaticMesh,
	FComponentCollisionSettings CollisionSettings)
{
	BodySetup->Modify();
	BodySetup->RemoveSimpleCollision();

	// set new collision geometry
	BodySetup->AggGeom = *NewGeometry;

	// update collision type
	BodySetup->CollisionTraceFlag = (ECollisionTraceFlag)CollisionSettings.CollisionTypeFlag;

	// rebuild physics meshes
	BodySetup->CreatePhysicsMeshes();

	// rebuild nav collision (? StaticMeshEditor does this)
	StaticMesh->CreateNavCollision(/*bIsUpdate=*/true);

	// update physics state on all components using this StaticMesh
	for (FThreadSafeObjectIterator Iter(UStaticMeshComponent::StaticClass()); Iter; ++Iter)
	{
		UStaticMeshComponent* SMComponent = Cast<UStaticMeshComponent>(*Iter);
		if (SMComponent->GetStaticMesh() == StaticMesh)
		{
			if (SMComponent->IsPhysicsStateCreated())
			{
				SMComponent->RecreatePhysicsState();
			}
		}
	}

	// mark static mesh as dirty so it gets resaved?
	[[maybe_unused]] bool MarkedDirty = StaticMesh->MarkPackageDirty();

#if WITH_EDITORONLY_DATA
	// mark the static mesh as having customized collision so it is not regenerated on reimport
	StaticMesh->bCustomizedCollision = CollisionSettings.bIsGeneratedCollision;
#endif // WITH_EDITORONLY_DATA
}


bool UE::Geometry::SetSimpleCollision(
	UPrimitiveComponent* Component,
	const FSimpleShapeSet3d* ShapeSet,
	FComponentCollisionSettings CollisionSettings)
{
	FPhysicsDataCollection PhysicsData;
	PhysicsData.InitializeFromComponent(Component, false);
	if (ensure(PhysicsData.SourceComponent.IsValid()) == false || ensure(ShapeSet != nullptr) == false )
	{
		return false;
	}

	PhysicsData.Geometry = *ShapeSet;
	PhysicsData.CopyGeometryToAggregate();

	// FPhysicsDataCollection stores its references as const, but the input Component was non-const so this is ok to do
	UStaticMesh* StaticMesh = const_cast<UStaticMesh*>(PhysicsData.SourceStaticMesh.Get());
	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	UpdateSimpleCollision(BodySetup, &PhysicsData.AggGeom, StaticMesh, CollisionSettings);

	return true;
}



bool UE::Geometry::TransformSimpleCollision(
	UPrimitiveComponent* Component,
	const FTransform3d& Transform)
{
	FPhysicsDataCollection PhysicsData;
	PhysicsData.InitializeFromComponent(Component, true);
	if ( ensure(PhysicsData.SourceComponent.IsValid()) == false)
	{
		return false;
	}

	FComponentCollisionSettings Settings = GetCollisionSettings(Component);
	Settings.bIsGeneratedCollision = false;

	PhysicsData.Geometry.ApplyTransform(Transform);
	PhysicsData.ClearAggregate();
	PhysicsData.CopyGeometryToAggregate();

	// FPhysicsDataCollection stores its references as const, but the input Component was non-const so this is ok to do
	UStaticMesh* StaticMesh = const_cast<UStaticMesh*>(PhysicsData.SourceStaticMesh.Get());
	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	UpdateSimpleCollision(BodySetup, &PhysicsData.AggGeom, StaticMesh, Settings);

	return true;
}




bool UE::Geometry::AppendSimpleCollision(
	const UPrimitiveComponent* Component,
	FSimpleShapeSet3d* ShapeSetOut,
	const FTransform3d& Transform)
{
	FPhysicsDataCollection PhysicsData;
	PhysicsData.InitializeFromComponent(Component, true);
	if (ensure(PhysicsData.SourceComponent.IsValid()) == false || ensure(ShapeSetOut != nullptr) == false)
	{
		return false;
	}

	ShapeSetOut->Append(PhysicsData.Geometry, Transform);
	return true;
}


bool UE::Geometry::AppendSimpleCollision(
	const UPrimitiveComponent* Component,
	FSimpleShapeSet3d* ShapeSetOut,
	const TArray<FTransform3d>& TransformSeqeuence)
{
	FPhysicsDataCollection PhysicsData;
	PhysicsData.InitializeFromComponent(Component, true);
	if (ensure(PhysicsData.SourceComponent.IsValid()) == false || ensure(ShapeSetOut != nullptr) == false)
	{
		return false;
	}

	ShapeSetOut->Append(PhysicsData.Geometry, TransformSeqeuence);

	return true;
}


