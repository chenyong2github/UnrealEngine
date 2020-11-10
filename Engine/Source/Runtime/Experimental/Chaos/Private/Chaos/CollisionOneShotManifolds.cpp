// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionOneShotManifolds.h"

#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/Transform.h"

PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace Collisions
	{
		// Forward delarations we need from CollisionRestitution.cpp

		FContactPoint BoxBoxContactPoint(const FImplicitBox3& Box1, const FImplicitBox3& Box2, const FRigidTransform3& Box1TM, const FRigidTransform3& Box2TM, const FReal CullDistance, const FReal ShapePadding);
		uint32 ReduceManifoldContactPoints(FVec3* Points, uint32 PointCount);
		FContactPoint ConvexConvexContactPoint(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal CullDistance, const FReal ShapePadding);

		// This function will clip the input vertices by a reference shape's planes
		// more vertices may be added to outputVertexBuffer by this function
		// This is the core of the Sutherland-Hodgman algorithm
		// Plane Normals face outwards 
		uint32 ClipVerticesAgainstPlane(const FVec3* InputVertexBuffer, FVec3* outputVertexBuffer, uint32 ClipPointCount, uint32 MaxNumberOfOutputPoints, FVec3 ClippingPlaneNormal, FReal PlaneDistance)
		{

			auto CalculateIntersect = [=](const FVec3& Point1, const FVec3& Point2) -> FVec3
			{
				// Only needs to be valid if the line connecting Point1 with Point2 actually intersects
				FVec3 Result;

				FReal Denominator = FVec3::DotProduct(Point2 - Point1, ClippingPlaneNormal); // Can be negative
				if (FMath::Abs(Denominator) < SMALL_NUMBER)
				{
					Result = Point1;
				}
				else
				{
					FReal Alpha = (PlaneDistance - FVec3::DotProduct(Point1, ClippingPlaneNormal)) / Denominator;
					Result = FMath::Lerp(Point1, Point2, Alpha);
				}
				return Result;
			};

			auto InsideClipFace = [=](const FVec3& Point) -> bool
			{
				// Epsilon is there so that previously clipped points will still be inside the plane
				return FVec3::DotProduct(Point, ClippingPlaneNormal) <= PlaneDistance + PlaneDistance * SMALL_NUMBER; 
			};

			uint32 NewClipPointCount = 0;

			for (uint32 ClipPointIndex = 0; ClipPointIndex < ClipPointCount; ClipPointIndex++)
			{
				FVec3 CurrentClipPoint = InputVertexBuffer[ClipPointIndex];
				FVec3 PrevClipPoint = InputVertexBuffer[(ClipPointIndex + ClipPointCount - 1) % ClipPointCount];
				FVec3 InterSect = CalculateIntersect(PrevClipPoint, CurrentClipPoint);

				if (InsideClipFace(CurrentClipPoint))
				{
					if (!InsideClipFace(PrevClipPoint))
					{
						outputVertexBuffer[NewClipPointCount++] = InterSect;
						if (NewClipPointCount >= MaxNumberOfOutputPoints)
						{
							break;
						}
					}
					outputVertexBuffer[NewClipPointCount++] = CurrentClipPoint;
				}
				else if (InsideClipFace(PrevClipPoint))
				{
					outputVertexBuffer[NewClipPointCount++] = InterSect;
				}

				if (NewClipPointCount >= MaxNumberOfOutputPoints)
				{
					break;
				}
			}

			return NewClipPointCount;
		}
		
		template <typename ConvexImplicitType>
		void ConstructConvexConvexOneShotManifold(
			const ConvexImplicitType& Convex1,
			const FRigidTransform3& Convex1Transform, //world
			const ConvexImplicitType& Convex2,
			const FRigidTransform3& Convex2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize)
		{
			const uint32 SpaceDimension = 3;

			// We only build one shot manifolds once
			// All convexes are pre-scaled
			ensure(Constraint.GetManifoldPoints().Num() == 0);
			ensure(Convex1Transform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(Convex2Transform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			const uint32 MaxContactPointCount = 32; // This should be tuned
			uint32 ContactPointCount = 0;

			// Use GJK only once
			const FContactPoint GJKContactPoint = ConvexConvexContactPoint(Convex1, Convex1Transform, Convex2, Convex2Transform, CullDistance, Constraint.Manifold.RestitutionPadding);

			// ToDo: should we generate no contacts here?
			//if (GJKContactPoint.Phi >= CullDistance)
				//return;

			const FVec3 SeparationDirectionLocalConvex1 = Convex1Transform.InverseTransformVectorNoScale(GJKContactPoint.Normal);

			const int32 MostOpposingPlaneIndexConvex1 = Convex1.GetMostOpposingPlane(SeparationDirectionLocalConvex1);
			const TPlaneConcrete<FReal, 3> BestPlaneConvex1 = Convex1.GetPlane(MostOpposingPlaneIndexConvex1);
			const FReal BestPlaneDotNormalConvex1 = FVec3::DotProduct(-SeparationDirectionLocalConvex1, BestPlaneConvex1.Normal());

			// Now for Convex2
			const FVec3 SeparationDirectionLocalConvex2 = Convex2Transform.InverseTransformVectorNoScale(GJKContactPoint.Normal);
			const int32 MostOpposingPlaneIndexConvex2 = Convex2.GetMostOpposingPlane(-SeparationDirectionLocalConvex2);
			const TPlaneConcrete<FReal, 3> BestPlaneConvex2 = Convex2.GetPlane(MostOpposingPlaneIndexConvex2);
			const FReal BestPlaneDotNormalConvex2 = FVec3::DotProduct(SeparationDirectionLocalConvex2, BestPlaneConvex2.Normal());

			const FReal SmallBiasToPreventFeatureFlipping = 0.002; // This improves frame coherence by penalizing convex 1 in favour of convex 2
			bool ReferenceFaceConvex1 = true; // Is the reference face on convex1 or convex2?
			if (BestPlaneDotNormalConvex2 + SmallBiasToPreventFeatureFlipping > BestPlaneDotNormalConvex1)
			{
				ReferenceFaceConvex1 = false;
			}

			// Setup pointers to other Convex and reference Convex
			const FRigidTransform3* RefConvexTM;
			const FRigidTransform3* OtherConvexTM;
			const ConvexImplicitType* RefConvex;
			const ConvexImplicitType* OtherConvex;

			if (ReferenceFaceConvex1)
			{
				RefConvexTM = &Convex1Transform;
				OtherConvexTM = &Convex2Transform;
				RefConvex = &Convex1;
				OtherConvex = &Convex2;
			}
			else
			{
				RefConvexTM = &Convex2Transform;
				OtherConvexTM = &Convex1Transform;
				RefConvex = &Convex2;
				OtherConvex = &Convex1;
			}

			// Populate the clipped vertices by the other face's vertices
			const FRigidTransform3 ConvexOtherToRef = OtherConvexTM->GetRelativeTransform(*RefConvexTM);
			FVec3 ClippedVertices[MaxContactPointCount];
			TArrayView<const int32> OtherConvexFaceVertices =  OtherConvex->GetPlaneVertices(ReferenceFaceConvex1? MostOpposingPlaneIndexConvex2 : MostOpposingPlaneIndexConvex1);
			ContactPointCount = FMath::Min(OtherConvexFaceVertices.Num(), (int32)MaxContactPointCount); // Number of face vertices

			for (int32 VertexIndex = 0; VertexIndex < (int32)ContactPointCount; ++VertexIndex)
			{
				// Todo Check for Grey code
				ClippedVertices[VertexIndex] = OtherConvex->GetVertex(OtherConvexFaceVertices[VertexIndex]);
				ClippedVertices[VertexIndex] = ConvexOtherToRef.TransformPositionNoScale(ClippedVertices[VertexIndex]);
			}			

			// Now clip against all planes that belong to the reference plane's, edges
			
			FVec3 ClippedVertices2[MaxContactPointCount]; // We will use a double buffer as an optimization
			FVec3* VertexBuffer1 = ClippedVertices;
			FVec3* VertexBuffer2 = ClippedVertices2;

			TArrayView<const int32> RefConvexFaceVertices = RefConvex->GetPlaneVertices(ReferenceFaceConvex1 ? MostOpposingPlaneIndexConvex1 : MostOpposingPlaneIndexConvex2);
			int32 ClippingPlaneCount = RefConvexFaceVertices.Num();
			for (int32 ClippingPlaneIndex = 0; ClippingPlaneIndex < ClippingPlaneCount; ++ClippingPlaneIndex)
			{
				// Note winding order matters here!
				FVec3 PrevPoint = RefConvex->GetVertex(RefConvexFaceVertices[(ClippingPlaneIndex + ClippingPlaneCount - 1) % ClippingPlaneCount]);
				FVec3 CurrentPoint = RefConvex->GetVertex(RefConvexFaceVertices[ClippingPlaneIndex]);
				FVec3 PlaneNormal = -FVec3::CrossProduct(ReferenceFaceConvex1 ? BestPlaneConvex1.Normal() : BestPlaneConvex2.Normal(), CurrentPoint - PrevPoint);
				PlaneNormal.SafeNormalize();
				ContactPointCount = ClipVerticesAgainstPlane(VertexBuffer1, VertexBuffer2, ContactPointCount, MaxContactPointCount, PlaneNormal, FVec3::DotProduct(CurrentPoint, PlaneNormal));
				Swap(VertexBuffer1, VertexBuffer2); // VertexBuffer1 will now point to the latest
			}

			
			// Reduce number of contacts to a maximum of 4
			if (ContactPointCount > 4)
			{
				FRotation3 RotateSeperationToZ = FRotation3::FromRotatedVector(ReferenceFaceConvex1 ? SeparationDirectionLocalConvex1 : SeparationDirectionLocalConvex2, FVec3(0.0f, 0.0f, 1.0f));
				for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
				{
					ClippedVertices[ContactPointIndex] = RotateSeperationToZ * ClippedVertices[ContactPointIndex];
				}

				ContactPointCount = ReduceManifoldContactPoints(ClippedVertices, ContactPointCount);

				for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
				{
					ClippedVertices[ContactPointIndex] = RotateSeperationToZ.Inverse() * ClippedVertices[ContactPointIndex];
				}
			}

			// Generate the contact points from the clipped vertices
			for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
			{
				FContactPoint ContactPoint;
				const FVec3 VertexInReferenceCubeCoordinates = VertexBuffer1[ContactPointIndex];
				const TPlaneConcrete<FReal, 3> RefPlane = ReferenceFaceConvex1 ? BestPlaneConvex1 : BestPlaneConvex2;
				const FVec3 RefFaceNormal = ReferenceFaceConvex1 ? BestPlaneConvex1.Normal() : BestPlaneConvex2.Normal();
				FReal ReferencePlaneDistance = FVec3::DotProduct(RefPlane.X(), RefPlane.Normal());
				FVec3 PointProjectedOntoReferenceFace = VertexInReferenceCubeCoordinates - (FVec3::DotProduct(VertexInReferenceCubeCoordinates, RefPlane.Normal()) - ReferencePlaneDistance) * RefPlane.Normal();
				FVec3 ClippedPointInOtherCubeCoordinates = ConvexOtherToRef.InverseTransformPositionNoScale(VertexInReferenceCubeCoordinates);

				ContactPoint.ShapeContactPoints[0] = ReferenceFaceConvex1 ? PointProjectedOntoReferenceFace : ClippedPointInOtherCubeCoordinates;
				ContactPoint.ShapeContactPoints[1] = ReferenceFaceConvex1 ? ClippedPointInOtherCubeCoordinates : PointProjectedOntoReferenceFace;
				ContactPoint.ShapeContactNormal = SeparationDirectionLocalConvex2;
				ContactPoint.Location = RefConvexTM->TransformPositionNoScale(PointProjectedOntoReferenceFace);
				ContactPoint.Normal = GJKContactPoint.Normal;
				ContactPoint.Phi = FVec3::DotProduct(PointProjectedOntoReferenceFace - VertexInReferenceCubeCoordinates, ReferenceFaceConvex1 ? SeparationDirectionLocalConvex1 : -SeparationDirectionLocalConvex2);

				Constraint.AddOneshotManifoldContact(ContactPoint, bInInitialize);
			}
		}

		template 
		void ConstructConvexConvexOneShotManifold<FImplicitBox3>(
			const FImplicitBox3& Implicit1,
			const FRigidTransform3& Convex1Transform, //world
			const FImplicitBox3& Implicit2,
			const FRigidTransform3& Convex2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize);


		template
		void ConstructConvexConvexOneShotManifold<FConvex>(
				const FConvex& Implicit1,
				const FRigidTransform3& Convex1Transform, //world
				const FConvex& Implicit2,
				const FRigidTransform3& Convex2Transform, //world
				const FReal CullDistance,
				FRigidBodyPointContactConstraint& Constraint,
				bool bInInitialize);

		


	}
}

