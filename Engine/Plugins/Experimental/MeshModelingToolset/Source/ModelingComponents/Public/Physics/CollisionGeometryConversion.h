// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OrientedBoxTypes.h"
#include "SegmentTypes.h"
#include "CapsuleTypes.h"
#include "SphereTypes.h"
#include "DynamicMesh3.h"
#include "ShapeApproximation/SimpleShapeSet3.h"

#include "PhysicsEngine/AggregateGeom.h"


namespace UE
{
	namespace Geometry
	{
		/**
		 * Convert FSphere3d to FKSphereElem
		 */
		void GetFKElement(const FSphere3d& Sphere, FKSphereElem& ElemInOut)
		{
			ElemInOut.Center = (FVector)Sphere.Center;
			ElemInOut.Radius = (float)Sphere.Radius;
		}

		/**
		 * Convert FOrientedBox3d to FKBoxElem
		 */
		void GetFKElement(const FOrientedBox3d& Box, FKBoxElem& BoxInOut)
		{
			BoxInOut.X = 2 * (float)Box.Extents.X;
			BoxInOut.Y = 2 * (float)Box.Extents.Y;
			BoxInOut.Z = 2 * (float)Box.Extents.Z;

			BoxInOut.Center = (FVector)Box.Frame.Origin;
			BoxInOut.Rotation = FRotator((FQuat)Box.Frame.Rotation);
		}

		/**
		 * Convert FCapsule3d to FKSphylElem
		 */
		void GetFKElement(const FCapsule3d& Capsule, FKSphylElem& CapsuleInOut)
		{
			FFrame3d CapsuleFrame(Capsule.Center(), Capsule.Direction());

			CapsuleInOut.Center = (FVector)CapsuleFrame.Origin;
			CapsuleInOut.Rotation = FRotator((FQuat)CapsuleFrame.Rotation);
			CapsuleInOut.Length = (float)Capsule.Length();		// Sphyl length is full length
			CapsuleInOut.Radius = (float)Capsule.Radius;
		}

		/**
		 * Convert FDynamicMesh3 to FKConvexElem
		 */
		void GetFKElement(const FDynamicMesh3& Mesh, FKConvexElem& ConvexInOut)
		{
			ConvexInOut.VertexData.Reset();

			for (int32 vid : Mesh.VertexIndicesItr())
			{
				FVector3d Pos = Mesh.GetVertex(vid);
				ConvexInOut.VertexData.Add((FVector)Pos);
			}

			// despite the name this actually computes the convex hull of the point set...
			ConvexInOut.UpdateElemBox();
		}




		/**
		 * Convert FKSphereElem to FSphereShape3d
		 */
		void GetShape(const FKSphereElem& SphereElem, FSphereShape3d& SphereShapeOut)
		{
			SphereShapeOut.Sphere.Center = (FVector3d)SphereElem.Center;
			SphereShapeOut.Sphere.Radius = (double)SphereElem.Radius;
		}

		/**
		 * Convert FKBoxElem to FBoxShape3d
		 */
		void GetShape(const FKBoxElem& BoxElem, FBoxShape3d& BoxShapeOut)
		{
			BoxShapeOut.Box.Frame.Origin = (FVector3d)BoxElem.Center;
			BoxShapeOut.Box.Frame.Rotation = (FQuaterniond)BoxElem.Rotation.Quaternion();
			BoxShapeOut.Box.Extents = FVector3d(BoxElem.X, BoxElem.Y, BoxElem.Z) / 2.0;
		}

		/**
		 * Convert FKSphylElem to FCapsuleShape3d
		 */
		void GetShape(const FKSphylElem& CapsuleElem, FCapsuleShape3d& CapsuleShapeOut)
		{
			FQuaterniond CapsuleRotation(CapsuleElem.Rotation.Quaternion());
			CapsuleShapeOut.Capsule.Segment.Center = (FVector3d)CapsuleElem.Center;
			CapsuleShapeOut.Capsule.Segment.Direction = CapsuleRotation.AxisZ();
			CapsuleShapeOut.Capsule.Segment.Extent = (double)CapsuleElem.Length / 2.0;
			CapsuleShapeOut.Capsule.Radius = (double)CapsuleElem.Radius;
		}


		/**
		 * Convert FKConvexElem to FDynamicMesh3 
		 */
		void GetShape(const FKConvexElem& ConvexElem, FDynamicMesh3& MeshOut)
		{
			MeshOut.Clear();
			for (int32 k = 0; k < ConvexElem.VertexData.Num(); ++k)
			{
				MeshOut.AppendVertex((FVector3d)ConvexElem.VertexData[k]);
			}
			int32 NumTriangles = ConvexElem.IndexData.Num() / 3;
			for (int32 k = 0; k < NumTriangles; ++k)
			{
				MeshOut.AppendTriangle( ConvexElem.IndexData[3*k], ConvexElem.IndexData[3*k+1], ConvexElem.IndexData[3*k+2]);
			}
		}

		/**
		 * Convert FKConvexElem to FConvexShape3d
		 */
		void GetShape(const FKConvexElem& ConvexElem, FConvexShape3d& ConvexShapeOut)
		{
			GetShape(ConvexElem, ConvexShapeOut.Mesh);
		}


		/**
		 * Convert FKAggregateGeom to FSimpleShapeSet3d
		 */
		void GetShapeSet(const FKAggregateGeom& AggGeom, FSimpleShapeSet3d& ShapeSetOut)
		{
			for (const FKBoxElem& BoxElem : AggGeom.BoxElems)
			{
				FBoxShape3d BoxShape;
				GetShape(BoxElem, BoxShape);
				ShapeSetOut.Boxes.Add(BoxShape);
			}

			for (const FKSphereElem& SphereElem : AggGeom.SphereElems)
			{
				FSphereShape3d SphereShape;
				GetShape(SphereElem, SphereShape);
				ShapeSetOut.Spheres.Add(SphereShape);
			}

			for (const FKSphylElem& CapsuleElem : AggGeom.SphylElems)
			{
				FCapsuleShape3d CapsuleShape;
				GetShape(CapsuleElem, CapsuleShape);
				ShapeSetOut.Capsules.Add(CapsuleShape);
			}

			for (const FKConvexElem& ConvexElem : AggGeom.ConvexElems)
			{
				FConvexShape3d ConvexShape;
				GetShape(ConvexElem, ConvexShape);
				ShapeSetOut.Convexes.Add(ConvexShape);
			}
		}


	}
}