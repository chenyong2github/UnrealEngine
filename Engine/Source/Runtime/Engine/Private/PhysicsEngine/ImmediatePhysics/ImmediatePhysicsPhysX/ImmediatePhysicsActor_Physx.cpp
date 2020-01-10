// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsActor_PhysX.h"

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

#include "PhysicsPublic.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace ImmediatePhysics_PhysX
{

	FMaterial* GetBaseMaterialFromUnrealMaterial(UPhysicalMaterial* InMaterial)
	{
		return &FMaterial::Default;
	}

	FActor::~FActor()
	{
		TerminateGeometry();
	}

	void FActor::CreateGeometry(PxRigidActor* RigidActor, const PxTransform& ActorToBodyTM)
	{
		const uint32 NumShapes = RigidActor->getNbShapes();
		TArray<PxShape*> ActorShapes;
		ActorShapes.AddUninitialized(NumShapes);
		RigidActor->getShapes(ActorShapes.GetData(), sizeof(PxShape*)*NumShapes);
		PxTransform BodyToActorTM = ActorToBodyTM.getInverse();

		Shapes.Empty(NumShapes);
		TArray<PxMaterial*, TInlineAllocator<1>> Materials;

		for (PxShape* Shape : ActorShapes)
		{
			if (!(Shape->getFlags() & PxShapeFlag::eSIMULATION_SHAPE))
			{
				continue;
			}

			const PxTransform LocalPose = Shape->getLocalPose();
			const PxTransform BodyLocalShape = BodyToActorTM * Shape->getLocalPose();
			const PxGeometryHolder& GeomHolder = Shape->getGeometry();
			const PxBounds3 Bounds = PxGeometryQuery::getWorldBounds(GeomHolder.any(), PxTransform(PxIdentity), /*inflation=*/1.f);
			const float BoundsMag = Bounds.getExtents().magnitude();
			const PxVec3 BoundsCenter = Bounds.getCenter();
			int32 NumMaterials = Shape->getNbMaterials();

			Materials.SetNumUninitialized(NumMaterials);
			Shape->getMaterials(Materials.GetData(), sizeof(Materials[0]) * NumMaterials);

			FMaterial NewMaterial;
			if (NumMaterials > 0)
			{
				NewMaterial = FMaterial(Materials[0]);
			}

			switch (GeomHolder.getType())
			{
			case PxGeometryType::eSPHERE:		Shapes.Emplace(BodyLocalShape, BoundsCenter, BoundsMag, new PxSphereGeometry(GeomHolder.sphere().radius), NewMaterial); break;
			case PxGeometryType::eCAPSULE:		Shapes.Emplace(BodyLocalShape, BoundsCenter, BoundsMag, new PxCapsuleGeometry(GeomHolder.capsule().radius, GeomHolder.capsule().halfHeight), NewMaterial); break;
			case PxGeometryType::eBOX:			Shapes.Emplace(BodyLocalShape, BoundsCenter, BoundsMag, new PxBoxGeometry(GeomHolder.box().halfExtents), NewMaterial); break;
			case PxGeometryType::eCONVEXMESH:	Shapes.Emplace(BodyLocalShape, BoundsCenter, BoundsMag, new PxConvexMeshGeometry(GeomHolder.convexMesh().convexMesh, GeomHolder.convexMesh().scale, GeomHolder.convexMesh().meshFlags), NewMaterial); break;
			case PxGeometryType::eHEIGHTFIELD:	Shapes.Emplace(BodyLocalShape, BoundsCenter, BoundsMag, new PxHeightFieldGeometry(GeomHolder.heightField().heightField, GeomHolder.heightField().heightFieldFlags, GeomHolder.heightField().heightScale, GeomHolder.heightField().rowScale, GeomHolder.heightField().columnScale), NewMaterial); break;
			case PxGeometryType::eTRIANGLEMESH: Shapes.Emplace(BodyLocalShape, BoundsCenter, BoundsMag, new PxTriangleMeshGeometry(GeomHolder.triangleMesh().triangleMesh, GeomHolder.triangleMesh().scale, GeomHolder.triangleMesh().meshFlags), NewMaterial); break;
			default: continue;	//we don't bother with other types
			}
		}
	}

	bool FActor::AddShape(PxShape* InShape)
	{
		if (!(InShape->getFlags() & PxShapeFlag::eSIMULATION_SHAPE))
		{
			return false;
		}

		const PxTransform LocalPose = InShape->getLocalPose();
		const PxGeometryHolder& GeomHolder = InShape->getGeometry();
		const PxBounds3 ShapeBounds = PxGeometryQuery::getWorldBounds(GeomHolder.any(), PxTransform(PxIdentity), 1.0f);
		const float BoundsMagnitude = ShapeBounds.getExtents().magnitude();
		const PxVec3 BoundsCenter = ShapeBounds.getCenter();
		const int32 NumMaterials = InShape->getNbMaterials();
		TArray<PxMaterial*, TInlineAllocator<1>> Materials;

		// #PHYS2 Add multi-material support
		Materials.SetNumUninitialized(NumMaterials);
		InShape->getMaterials(Materials.GetData(), sizeof(Materials[0]) * NumMaterials);

		FMaterial NewMaterial;
		if (NumMaterials > 0)
		{
			NewMaterial = FMaterial(Materials[0]);
		}

		switch (GeomHolder.getType())
		{
		case PxGeometryType::eSPHERE:		Shapes.Emplace(LocalPose, BoundsCenter, BoundsMagnitude, new PxSphereGeometry(GeomHolder.sphere().radius), NewMaterial); break;
		case PxGeometryType::eCAPSULE:		Shapes.Emplace(LocalPose, BoundsCenter, BoundsMagnitude, new PxCapsuleGeometry(GeomHolder.capsule().radius, GeomHolder.capsule().halfHeight), NewMaterial); break;
		case PxGeometryType::eBOX:			Shapes.Emplace(LocalPose, BoundsCenter, BoundsMagnitude, new PxBoxGeometry(GeomHolder.box().halfExtents), NewMaterial); break;
		case PxGeometryType::eCONVEXMESH:	Shapes.Emplace(LocalPose, BoundsCenter, BoundsMagnitude, new PxConvexMeshGeometry(GeomHolder.convexMesh().convexMesh, GeomHolder.convexMesh().scale, GeomHolder.convexMesh().meshFlags), NewMaterial); break;
		case PxGeometryType::eHEIGHTFIELD:	Shapes.Emplace(LocalPose, BoundsCenter, BoundsMagnitude, new PxHeightFieldGeometry(GeomHolder.heightField().heightField, GeomHolder.heightField().heightFieldFlags, GeomHolder.heightField().heightScale, GeomHolder.heightField().rowScale, GeomHolder.heightField().columnScale), NewMaterial); break;
		case PxGeometryType::eTRIANGLEMESH: Shapes.Emplace(LocalPose, BoundsCenter, BoundsMagnitude, new PxTriangleMeshGeometry(GeomHolder.triangleMesh().triangleMesh, GeomHolder.triangleMesh().scale, GeomHolder.triangleMesh().meshFlags), NewMaterial); break;
		default: return false;	//we don't bother with other types
		}

		// If we get here then we succeeded in creating a shape.
		Shapes.Last().UserData = InShape->userData;

		return true;
	}

	void FActor::TerminateGeometry()
	{
		for (FShape& Shape : Shapes)
		{
			switch (Shape.Geometry->getType())
			{
			case PxGeometryType::eSPHERE: delete static_cast<PxSphereGeometry*>(Shape.Geometry); break;
			case PxGeometryType::eCAPSULE: delete static_cast<PxCapsuleGeometry*>(Shape.Geometry); break;
			case PxGeometryType::eBOX: delete static_cast<PxBoxGeometry*>(Shape.Geometry); break;
			case PxGeometryType::eCONVEXMESH: delete static_cast<PxConvexMeshGeometry*>(Shape.Geometry); break;
			case PxGeometryType::eTRIANGLEMESH: delete static_cast<PxTriangleMeshGeometry*>(Shape.Geometry); break;
			case PxGeometryType::eHEIGHTFIELD: delete static_cast<PxHeightFieldGeometry*>(Shape.Geometry); break;
			default: break;
			}
		}

		Shapes.Empty();
	}

}

#endif // WITH_PHYSX


