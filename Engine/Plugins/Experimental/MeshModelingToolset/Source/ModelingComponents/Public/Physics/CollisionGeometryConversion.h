// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OrientedBoxTypes.h"
#include "SegmentTypes.h"
#include "CapsuleTypes.h"
#include "SphereTypes.h"
#include "DynamicMesh3.h"


#include "Engine/Classes/PhysicsEngine/AggregateGeom.h"


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


	}
}