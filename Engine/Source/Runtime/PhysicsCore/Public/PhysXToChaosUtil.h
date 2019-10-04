// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"

#if WITH_PHYSX
#include "PhysXIncludes.h"
#include "PhysXSupportCore.h"

#include "Chaos/Particles.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/Convex.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/HeightField.h"
#include "Chaos/ImplicitObjectScaled.h"

//convert physx shape to chaos geometry - must be inline because of unique ptr crossing dlls
inline TUniquePtr<Chaos::TImplicitObjectTransformed<float, 3>> PxShapeToChaosGeom(PxShape* Shape)
{
	using namespace Chaos;

	TUniquePtr<TImplicitObject<float, 3>> InnerObj;
	PxTransform ShapeTM = Shape->getLocalPose();
	PxGeometryHolder Geom = Shape->getGeometry();
	switch (Geom.getType())
	{
	case PxGeometryType::eSPHERE:
	{
		InnerObj = MakeUnique<TSphere<float, 3>>(TVector<float, 3>(0), Geom.sphere().radius);
		break;
	}
	case PxGeometryType::eBOX:
	{
		const TVector<float, 3> HalfExtents = P2UVector(Geom.box().halfExtents);
		InnerObj = MakeUnique<TBox<float, 3>>(-HalfExtents, HalfExtents);
		break;
	}
	case PxGeometryType::eCAPSULE:
	{
		const float HalfHeight = Geom.capsule().halfHeight;
		const float Radius = Geom.capsule().radius;
		const TVector<float, 3> Top(HalfHeight, 0, 0);	//PhysX capsules are aligned along the x-axis
		const TVector<float, 3> Bottom = -Top;
		InnerObj = MakeUnique<TCapsule<float>>(Top, Bottom, Radius);
		break;
	}
	case PxGeometryType::eCONVEXMESH:
	{
		//todo: instancing support
		const PxConvexMeshGeometry& ConvexMeshGeom = Geom.convexMesh();
		const PxConvexMesh* ConvexMesh = ConvexMeshGeom.convexMesh;
		const PxVec3* Vertices = ConvexMesh->getVertices();
		const uint32 NumVerts = ConvexMesh->getNbVertices();

		TParticles<float, 3> Particles;
		Particles.AddParticles(NumVerts);
		for (uint32 Idx = 0; Idx < NumVerts; ++Idx)
		{
			Particles.X(Idx) = P2UVector(Vertices[Idx]);
		}

		TUniquePtr<TConvex<float, 3>> ConvexObj = MakeUnique<TConvex<float, 3>>(Particles);
		if (ConvexMeshGeom.scale.isIdentity())
		{
			InnerObj = MoveTemp(ConvexObj);
		}
		else
		{
			ensure(ConvexMeshGeom.scale.rotation == PxQuat(PxIdentity));
			InnerObj = MakeUnique<TImplicitObjectScaled<float, 3, /*bInstanced=*/false>>(MoveTemp(ConvexObj), P2UVector(ConvexMeshGeom.scale.scale));	//todo: make instanced
		}
		break;
	}
	case PxGeometryType::eHEIGHTFIELD:
	{
		const PxHeightFieldGeometry& HeightFieldGeom = Geom.heightField();
		const PxHeightField* HeightField = HeightFieldGeom.heightField;
		const int32 NumRows = HeightField->getNbRows();
		const int32 NumCols = HeightField->getNbColumns();
		const int32 NumCells = NumRows * NumCols;

		TArray<PxHeightFieldSample> CellBuffer;
		CellBuffer.AddUninitialized(NumCells);

		HeightField->saveCells(CellBuffer.GetData(), sizeof(CellBuffer[0]) * CellBuffer.Num());

		TArray<float> Height;
		Height.AddUninitialized(NumRows * NumCols);
		//PhysX and unreal have opposite handedness so we flip the data (see LandscapeComponent)
		for (int32 RowIdx = 0; RowIdx < NumRows; ++RowIdx)
		{
			for (int32 ColIdx = 0; ColIdx < NumCols; ++ColIdx)
			{
				int32 FlippedRowIdx = NumRows - RowIdx - 1;
				Height[ColIdx * NumRows + RowIdx] = CellBuffer[FlippedRowIdx * NumCols + ColIdx].height;	//swap rows and columns because of how physx stores it vs chaos
			}
		}

		// Now we fix the rotation (x,y,z) becomes (x,z,y)
		// Note we are assuming all heightfields come from LandscapeComponent
		//FTransform LandscapeComponentTransform = P2UTransform(ShapeTM);
		FTransform LandscapeComponentTransform = FTransform::Identity;
		FMatrix LandscapeComponentMatrix = LandscapeComponentTransform.ToMatrixWithScale();

		// Reorder the axes
		FVector TerrainX = LandscapeComponentMatrix.GetScaledAxis(EAxis::X);
		FVector TerrainY = LandscapeComponentMatrix.GetScaledAxis(EAxis::Y);
		FVector TerrainZ = LandscapeComponentMatrix.GetScaledAxis(EAxis::Z);
		LandscapeComponentMatrix.SetAxis(0, TerrainX);
		LandscapeComponentMatrix.SetAxis(2, TerrainY);
		LandscapeComponentMatrix.SetAxis(1, TerrainZ);

		ShapeTM = U2PTransform(FTransform(LandscapeComponentMatrix));

		InnerObj = MakeUnique<THeightField<float>>(MoveTemp(Height), NumRows, NumCols, TVector<float, 3>(HeightFieldGeom.columnScale, HeightFieldGeom.rowScale, HeightFieldGeom.heightScale));
		break;
	}
	case PxGeometryType::eTRIANGLEMESH:
	{
		//todo: instancing support
		const PxTriangleMeshGeometry& TriMeshGeom = Geom.triangleMesh();
		const PxTriangleMesh* TriangleMesh = TriMeshGeom.triangleMesh;
		const PxVec3* Vertices = TriangleMesh->getVertices();
		const uint32 NumVerts = TriangleMesh->getNbVertices();

		TParticles<float, 3> Particles;
		Particles.AddParticles(NumVerts);
		for (uint32 Idx = 0; Idx < NumVerts; ++Idx)
		{
			Particles.X(Idx) = P2UVector(Vertices[Idx]);
		}

		const void* IndexBuffer = TriangleMesh->getTriangles();
		const uint32 NumTriangles = TriangleMesh->getNbTriangles();
		PxTriangleMeshFlags Flags = TriangleMesh->getTriangleMeshFlags();
		TArray<TVector<int32, 3>> Triangles;
		Triangles.AddUninitialized(NumTriangles);
		for (uint32 TriIdx = 0; TriIdx < NumTriangles; ++TriIdx)
		{
			if (Flags & PxTriangleMeshFlag::e16_BIT_INDICES)
			{
				TVector<int32, 3> Triangle = {
					static_cast<const uint16*>(IndexBuffer)[TriIdx * 3],
					static_cast<const uint16*>(IndexBuffer)[TriIdx * 3+1],
					static_cast<const uint16*>(IndexBuffer)[TriIdx * 3+2] };
				Triangles[TriIdx] = Triangle;
			}
			else
			{
				TVector<int32, 3> Triangle = {
					static_cast<const int32*>(IndexBuffer)[TriIdx * 3],
					static_cast<const int32*>(IndexBuffer)[TriIdx * 3+1],
					static_cast<const int32*>(IndexBuffer)[TriIdx * 3+2] };
				Triangles[TriIdx] = Triangle;
			}
		}

		TUniquePtr<TTriangleMeshImplicitObject<float>> TriMeshObj = MakeUnique<TTriangleMeshImplicitObject<float>>(MoveTemp(Particles), MoveTemp(Triangles));
		if (TriMeshGeom.scale.isIdentity())
		{
			InnerObj = MoveTemp(TriMeshObj);
		}
		else
		{
			ensure(TriMeshGeom.scale.rotation == PxQuat(PxIdentity));
			InnerObj = MakeUnique<TImplicitObjectScaled<float, 3, /*bInstanced=*/false>>(MoveTemp(TriMeshObj), P2UVector(TriMeshGeom.scale.scale));	//todo: make instanced
		}
		break;
	}
	default: ensure(false); return nullptr;	//missing support for this geometry type
	}

	return MakeUnique<TImplicitObjectTransformed<float, 3>>(MoveTemp(InnerObj), P2UTransform(ShapeTM));
}

#endif