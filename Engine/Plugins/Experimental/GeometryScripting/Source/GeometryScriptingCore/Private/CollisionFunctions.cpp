// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/CollisionFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/MeshConvexHull.h"
#include "Operations/MeshProjectionHull.h"
#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"

#include "Selections/MeshConnectedComponents.h"
#include "DynamicSubmesh3.h"
#include "Polygroups/PolygroupUtil.h"

#include "ShapeApproximation/ShapeDetection3.h"
#include "ShapeApproximation/MeshSimpleShapeApproximation.h"

// physics data
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

// requires ModelingComponents
#include "Physics/PhysicsDataCollection.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_CollisionFunctions"


namespace UELocal
{

void ComputeCollisionFromMesh(
	const FDynamicMesh3& Mesh, 
	FKAggregateGeom& GeneratedCollision, 
	FGeometryScriptCollisionFromMeshOptions& Options)
{
	FPhysicsDataCollection NewCollision;

	FMeshConnectedComponents Components(&Mesh);
	Components.FindConnectedTriangles();
	int32 NumComponents = Components.Num();

	TArray<FDynamicMesh3> Submeshes;
	TArray<const FDynamicMesh3*> SubmeshPointers;
	SubmeshPointers.SetNum(NumComponents);

	if (NumComponents == 1)
	{
		SubmeshPointers.Add(&Mesh);
	}
	else
	{
		Submeshes.SetNum(NumComponents);
		ParallelFor(NumComponents, [&](int32 k)
		{
			FDynamicSubmesh3 Submesh(&Mesh, Components[k].Indices, (int32)EMeshComponents::None, false);
			Submeshes[k] = MoveTemp(Submesh.GetSubmesh());
			SubmeshPointers[k] = &Submeshes[k];
		});
	}

	FMeshSimpleShapeApproximation ShapeGenerator;
	ShapeGenerator.InitializeSourceMeshes(SubmeshPointers);

	ShapeGenerator.bDetectSpheres = Options.bAutoDetectSpheres;
	ShapeGenerator.bDetectBoxes = Options.bAutoDetectBoxes;
	ShapeGenerator.bDetectCapsules = Options.bAutoDetectCapsules;

	ShapeGenerator.MinDimension = Options.MinThickness;

	switch (Options.Method)
	{
	case EGeometryScriptCollisionGenerationMethod::AlignedBoxes:
		ShapeGenerator.Generate_AlignedBoxes(NewCollision.Geometry);
		break;
	case EGeometryScriptCollisionGenerationMethod::OrientedBoxes:
		ShapeGenerator.Generate_OrientedBoxes(NewCollision.Geometry);
		break;
	case EGeometryScriptCollisionGenerationMethod::MinimalSpheres:
		ShapeGenerator.Generate_MinimalSpheres(NewCollision.Geometry);
		break;
	case EGeometryScriptCollisionGenerationMethod::Capsules:
		ShapeGenerator.Generate_Capsules(NewCollision.Geometry);
		break;
	case EGeometryScriptCollisionGenerationMethod::ConvexHulls:
		ShapeGenerator.bSimplifyHulls = Options.bSimplifyHulls;
		ShapeGenerator.HullTargetFaceCount = Options.ConvexHullTargetFaceCount;
		ShapeGenerator.Generate_ConvexHulls(NewCollision.Geometry);
		break;
	case EGeometryScriptCollisionGenerationMethod::SweptHulls:
		ShapeGenerator.bSimplifyHulls = Options.bSimplifyHulls;
		ShapeGenerator.HullSimplifyTolerance = Options.SweptHullSimplifyTolerance;
		ShapeGenerator.Generate_ProjectedHulls(NewCollision.Geometry, 
			static_cast<FMeshSimpleShapeApproximation::EProjectedHullAxisMode>(Options.SweptHullAxis));
		break;
	case EGeometryScriptCollisionGenerationMethod::MinVolumeShapes:
		ShapeGenerator.Generate_MinVolume(NewCollision.Geometry);
		break;
	}

	if (Options.bRemoveFullyContainedShapes && Components.Num() > 1)
	{
		NewCollision.Geometry.RemoveContainedGeometry();
	}

	if (Options.MaxShapeCount > 0 && Options.MaxShapeCount < Components.Num())
	{
		NewCollision.Geometry.FilterByVolume(Options.MaxShapeCount);
	}

	NewCollision.CopyGeometryToAggregate();
	GeneratedCollision = NewCollision.AggGeom;
}

}		// end namespace UELocal


UDynamicMesh* UGeometryScriptLibrary_CollisionFunctions::SetStaticMeshCollisionFromMesh(
	UDynamicMesh* FromDynamicMesh,
	UStaticMesh* ToStaticMeshAsset,
	FGeometryScriptCollisionFromMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (FromDynamicMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetStaticMeshCollisionFromMesh_InvalidInput1", "SetStaticMeshCollisionFromMesh: FromDynamicMesh is Null"));
		return FromDynamicMesh;
	}
	if (ToStaticMeshAsset == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetStaticMeshCollisionFromMesh_InvalidInput2", "SetStaticMeshCollisionFromMesh: ToStaticMeshAsset is Null"));
		return FromDynamicMesh;
	}

	FKAggregateGeom NewCollision;
	FromDynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		UELocal::ComputeCollisionFromMesh(ReadMesh, NewCollision, Options);
	});

#if WITH_EDITOR
	if (Options.bEmitTransaction)
	{
		GEditor->BeginTransaction(LOCTEXT("UpdateStaticMesh", "Set Simple Collision"));
	}

	ToStaticMeshAsset->Modify();
#endif

	UBodySetup* BodySetup = ToStaticMeshAsset->GetBodySetup();
	if (BodySetup != nullptr)
	{
		// mark the BodySetup for modification. Do we need to modify the UStaticMesh??
#if WITH_EDITOR
		if (Options.bEmitTransaction)
		{
			BodySetup->Modify();
		}
#endif

		// clear existing simple collision. This will call BodySetup->InvalidatePhysicsData()
		BodySetup->RemoveSimpleCollision();

		// set new collision geometry
		BodySetup->AggGeom = NewCollision;

		// update collision type
		//BodySetup->CollisionTraceFlag = (ECollisionTraceFlag)(int32)Settings->SetCollisionType;

		// rebuild physics meshes
		BodySetup->CreatePhysicsMeshes();

		// rebuild nav collision (? StaticMeshEditor does this)
		ToStaticMeshAsset->CreateNavCollision(/*bIsUpdate=*/true);

		// update physics state on all components using this StaticMesh
		for (FThreadSafeObjectIterator Iter(UStaticMeshComponent::StaticClass()); Iter; ++Iter)
		{
			UStaticMeshComponent* SMComponent = Cast<UStaticMeshComponent>(*Iter);
			if (SMComponent->GetStaticMesh() == ToStaticMeshAsset)
			{
				if (SMComponent->IsPhysicsStateCreated())
				{
					SMComponent->RecreatePhysicsState();
				}
			}
		}

		// do we need to do a post edit change here??

		// mark static mesh as dirty so it gets resaved?
		ToStaticMeshAsset->MarkPackageDirty();

#if WITH_EDITORONLY_DATA
		// mark the static mesh as having customized collision so it is not regenerated on reimport
		ToStaticMeshAsset->bCustomizedCollision = true;
#endif // WITH_EDITORONLY_DATA
	}

#if WITH_EDITOR
	if (Options.bEmitTransaction)
	{
		GEditor->EndTransaction();
	}
#endif


	return FromDynamicMesh;
}







UDynamicMesh* UGeometryScriptLibrary_CollisionFunctions::SetDynamicMeshCollisionFromMesh(
	UDynamicMesh* FromDynamicMesh,
	UDynamicMeshComponent* DynamicMeshComponent,
	FGeometryScriptCollisionFromMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (FromDynamicMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetDynamicMeshCollisionFromMesh_InvalidInput1", "SetDynamicMeshCollisionFromMesh: FromDynamicMesh is Null"));
		return FromDynamicMesh;
	}
	if (DynamicMeshComponent == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetDynamicMeshCollisionFromMesh_InvalidInput2", "SetDynamicMeshCollisionFromMesh: ToDynamicMeshComponent is Null"));
		return FromDynamicMesh;
	}

	FKAggregateGeom NewCollision;
	FromDynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		UELocal::ComputeCollisionFromMesh(ReadMesh, NewCollision, Options);
	});

#if WITH_EDITOR
	if (Options.bEmitTransaction)
	{
		GEditor->BeginTransaction(LOCTEXT("UpdateDynamicMesh", "Set Simple Collision"));
	}

	DynamicMeshComponent->Modify();
#endif

	UBodySetup* BodySetup = DynamicMeshComponent->GetBodySetup();
	if (BodySetup != nullptr)
	{
		// mark the BodySetup for modification. Do we need to modify the UStaticMesh??
#if WITH_EDITOR
		if (Options.bEmitTransaction)
		{
			BodySetup->Modify();
		}
#endif

		// clear existing simple collision. This will call BodySetup->InvalidatePhysicsData()
		BodySetup->RemoveSimpleCollision();

		// set new collision geometry
		BodySetup->AggGeom = NewCollision;

		// update collision type
		//BodySetup->CollisionTraceFlag = (ECollisionTraceFlag)(int32)Settings->SetCollisionType;

		// do we need to do a post edit change here??
		DynamicMeshComponent->UpdateCollision(false);
	}

#if WITH_EDITOR
	if (Options.bEmitTransaction)
	{
		GEditor->EndTransaction();
	}
#endif

	return FromDynamicMesh;
}




#undef LOCTEXT_NAMESPACE